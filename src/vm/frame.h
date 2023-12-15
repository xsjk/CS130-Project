#ifndef VM_FRAME_H
#define VM_FRAME_H
#include "swap.h"

#include "threads/palloc.h"
#include "threads/thread.h"
#include <hash.h>

extern struct hash global_frame_table; // frame table

enum frame_type
{
  SPTE_FILE,  // file
  SPTE_SWAP,  // swap
  SPTE_FRAME, // frame
  SPTE_ZERO   // zero
};

struct fte;

struct mmap_entry
{
  int mapid;
  struct file *file;
  struct list fte_list;
};

struct fte
{
  struct thread *owner; // pointer to the owner thread

  void *upage; // user page
  union
  {
    void *phys_addr;           // frame
    block_sector_t swap_index; // swap
  };

  uint32_t file_offset;
  struct mmap_entry *mmap_entry; // file

  enum frame_type type; // type of the supplemental page table entry
  bool writable;        // writable or not

  struct hash_elem cur_frame_table_elem;    // hash element for frame table
  struct hash_elem global_frame_table_elem; // hash element for frame table

  struct list_elem clock_list_elem; // list element for clock algorithm
  struct list_elem fte_elem;
};

struct fte *cur_frame_table_find (void *upage);

/* ------------------- Current Frame Table Methods -------------------- */

void cur_frame_table_init (struct hash *cur_frame_table);
struct fte *cur_frame_table_find (void *upage);

/* ------------------- Global Frame Table Methods -------------------- */

struct fte *global_frame_table_find (void *upage);
void frame_init (void);  // initialize frame table
void frame_evict (void); // automatically evict a frame

/* -------------------- Frame Table Entry Methods -------------------- */

struct fte *fte_create (void *upage, bool writable, enum palloc_flags flags);
void fte_evict (struct fte *fte);
void fte_unevict (struct fte *fte);
void fte_destroy (struct fte *fte);
struct fte *fte_attach_to_file (struct file *file, uint32_t file_offset,
                                void *upage, bool writable);
void fte_detach_to_file (struct fte *fte);

void frame_lock_acquire (void); // acquire frame table lock
void frame_lock_release (void); // release frame table lock

typedef void (*mmap_elem_destroy_func) (struct mmap_entry *mmap_entry);

void mmap_destroy (struct mmap_entry *mmap_entry,
                   mmap_elem_destroy_func destroy_func);

#endif /* vm/frame.h */