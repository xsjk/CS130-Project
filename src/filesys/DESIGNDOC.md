# PROJECT 4: FILE SYSTEMS

## GROUP

> Fill in the names and email addresses of your group members.

Sijie Xu <xusj@shanghaitech.edu.cn>

Yichun Bai <baiych@shanghaitech.edu.cn>

## PRELIMINARIES

> If you have any preliminary comments on your submission, notes for the
TAs, or extra credit, please give them here.

> Please cite any offline or online sources you consulted while
preparing your submission, other than the Pintos documentation, course
text, lecture notes, and course staff.

## INDEXED AND EXTENSIBLE FILES

### DATA STRUCTURES

> A1: Copy here the declaration of each new or changed `struct' or
`struct' member, global or static variable, `typedef', or
enumeration.Identify the purpose of each in 25 words or less.

```C
#define DIRECT_POINTERS 120
#define INDIRECT_BLOCKS 4
#define IINDIRECT_BLOCKS 1

struct inode_disk {
    off_t length;        /* File size in bytes. */
    bool is_dir : 1;     /* where the inode represents a disk */
    unsigned count : 31; /* file or dir num*/
    block_sector_t direct[DIRECT_POINTERS];     /* direct pointers */
    block_sector_t indirect[INDIRECT_BLOCKS];   /* indirect pointer */
    block_sector_t iindirect[IINDIRECT_BLOCKS]; /* doubly indirect pointer */
    unsigned magic;                             /* Magic number. */
};
```

> A2: What is the maximum size of a file supported by your inode
structure ? Show your work.

We have 120 direct pointers, 4 indirect pointers and 1 doubly indirect pointer.
Each pointer points to a block of 512B.So the maximum size is(120 + 4 * 128 + 1 * 128 * 128) * 512B = 8.3MB.

## SYNCHRONIZATION

> A3: Explain how your code avoids a race if two processes attempt to
extend a file at the same time.

We use lock in inode struct to protect the inode.
When two processes attempt to extend a file at the same time, they will acquire the lock in inode struct, but only one process can acquire the lock at the same time.

> A4 : Suppose processes A and B both have file F open, both
positioned at end - of - file.If A reads and B writes F at the same
time, A may read all, part, or none of what B writes.However, A
may not read data other than what B writes, e.g. if B writes
nonzero data, A is not allowed to see all zeros.Explain how your
code avoids this race.

When B extends F, the "disk_inode->length" is changed, but the data in the block is not changed.
When A reads F, it will read the data in the block according to the previous length unless "inode_write_at()" is for B is finished.
Therefore, A will not see all zeros.

> A5: Explain how your synchronization design provides "fairness".
File access is "fair" if readers cannot indefinitely block writers
or vice versa.That is, many processes reading from a file cannot
prevent forever another process from writing the file, and many
processes writing to a file cannot prevent another process forever
from reading the file.

File reading & writing operations both acquire inode->lock. Therefore, the orders of reading & writing operations are the same as the order of acquiring the lock by following the orders in semaphore->waiters.

### RATIONALE

> A6: Is your inode structure a multilevel index ? If so, why did you
choose this particular combination of direct, indirect, and doubly
indirect blocks ? If not, why did you choose an alternative inode
structure, and what advantages and disadvantages does your
structure have, compared to a multilevel index ?

Yes, it is a multilevel index.
We have 120 direct blocks, 4 indirect blocks and 1 doubly indirect block.
This combination can support a file of 8.3MB, which is enough for most files.
Additionally, 120 direct blocks can reduce the number of disk accesses.

## SUBDIRECTORIES

### DATA STRUCTURES

> B1: Copy here the declaration of each new or changed `struct' or
`struct' member, global or static variable, `typedef', or
enumeration.Identify the purpose of each in 25 words or less.

```C
// add
struct inode_disk {
    bool is_dir : 1;     /* where the inode represents a disk */
    unsigned count : 31; /* file or dir num*/
};

// add
struct thread {
    struct dir* cwd; /* Current working directory */
}
```

### ALGORITHMS

> B2: Describe your code for traversing a user - specified path.How
do traversals of absolute and relative paths differ ?

