#ifndef VM_FRAME_H
#define VM_FRAME_H
#include "swap.h"

#include "threads/palloc.h"
#include "threads/thread.h"
#include <hash.h>

extern struct hash all_frame_table; // frame table

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

  void *upage; // user page
  union
  {
    void *phys_addr; // frame
    struct           // file
    {
      uint32_t file_offset;
      struct file *file;
    };
    block_sector_t swap_index; // swap
  };

  enum frame_type type; // type of the supplemental page table entry
  bool writable;        // writable or not

  struct hash_elem thread_hash_elem; // hash element for frame table
  struct hash_elem all_hash_elem;    // hash element for frame table

  struct list_elem clock_list_elem; // list element for clock algorithm
};

unsigned frame_hash (const struct hash_elem *p_, void *aux); // hash function
bool frame_less (const struct hash_elem *a_, const struct hash_elem *b_,
                 void *aux); // less function

unsigned upage_hash (const struct hash_elem *p_, void *aux); // hash function
bool upage_less (const struct hash_elem *a_, const struct hash_elem *b_,
                 void *aux); // less function

struct fte *find_frame_from_upage (void *upage); // find a frame
struct fte *frame_table_find (struct hash *page_table, void *upage);

void frame_init (void);  // initialize frame table
void frame_evict (void); // evict a frame
struct fte *frame_install (void *upage, bool writable, enum palloc_flags flags,
                           block_sector_t swap_index); // install a frame

#endif /* vm/frame.h */