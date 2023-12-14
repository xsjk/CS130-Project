#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "devices/block.h"
#include "threads/thread.h"

struct swap_ele
{
  block_sector_t swap_index;
  struct list_elem swap_elem;
};

void swap_init (void);

block_sector_t
swap_find_free (void); // find a free swap block from free_swap_slots

void swap_free (block_sector_t);         // free a swap block
block_sector_t swap_write (void *);      // write 8 sectors to swap block
void swap_read (block_sector_t, void *); // read 8 sectors from swap block

#endif /* vm/swap.h */