We use filesys_parsing_path() to parse the user - specified path.
After that, we get the file_name(file or dir) and its directory.
If the path starts with '/', it is an absolute path; otherwise, it is a relative path.
When facing an absolute path, we start from the root directory.
When facing a relative path, we start from the current directory.

### SYNCHRONIZATION

> B4: How do you prevent races on directory entries ? For example,
only one of two simultaneous attempts to remove a single file
should succeed, as should only one of two simultaneous attempts to
create a file with the same name, and so on.

The remove and create operations are protected by the lock functions like dir_lookup(), dir_readdir(), dir_add() and dir_remove() are synchronized by the lock in inode struct.

> B5 : Does your implementation allow a directory to be removed if it
is open by a process or if it is in use as a process's current
working directory ? If so, what happens to that process's future
file system operations ? If not, how do you prevent it ?

Yes, only if the directory is empty, we can remove it. If the directory is not empty, checked by dir_is_empty(), we cannot remove it.
The process should change its current working directory in the future.

### RATIONALE

> B6: Explain why you chose to represent the current directory of a
process the way you did.

We store the current directory(cwd) in the thread struct.It is convenient to access the current directory.Then it's easy to access the parent directory of the current directory.

## BUFFER CACHE

### DATA STRUCTURES

> C1: Copy here the declaration of each new or changed `struct' or
`struct' member, global or static variable, `typedef', or
enumeration.Identify the purpose of each in 25 words or less.

```C
#define CACHE_SIZE 64 /* no greater than 64 sectors */

struct cache_entry {
    bool dirty : 1; //
    bool valid : 1;
    bool accessed : 1;
    block_sector_t sector;
    uint8_t* data;
    struct lock lock;
    struct list_elem clock_list_elem; // for clock algorithm
    struct hash_elem hash_elem;       // for hash table
};
```

### ALGORITHMS

> C2: Describe how your cache replacement algorithm chooses a cache
block to evict.

we use CLOCK ALGORITHM : We use "accessed" to record whether the block is accessed recently and "dirty" to record whether the block is modified recently.When we need to evict a block, we first check whether the block is accessed recently.If it is, we set "accessed" to false and move to the next block.If it is not, we check whether the block is modified recently.If it is, we write the block to the disk and set "dirty" to false.If it is not, we evict the block.

> C3 : Describe your implementation of write - behind.

When writing to a sector, we first check whether the sector is in the cache.If it is, we write to the cache.If it is not, we read the sector from the disk and write to the cache.It does not write back to disk util the block is evicted, by cache_flush().

> C4 : Describe your implementation of read - ahead.

When reading a sector, we first check whether the sector is in the cache.If it is, we read from the cache.If it is not, we read the sector from the disk and write to the cache.
Additionally, we try to find next several sectors and bring them to cache table, which take advantage of locality.

### SYNCHRONIZATION

> C5 : When one process is actively reading or writing data in a
buffer cache block, how are other processes prevented from evicting
that block ?

Each cache_entry has a lock to protect it.When one process is actively reading or writing data in a buffer cache block, it will acquire the lock.Other processes will wait until the lock is released.

> C6 : During the eviction of a block from the cache, how are other
processes prevented from attempting to access the block ?

Similarly, the evict operation also holds the lock in cache_entry. Other processes will wait until the lock is released.

### RATIONALE

> C7: Describe a file workload likely to benefit from buffer caching,
and workloads likely to benefit from read - ahead and write - behind.

from buffer caching, When we read a file, we can read the data from the cache instead of the disk.It is much faster.
from read - ahead and write - behind, we can decrease I / O operations and take advantage of the locality of reference.

## SURVEY QUESTIONS

Answering these questions is optional, but it will help us improve the
course in future quarters.Feel free to tell us anything you
want--these questions are just to spur your thoughts.You may also
choose to respond anonymously in the course evaluations at the end of
the quarter.

> In your opinion, was this assignment, or any one of the three problems
in it, too easy or too hard ? Did it take too long or too little time ?

> Did you find that working on a particular part of the assignment gave
you greater insight into some aspect of OS design ?

> Is there some particular fact or hint we should give students in
future quarters to help them solve the problems ? Conversely, did you
find any of our guidance to be misleading ?

> Do you have any suggestions for the TAs to more effectively assist
students in future quarters ?

> Any other comments ?
