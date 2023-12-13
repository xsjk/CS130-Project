#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/palloc.h"
#include "threads/thread.h"
#include <hash.h>

enum frame_type
{
  SPTE_FILE,  // file
  SPTE_SWAP,  // swap
  SPTE_FRAME, // frame
  SPTE_ZERO   // zero
};

struct fte
{
  struct thread *owner; // pointer to the owner thread

  void *upage;          // user page
  void *value;          // phys_addr or swap_index or file_offset
  enum frame_type type; // type of the supplemental page table entry
  bool writable;        // writable or not

  struct hash_elem thread_hash_elem; // hash element for frame table
  struct hash_elem all_hash_elem;    // hash element for frame table

  struct list_elem list_elem; // list element for clock algorithm
};

unsigned frame_hash (const struct hash_elem *p_, void *aux); // hash function
bool frame_less (const struct hash_elem *a_, const struct hash_elem *b_,
                 void *aux); // less function

struct fte *find_frame_from_upage (void *upage); // find a frame
struct fte *frame_table_find (struct hash *page_table, void *upage);

void frame_init (void);  // initialize frame table
void frame_evict (void); // evict a frame
void *frame_alloc (enum palloc_flags flags, void *upage); // allocate a frame
void frame_free (void *page);                             // free a frame

#endif /* vm/frame.h */