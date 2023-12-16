#include "swap.h"
#include "bitmap.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

static struct block *swap_device;
static struct bitmap *swap_used_map;
static struct lock swap_lock;

#define BLOCK_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

/**
 * @brief initalize the swap
 */
void
swap_init (void)
{
  swap_device = block_get_role (BLOCK_SWAP);
  swap_used_map = bitmap_create (block_size (swap_device) / BLOCK_PER_PAGE);
  lock_init (&swap_lock);
}

/**
 * @brief alloc a block in swap table
 * @return the swap index (block sector / 8)
 */
swap_id_t
swap_alloc ()
{
  lock_acquire (&swap_lock);
  swap_id_t swap_idx = bitmap_scan_and_flip (swap_used_map, 0, 1, false);
  lock_release (&swap_lock);
  if (swap_idx == BITMAP_ERROR)
    PANIC ("Swap if full");
  return swap_idx;
}

/**
 * @brief free a block in swap table
 * @param swap_idx
 */
void
swap_free (swap_id_t swap_idx)
{
  ASSERT (bitmap_all (swap_used_map, swap_idx, 1));
  bitmap_set_multiple (swap_used_map, swap_idx, 1, false);
}

/**
 * @brief write a page to swap
 * @param src the source page
 * @return block_selector in which the source page is written
 */
swap_id_t
swap_write (void *src)
{
  swap_id_t swap_idx = swap_alloc ();
  for (int i = 0; i < BLOCK_PER_PAGE; i++)
    block_write (swap_device, swap_idx * BLOCK_PER_PAGE + i,
                 src + i * BLOCK_SECTOR_SIZE);
  return swap_idx;
}

/**
 * @brief read a page from swap
 * @param swap_idx the index of the page to read
 * @param dst the buffer to store the page
 */
void
swap_read (swap_id_t swap_idx, void *dst)
{
  for (int i = 0; i < BLOCK_PER_PAGE; i++)
    block_read (swap_device, swap_idx * BLOCK_PER_PAGE + i,
                dst + i * BLOCK_SECTOR_SIZE);
}
