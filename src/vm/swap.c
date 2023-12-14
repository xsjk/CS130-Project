#include "swap.h"

struct list free_swap_slots; // list for swap out block
struct block *block_entry;   // pointer to swap table
block_sector_t block_end;    // index for next swap block to use
struct lock swap_lock;       // lock for swap table

void
swap_init (void)
{
  list_init (&free_swap_slots);
  lock_init (&swap_lock);
  block_entry = block_get_role (BLOCK_SWAP);
  block_end = 0;
}

block_sector_t
swap_find_free (void)
{
  // if no free block
  if (list_empty (&free_swap_slots))
    {
      block_sector_t swap_index = block_end;
      block_end += 8;
      if (block_end >= block_size (block_entry))
        PANIC ("swap table is full");
      return swap_index;
    }

  else
    {
      struct list_elem *e = list_pop_front (&free_swap_slots);
      struct swap_ele *swap = list_entry (e, struct swap_ele, swap_elem);
      return swap->swap_index;
    }
}

void
swap_free (block_sector_t swap_index)
{
  // if delete the last block, then index should be the last block
  if (swap_index + 8 == block_end)
    {
      block_end = swap_index;
      return;
    }

  struct swap_ele *swap = malloc (sizeof (struct swap_ele));
  swap->swap_index = swap_index;
  list_push_back (&free_swap_slots, &swap->swap_elem);
}

block_sector_t
swap_write (void *kpage)
{
  lock_acquire (&swap_lock);
  block_sector_t swap_index = swap_find_free ();
  for (int i = 0; i < 8; i++)
    {
      block_write (block_entry, swap_index + i, kpage + i * BLOCK_SECTOR_SIZE);
    }
  lock_release (&swap_lock);
  return swap_index;
}

void
swap_read (block_sector_t swap_index, void *kpage)
{
  lock_acquire (&swap_lock);
  for (int i = 0; i < 8; i++)
    {
      block_read (block_entry, swap_index + i, kpage + i * BLOCK_SECTOR_SIZE);
    }
  swap_free (swap_index);
  lock_release (&swap_lock);
}
