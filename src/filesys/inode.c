#include "filesys/inode.h"
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <debug.h>
#include <list.h>
#include <round.h>
#include <string.h>

/**
 * @brief Get the number of sectors to allocate for an inode `size` bytes long.
 * @param size
 * @return the number of sectors
 */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/**
 * @brief Get a block device sector that contains byte offset `pos` within
 * `inode`,
 * @param inode
 * @param pos
 * @return the block device sector,
 * @return INVALID_SECTOR if `inode` does not contain data for a byte at offset
 */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  int i, j, k;

  i = pos / BLOCK_SECTOR_SIZE;

  if (i < DIRECT_POINTERS)
    return inode->data.direct[i];

  i -= DIRECT_POINTERS;
  j = i / POINTERS_PER_BLOCK;
  i = i % POINTERS_PER_BLOCK;

  if (j < INDIRECT_BLOCKS)
    {
      block_sector_t p[POINTERS_PER_BLOCK];
      cache_read (inode->data.indirect[j], p);
      return p[i];
    }

  j -= INDIRECT_BLOCKS;
  i -= INDIRECT_POINTERS;
  k = j / POINTERS_PER_BLOCK;
  j = j % POINTERS_PER_BLOCK;
  i = i % POINTERS_PER_BLOCK;

  if (k < IINDIRECT_BLOCKS)
    {
      block_sector_t p[POINTERS_PER_BLOCK];
      cache_read (inode->data.iindirect[k], p);
      cache_read (p[j], p);
      return p[i];
    }

  PANIC ("out of max size of a inode");
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
}

/**
 * @brief alloc a zeroed sector
 * @param sectorp the pointer to the sector which store the return value
 * @return true if successfully allocated else false
 */
static bool
block_calloc (block_sector_t *sectorp)
{
  static uint8_t zeros[BLOCK_SECTOR_SIZE];
  if (!free_map_allocate (1, sectorp))
    return false;
  cache_write (*sectorp, zeros);
  return true;
}

/**
 * @brief resize the direct sector array to `n`,
 * call `block_calloc` for unallocated (non-zero) slots
 * @param sectorp the array of sectors
 * @param n the target size
 * @return true if successfully allocated else false
 */
static bool
block_arr_resize (block_sector_t direct_sectorp[], int n)
{
  int i = 0;
  for (; i < n; i++)
    if (direct_sectorp[i] == 0)
      if (!block_calloc (&direct_sectorp[i]))
        return false;
  return i == n;
}

/**
 * @brief resize the inode size to `n`,
 * extend if `n` is smaller than current data blocks, else do nothing
 * @param inode the target to resize
 * @param n the size to extend to
 * @return true if successfully resized
 */
static bool
inode_disk_resize (struct inode_disk *inode, int n)
{
  if (n > DIRECT_POINTERS + INDIRECT_POINTERS + IINDIRECT_POINTERS)
    return false;

  int direct_ptr_count = MIN (n, DIRECT_POINTERS);
  if (!block_arr_resize (inode->direct, direct_ptr_count))
    return false;

  n -= direct_ptr_count;

  int indirect_ptr_count = MIN (n, INDIRECT_POINTERS);
  int indirect_block_count
      = DIV_ROUND_UP (indirect_ptr_count, POINTERS_PER_BLOCK);

  if (!block_arr_resize (inode->indirect, indirect_block_count))
    return false;

  for (int j = 0; j < indirect_block_count; j++)
    {
      block_sector_t p[POINTERS_PER_BLOCK];
      cache_read (inode->indirect[j], p);
      int ptr_count = MIN (n, POINTERS_PER_BLOCK);
      if (!block_arr_resize (p, ptr_count))
        return false;
      cache_write (inode->indirect[j], p);
      n -= ptr_count;
    }

  int iindirect_ptr_count = MIN (n, IINDIRECT_POINTERS);
  int iindirect_block_count = DIV_ROUND_UP (
      iindirect_ptr_count, POINTERS_PER_BLOCK * POINTERS_PER_BLOCK);

  if (!block_arr_resize (inode->iindirect, iindirect_block_count))
    return false;

  for (int k = 0; k < iindirect_block_count; k++)
    {
      block_sector_t pp[POINTERS_PER_BLOCK];
      cache_read (inode->iindirect[k], pp);

      int indirect_block_count
          = MIN (DIV_ROUND_UP (n, POINTERS_PER_BLOCK), POINTERS_PER_BLOCK);

      if (!block_arr_resize (pp, indirect_block_count))
        return false;

      for (int j = 0; j < indirect_block_count; j++)
        {
          block_sector_t p[POINTERS_PER_BLOCK];
          cache_read (pp[j], p);
          int ptr_count = MIN (n, POINTERS_PER_BLOCK);
          if (!block_arr_resize (p, ptr_count))
            return false;
          cache_write (pp[j], p);
          n -= ptr_count;
        }

      cache_write (inode->iindirect[k], pp);
    }

  return n == 0;
}

/**
 * @brief reserve the inode size to `n`,
 * extend if `n` is smaller than current data blocks, else do nothing
 * @param inode the target to resize
 * @param n the size to extend to
 * @return true if successfully resized
 */
static bool
inode_reserve (struct inode *inode, int n)
{
  ASSERT (lock_held_by_current_thread (&inode->lock));
  if (n <= inode->data.length)
    return true;

  if (!inode_disk_resize (&inode->data, bytes_to_sectors (n)))
    return false;

  inode->data.length = n;
  cache_write (inode->sector, &inode->data);
  return true;
}

