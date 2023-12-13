#ifndef VM_SUPPLEMENTAL_PAGE_TABLE_H
#define VM_SUPPLEMENTAL_PAGE_TABLE_H

#include "../threads/palloc.h"
#include <hash.h>

// struct spte
// {
//   void *upage;                // user page
//   void *value;                // phys_addr or swap_index or file_offset
//   enum spte_type type;        // type of the supplemental page table entry
//   bool writable;              // writable or not
//   struct hash_elem hash_elem; // hash element for supplemental page table
// };

void pt_lock_init ();
void pt_lock_acquire ();
void pt_lock_release ();

struct hash *frame_table_create ();
void page_destroy (struct hash *page_table);

struct fte *fte_create (void *upage, void *value, bool writable);
void fte_destroy (struct hash *page_table, struct fte *spte);

bool page_fault_handler (void *fault_addr, void *esp, bool write);

#endif /* vm/supplemental_page_table.h */