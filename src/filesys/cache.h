#ifndef FILESYS_CACHE
#define FILESYS_CACHE

#include "devices/block.h"
#include "filesys.h"
#include "hash.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

#define CACHE_SIZE 64 /* no greater than 64 sectors */

struct cache_entry
{
  bool dirty;
  bool valid;
  bool accessed;
  block_sector_t sector;
  uint8_t data[BLOCK_SECTOR_SIZE];
  struct lock cache_lock;
  struct list_elem clock_list_elem; // for clock algorithm
  struct hash_elem hash_elem;       // for hash table
};

void cache_init (void);
void cache_read (block_sector_t sector, void *buffer);
void cache_write (block_sector_t sector, const void *buffer);
void cache_flush (void);

void cache_table_init (void);
struct cache_entry *cache_table_find (block_sector_t sector);

#endif /* filesys/cache */
