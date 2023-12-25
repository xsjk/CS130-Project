#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "devices/block.h"

void swap_init (void);

typedef unsigned swap_id_t;

swap_id_t swap_alloc (void);
void swap_free (swap_id_t);
swap_id_t swap_write (void *);
void swap_read (swap_id_t, void *);

#endif /* vm/swap.h */