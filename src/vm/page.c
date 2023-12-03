#include "page.h"
#include "frame.h"

#include "../userprog/pagedir.h"

static struct lock page_table_lock; // lock for supplemental page table

static void
page_destroy_action (struct hash_elem *e, void *aux)
{
  struct spte *spte = hash_entry (e, struct spte, hash_elem);
  if (spte->type == SPTE_FRAME)
    {
      frame_free (spte->value);
      pagedir_clear_page (thread_current ()->pagedir, spte->upage);
    }
  free (spte);
}

static unsigned
page_hash (const struct hash_elem *e, void *aux)
{
  const struct spte *spte = hash_entry (e, struct spte, hash_elem);
  return hash_bytes (&spte->upage, sizeof spte->upage);
}

static bool
page_less (const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
  const struct spte *spte_a = hash_entry (a, struct spte, hash_elem);
  const struct spte *spte_b = hash_entry (b, struct spte, hash_elem);
  return spte_a->upage < spte_b->upage;
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

struct spte *
find_page (struct hash *page_table, void *upage)
{
  struct spte spte;
  spte.upage = upage;
  struct hash_elem *e = hash_find (page_table, &spte.hash_elem);
  if (e != NULL)
    return hash_entry (e, struct spte, hash_elem);
  return NULL;
}

struct hash *
create_page_table ()
{
  struct hash *page_table = malloc (sizeof (struct hash));
  if (page_table == NULL)
    return NULL;
  hash_init (page_table, page_hash, page_less, NULL);
  return page_table;
}

void
page_destroy (struct hash *page_table)
{
  lock_acquire (&page_table_lock);
  hash_destroy (page_table, page_destroy_action);
  lock_release (&page_table_lock);
}