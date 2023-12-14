#include "frame.h"

#include "../threads/synch.h"
#include "../userprog/process.h"
#include "userprog/pagedir.h"

static struct lock frame_table_lock; // lock for frame table sychronization
struct hash all_frame_table;         // frame table
static struct fte *cur_frame;        // current position for clock list
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

unsigned
frame_hash (const struct hash_elem *e, void *aux)
{
  const struct fte *fte = hash_entry (e, struct fte, all_hash_elem);
  return hash_bytes (&fte->phys_addr, sizeof fte->phys_addr);
}

bool
frame_less (const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
  const struct fte *fte_a = hash_entry (a, struct fte, all_hash_elem);
  const struct fte *fte_b = hash_entry (b, struct fte, all_hash_elem);
  return fte_a->phys_addr < fte_b->phys_addr;
}

unsigned
upage_hash (const struct hash_elem *e, void *aux)
{
  const struct fte *fte = hash_entry (e, struct fte, thread_hash_elem);
  return hash_bytes (&fte->upage, sizeof fte->upage);
}

bool
upage_less (const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
  const struct fte *fte_a = hash_entry (a, struct fte, thread_hash_elem);
  const struct fte *fte_b = hash_entry (b, struct fte, thread_hash_elem);
  return fte_a->upage < fte_b->upage;
}

struct fte *
find_frame_from_upage (void *frame)
{
  ASSERT (frame == NULL);
  struct fte fte;
  fte.phys_addr = frame;
  struct hash_elem *e = hash_find (&all_frame_table, &fte.all_hash_elem);
  if (e != NULL)
    return hash_entry (e, struct fte, all_hash_elem);
  else
    return NULL;
}

struct fte *
frame_table_find (struct hash *page_table, void *upage)
{
  struct fte fte;
  fte.upage = upage;
  struct hash_elem *e = hash_find (page_table, &fte.thread_hash_elem);
  if (e != NULL)
    return hash_entry (e, struct fte, thread_hash_elem);
  return NULL;
}

void
frame_init (void)
{
  lock_init (&frame_table_lock);
  hash_init (&all_frame_table, frame_hash, frame_less, NULL);
  list_init (&clock_list);
  cur_frame = NULL;
}

/* page replacement algorithm: LRU by "clock" algorithm */
void
frame_evict (void)
{
  lock_acquire (&frame_table_lock);

  replace_by_clock (); // set cur_frame to the frame to be evicted

  ASSERT (cur_frame->phys_addr != NULL)

  void *kpage = cur_frame->phys_addr;

  /* write to swap & change the type */
  cur_frame->swap_index = swap_write (cur_frame->upage);

  cur_frame->type = SPTE_SWAP;

  palloc_free_page (kpage);
  pagedir_set_dirty (cur_frame->owner->pagedir, cur_frame->upage, true);
  pagedir_clear_page (cur_frame->owner->pagedir, cur_frame->upage);

  // hash_delete (&all_frame_table, &cur_frame->all_hash_elem);
  struct fte *last_frame = cur_frame;
  // free (cur_frame);
  clock_list_next ();
  list_remove (&last_frame->clock_list_elem);

  lock_release (&frame_table_lock);
}

/**
 * Install a frame
 * @param upage user page
 * @param writable writable or not
 * @param flags flags for palloc_get_page
 * @param swap_index swap index for restore, if -1, then not restore
 * @return fte if success, NULL if failed
 */
struct fte *
frame_install (void *upage, bool writable, enum palloc_flags flags,
               block_sector_t swap_index)
{
  bool success = true;

  // malloc fte
  struct fte *fte = malloc (sizeof (struct fte));
  if (fte == NULL)
    return NULL;
  fte->upage = upage;
  fte->writable = writable;
  fte->owner = thread_current ();

  char *kpage = palloc_get_page (flags);

  // try create frame
  if (kpage)
    {
      fte->phys_addr = kpage;
    }
  else
    {
      // evict a frame
      frame_evict ();
      // try alloc again
      fte->phys_addr = palloc_get_page (flags);
      if (!fte->phys_addr)
        PANIC ("No free frame");
    }

  fte->type = SPTE_FRAME;

  // restore if necessary
  if (swap_index != (block_sector_t)-1)
    {
      swap_read (swap_index, fte->phys_addr);
    }

  success = install_page (fte->upage, fte->phys_addr, fte->writable);

  hash_insert (&fte->owner->frame_table, &fte->thread_hash_elem);

  ASSERT (frame_table_find (&fte->owner->frame_table, upage));

  lock_acquire (&frame_table_lock);
  hash_insert (&all_frame_table, &fte->all_hash_elem);
  list_push_back (&clock_list, &fte->clock_list_elem);
  lock_release (&frame_table_lock);
  return success ? fte : NULL;
}
