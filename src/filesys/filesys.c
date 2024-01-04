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

/**
 * @brief parse the path and get the file name and the directory
 * @param path the path to parse
 * @param file_name the file name without path info
 * @param dir the directory will be moved to
 * @return true if successful, false on failure.
 */
static bool
filesys_parsing_path (const char *path, char *file_name, struct dir **dir,
                      bool *is_file)
{
#ifdef FILESYS
  // #if false
  // default is file
  *is_file = true;
  struct thread *t = thread_current ();

  // check if the path is valid
  if (path == NULL || file_name == NULL || dir == NULL)
    return false;

  // empty path
  if (strlen (path) == 0)
    return false;

  char *path_copy = malloc (strlen (path) + 1);
  strlcpy (path_copy, path, strlen (path) + 1);

  // check if the path is a directory, if so, remove the last '/'
  if (path[strlen (path) - 1] == '/')
    {
      *is_file = false;
      path_copy[strlen (path) - 1] = '\0';
    }

  // check if the path is absolute
  if (path[0] == '/')
    *dir = dir_open_root ();
  else
    *dir = dir_reopen (t->cwd);

  if (*dir == NULL)
    return false;

  char *token, *save_ptr;
  char *next_token = strtok_r (path_copy, "/", &save_ptr);

  // current dir is root directory
  if (next_token == NULL)
    {
      file_name[0] = '.';
      file_name[1] = '\0';
      return true;
    }

  // check if the token is valid
  if (strlen (next_token) > NAME_MAX)
    {
      dir_close (*dir);
      free (path_copy);
      return false;
    }

  while (next_token != NULL)
    {
      token = next_token;
      next_token = strtok_r (NULL, "/", &save_ptr);
      if (next_token == NULL)
        break;

      // check if the token is valid
      if (strlen (token) > NAME_MAX)
        {
          dir_close (*dir);
          free (path_copy);
          return false;
        }

      // check if the token is a directory
      struct dir *next_dir = subdir_lookup (*dir, token);
      if (next_dir == NULL)
        {
          dir_close (*dir);
          free (path_copy);
          return false;
        }
      dir_close (*dir);
      *dir = next_dir;
    }
  strlcpy (file_name, token, strlen (token) + 1);
  free (path_copy);
  return true;
#endif
}

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
#ifdef FILESYS
  struct dir *dir;
  char file_name[NAME_MAX + 1];
  bool is_file;
  if (filesys_parsing_path (name, file_name, &dir, &is_file))
    {
      if (is_file == false)
        {
          dir_close (dir);
          return false;
        }
      else
        {
          bool success = subfile_create (dir, file_name, initial_size);
          dir_close (dir);
          return success;
        }
    }
  return false;

#else
  block_sector_t inode_sector = 0;
  struct dir *dir = dir_open_root ();
  bool success = (dir != NULL && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size)
                  && dir_add (dir, name, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
#endif
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
#ifdef FILESYS

  // refers to a file
  struct dir *dir;
  char file_name[NAME_MAX + 1];
  bool is_file;
  if (!filesys_parsing_path (name, file_name, &dir, &is_file))
    return NULL;
  struct file *file = subfile_lookup (dir, file_name);
  dir_close (dir);

  return file;
#else
  struct dir *dir = dir_open_root ();
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  dir_close (dir);

  return file_open (inode);
#endif
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
#ifdef FILESYS
  struct dir *dir;
  char file_name[NAME_MAX + 1];

  // check the root directory
  if (strcmp (name, "/") == 0)
    return false;

  bool is_file;
  if (!filesys_parsing_path (name, file_name, &dir, &is_file))
    return false;
  bool success
      = subdir_remove (dir, file_name) || subfile_remove (dir, file_name);
  return success;

#else
  struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir);
  return success;
#endif
}

/**
 * @brief Opens the directory with the given NAME.
 * @param name the name of the directory
 * @return the new directory if successful or a null pointer otherwise.
 * Fails if no directory named NAME exists, or if an internal memory
 * allocation fails.
 */
struct dir *
filesys_opendir (const char *name)
{
#ifdef FILESYS
  struct dir *dir;
  char file_name[NAME_MAX + 1];
  bool is_file;
  if (!filesys_parsing_path (name, file_name, &dir, &is_file))
    return NULL;

  struct dir *sub_dir = subdir_lookup (dir, file_name);
  dir_close (dir);
  return sub_dir;

#endif
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
  lock_acquire (&filesys_lock);
  struct thread *t = thread_current ();
  struct dir *dir = filesys_opendir (name);
  if (dir == NULL)
    {
      lock_release (&filesys_lock);
      return false;
    }
  dir_close (thread_current ()->cwd);
  t->cwd = dir;
  lock_release (&filesys_lock);
  return true;
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
#ifdef FILESYS

  if (name == NULL)
    return false;

  // empty name & root directory
  if (strlen (name) == 0 || strcmp (name, "/") == 0)
    return false;

  struct dir *dir;
  char dir_name[NAME_MAX + 1];
  bool is_file;
  bool success = false;
  if (filesys_parsing_path (name, dir_name, &dir, &is_file))
    success = subdir_create (dir, dir_name);

  dir_close (dir);
  return success;

#endif
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
filesys_readdir (int fd, char *name)
{
#ifdef FILESYS
  // return dir_readdir (dir, name);
#endif
}

/**
 * @brief get inode number of fd
 * @param dir
 * @return
 */
int
filesys_inumber (int fd)
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