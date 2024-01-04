#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include "devices/block.h"
#include "filesys/off_t.h"
#include "threads/synch.h"
#include <stdbool.h>

struct bitmap;

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* DIRECT_POINTERS, INDIRECT_BLOCKS, IINDIRECT_BLOCKS sum up to 126 */
#define DIRECT_POINTERS 120
#define INDIRECT_BLOCKS 4
#define IINDIRECT_BLOCKS 1

#define POINTERS_PER_BLOCK (BLOCK_SECTOR_SIZE / 4)
#define INDIRECT_POINTERS (INDIRECT_BLOCKS * POINTERS_PER_BLOCK)
#define IINDIRECT_POINTERS                                                    \
  (IINDIRECT_BLOCKS * POINTERS_PER_BLOCK * POINTERS_PER_BLOCK)

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
  off_t length;        /* File size in bytes. */
  bool is_dir : 1;     /* where the inode represents a disk */
  unsigned count : 31; /* file or dir num*/
  block_sector_t direct[DIRECT_POINTERS];     /* direct pointers */
  block_sector_t indirect[INDIRECT_BLOCKS];   /* indirect pointer */
  block_sector_t iindirect[IINDIRECT_BLOCKS]; /* doubly indirect pointer */
  unsigned magic;                             /* Magic number. */
};

/* In-memory inode. */
struct inode
{
  struct list_elem elem; /* Element in inode list. */
  block_sector_t sector; /* Sector number of disk location. */
  int open_cnt;          /* Number of openers. */
  bool removed;          /* True if deleted, false otherwise. */
  int deny_write_cnt;    /* 0: writes ok, >0: deny writes. */
  struct lock lock;
  struct inode_disk data; /* Inode content. */
};

void inode_init (void);
bool inode_create (block_sector_t, off_t);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

#ifdef FILESYS
bool inode_is_dir (const struct inode *);
void inode_set_dir (struct inode *, bool);
#endif

#endif /* filesys/inode.h */
