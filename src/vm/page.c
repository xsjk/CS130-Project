#include "page.h"
#include "frame.h"
#include "swap.h"

#include "../threads/vaddr.h"
#include "../userprog/pagedir.h"
#include "../userprog/process.h"
#include "filesys/off_t.h"

#define STACK_MAX (1 << 23) // 8MB

static struct lock all_frame_table_lock; // lock for supplemental page table

void
pt_lock_init ()
{
  lock_init (&all_frame_table_lock);
}

void
pt_lock_acquire ()
{
  lock_acquire (&all_frame_table_lock);
}

void
pt_lock_release ()
{
  lock_release (&all_frame_table_lock);
}

struct fte *
fte_create (void *upage, bool writable, enum palloc_flags flags)
{
  // malloc fte
  struct fte *fte = malloc (sizeof (struct fte));
  if (fte == NULL)
    return NULL;
  fte->upage = upage;
  fte->writable = writable;
  fte->phys_addr = palloc_get_page (flags);
  fte->owner = thread_current ();

  // try create frame
  if (fte->phys_addr)
    fte->type = SPTE_FRAME;
  else
    {
      free (fte);
      // evict a frame
      frame_evict ();
      // try alloc again
      fte->phys_addr = palloc_get_page (flags);
      if (!fte->phys_addr)
        PANIC ("No free frame");
    }
  hash_insert (&fte->owner->frame_table, &fte->thread_hash_elem);
  lock_acquire (&all_frame_table_lock);
  hash_insert (&all_frame_table, &fte->all_hash_elem);
  lock_release (&all_frame_table_lock);
  return fte;
}

void
fte_destroy (struct fte *fte)
{
  ASSERT (fte->owner == thread_current ());
  hash_delete (&fte->owner->frame_table, &fte->thread_hash_elem);
  lock_acquire (&all_frame_table_lock);
  hash_delete (&all_frame_table, &fte->all_hash_elem);
  lock_release (&all_frame_table_lock);
  free (fte);
}

bool
user_stack_grouth (void *fault_addr, void *esp)
{
  void *upage = pg_round_down (fault_addr);
  struct thread *t = thread_current ();
  bool writable = true;
  bool success = true;

  struct fte *fte = frame_table_find (&thread_current ()->frame_table, upage);

  if (fte == NULL)
    {
      // try stack growth
      if (fault_addr >= esp - 32 && fault_addr < PHYS_BASE
          && upage >= PHYS_BASE - STACK_MAX)
        {
          frame_install (upage, writable, PAL_USER, -1);
        }
    }
  else
    {
      switch (fte->type)
        {
        case SPTE_FRAME:
          PANIC (" frame should not exist, otherwise won't raise page fault.");
          break;
        case SPTE_SWAP:
          {
            /* read from swap to new frame & change type to frame*/
            block_sector_t swap_index = fte->swap_index;
            fte = frame_install (upage, fte->writable, PAL_USER, swap_index);
          }
          break;
        }
    }
  return success;
}