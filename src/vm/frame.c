#include "frame.h"

#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"

struct lock global_frame_table_lock; // lock for frame table sychronization
struct hash global_frame_table;      // frame table
static struct fte *cur_frame;        // current position for clock list,
                                     // the one to be evicted
static struct list clock_list;       // a cycle list for clock algorithm

static void
clock_list_next (void)
{
  ASSERT (cur_frame != NULL);
  /* check if tail */
  if (list_next (&cur_frame->clock_list_elem) == list_tail (&clock_list))
    cur_frame
        = list_entry (list_begin (&clock_list), struct fte, clock_list_elem);
  else
    cur_frame = list_entry (list_next (&cur_frame->clock_list_elem),
                            struct fte, clock_list_elem);
  ASSERT (cur_frame->phys_addr != NULL)
}

static void
replace_by_clock (void)
{
  // initialize when start replacement
  if (cur_frame == NULL)
    cur_frame
        = list_entry (list_begin (&clock_list), struct fte, clock_list_elem);
  ASSERT (cur_frame != NULL);

  while (pagedir_is_accessed (cur_frame->owner->pagedir, cur_frame->upage))
    {
      ASSERT (cur_frame->phys_addr != NULL)
      /* if accessed, set accessed to false and move to next */
      pagedir_set_accessed (cur_frame->owner->pagedir, cur_frame->upage,
                            false);
      clock_list_next ();
    }
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
  hash_init (&global_frame_table, global_frame_table_hash,
             global_frame_table_less, NULL);
  list_init (&clock_list);
  cur_frame = NULL;
}

/**
 * @brief Automatically evict a frame from the memory
 */
void
frame_evict (void)
{
  replace_by_clock (); // set cur_frame to the frame to be evicted

  ASSERT (cur_frame->phys_addr != NULL)

  struct fte *last_frame = cur_frame;
  fte_evict (cur_frame);
  clock_list_next ();
  list_remove (&last_frame->clock_list_elem);
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
  list_push_back (&clock_list, &fte->clock_list_elem);
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
  ASSERT (fte->owner == thread_current ());
  ASSERT (fte->type == SPTE_FRAME);
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

  fte->type == SPTE_FRAME;
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
