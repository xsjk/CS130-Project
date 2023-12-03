#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/palloc.h"
#include "threads/thread.h"
#include <hash.h>

struct frame_table_entry
{
  void *frame;                // pointer to the frame
  struct thread *owner;       // pointer to the owner thread
  void *upage;                // pointer to the user page
  struct hash_elem hash_elem; // hash element for frame table
  struct list_elem list_elem; // list element for clock algorithm
};

struct frame_table_entry *find_frame (void *frame); // find a frame

void frame_init (void);  // initialize frame table
void frame_evict (void); // evict a frame
void *frame_alloc (enum palloc_flags flags, void *upage); // allocate a frame
void frame_free (void *frame);                            // free a frame

#endif /* vm/frame.h */