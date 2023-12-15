#ifndef VM_FRAME_H
#define VM_FRAME_H
#include "swap.h"

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

  struct hash_elem cur_frame_table_elem;    // hash element for frame table
  struct hash_elem global_frame_table_elem; // hash element for frame table

  struct list_elem clock_list_elem; // list element for clock algorithm
};

struct fte *cur_frame_table_find (void *upage);

/* ------------------- Current Frame Table Methods -------------------- */

void cur_frame_table_init (struct hash *cur_frame_table);
struct fte *cur_frame_table_find (void *upage);

/* ------------------- Global Frame Table Methods -------------------- */

struct fte *global_cur_frame_table_find (void *upage);
void frame_init (void);  // initialize frame table
void frame_evict (void); // automatically evict a frame

/* -------------------- Frame Table Entry Methods -------------------- */

struct fte *fte_create (void *upage, bool writable, enum palloc_flags flags);
void fte_evict (struct fte *fte);
void fte_unevict (struct fte *fte);
void fte_destroy (struct fte *fte);

#endif /* vm/frame.h */