#include "userprog/syscall.h"
#include "devices/block.h"
#include "process.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include <filesys/directory.h>
#include <filesys/file.h>
#include <filesys/filesys.h>
#include <filesys/inode.h>
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>

#define __user

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
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
static bool
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
sys_halt ()
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
  struct thread *cur = thread_current ();
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

off_t
sys_filesize (int fd)
{
  struct file *file = file_owner_validate (fd);
  acquire_filesys ();
  off_t size = file_length (file);
  release_filesys ();
  return size;
}

void
sys_seek (int fd, unsigned position)
{
  struct file *file = file_owner_validate (fd);
  acquire_filesys ();
  file_seek (file, position);
  release_filesys ();
}

off_t
sys_tell (int fd)
{
  struct file *file = file_owner_validate (fd);
  acquire_filesys ();
  off_t pos = file_tell (file);
  release_filesys ();
  return pos;
}

void
sys_close (int fd)
{
  struct file *file = file_owner_validate (fd);
  acquire_filesys ();
  list_remove (&file->elem);
  file_close (file);
  release_filesys ();
}

int
sys_read (int fd, uint8_t __user *buffer, unsigned size)
{
  user_access_validate (buffer, size);

  // stdin
  if (fd == STDIN_FILENO)
    {
      int i;
      for (i = 0; i < size; i++)
        buffer[i] = input_getc ();
      return i;
    }

  // file
  struct file *file = file_owner_validate (fd);
  acquire_filesys ();
  int bytes_read = file_read (file, buffer, size);
  release_filesys ();
  return bytes_read;
}

int
sys_write (int fd, const char __user *buffer, unsigned size)
{
  user_access_validate (buffer, size);

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

/*************************/
/* System call interface */
/*************************/

static void
syscall_handler (struct intr_frame *f)
{
  user_access_validate (f->esp, sizeof (int) * 4);
  thread_current ()->esp = f->esp;
  thread_current ()->sys_flag = true;

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
    default:
      sys_exit (-1);
    }
}

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}
