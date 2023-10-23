#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include <stdio.h>
static thread_func thread1;
static thread_func thread2;

struct lock l[5];

void
my_test (void)
{

  lock_init (&l[0]);
  lock_init (&l[1]);
  lock_init (&l[2]);
  lock_init (&l[3]);
  lock_init (&l[4]);

  /* This test does not work with the MLFQS. */
  ASSERT (!thread_mlfqs);

  lock_acquire (&l[0]);

  ASSERT (l[0].holder->priority == PRI_DEFAULT);

  lock_acquire (&l[4]);
  msg ("Main thread acquired the lock");

  ASSERT (l[0].holder->priority == PRI_DEFAULT);

  thread_create ("thread1", PRI_DEFAULT + 1, thread1, NULL);
  ASSERT (l[0].holder->priority == PRI_DEFAULT + 1);
  ASSERT (l[1].holder->priority == PRI_DEFAULT + 1);

  thread_create ("thread2", PRI_DEFAULT + 2, thread2, NULL);
  ASSERT (l[0].holder->priority == PRI_DEFAULT + 2);
  // ASSERT (l[1].holder->priority == PRI_DEFAULT + 2); // this is a bug
  ASSERT (l[2].holder->priority == PRI_DEFAULT + 2);

  lock_release (&l[4]);
  msg ("thread2 should have just finished.");
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 1, thread_get_priority ());
}

static void
thread1 (void *)
{
  lock_acquire (&l[1]);

  ASSERT (l[0].holder->priority == PRI_DEFAULT);
  ASSERT (l[1].holder->priority == PRI_DEFAULT + 1);

  lock_acquire (&l[4]);
  msg ("thread1 acquired the lock");

  ASSERT (l[0].holder->priority == PRI_DEFAULT + 1);
  ASSERT (l[1].holder->priority == PRI_DEFAULT + 1);

  ASSERT (thread_get_priority () == PRI_DEFAULT + 1);
  lock_release (&l[4]);
  msg ("thread1 finished.");
}

static void
thread2 (void *)
{
  lock_acquire (&l[2]);

  ASSERT (l[0].holder->priority == PRI_DEFAULT + 1);
  ASSERT (l[1].holder->priority == PRI_DEFAULT + 1);
  ASSERT (l[2].holder->priority == PRI_DEFAULT + 2);

  lock_acquire (&l[4]);
  msg ("thread2 acquired the lock");

  ASSERT (l[0].holder->priority == PRI_DEFAULT + 1);
  ASSERT (l[1].holder->priority == PRI_DEFAULT + 1);
  ASSERT (l[2].holder->priority == PRI_DEFAULT + 2);

  lock_release (&l[4]);
  msg ("thread2 finished.");
}
