#include "filesys/directory.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include <list.h>
#include <stdio.h>
#include <string.h>

#define DIR_MAGIC 0x726964

bool
is_dir (struct dir *dir)
{
  return dir->magic == DIR_MAGIC;
}

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
#ifdef FILESYS
  bool success = inode_create (sector, entry_cnt * sizeof (struct dir_entry));
  if (success)
    {
      struct inode *inode = inode_open (sector);
      inode_set_dir (inode, true);

      struct dir *dir = dir_open (inode);
      dir_add (dir, ".", sector);

      // check if the directory is the root directory
      if (inode_get_inumber (inode) == ROOT_DIR_SECTOR)
        dir_add (dir, "..", sector);

      dir_close (dir);
    }
  return success;
#else
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry));
#endif
}

/**
 * @brief create a subdir with name NAME in DIR
 * @return true if successful, false otherwise
 */
bool
subdir_create (struct dir *parent, const char *name)
{
  block_sector_t inode_sector = 0;
  bool success = (parent != NULL && free_map_allocate (1, &inode_sector)
                  && dir_create (inode_sector, 0)
                  && dir_add (parent, name, inode_sector));
  ASSERT (bitmap_all (free_map, inode_sector, 1));
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  return success;
}

bool
subfile_create (struct dir *parent, const char *name, off_t initial_size)
{
  block_sector_t inode_sector = 0;
  bool success = (parent != NULL && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size)
                  && dir_add (parent, name, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  return success;
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode)
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 2 * sizeof (struct dir_entry);
      dir->magic = DIR_MAGIC;
      dir->count = 0;
      dir_set_owner (dir);
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL;
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir)
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir)
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir)
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name, struct dir_entry *ep,
        off_t *ofsp)
{
  struct dir_entry e;
  size_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
    if (e.in_use && !strcmp (name, e.name))
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name, struct inode **inode)
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}

/**
 * @brief lookup the subdir with name NAME in DIR
 * @return the subdir if found, NULL otherwise
 */
struct dir *
subdir_lookup (struct dir *parent, const char *name)
{
  struct inode *inode;
  if (!dir_lookup (parent, name, &inode))
    return NULL;
  if (!inode_is_dir (inode))
    {
      inode_close (inode);
      return NULL;
    }
  return dir_open (inode);
}

struct file *
subfile_lookup (struct dir *parent, const char *name)
{
  struct inode *inode;
  if (!dir_lookup (parent, name, &inode))
    return NULL;
  if (inode_is_dir (inode))
    {
      inode_close (inode);
      return NULL;
    }
  return file_open (inode);
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (dir->inode->removed == true)
    return false;

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.

     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

  // struct inode *inode;
  // ASSERT (dir_lookup (dir, name, &inode));
  // inode_close (inode);

  if (success && inode_sector != ROOT_DIR_SECTOR && name[0] != '.')
    {
      struct inode *sub_inode = inode_open (inode_sector);
      if (inode_is_dir (sub_inode))
        {
          // add parent dir to this sub dir
          struct dir *sub_dir = dir_open (sub_inode);
          ASSERT (dir_add (sub_dir, "..", inode_get_inumber (dir->inode)));
          dir_close (sub_dir);
        }
      inode_close (sub_inode);
    }
  if (strcmp (name, ".") && strcmp (name, ".."))
    {
      dir->inode->data.count++;
      cache_write (dir->inode->sector, &dir->inode->data);
    }

done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name)
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e)
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

  if (strcmp (name, ".") && strcmp (name, ".."))
    {
      dir->inode->data.count--;
      cache_write (inode->sector, &inode->data);
    }
done:
  inode_close (inode);
  return success;
}

static bool
dir_is_empty (struct dir *dir)
{
  if (dir->inode->data.count == 0)
    return true;
}

/**
 * @brief remove the subdir with name NAME in DIR
 * @return true if successful, false otherwise
 */
bool
subdir_remove (struct dir *parent, const char *name)
{
  struct dir *subdir = subdir_lookup (parent, name);
  if (subdir == NULL)
    return false;

  // check if empty subdir
  if (!dir_is_empty (subdir))
    return false;

  else
    {
      ASSERT (dir_remove (subdir, "."));
      ASSERT (dir_remove (subdir, ".."));
      bool success = dir_remove (parent, name);
      dir_close (subdir);
      return success;
    }
}

bool
subfile_remove (struct dir *parent, const char *name)
{
  struct file *subfile = subfile_lookup (parent, name);
  if (subfile == NULL)
    return false;
  else
    {
      file_close (subfile);
      return dir_remove (parent, name);
    }
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e)
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        }
    }
  return false;
}

struct dir *
dir_from_fd (int fd)
{
  return (struct dir *)(fd + (char *)process_current () - 0x40000000);
}

void
dir_set_owner (struct dir *dir)
{
  dir->fd = (char *)dir - (char *)process_current () + 0x40000000;
}