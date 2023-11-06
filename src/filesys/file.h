#ifndef FILESYS_FILE_H
#define FILESYS_FILE_H

#include "filesys/off_t.h"
#include <list.h>
#include <stdbool.h>

struct inode;
#ifdef USERPROG
struct thread;
#endif

/* An open file. */
struct file
{
  struct inode *inode; /* File's inode. */
  off_t pos;           /* Current position. */
  bool deny_write : 1; /* Has file_deny_write() been called? */
  int magic : 31;
#ifdef USERPROG
  struct list_elem elem;
  int fd;
#endif
};

/* Opening and closing files. */
struct file *file_open (struct inode *);
struct file *file_reopen (struct file *);
void file_close (struct file *);
struct inode *file_get_inode (struct file *);

/* Reading and writing. */
off_t file_read (struct file *, void *, off_t);
off_t file_read_at (struct file *, void *, off_t size, off_t start);
off_t file_write (struct file *, const void *, off_t);
off_t file_write_at (struct file *, const void *, off_t size, off_t start);

/* Preventing writes. */
void file_deny_write (struct file *);
void file_allow_write (struct file *);

/* File position. */
void file_seek (struct file *, off_t);
off_t file_tell (struct file *);
off_t file_length (struct file *);

#ifdef USERPROG

/* get the owner thread of a file */
struct thread *file_get_owner (struct file *file);

/* set current thread as the owner of a file */
void file_set_ownwer (struct file *file);

/* get the file owned by current thread with fd */
struct file *file_from_fd (int fd);

#endif

bool is_file (struct file *);

#endif /* filesys/file.h */
