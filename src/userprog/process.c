#include "userprog/process.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include <debug.h>
#include <inttypes.h>
#include <list.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef VM
#include "vm/frame.h"
#include "vm/page.h"
#endif

#define PROCESS_MAGIC 0x636f7270

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
static bool load_entry (struct file *file, void (**eip) (void), void **esp);
struct args
{
  int argc;
  char **argv;
};

static inline struct args *
parse_args (const char *cmd)
{
  char *page = palloc_get_page (0);
  if (page == NULL)
    return NULL;

  int offset = strlcpy (page, cmd, 128) + 1;
  struct args *args = page + offset; // struct args start from the end of cmd
  args->argc = 0;
  args->argv = args + 1; // argv[0] start from the end of struct args

  for (char *save_ptr, *token = strtok_r (page, " ", &save_ptr); token != NULL;
       token = strtok_r (NULL, " ", &save_ptr))
    args->argv[args->argc++] = token;
  args->argv[args->argc] = NULL;

  return args;
}

static inline void
free_args (struct args *args)
{
  palloc_free_page (args->argv[0]);
}

void
process_init (void)
{
}

struct process *
process_current (void)
{
  struct thread *cur = thread_current ();
  return cur->process;
}

pid_t
process_pid (void)
{
  return process_current ()->pid;
}

void
init_process (struct process *this, struct thread *thread)
{
  ASSERT (this != NULL);
  ASSERT (thread != NULL);
  this->exit_status = -1;
  this->pid = -(int)this;
  this->thread = thread;
  this->parent = thread->parent ? thread->parent->process : NULL;
  if (this->parent)
    list_push_back (&this->parent->child_list, &this->childelem);

  list_init (&this->child_list);
  list_init (&this->files);
  sema_init (&this->wait_sema, 0);
  sema_init (&this->elf_load_sema, 0);
  sema_init (&this->exec_sama, 0);
  this->magic = PROCESS_MAGIC;
}

#include "threads/malloc.h"
struct process *
new_process (struct thread *t)
{
  return malloc (sizeof (struct process));
}

void
delete_process (struct process *p)
{
  free (p);
}

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
pid_t
process_execute (const char *cmd)
{
  struct thread *t = thread_current ();
  struct args *args = parse_args (cmd);

  enum intr_level old_level = intr_disable ();

  tid_t tid = thread_create (args->argv[0], PRI_DEFAULT, start_process, args);

  if (tid == TID_ERROR)
    goto error;

  struct thread *t_child = get_thread (tid);
  struct process *p_child = t_child->process;
  if (p_child == NULL)
    goto error;

  intr_set_level (old_level);

  // block until the child process load its elf
  sema_down (&p_child->elf_load_sema);

  // allow the child process to exit
  sema_up (&p_child->exec_sama);

  if (p_child->thread == NULL)
    // child's elf is not loaded due to some error e.g. file not found
    goto error;

  return p_child->pid;

error:
  intr_set_level (old_level);

  free_args (args);
  return TID_ERROR;
}

static bool
intr_frame_init (struct intr_frame *infr, struct args *args)
{
  /* Initialize interrupt frame and load executable. */
  memset (infr, 0, sizeof *infr);
  infr->gs = infr->fs = infr->es = infr->ds = infr->ss = SEL_UDSEG;
  infr->cs = SEL_UCSEG;
  infr->eflags = FLAG_IF | FLAG_MBS;
  bool success = load (args->argv[0], &infr->eip, &infr->esp);

  /* Argument Passing*/
  if (success)
    {
      // offset
      int cmd_length = (char *)args - args->argv[0];
      char *cmd = infr->esp -= cmd_length;
      infr->esp = (uint32_t)infr->esp & 0xfffffffc;
      infr->esp -= (args->argc + 1) * 4 + 12;

      // copy data
      int *esp = infr->esp;
      esp[0] = NULL;                                      // return addr
      esp[1] = args->argc;                                // argc
      esp[2] = &esp[3];                                   // argv
      for (int i = 0; i < args->argc; i++)                // argv[]
        esp[3 + i] = args->argv[i] - args->argv[0] + cmd; //
      esp[3 + args->argc] = NULL;                         //
      memcpy (cmd, args->argv[0], cmd_length);            // argv[][]
    }

  return success;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *aux)
{
  struct args *args = aux;
  struct intr_frame infr;
  struct thread *t = thread_current ();
  struct process *p = t->process;

  if (t->process == NULL)
    goto error;

  if (!intr_frame_init (&infr, args))
    goto error;

  free_args (args);

  sema_up (&p->elf_load_sema);
  thread_yield ();

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g"(&infr) : "memory");
  NOT_REACHED ();

error:
  // delete_process (t->process);
  p->thread = NULL;
  t->process = NULL;
  sema_up (&p->elf_load_sema);
}

#include "syscall.h"

