#include "frame.h"

#include "../threads/synch.h"
#include "userprog/pagedir.h"

static struct lock frame_table_lock; // lock for frame table sychronization
static struct hash frame_table;      // frame table
static struct frame_table_entry *cur_frame; // current position for clock list
static struct list clock_list;              // a cycle list for clock algorithm

static void
clock_list_next (void)
{
  ASSERT (cur_frame != NULL);
  /* check if tail */
  if (list_next (&cur_frame->list_elem) == list_tail (&clock_list))
    cur_frame = list_entry (list_begin (&clock_list), struct frame_table_entry,
                            list_elem);
  else
    cur_frame = list_entry (list_next (&cur_frame->list_elem),
                            struct frame_table_entry, list_elem);
}

static void
replace_by_clock (void)
{
  ASSERT (cur_frame != NULL);
  while (pagedir_is_accessed (cur_frame->owner->pagedir, cur_frame->upage))
    {
      /* if accessed, set accessed to false and move to next */
      pagedir_set_accessed (cur_frame->owner->pagedir, cur_frame->upage,
                            false);
      clock_list_next ();
    }
  struct frame_table_entry *fte = cur_frame;
  void *frame = fte->frame;
}

static unsigned
frame_hash (const struct hash_elem *e, void *aux)
{
  const struct frame_table_entry *fte
      = hash_entry (e, struct frame_table_entry, hash_elem);
  return hash_bytes (&fte->frame, sizeof fte->frame);
}

static bool
frame_less (const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
  const struct frame_table_entry *fte_a
      = hash_entry (a, struct frame_table_entry, hash_elem);
  const struct frame_table_entry *fte_b
      = hash_entry (b, struct frame_table_entry, hash_elem);
  return fte_a->frame < fte_b->frame;
}

struct frame_table_entry *
find_frame (void *frame)
{
  struct frame_table_entry fte;
  fte.frame = frame;
  struct hash_elem *e = hash_find (&frame_table, &fte.hash_elem);
  if (e != NULL)
    return hash_entry (e, struct frame_table_entry, hash_elem);
  else
    return NULL;
}

void
frame_init (void)
{
  lock_init (&frame_table_lock);
  hash_init (&frame_table, frame_hash, frame_less, NULL);
  list_init (&clock_list);
  cur_frame = NULL;
}

/* page replacement algorithm: LRU by "clock" algorithm */
void
frame_evict (void)
{
  lock_acquire (&frame_table_lock);

  replace_by_clock ();

  /* write back to swap */
  if (pagedir_is_dirty (cur_frame->owner->pagedir, cur_frame->upage))
    {
      /// TODO: write back to swap
      // swap_write (cur_frame->frame, cur_frame->upage);
      pagedir_set_dirty (cur_frame->owner->pagedir, cur_frame->upage, false);
    }

  pagedir_clear_page (cur_frame->owner->pagedir, cur_frame->upage);
  hash_delete (&frame_table, &cur_frame->hash_elem);
  free (cur_frame);

  lock_release (&frame_table_lock);
}

void *
frame_alloc (enum palloc_flags flags, void *upage)
{
  lock_acquire (&frame_table_lock);
  void *frame = palloc_get_page (flags);

  /* No free frame, evict a frame and try again */

  if (frame == NULL)
    {
      PANIC ("No free frame"); // not implemented yet

      // frame_evict ();
      // frame = palloc_get_page (flags);
      // if (frame == NULL)
      //   PANIC ("No free frame");
    }

  struct frame_table_entry *fte
      = (struct frame_table_entry *)malloc (sizeof (struct frame_table_entry));
  fte->frame = frame;
  fte->owner = thread_current ();
  fte->upage = upage;
  hash_insert (&frame_table, &fte->hash_elem);
  lock_release (&frame_table_lock);
  return frame;
}

void
frame_free (void *frame)
{
  lock_acquire (&frame_table_lock);
  struct frame_table_entry *fte = find_frame (frame);
  ASSERT (fte != NULL);
  hash_delete (&frame_table, &fte->hash_elem);
  free (fte);
  palloc_free_page (frame);
  lock_release (&frame_table_lock);
}