#include "cache.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include <string.h>

static struct list clock_list; // a cycle list for clock algorithm
static struct list_elem *clock_list_iterator;
struct lock clock_list_lock;

struct hash cache_table;

static void
clock_list_next (void)
{
  clock_list_iterator
      = (list_next (clock_list_iterator) == list_tail (&clock_list))
            ? list_begin (&clock_list)
            : list_next (clock_list_iterator);
}

/**
 * @brief Set cur_frame to the frame to be evicted
 * @note This function gaurentees thread safety
 */
static void
clock_list_push_back (struct list_elem *elem)
{
  lock_acquire (&clock_list_lock);
  if (list_empty (&clock_list))
    {
      clock_list_iterator = elem;
      list_push_back (&clock_list, elem);
    }
  else
    list_insert (clock_list_iterator, elem);
  lock_release (&clock_list_lock);
}

/**
 * @brief Clock list remove
 * @param elem the elem in struct fte
 * @note This function gaurentees thread safety
 */
static void
clock_list_remove (struct list_elem *elem)
{
  ASSERT (elem != NULL);
  ASSERT (!list_empty (&clock_list));
  lock_acquire (&clock_list_lock);
  if (clock_list_iterator == elem)
    clock_list_next ();
  list_remove (elem);
  lock_release (&clock_list_lock);
}

static struct cache_entry *
clock_find_block_to_evict (void)
{
  lock_acquire (&clock_list_lock);
  struct cache_entry *evict_entry;
  bool found = false;
  while (!found)
    {
      evict_entry = list_entry (clock_list_iterator, struct cache_entry,
                                clock_list_elem);
      if (!lock_try_acquire (&evict_entry->cache_lock))
        {
          if (evict_entry->accessed)
            {
              evict_entry->accessed = false;
              clock_list_next ();
            }
          else
            {
              found = true;
              lock_release (&evict_entry->cache_lock);
            }
        }
      else
        {
          clock_list_next ();
        }
    }

  clock_list_next ();
  list_remove (&evict_entry->clock_list_elem);

  lock_release (&clock_list_lock);
  return evict_entry;
}

static unsigned
cache_table_hash (const struct hash_elem *elem, void *aux UNUSED)
{
  struct cache_entry *entry = hash_entry (elem, struct cache_entry, hash_elem);
  return hash_bytes (&entry->sector, sizeof entry->sector);
}

static bool
cache_table_less (const struct hash_elem *a, const struct hash_elem *b,
                  void *aux UNUSED)
{
  struct cache_entry *entry_a = hash_entry (a, struct cache_entry, hash_elem);
  struct cache_entry *entry_b = hash_entry (b, struct cache_entry, hash_elem);
  return entry_a->sector < entry_b->sector;
}

void
cache_table_init ()
{
  hash_init (&cache_table, cache_table_hash, cache_table_less, NULL);
}

struct cache_entry *
cache_table_find (block_sector_t sector)
{
  struct cache_entry entry;
  entry.sector = sector;
  struct hash_elem *elem = hash_find (&cache_table, &entry.hash_elem);
  return elem != NULL ? hash_entry (elem, struct cache_entry, hash_elem)
                      : NULL;
}

void
cache_init ()
{
  lock_init (&clock_list_lock);
  list_init (&clock_list);
}

static struct cache_entry *
cache_get_block (block_sector_t sector)
{
  struct cache_entry *entry;
  // if cache is full, evict a block
  if (list_size (&clock_list) == CACHE_SIZE)
    {
      entry = clock_find_block_to_evict ();
      lock_acquire (&entry->cache_lock);
      if (entry->dirty)
        block_write (fs_device, entry->sector, entry->data);
      hash_delete (&cache_table, &entry->hash_elem);
      list_remove (&entry->clock_list_elem);
      lock_release (&entry->cache_lock);
    }
  else
    {
      entry = malloc (sizeof *entry);
      lock_init (&entry->cache_lock);
    }
  block_read (fs_device, sector, entry->data);
  entry->valid = true;
  entry->dirty = false;
  entry->sector = sector;
  hash_insert (&cache_table, &entry->hash_elem);
  lock_acquire (&clock_list_lock);
  clock_list_push_back (&entry->clock_list_elem);
  lock_release (&clock_list_lock);
  return entry;
}

void
cache_read (block_sector_t sector, void *buffer)
{
  struct cache_entry *entry = cache_table_find (sector);
  // if entry is not in cache
  if (entry == NULL)
    {
      // evict a cache block
      entry = cache_get_block (sector);
    }
  lock_acquire (&entry->cache_lock);
  entry->accessed = true;
  memcpy (buffer, entry->data, BLOCK_SECTOR_SIZE);
  lock_release (&entry->cache_lock);
}

void
cache_write (block_sector_t sector, const void *buffer)
{
  struct cache_entry *entry = cache_table_find (sector);
  // if entry is not in cache
  if (entry == NULL)
    {
      // evict a cache block
      entry = cache_get_block (sector);
    }
  lock_acquire (&entry->cache_lock);
  entry->accessed = true;
  entry->dirty = true;
  memcpy (entry->data, buffer, BLOCK_SECTOR_SIZE);
  lock_release (&entry->cache_lock);
}

void
cache_flush ()
{
  for (struct list_elem *e = list_begin (&clock_list);
       e != list_end (&clock_list); e = list_next (e))
    {
      struct cache_entry *entry
          = list_entry (e, struct cache_entry, clock_list_elem);
      lock_acquire (&entry->cache_lock);
      if (entry->dirty)
        {
          block_write (fs_device, entry->sector, entry->data);
        }
      lock_release (&entry->cache_lock);
      entry->valid = false;
      entry->dirty = false;
      entry->sector = -1;
    }
}