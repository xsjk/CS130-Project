#include "filesys/filesys.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "threads/thread.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>

#ifdef USERPROG

#include "threads/synch.h"
static struct lock filesys_lock;

void
acquire_filesys (void)
{
  lock_acquire (&filesys_lock);
}

void
release_filesys (void)
{
  lock_release (&filesys_lock);
}

bool
has_acquired_filesys (void)
{
  return lock_held_by_current_thread (&filesys_lock);
}

#endif

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format)
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format)
    do_format ();

  free_map_open ();
#ifdef USERPROG
  lock_init (&filesys_lock);
#endif
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size)
{
  block_sector_t inode_sector = 0;
  struct dir *dir = dir_open_root ();
  bool success = (dir != NULL && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size)
                  && dir_add (dir, name, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct dir *dir = dir_open_root ();
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  dir_close (dir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir);

  return success;
}

/**
 * @brief Creates the directory named dir, which may be relative or absolute.
 * @param name the name of the directory
 * @return true if successful, false on failure. Fails if dir already
 * exists or if any directory name in dir, besides the last, does not already
 * exist. That is, mkdir("/a/b/c") succeeds only if /a/b already exists and
 * /a/b/c does not.
 */
bool
filesys_mkdir (const char *name)
{
}

/**
 * @brief Changes the current working directory of the process to dir, which
 * may be relative or absolute.
 * @param name name of the directory
 * @return true if successful, false on failure.
 */
bool
filesys_chdir (const char *name)
{
}

/**
 * @brief Reads a directory entry from file descriptor fd, which must represent
 * a directory.
 * @param dir
 * @param name
 * @return true if successful, false on failure. If successful, stores the
 * null-terminated file name in name, which must have room for READDIR_MAX_LEN
 * + 1 bytes, and returns true. If no entries are left in the directory,
 * returns false.
 * @note . and .. should not be returned by readdir.
 * @note If the directory changes while it is open, then it is
 * acceptable for some entries not to be read at all or to be read multiple
 * times. Otherwise, each directory entry should be read once, in any order.
 * @note READDIR_MAX_LEN is defined in lib/user/syscall.h. If your file
 * system supports longer file names than the basic file system, you should
 * increase this value from the default of 14.
 */
bool
filesys_readdir (struct dir *dir, char name[READDIR_MAX_LEN + 1])
{
}

/**
 * @brief get inode number of fd
 * @param dir
 * @return
 */
int
filesys_inumber (struct dir *dir)
{
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
