#include "page.h"
#include "frame.h"

#include "../threads/vaddr.h"
#include "../userprog/pagedir.h"
#include "filesys/off_t.h"

#define STACK_MAX (1 << 23) // 8MB

static struct lock page_table_lock; // lock for supplemental page table

static void
page_destroy_action (struct hash_elem *e, void *aux)
{
  struct fte *fte = hash_entry (e, struct fte, thread_hash_elem);
  if (fte->type == SPTE_FRAME)
    {
      frame_free (fte->value);
      pagedir_clear_page (thread_current ()->pagedir, fte->upage);
    }
  free (fte);
}

void
pt_lock_init ()
{
  lock_init (&page_table_lock);
}

void
pt_lock_acquire ()
{
  lock_acquire (&page_table_lock);
}

void
pt_lock_release ()
{
  lock_release (&page_table_lock);
}

struct fte *
fte_create (void *upage, void *value, bool writable)
{
  bool success = true;
  // malloc fte
  struct fte *fte = malloc (sizeof (struct fte));
  if (fte == NULL)
    return NULL;
  fte->upage = upage;
  fte->value = value;
  fte->writable = writable;
  // try create frame
  // success = install_page (upage, value, writable);
  if (success)
    fte->type = SPTE_FRAME;
  else
    {
      // evict a frame
      frame_evict ();
      // try install again
      // install_page (upage, value, writable);
    }
  return fte;
}

void
fte_destroy (struct hash *page_table, struct fte *fte)
{
  hash_delete (page_table, &fte->thread_hash_elem);
  free (fte);
}

struct hash *
frame_table_create ()
{
  struct hash *page_table = malloc (sizeof (struct hash));
  if (page_table == NULL)
    return NULL;
  hash_init (page_table, frame_hash, frame_less, NULL);
  return page_table;
}

void
page_destroy (struct hash *page_table)
{
  lock_acquire (&page_table_lock);
  hash_destroy (page_table, page_destroy_action);
  lock_release (&page_table_lock);
}

bool
page_fault_handler (void *fault_addr, void *esp, bool write)
{
  void *upage = pg_round_down (fault_addr);
  struct thread *t = thread_current ();
  bool success = true;

  pt_lock_acquire ();
  struct fte *fte
      = frame_table_find (thread_current ()->frame_table, fault_addr);
  void *frame;

  // stack growth
  if (upage >= PHYS_BASE - STACK_MAX)
    {
      if (fault_addr >= esp - 32)
        {
          if (fte == NULL)
            {
              frame = frame_alloc (0, upage);
              if (frame == NULL)
                success = false;
              else
                {
                  // add new fte
                  fte = malloc (sizeof (struct fte));
                  fte->type = SPTE_FRAME;
                  fte->upage = upage;
                  fte->value = frame;
                  fte->writable = true;
                  hash_insert (t->frame_table, &fte->thread_hash_elem);
                }
            }
        }
      else
        {
          success = false;
        }
    }
  pt_lock_release ();
  return success;
}