#include "filesys/cache.h"
#include "devices/block.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include <string.h>

static struct list cache_clock_list; // a cycle list for clock algorithm
static struct list_elem *cache_clock_list_iterator;
struct lock cache_clock_list_lock;

struct hash cache_table;

uint8_t cache[CACHE_SIZE][BLOCK_SECTOR_SIZE];
struct cache_entry cache_entries[CACHE_SIZE];
int cache_count;

static void
cache_clock_list_next (void)
{
  cache_clock_list_iterator = (list_next (cache_clock_list_iterator)
                               == list_tail (&cache_clock_list))
                                  ? list_begin (&cache_clock_list)
                                  : list_next (cache_clock_list_iterator);
}

/**
 * @brief Set cur_frame to the frame to be evicted
 * @note This function gaurentees thread safety
 */
static void
cache_clock_list_push_back (struct list_elem *elem)
{
  lock_acquire (&cache_clock_list_lock);
  if (list_empty (&cache_clock_list))
    {
      cache_clock_list_iterator = elem;
      list_push_back (&cache_clock_list, elem);
    }
  else
    list_insert (cache_clock_list_iterator, elem);
  lock_release (&cache_clock_list_lock);
}

/**
 * @brief Clock list remove
 * @param elem the elem in struct fte
 * @note This function gaurentees thread safety
 */
static void
cache_clock_list_remove (struct list_elem *elem)
{
  ASSERT (elem != NULL);
  ASSERT (!list_empty (&cache_clock_list));
  lock_acquire (&cache_clock_list_lock);
  if (cache_clock_list_iterator == elem)
    cache_clock_list_next ();
  list_remove (elem);
  lock_release (&cache_clock_list_lock);
}

static struct cache_entry *
clock_find_block_to_evict (void)
{
  lock_acquire (&cache_clock_list_lock);
  struct cache_entry *evict_entry;
  bool found = false;
  while (!found)
    {
      evict_entry = list_entry (cache_clock_list_iterator, struct cache_entry,
                                list_elem);
      if (lock_try_acquire (&evict_entry->lock))
        {
          if (evict_entry->accessed)
            {
              evict_entry->accessed = false;
              cache_clock_list_next ();
            }
          else
            found = true;
          lock_release (&evict_entry->lock);
        }
      else
        {
          cache_clock_list_next ();
        }
    }

  cache_clock_list_next ();
  list_remove (&evict_entry->list_elem);
  hash_delete (&cache_table, &evict_entry->hash_elem);

  lock_release (&cache_clock_list_lock);
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
  for (int i = 0; i < CACHE_SIZE; i++)
    {
      struct cache_entry *e = &cache_entries[i];
      e->dirty = false;
      e->valid = false;
      e->accessed = false;
      e->sector = 0;
      e->data = &cache[i][0];
      lock_init (&e->lock);
    }
  hash_init (&cache_table, cache_table_hash, cache_table_less, NULL);
  lock_init (&cache_clock_list_lock);
  list_init (&cache_clock_list);
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

static struct cache_entry *
cache_get_block (block_sector_t sector)
{
  struct cache_entry *entry;
  // if cache is full, evict a block
  if (cache_count == CACHE_SIZE)
    {
      entry = clock_find_block_to_evict ();
      lock_acquire (&entry->lock);
      if (entry->dirty)
        block_write (fs_device, entry->sector, entry->data);
      lock_release (&entry->lock);
    }
  else
    entry = &cache_entries[cache_count++];

  block_read (fs_device, sector, entry->data);
  entry->valid = true;
  entry->dirty = false;
  entry->sector = sector;
  hash_insert (&cache_table, &entry->hash_elem);
  cache_clock_list_push_back (&entry->list_elem);
  return entry;
}

void
cache_read (block_sector_t sector, void *buffer)
{
  struct cache_entry *entry = cache_table_find (sector);
  // if entry is not in cache
  if (entry == NULL)
    // evict a cache block
    entry = cache_get_block (sector);

  lock_acquire (&entry->lock);
  entry->accessed = true;
  memcpy (buffer, entry->data, BLOCK_SECTOR_SIZE);
  lock_release (&entry->lock);

  for (int i = 1; i <= READ_AHEAD_COUNT; i++)
    {
      block_sector_t next_sector = sector + i;
      if (next_sector >= block_size (fs_device))
        break;

      struct cache_entry *entry = cache_table_find (next_sector);
      if (entry == NULL)
        entry = cache_get_block (next_sector);

      lock_acquire (&entry->lock);
      entry->accessed = true;
      lock_release (&entry->lock);
    }
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
  lock_acquire (&entry->lock);
  entry->accessed = true;
  entry->dirty = true;
  memcpy (entry->data, buffer, BLOCK_SECTOR_SIZE);
  lock_release (&entry->lock);
}

void
cache_flush ()
{
  for (struct list_elem *e = list_begin (&cache_clock_list);
       e != list_end (&cache_clock_list); e = list_next (e))
    {
      struct cache_entry *entry
          = list_entry (e, struct cache_entry, list_elem);
      lock_acquire (&entry->lock);
      if (entry->dirty)
        {
          block_write (fs_device, entry->sector, entry->data);
        }
      lock_release (&entry->lock);
      entry->valid = false;
      entry->dirty = false;
      entry->sector = 0;
    }
}