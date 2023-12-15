#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/synch.h"
#include "user/syscall.h"

struct thread;
struct process
{
  struct thread *thread; /* The thread on which this process is running */
  pid_t pid;
  int exit_status; /* Exit status of the thread. */

  struct process *parent; /* Parent thread. */

  /// TODO: move any of these to thread struct if they can be freed with thread
  struct list files;         /* Files opened by this thread*/
  struct list mmapped_files; /* Files opened by this thread*/

  struct list child_list; /* List of child processes. */
  /// TODO: use hash table to store child processes

  struct list_elem childelem;     /* List element for child_list. */
  struct semaphore wait_sema;     /* Semaphore for waiting (block parent) */
  struct semaphore elf_load_sema; /* Semaphore for waiting child to load elf
                                     (block parent) */
  struct semaphore
      exec_sama; /* Block this thread from exiting too early, not before
                       "process_exec" of parent process is done */
  int magic;
};

void init_process (struct process *this, struct thread *);
struct process *new_process (struct thread *);
void delete_process (struct process *);

struct process *process_current (void);
pid_t process_pid (void);

pid_t process_execute (const char *cmd);
int process_wait (pid_t);
void process_exit (void);
void process_activate (void);

void process_close_all_files (struct list *files);

struct process *get_process (pid_t);

bool is_process (struct process *);

extern bool install_page (void *upage, void *kpage, bool writable);
#endif /* userprog/process.h */
