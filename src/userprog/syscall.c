#include "userprog/syscall.h"
#include "debug.h"
#include "devices/block.h"
#include "devices/shutdown.h"
#include "process.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/frame.h"
#include "vm/page.h"
#include <filesys/directory.h>
#include <filesys/file.h>
#include <filesys/filesys.h>
#include <filesys/inode.h>
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include <threads/malloc.h>

#define __user

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
NO_INLINE int
try_load (const uint8_t __user *uaddr)
{
  int result;
  asm ("movl $1f, %0\n\t"
       "movzbl %1, %0\n"
       "1:"
       : "=&a"(result)
       : "m"(*uaddr));
  return result;
}

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
NO_INLINE bool
try_store (uint8_t __user *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0\n\t"
       "movb %b2, %1\n"
       "1:"
       : "=&a"(error_code), "=m"(*udst)
       : "q"(byte));
  return error_code != -1;
}

static bool
copy_from_user (uint8_t *dst, const uint8_t __user *src, size_t size)
{
  if (src + size >= PHYS_BASE)
    return false;

  for (size_t i = 0; i < size; i++)
    {
      int byte = try_load (src + i);
      if (byte == -1)
        return false;
      dst[i] = byte;
    }
  return true;
}

static bool
copy_to_user (uint8_t __user *dst, const uint8_t *src, size_t size)
{
  if (dst + size >= PHYS_BASE)
    return false;

  for (size_t i = 0; i < size; i++)
    if (!try_store (dst + i, src[i]))
      return false;
  return true;
}

bool
kernel_has_access (uint8_t __user *uaddr, size_t size)
{
  return try_load (uaddr) != -1 && try_load (uaddr + size - 1) != -1;
}

bool
user_has_access (uint8_t __user *uaddr, size_t size)
{
  return uaddr + size < PHYS_BASE && kernel_has_access (uaddr, size);
}

/*************************/
/* System call handlers. */
/*************************/

static void
sys_halt (void)
{
  shutdown_power_off ();
  NOT_REACHED ();
}

static void
sys_exit (int status)
{
  if (has_acquired_filesys ())
    release_filesys ();

  process_current ()->exit_status = status;
  thread_exit ();
  NOT_REACHED ();
}

static void
kernel_access_validate (void *addr, size_t size)
{
  if (try_load (addr) == -1 || try_load (addr + size - 1) == -1)
    sys_exit (-1);
}

static void
user_access_validate (void __user *uaddr, size_t size)
{
  if (uaddr + size >= PHYS_BASE)
    sys_exit (-1);
  kernel_access_validate (uaddr, size);
}

static void
user_access_validate_string (const char __user *uaddr)
{
  int byte;
  do
    {
      if (uaddr >= PHYS_BASE || (byte = try_load (uaddr++)) == -1)
        sys_exit (-1);
    }
  while (byte != 0);
}

static struct file *
file_owner_validate (int fd)
{
  struct file *file = file_from_fd (fd);
  kernel_access_validate (file, sizeof (struct file));
  if (!is_file (file))
    sys_exit (-1);
  return file;
}

static int
sys_exec (const char __user *cmd)
{
  user_access_validate_string (cmd);
  int pid = process_execute (cmd);
  // at this point, the process may already finished
  return pid;
}

static int
sys_wait (int pid)
{
  return process_wait (pid);
}

static bool
sys_create (const char __user *file, unsigned initial_size)
{
  user_access_validate_string (file);

  acquire_filesys ();
  bool result = filesys_create (file, initial_size);
  release_filesys ();
  return result;
}

static int
sys_open (const char *path)
{
  int fd = -1;

  // memory validation
  user_access_validate_string (path);

  acquire_filesys ();
  struct file *file = filesys_open (path);
  if (file == NULL)
    goto done;

  // set fd
  fd = file->fd;

  // add to thread's file list
  list_push_back (&process_current ()->files, &file->elem);

done:
  release_filesys ();
  return fd;
}

static bool
sys_remove (const char *path)
{
  user_access_validate_string (path);

  acquire_filesys ();
  bool result = filesys_remove (path);
  release_filesys ();

  return result;
}

static off_t
sys_filesize (int fd)
{
  struct file *file = file_owner_validate (fd);
  acquire_filesys ();
  off_t size = file_length (file);
  release_filesys ();
  return size;
}

static void
sys_seek (int fd, unsigned position)
{
  struct file *file = file_owner_validate (fd);
  acquire_filesys ();
  file_seek (file, position);
  release_filesys ();
}

static off_t
sys_tell (int fd)
{
  struct file *file = file_owner_validate (fd);
  acquire_filesys ();
  off_t pos = file_tell (file);
  release_filesys ();
  return pos;
}

static void
sys_close (int fd)
{
  struct file *file = file_owner_validate (fd);
  acquire_filesys ();
  list_remove (&file->elem);
  file_close (file);
  release_filesys ();
}

static int
sys_read (int fd, uint8_t __user *buffer, unsigned size)
{
  if (!is_user_vaddr (buffer) || !is_user_vaddr (buffer + size - 1))
    sys_exit (-1);

  // stdin
  if (fd == STDIN_FILENO)
    {
      unsigned i;
      for (i = 0; i < size; i++)
        buffer[i] = input_getc ();
      return i;
    }

  // file
  struct file *file = file_owner_validate (fd);
  for (unsigned i = 0; i < size; i += PGSIZE)
    buffer[i] = 0;
  acquire_filesys ();
  int bytes_read = file_read (file, buffer, size);
  release_filesys ();
  return bytes_read;
}