struct process *
get_process (pid_t pid)
{
  struct process *p = -pid;
  if (is_process (p))
    return p;
  return NULL;
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (pid_t pid)
{
  struct process *p = process_current ();
  struct process *child = get_process (pid);
  if (child == NULL)
    return -1;

  // use list_contains to check
  //    if the child is the direct child of current process
  //    if the child is already waited
  if (!list_contains (&p->child_list, &child->childelem))
    // it is not the direct child of current process
    return -1;

  /// NOTE: wait will remove the process from the parent's child list
  ///       so that the parent view the child as waited
  list_remove (&child->childelem);

  sema_down (&child->wait_sema);
  int exit_status = child->exit_status;
  delete_process (child);
  return exit_status;
}

bool
is_process (struct process *p)
{
  return kernel_has_access (p, sizeof *p) && p->magic == PROCESS_MAGIC;
}

void
process_close_all_files (struct process *p)
{

  acquire_filesys ();
  for (struct list_elem *e = list_begin (&p->files);
       e != list_end (&p->files);)
    {
      struct file *f = list_entry (e, struct file, elem);
      ASSERT (is_file (f));
      struct process *onwer = file_get_owner (f);
      ASSERT (is_process (onwer));
      ASSERT (onwer == p);
      e = list_remove (e);
      file_close (f);
    }
  release_filesys ();
}

static void
page_destroy_action (struct hash_elem *e, void *aux)
{
  struct fte *fte = hash_entry (e, struct fte, thread_hash_elem);
  pagedir_clear_page (thread_current ()->pagedir, fte->upage);
  fte_destroy (fte);
  // free (fte);
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *t = thread_current ();
  ASSERT (is_thread (t));

  struct process *p = process_current ();
  uint32_t *pd;

  ASSERT (is_process (p));
  ASSERT (p->thread == t);

  // close all open files
  process_close_all_files (p);
  ASSERT (list_empty (&p->files));

  // prevent exiting before "process_exec" of parent process is done
  sema_down (&p->exec_sama);

  // wait all child processes
  for (struct list_elem *e = list_begin (&p->child_list);
       e != list_end (&p->child_list);)
    {
      struct process *child = list_entry (e, struct process, childelem);
      ASSERT (is_process (child));
      pid_t pid = child->pid;
      e = list_remove (e);
      process_wait (pid);
    }

  p->thread = NULL;

  printf ("%s: exit(%d)\n", t->name, p->exit_status);

  // tell the parent process that this process is finished
  if (p->parent)
    sema_up (&p->wait_sema);

#ifdef VM
  hash_destroy (&t->frame_table, page_destroy_action);

#endif

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = t->pagedir;
  if (pd != NULL)
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      t->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32 /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32 /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32 /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16 /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
{
  unsigned char e_ident[16];
  Elf32_Half e_type;
  Elf32_Half e_machine;
  Elf32_Word e_version;
  Elf32_Addr e_entry;
  Elf32_Off e_phoff;
  Elf32_Off e_shoff;
  Elf32_Word e_flags;
  Elf32_Half e_ehsize;
  Elf32_Half e_phentsize;
  Elf32_Half e_phnum;
  Elf32_Half e_shentsize;
  Elf32_Half e_shnum;
  Elf32_Half e_shstrndx;
};

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
{
  Elf32_Word p_type;
  Elf32_Off p_offset;
  Elf32_Addr p_vaddr;
  Elf32_Addr p_paddr;
  Elf32_Word p_filesz;
  Elf32_Word p_memsz;
  Elf32_Word p_flags;
  Elf32_Word p_align;
};

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL 0           /* Ignore. */
#define PT_LOAD 1           /* Loadable segment. */
#define PT_DYNAMIC 2        /* Dynamic linking info. */
#define PT_INTERP 3         /* Name of dynamic loader. */
#define PT_NOTE 4           /* Auxiliary info. */
#define PT_SHLIB 5          /* Reserved. */
#define PT_PHDR 6           /* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
static bool
load_entry (struct file *file, void (**eip) (void), void **esp)
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  off_t file_ofs;
  bool success = false;
  int i;
  char *save_ptr;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL)
    goto done;
  process_activate ();

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7) || ehdr.e_type != 2
      || ehdr.e_machine != 3 || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr) || ehdr.e_phnum > 1024)
    {
      printf ("load: %s: error loading executable\n");
      goto done;
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++)
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type)
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file))
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *)mem_page, read_bytes,
                                 zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void))ehdr.e_entry;

  success = true;

done:
  /* We arrive here whether the load is successful or not. */
  return success;
}

static bool
load (const char *file_name, void (**eip) (void), void **esp)
{
  bool success = false;
  struct process *p = process_current ();
  ASSERT (p != NULL);
  acquire_filesys ();
  struct file *file = filesys_open (file_name);
  if (file == NULL)
    goto done;
  file_deny_write (file);
  success = load_entry (file, eip, esp);
  list_push_back (&p->files, &file->elem);
done:
  release_filesys ();
  return success;
}

/* load() helpers. */

// static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file)
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off)file_length (file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *)phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *)(phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0)
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int)page_read_bytes)
        {
          palloc_free_page (kpage);
          return false;
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable))
        {
          palloc_free_page (kpage);
          return false;
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp)
{
  uint8_t *kpage;
  bool success = false;

#ifdef VM
  struct fte *fte = fte_create (((uint8_t *)PHYS_BASE - PGSIZE), true,
                                PAL_USER | PAL_ZERO);
  kpage = fte->phys_addr;
#else
  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
#endif

  if (kpage != NULL)
    {
      success = install_page (((uint8_t *)PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
#ifdef VM
        fte_destroy (fte);
#else
        palloc_free_page (kpage);
#endif
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
