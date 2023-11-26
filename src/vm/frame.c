#include "frame.h"

#include "../threads/synch.h"

struct lock frame_table_lock;        // lock for frame table sychronization
struct hash frame_table;             // frame table
struct frame_table_entry *cur_frame; // current frame

static unsigned
frame_hash (const struct hash_elem *e, void *aux)
{
  const struct frame_table_entry *fte
      = hash_entry (e, struct frame_table_entry, hash_elem);
  return hash_bytes (&fte->frame, sizeof fte->frame);
}

static unsigned
frame_less (const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
  const struct frame_table_entry *fte_a
      = hash_entry (a, struct frame_table_entry, hash_elem);
  const struct frame_table_entry *fte_b
      = hash_entry (b, struct frame_table_entry, hash_elem);
  return fte_a->frame < fte_b->frame;
}

void
frame_init (void)
{
  lock_init (&frame_table_lock);
  hash_init (&frame_table, frame_hash, frame_less, NULL);
  cur_frame = NULL;
}

/* page replacement algorithm: LRU by "clock" algorithm */
void
frame_evict (void)
{
  lock_acquire (&frame_table_lock);
  /// TODO: implement clock algorithm
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
      frame_evict ();
      frame = palloc_get_page (flags);
    }

  if (frame == NULL)
    PANIC ("No free frame");

  struct frame_table_entry *fte = malloc (sizeof *fte);
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
  struct frame_table_entry fte;
  fte.frame = frame;
  struct hash_elem *e = hash_find (&frame_table, &fte.hash_elem);
  if (e != NULL)
    {
      struct frame_table_entry *fte
          = hash_entry (e, struct frame_table_entry, hash_elem);
      hash_delete (&frame_table, &fte->hash_elem);
      free (fte);
      palloc_free_page (frame);
    }
  else
    PANIC ("Frame not found");
  lock_release (&frame_table_lock);
}