/**
 * @brief Initializes an inode with `length` bytes of data and writes the new
 * inode to sector `sector` on the file system device.
 * @param sector
 * @param length
 * @return true if successful.
 * @return false if memory or disk allocation fails.
 */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->is_dir = false;
      if (inode_disk_resize (disk_inode, sectors))
        {
          cache_write (sector, disk_inode);
          success = true;
        }
      free (disk_inode);
    }
  return success;
}

/**
 * @brief Reads an inode from `sector`
 * @param sector
 * @return an inode ptr that contains the `sector`,
 * @return null pointer if memory allocation fails.
 */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  // printf ("open%d\n", sector);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  inode->data.is_dir = false;
  cache_read (inode->sector, &inode->data);
  lock_init (&inode->lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/**
 * @brief Free the sector array `direct[n]`.
 * Call `free_map_release` for all allocated (non-zero) slots
 * @param direct the array of sectors
 * @param n the capacity of the array
 */
static void
block_arr_free_direct (block_sector_t *direct, int n)
{
  for (int i = 0; i < n; i++)
    if (direct[i] != 0)
      free_map_release (direct[i], 1);
}

/**
 * @brief Free the sector 2d array `indirect[m][POINTERS_PER_BLOCK]`.
 * Call `block_arr_free_direct` for all allocated (non-zero) slots
 * @param indirect the array of array of sectors
 * @param m the size of outer array
 */
static void
block_arr_free_indirect (block_sector_t *indirect, int m)
{
  block_sector_t sectorp[POINTERS_PER_BLOCK];
  for (int j = 0; j < m; j++)
    if (indirect[j] != 0)
      {
        cache_read (indirect[j], sectorp);
        block_arr_free_direct (sectorp, POINTERS_PER_BLOCK);
      }
  block_arr_free_direct (indirect, m);
}

/**
 * @brief Free the sector 3d array
 * `iindirect[n][POINTERS_PER_BLOCK][POINTERS_PER_BLOCK]`.
 * Call `block_arr_free_indirect` for all allocated (non-zero) slots
 * @param indirect the array of array of array of sectors
 * @param p the size of outer array
 */
static void
block_arr_free_iindirect (block_sector_t *iindirect, int p)
{
  block_sector_t indirect[POINTERS_PER_BLOCK];
  for (int k = 0; k < p; k++)
    if (iindirect[k] != 0)
      {
        cache_read (iindirect[k], indirect);
        block_arr_free_indirect (indirect, POINTERS_PER_BLOCK);
      }
  block_arr_free_direct (iindirect, p);
}

/**
 * @brief close the inode disk
 * @param inode to close
 */
static void
inode_disk_close (struct inode_disk *inode)
{
  block_arr_free_direct (inode->direct, DIRECT_POINTERS);
  block_arr_free_indirect (inode->indirect, INDIRECT_BLOCKS);
  block_arr_free_iindirect (inode->iindirect, IINDIRECT_BLOCKS);
}

/**
 * @brief Closes INODE and writes it to disk.
 * If this was the last reference to INODE, frees its memory.
 * If INODE was also a removed inode, frees its blocks.
 * @param inode
 */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          cache_write (inode->sector, &inode->data);
          free_map_release (inode->sector, 1);
          inode_disk_close (&inode->data);
        }
      // printf ("close%d\n", inode->sector);
      free (inode);
    }
}

/**
 * @brief Marks INODE to be deleted when it is closed by the last caller who
 * has it open.
 * @param inode
 */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/**
 * @brief Reads `size` bytes from `inode` into `buffer`, starting at position
 * `offset`.
 * @param inode from which to read
 * @param buffer the output buffer
 * @param size the number of bytes want to read
 * @param offset the offset from the beginning of the inode
 * @return the number of bytes actually read, which may be less than `size`
 * if an error occurs or end of file is reached.
 */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  lock_acquire (&inode->lock);
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  size = MIN (size, MAX (0, inode_length (inode) - offset));

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);

      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          cache_read (sector_idx, buffer + bytes_read);
        }
      else
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          cache_read (sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  lock_release (&inode->lock);
  return bytes_read;
}

/**
 * @brief Writes `size` bytes from `buffer` into `inode`,
 * starting at `offset`.
 * @note Normally a write at end of file would extend the inode.
 * @param inode towards which to write
 * @param buffer the input buffer
 * @param size the number of bytes want to write
 * @param offset the offset from the beginning of the inode
 * @return the number of bytes actually written, which may be less than
 `size` if end of file is reached or an error occurs.
 */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  lock_acquire (&inode->lock);

  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    goto exit;

  if (!inode_reserve (inode, size + offset))
    return 0;
  ASSERT (inode_length (inode) >= size + offset);

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          cache_write (sector_idx, buffer + bytes_written);
        }
      else
        {
          /* We need a bounce buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left)
            cache_read (sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          cache_write (sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

exit:
  lock_release (&inode->lock);
  return bytes_written;
}

/**
 * @brief Disables writes to `inode`.
 * @note May be called at most once per inode opener.
 * @param inode
 */
void
inode_deny_write (struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/**
 * @brief Re-enables writes to `inode`.
 * @note Must be called once by each inode opener who has called
 * `inode_deny_write()` on the inode, before closing the inode.
 * @param inode
 */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/**
 * @brief Get the length of a `inode`
 * @param inode
 * @return Returns the length, in bytes.
 */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

#ifdef FILESYS
bool
inode_is_dir (const struct inode *inode)
{
  return inode->data.is_dir;
}

void
inode_set_dir (struct inode *inode, bool is_dir)
{
  inode->data.is_dir = is_dir;
  cache_write (inode->sector, &inode->data);
}

#endif