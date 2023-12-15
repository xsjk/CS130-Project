#include "frame.h"

#include "threads/malloc.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"

struct lock global_frame_table_lock; // lock for frame table sychronization
struct hash global_frame_table;      // frame table

struct lock clock_list_lock;   // lock for clock list sychronization
static struct list clock_list; // a cycle list for clock algorithm
static struct list_elem *clock_list_iterator;

/**
 * @brief Set cur_frame to the frame to be evicted
 */
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
 */
static void
clock_list_push_back (struct list_elem *elem)
{
  if (list_empty (&clock_list))
    {
      clock_list_iterator = elem;
      list_push_back (&clock_list, elem);
    }
  else
    list_insert (clock_list_iterator, elem);
}

/**
 * @brief Helper function for evicting a frame
 */
static struct fte *
get_target_to_evict (void)
{
  lock_acquire (&clock_list_lock);
  struct fte *evict_target;
  while (true)
    {
      evict_target
          = list_entry (clock_list_iterator, struct fte, clock_list_elem);
      if (pagedir_is_accessed (evict_target->owner->pagedir,
                               evict_target->upage))
        /* if accessed, set accessed to false and move to next */
        {
          pagedir_set_accessed (evict_target->owner->pagedir,
                                evict_target->upage, false);
          clock_list_next ();
        }
      else
        break;
    }

  // Now, cur_frame is the one that satisfies the condition:
  // 1. not accessed
  // 2. is a frame
  // so we can evict it
  ASSERT (evict_target != NULL);
  ASSERT (evict_target->type == SPTE_FRAME);
  ASSERT (evict_target->phys_addr != NULL);
  clock_list_next ();
  list_remove (&evict_target->clock_list_elem);
  lock_release (&clock_list_lock);
  return evict_target;
}

static unsigned
global_frame_table_hash (const struct hash_elem *e, void *aux)
{
  const struct fte *fte = hash_entry (e, struct fte, global_frame_table_elem);
  return hash_bytes (&fte->phys_addr, sizeof fte->phys_addr);
}

static bool
global_frame_table_less (const struct hash_elem *a, const struct hash_elem *b,
                         void *aux)
{
  const struct fte *fte_a
      = hash_entry (a, struct fte, global_frame_table_elem);
  const struct fte *fte_b
      = hash_entry (b, struct fte, global_frame_table_elem);
  return fte_a->phys_addr < fte_b->phys_addr;
}

static unsigned
cur_frame_table_hash (const struct hash_elem *e, void *aux)
{
  const struct fte *fte = hash_entry (e, struct fte, cur_frame_table_elem);
  return hash_bytes (&fte->upage, sizeof fte->upage);
}

static bool
cur_frame_table_less (const struct hash_elem *a, const struct hash_elem *b,
                      void *aux)
{
  const struct fte *fte_a = hash_entry (a, struct fte, cur_frame_table_elem);
  const struct fte *fte_b = hash_entry (b, struct fte, cur_frame_table_elem);
  return fte_a->upage < fte_b->upage;
}

/**
 * @brief initialize current frame table of a thread
 * @param frame_table
 */
void
cur_frame_table_init (struct hash *frame_table)
{
  hash_init (frame_table, cur_frame_table_hash, cur_frame_table_less, NULL);
}

struct fte *
global_cur_frame_table_find (void *frame)
{
  ASSERT (frame == NULL);
  struct fte fte;
  fte.phys_addr = frame;
  struct hash_elem *e
      = hash_find (&global_frame_table, &fte.global_frame_table_elem);
  if (e != NULL)
    return hash_entry (e, struct fte, global_frame_table_elem);
  else
    return NULL;
}

/**
 * @brief find a frame in current frame table
 * @param upage user page
 */
struct fte *
cur_frame_table_find (void *upage)
{
  struct thread *t = thread_current ();
  struct hash *page_table = &t->frame_table;
  struct fte fte;
  fte.upage = upage;
  struct hash_elem *e = hash_find (page_table, &fte.cur_frame_table_elem);
  if (e != NULL)
    return hash_entry (e, struct fte, cur_frame_table_elem);
  return NULL;
}

void
frame_init (void)
{
  lock_init (&global_frame_table_lock);
  lock_init (&clock_list_lock);
  hash_init (&global_frame_table, global_frame_table_hash,
             global_frame_table_less, NULL);
  list_init (&clock_list);
}

/**
 * @brief Automatically evict a frame from the memory
 */
void
frame_evict (void)
{
  fte_evict (get_target_to_evict ());
}

/**
 * Create a frame and install it to current thread
 * @param upage user page
 * @param writable writable or not
 * @param flags flags for palloc_get_page
 * @param swap_index swap index for restore, if -1, then not restore
 * @return fte if success, NULL if failed
 */
struct fte *
fte_create (void *upage, bool writable, enum palloc_flags flags)
{
  bool success = true;

  // malloc fte
  struct fte *fte = malloc (sizeof (struct fte));
  if (fte == NULL)
    return NULL;

  fte->upage = upage;
  fte->writable = writable;
  fte->owner = thread_current ();
  fte->phys_addr = palloc_get_page_force (flags); // eviction may happen here
  fte->type = SPTE_FRAME;

  success = install_page (fte->upage, fte->phys_addr, fte->writable);

  hash_insert (&fte->owner->frame_table, &fte->cur_frame_table_elem);

  ASSERT (cur_frame_table_find (upage));

  lock_acquire (&global_frame_table_lock);
  hash_insert (&global_frame_table, &fte->global_frame_table_elem);

  // initialize cur_frame at the first run
  clock_list_push_back (&fte->clock_list_elem);
  lock_release (&global_frame_table_lock);

  return success ? fte : NULL;
}

/**
 * @brief evict a fte, copy from memory to swap
 * @param fte the frame table entry to evict
 */
void
fte_evict (struct fte *fte)
{
  ASSERT (fte->phys_addr != NULL);

  // remove the virtual map from current thread
  pagedir_clear_page (fte->owner->pagedir, fte->upage);

  void *kpage = fte->phys_addr;
  fte->swap_index = swap_write (kpage);
  palloc_free_page (kpage);

  fte->type = SPTE_SWAP;
}

/**
 * @brief unevict a fte, copy from swap to memory
 * @param fte the frame table entry to unevict
 */
void
fte_unevict (struct fte *fte)
{
  ASSERT (fte->owner == thread_current ());
  ASSERT (fte->type == SPTE_SWAP);

  block_sector_t swap_index = fte->swap_index; // from which to restore

  // force allocate memory, eviction may happen here
  fte->phys_addr = palloc_get_page_force (PAL_USER);

  // restore memory
  swap_read (swap_index, fte->phys_addr);

  // install virtual map
  ASSERT (install_page (fte->upage, fte->phys_addr, fte->writable));

  fte->type = SPTE_FRAME;

  clock_list_push_back (&fte->clock_list_elem);
}

/**
 * @brief destroy a fte
 * @param fte to destroy
 */
void
fte_destroy (struct fte *fte)
{
  ASSERT (fte->owner == thread_current ());
  hash_delete (&fte->owner->frame_table, &fte->cur_frame_table_elem);

  lock_acquire (&global_frame_table_lock);
  hash_delete (&global_frame_table, &fte->global_frame_table_elem);
  lock_release (&global_frame_table_lock);

  free (fte);
}