static int
sys_write (int fd, const char __user *buffer, unsigned size)
{
  if (!is_user_vaddr (buffer) || !is_user_vaddr (buffer + size - 1))
    sys_exit (-1);

  // stdout
  if (fd == STDOUT_FILENO)
    {
      putbuf (buffer, size);
      return size;
    }

  // file
  struct file *file = file_owner_validate (fd);
  acquire_filesys ();
  int bytes_written = file_write (file, buffer, size);
  release_filesys ();
  return bytes_written;
}

static mapid_t
sys_mmap (int fd, void *addr)
{

  // check the fd and addr
  if (fd == STDIN_FILENO || fd == STDOUT_FILENO || addr == NULL
      || pg_ofs (addr) != 0)
    return MAP_FAILED;
  if (!is_user_vaddr (addr) || !is_user_vaddr (addr + 4095))
    return MAP_FAILED;

  // check the file
  struct file *file = file_owner_validate (fd);
  acquire_filesys ();
  off_t size = file_length (file);
  release_filesys ();
  if (size == 0)
    return MAP_FAILED;

  // check the overlap
  struct thread *cur = thread_current ();
  int page_cnt = (size - 1) / PGSIZE + 1;
  for (int i = 0; i < page_cnt; i++)
    {
      void *upage = addr + i * PGSIZE;
      if (cur_frame_table_find (upage) != NULL)
        return MAP_FAILED;
    }

  cur->mapid++;

  // create mmap_entry
  file->mmap_entry = malloc (sizeof (struct mmap_entry));
  file->mmap_entry->mapid = cur->mapid;
  file->mmap_entry->file = file;
  list_init (&file->mmap_entry->fte_list);

  off_t ofs = 0;
  // create mmap
  while (size > 0)
    {
      size_t page_read_bytes = size < PGSIZE ? size : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      // create a fte
      ASSERT (file->mmap_entry != NULL);
      struct fte *fte = fte_attach_to_file (file, ofs, addr, true);
      if (fte == NULL)
        return MAP_FAILED;

      size -= page_read_bytes;
      addr += PGSIZE;
      ofs += PGSIZE;
    }

  // move the file from normal file list to mmapped file list
  list_remove (&file->elem);
  list_push_back (&cur->process->mmapped_files, &file->elem);

  return cur->mapid;
}

static void
sys_munmap (int mapping)
{
  struct process *cur = process_current ();
  for (struct list_elem *e = list_begin (&cur->mmapped_files);
       e != list_end (&cur->mmapped_files);)
    {
      struct file *mmap_file = list_entry (e, struct file, elem);
      struct mmap_entry *mmap_entry = mmap_file->mmap_entry;
      e = list_remove (e);

      if (mmap_entry->mapid == mapping)
        {
          // iterate the fte list and destroy
          mmap_destroy (mmap_entry, fte_destroy);
        }
    }
}

/*************************/
/* System call interface */
/*************************/

static void
syscall_handler (struct intr_frame *f)
{
  struct thread *t = thread_current ();
  user_access_validate (f->esp, sizeof (int) * 4);
  t->esp = f->esp;

  switch (*(int *)f->esp)
    {
    case SYS_HALT:
      sys_halt ();
      break;
    case SYS_EXIT:
      sys_exit (*(int *)(f->esp + 4));
      break;
    case SYS_EXEC:
      f->eax = sys_exec (*(const char **)(f->esp + 4));
      break;
    case SYS_WAIT:
      f->eax = sys_wait (*(int *)(f->esp + 4));
      break;
    case SYS_CREATE:
      f->eax = sys_create (*(const char **)(f->esp + 4),
                           *(unsigned *)(f->esp + 8));
      break;
    case SYS_REMOVE:
      f->eax = sys_remove (*(const char **)(f->esp + 4));
      break;
    case SYS_OPEN:
      f->eax = sys_open (*(const char **)(f->esp + 4));
      break;
    case SYS_FILESIZE:
      f->eax = sys_filesize (*(int *)(f->esp + 4));
      break;
    case SYS_READ:
      f->eax = sys_read (*(int *)(f->esp + 4), *(void **)(f->esp + 8),
                         *(unsigned *)(f->esp + 12));
      break;
    case SYS_WRITE:
      f->eax = sys_write (*(int *)(f->esp + 4), *(void **)(f->esp + 8),
                          *(unsigned *)(f->esp + 12));
      break;
    case SYS_SEEK:
      sys_seek (*(int *)(f->esp + 4), *(unsigned *)(f->esp + 8));
      break;
    case SYS_TELL:
      f->eax = sys_tell (*(int *)(f->esp + 4));
      break;
    case SYS_CLOSE:
      sys_close (*(int *)(f->esp + 4));
      break;
#ifdef VM
    case SYS_MMAP:
      f->eax = sys_mmap (*(int *)(f->esp + 4), *(void **)(f->esp + 8));
      break;
    case SYS_MUNMAP:
      sys_munmap (*(int *)(f->esp + 4));
      break;
#endif
    default:
      sys_exit (-1);
    }

  t->esp = NULL;
}

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}
