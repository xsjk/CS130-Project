#ifndef FILESYS_DIRECTORY_H
#define FILESYS_DIRECTORY_H

#include "devices/block.h"
#include <filesys/off_t.h>
#include <stdbool.h>
#include <stddef.h>

/* Maximum length of a file name component.
   This is the traditional UNIX maximum length.
   After directories are implemented, this maximum length may be
   retained, but much longer full path names must be allowed. */
#define NAME_MAX 14

struct inode;

/* A directory. */
struct dir
{
  struct inode *inode; /* Backing store. */
  off_t pos;           /* Current position. */
  int magic;
  int fd;
};

/* Opening and closing directories. */
bool dir_create (block_sector_t sector, size_t entry_cnt);
struct dir *dir_open (struct inode *);
struct dir *dir_open_root (void);
struct dir *dir_reopen (struct dir *);
void dir_close (struct dir *);
struct inode *dir_get_inode (struct dir *);

/* Reading and writing. */
bool dir_lookup (const struct dir *, const char *name, struct inode **);
bool dir_add (struct dir *, const char *name, block_sector_t);
bool dir_remove (struct dir *, const char *name);
bool dir_readdir (struct dir *, char name[NAME_MAX + 1]);

/* subdir operations */
bool subdir_create (struct dir *parent, const char *name);
struct dir *subdir_lookup (struct dir *parent, const char *name);
bool subdir_remove (struct dir *parent, const char *name);

/* subfile operations */
bool subfile_create (struct dir *parent, const char *name, off_t initial_size);
struct file *subfile_lookup (struct dir *parent, const char *name);
bool subfile_remove (struct dir *parent, const char *name);

bool is_dir (struct dir *);
struct dir *dir_from_fd (int fd);
void dir_set_owner (struct dir *dir);

#endif /* filesys/directory.h */
