#include "userprog/syscall.h"
#include "devices/block.h"
#include "filesys/directory.h"
#include "filesys/filesys.h"
#include "process.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include <stdio.h>
#include <syscall-nr.h>

#define __user

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t __user *uaddr)
{
  if (uaddr >= PHYS_BASE)
    return -1;

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
put_user (uint8_t __user *udst, uint8_t byte)
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
  for (size_t i = 0; i < size; i++)
    {
      int byte = get_user (src + i);
      if (byte == -1)
        return false;
      dst[i] = byte;
    }
  return true;
}

static bool
copy_to_user (uint8_t __user *dst, const uint8_t *src, size_t size)
{
  for (size_t i = 0; i < size; i++)
    if (!put_user (dst + i, src[i]))
      return false;
  return true;
}

/*************************/
/* System call handlers. */
/*************************/

static struct lock filesys_lock;

static void
sys_halt ()
{
  shutdown_power_off ();
  NOT_REACHED ();
}

static void
sys_exit (int status)
{
  if (lock_held_by_current_thread (&filesys_lock))
    lock_release (&filesys_lock);

  printf ("%s: exit(%d)\n", thread_current ()->name, status);
  thread_exit ();
  NOT_REACHED ();
}

static void
test_access (void __user *uaddr)
{
  if (get_user (uaddr) == -1)
    sys_exit (-1);
}

static int
sys_exec (const char __user *cmd)
{
  test_access (cmd);
  lock_acquire (&filesys_lock);
  int pid = process_execute (cmd);
  lock_release (&filesys_lock);
  return pid;
}

static int
sys_wait (int pid)
{
  struct thread *child = get_child (pid);
  if (child == NULL)
    // it is not a child of current thread
    return -1;
  sema_down (&child->wait_sema);
  return child->exit_status;
  // return process_wait (pid);
}

static int
sys_create (const char __user *file, unsigned initial_size)
{
  /// TODO: implement
  printf ("sys_create\n");
  return 0;
}

static int
sys_open (const char *file)
{
  // memory validation
  test_access (file);
  /// TODO: implement
  return 0;
}

static int
sys_remove (const char *file)
{
  // memory validation
  test_access (file);

  lock_acquire (&filesys_lock);
  int result = filesys_remove (file);
  lock_release (&filesys_lock);
  return result;
}

int
sys_filesize (int fd)
{
  /// TODO: implement
  printf ("sys_filesize\n");
  return 0;
}

void
sys_seek (int fd, unsigned position)
{
  /// TODO: implement
  printf ("sys_seek\n");
}

unsigned
sys_tell (int fd)
{
  /// TODO: implement
  printf ("sys_tell\n");
  return 0;
}

void
sys_close (int fd)
{
  /// TODO: implement
  printf ("sys_close\n");
}

int
sys_read (int fd, void *buffer, unsigned size)
{
  /// TODO: implement
  printf ("sys_read\n");
  return 0;
}

int
sys_write (int fd, const void *buffer, unsigned size)
{
  /// TODO: implement
  if (fd == 1)
    {
      putbuf (buffer, size);
      return size;
    }
  return 0;
}

/*************************/
/* System call interface */
/*************************/

static void
syscall_handler (struct intr_frame *f)
{
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
  lock_init (&filesys_lock);
}
