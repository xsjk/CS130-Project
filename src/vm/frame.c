#include "frame.h"

#include "../threads/synch.h"
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
  if (list_next (&cur_frame->list_elem) == list_tail (&clock_list))
    cur_frame = list_entry (list_begin (&clock_list), struct fte, list_elem);
  else
    cur_frame = list_entry (list_next (&cur_frame->list_elem), struct fte,
                            list_elem);
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
  struct fte *fte = cur_frame;
  void *frame = fte->phys_addr;
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
  const struct fte *fte = hash_entry (e, struct fte, all_hash_elem);
  return hash_bytes (&fte->upage, sizeof fte->upage);
}

bool
upage_less (const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
  const struct fte *fte_a = hash_entry (a, struct fte, all_hash_elem);
  const struct fte *fte_b = hash_entry (b, struct fte, all_hash_elem);
  return fte_a->upage < fte_b->upage;
}

struct fte *
find_frame_from_upage (void *frame)
{
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

  replace_by_clock ();

  /* write back to swap */
  if (pagedir_is_dirty (cur_frame->owner->pagedir, cur_frame->upage))
    {
      /// TODO: write back to swap
      // swap_write (cur_frame->frame, cur_frame->upage);
      pagedir_set_dirty (cur_frame->owner->pagedir, cur_frame->upage, false);
    }

  pagedir_clear_page (cur_frame->owner->pagedir, cur_frame->upage);
  hash_delete (&all_frame_table, &cur_frame->all_hash_elem);
  free (cur_frame);

  lock_release (&frame_table_lock);
}

// void *
// frame_alloc (enum palloc_flags flags, void *upage)
// {
//   lock_acquire (&frame_table_lock);
//   void *frame = palloc_get_page (flags);

//   /* No free frame, evict a frame and try again */

//   if (frame == NULL)
//     {
//       PANIC ("No free frame"); // not implemented yet

//       // frame_evict ();
//       // frame = palloc_get_page (flags);
//       // if (frame == NULL)
//       //   PANIC ("No free frame");
//     }

//   struct fte *fte = (struct fte *)malloc (sizeof (struct fte));
//   fte->phys_addr = frame;
//   fte->owner = thread_current ();
//   fte->upage = upage;
//   hash_insert (&frame_table, &fte->all_hash_elem);
//   lock_release (&frame_table_lock);
//   return frame;
// }

// void
// frame_free (void *page)
// {
//   lock_acquire (&frame_table_lock);
//   struct fte *fte = find_frame_from_upage (page);
//   ASSERT (fte != NULL);
//   hash_delete (&frame_table, &fte->all_hash_elem);
//   free (fte);
//   palloc_free_page (page);
//   lock_release (&frame_table_lock);
// }