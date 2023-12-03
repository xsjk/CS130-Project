#ifndef VM_SUPPLEMENTAL_PAGE_TABLE_H
#define VM_SUPPLEMENTAL_PAGE_TABLE_H

#include "../threads/palloc.h"
#include <hash.h>

enum spte_type
{
  SPTE_FILE,  // file
  SPTE_SWAP,  // swap
  SPTE_FRAME, // frame
  SPTE_ZERO   // zero
};

struct spte
{
  void *upage;                // user page
  void *value;                // phys_addr or swap_index
  enum spte_type type;        // type of the supplemental page table entry
  bool writable;              // writable or not
  struct hash_elem hash_elem; // hash element for supplemental page table
};

void pt_lock_init ();
void pt_lock_acquire ();
void pt_lock_release ();

struct spte *find_page (struct hash *page_table, void *upage);

struct hash *create_page_table ();
void page_destroy (struct hash *page_table);

#endif /* vm/supplemental_page_table.h */