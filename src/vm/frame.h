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

extern struct lock clock_list_lock;

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
    void *kpage;       // frame
    swap_id_t swap_id; // swap
  };

  uint32_t file_offset;
  struct mmap_entry *mmap_entry; // file

  enum frame_type type; // type of the supplemental page table entry
  bool writable;        // writable or not

  struct hash_elem cur_frame_table_elem; // hash element for frame table

  struct list_elem clock_list_elem; // list element for clock algorithm
  struct list_elem fte_elem;

  struct lock lock; // lock for frame table entry
};

struct fte *cur_frame_table_find (void *upage);

/* ------------------- Current Frame Table Methods -------------------- */

void cur_frame_table_init (struct hash *cur_frame_table);
struct fte *cur_frame_table_find (void *upage);

/* ------------------- Global Frame Table Methods -------------------- */

void frame_init (void);  // initialize frame table
void frame_evict (void); // automatically evict a frame

/* -------------------- Frame Table Entry Methods -------------------- */

struct fte *fte_create (void *upage, bool writable);
void fte_evict (struct fte *fte);
void fte_unevict (struct fte *fte);
void fte_destroy (struct fte *fte);
struct fte *fte_attach_to_file (struct file *file, uint32_t file_offset,
                                void *upage, bool writable);
void fte_detach_from_file (struct fte *fte);

typedef void (*mmap_elem_destroy_func) (struct fte *);

void mmap_destroy (struct mmap_entry *mmap_entry,
                   mmap_elem_destroy_func destroy_func);

#endif /* vm/frame.h */