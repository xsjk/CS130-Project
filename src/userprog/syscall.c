#include "userprog/syscall.h"
#include "debug.h"
#include "devices/block.h"
#include "devices/input.h"
#include "devices/shutdown.h"
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
#include <threads/malloc.h>

#define __user

/**
 * @brief Read a byte at user virtual address `uaddr`.
 * @note `udst` must be below PHYS_BASE.
 * @param uaddr the address to read from
 * @return the byte value if successful, -1 if a segfault occurred.
 */
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

/**
 * @brief Writes to user address.
 * @note `udst` must be below PHYS_BASE.
 * @param udst the destination for user to write
 * @param byte the byte to write
 * @return true if successful, false if a segfault occurred.
 */
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

/**
 * @brief copy `size` bytes from `src` to `dst`
 * @param dst destination
 * @param src source
 * @param size size to copy
 * @return true if successful, false if a segfault occurred.
 */
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

/**
 * @brief check if `size` bytes from `addr` is accessible by kernel
 * @param addr
 * @param size
 * @return true if accessible, false if inaccessible
 */
bool
kernel_has_access (void __user *addr, size_t size)
{
  return try_load (addr) != -1 && try_load (addr + size - 1) != -1;
}

/**
 * @brief check if `size` bytes from `uaddr` if accessible by user
 * @param uaddr
 * @param size
 * @return true if accessible, false if inaccessible
 */
bool
user_has_access (void __user *uaddr, size_t size)
{
  return uaddr + size < PHYS_BASE && kernel_has_access (uaddr, size);
}

static void sys_exit (int status);

/**
 * @brief check if `size` bytes from `addr` is accessible by kernel, if not
 * then exit(-1)
 * @param addr
 * @param size
 */
static void
kernel_access_validate (void *addr, size_t size)
{
  if (!kernel_has_access (addr, size))
    sys_exit (-1);
}

/**
 * @brief check if `size` bytes from `uaddr` is accessible by user, if not then
 * exit(-1)
 * @param uaddr
 * @param size
 */
static void
user_access_validate (void __user *uaddr, size_t size)
{
  if (!user_has_access (uaddr, size))
    sys_exit (-1);
}

/**
 * @brief check if the string at `str` is accessible by user, if not then
 * exit(-1)
 * @param str
 */
static void
user_access_validate_string (const char __user *str)
{
  int byte;
  do
    {
      if (str >= PHYS_BASE || (byte = try_load (str++)) == -1)
        sys_exit (-1);
    }
  while (byte != 0);
}

/**
 * @brief check if `fd` represents a file of this thread, if not then exit(-1)
 * @param fd
 * @return the pointer to the file struct
 */
static struct file *
file_owner_validate (int fd)
{
  struct file *file = file_from_fd (fd);
  kernel_access_validate (file, sizeof *file);
  if (!is_file (file))
    sys_exit (-1);
  return file;
}

/**
 * @brief check if `fd` represents a dir, if not then exit(-1)
 * @param fd
 * @return the pointer to the dir struct
 */
static struct dir *
dir_validate (int fd)
{
  struct dir *dir = dir_from_fd (fd);
  kernel_access_validate (dir, sizeof *dir);
  if (!is_dir (dir))
    sys_exit (-1);
  return dir;
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

  // refers to a file or directory
  struct file *file = filesys_open (path);
  struct dir *dir = filesys_opendir (path);

  if (file == NULL && dir == NULL)
    goto done;

  // set fd
  if (file != NULL)
    {
      fd = file->fd;
      // add to thread's file list
      list_push_back (&process_current ()->files, &file->elem);
    }

  if (dir != NULL)
    {
      fd = dir->fd;
      dir_reopen (dir);
      thread_current ()->cwd = dir;
    }

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
  buffer[size - 1] = 0;
  acquire_filesys ();
  int bytes_read = file_read (file, buffer, size);
  release_filesys ();
  return bytes_read;
}

static int
sys_write (int fd, const char __user *buffer, unsigned size)
{
  // if (!is_user_vaddr (buffer) || !is_user_vaddr (buffer + size - 1))
  //   sys_exit (-1);
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

static bool
sys_chdir (const char __user *path)
{
  user_access_validate_string (path);
  return filesys_chdir (path);
}

static bool
sys_mkdir (const char __user *path)
{
  user_access_validate_string (path);
  return filesys_mkdir (path);
}

static bool
sys_readdir (int fd, char __user *name)
{
  user_access_validate (name, READDIR_MAX_LEN + 1);
  struct dir *dir = dir_validate (fd);
  return filesys_readdir (dir, name);
}

static bool
sys_isdir (int fd)
{
  struct file *file = file_from_fd (fd);
  if (kernel_has_access (file, sizeof *file) && is_file (file))
    return false;
  struct dir *dir = dir_from_fd (fd);
  if (kernel_has_access (dir, sizeof *dir) && is_dir (dir))
    return true;
  sys_exit (-1); // neither file nor dir
}

static int
sys_inumber (int fd)
{
  struct dir *dir = dir_validate (fd);
  return filesys_inumber (dir);
}

/*************************/
/* System call interface */
/*************************/

static void
syscall_handler (struct intr_frame *f)
{
  struct thread *t = thread_current ();
  user_access_validate (f->esp, sizeof (int) * 4);
#ifdef VM
  t->esp = f->esp;
#endif
  int *argv = f->esp;
  switch (argv[0])
    {
    case SYS_HALT:
      sys_halt ();
      break;
    case SYS_EXIT:
      sys_exit (argv[1]);
      break;
    case SYS_EXEC:
      f->eax = sys_exec (argv[1]);
      break;
    case SYS_WAIT:
      f->eax = sys_wait (argv[1]);
      break;
    case SYS_CREATE:
      f->eax = sys_create (argv[1], argv[2]);
      break;
    case SYS_REMOVE:
      f->eax = sys_remove (argv[1]);
      break;
    case SYS_OPEN:
      f->eax = sys_open (argv[1]);
      break;
    case SYS_FILESIZE:
      f->eax = sys_filesize (argv[1]);
      break;
    case SYS_READ:
      f->eax = sys_read (argv[1], argv[2], argv[3]);
      break;
    case SYS_WRITE:
      f->eax = sys_write (argv[1], argv[2], argv[3]);
      break;
    case SYS_SEEK:
      sys_seek (argv[1], argv[2]);
      break;
    case SYS_TELL:
      f->eax = sys_tell (argv[1]);
      break;
    case SYS_CLOSE:
      sys_close (argv[1]);
      break;
#ifdef VM
    case SYS_MMAP:
      f->eax = sys_mmap (argv[1], argv[2]);
      break;
    case SYS_MUNMAP:
      sys_munmap (argv[1]);
      break;
#endif
    case SYS_CHDIR:
      f->eax = sys_chdir (argv[1]);
      break;
    case SYS_MKDIR:
      f->eax = sys_mkdir (argv[1]);
      break;
    case SYS_READDIR:
      f->eax = sys_readdir (argv[1], argv[2]);
      break;
    case SYS_ISDIR:
      f->eax = sys_isdir (argv[1]);
      break;
    case SYS_INUMBER:
      f->eax = sys_inumber (argv[1]);
      break;
    default:
      sys_exit (-1);
    }
#ifdef VM
  t->esp = NULL;
#endif
}

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}
