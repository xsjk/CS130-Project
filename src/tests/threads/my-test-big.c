#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include <stdio.h>

static thread_func thread32;
static thread_func thread33;
static thread_func thread34;
static thread_func thread35;

struct locks
{
  struct lock a;
  struct lock b;
  struct lock c;
  struct lock d;
  struct lock e;
};

static struct thread *t[6];
static struct thread *tmain, *t32, *t33, *t34, *t35;

static struct locks locks_holder;

void
my_test_big (void)
{
  tmain = thread_current ();

  struct locks *locks = &locks_holder;

  /* This test does not work with the MLFQS. */
  ASSERT (!thread_mlfqs);

  thread_set_priority (31);

  lock_init (&locks->a);
  lock_init (&locks->b);
  lock_init (&locks->c);
  lock_init (&locks->d);
  lock_init (&locks->e);

  lock_acquire (&locks->e); // no block

  ASSERT (tmain->priority == 31);

  thread_create ("thread32", 32, thread32, locks);

  ASSERT (tmain->priority == 32);
  ASSERT (t32->priority == 32);

  lock_release (&locks->e); // continue thread32

  ASSERT (tmain->priority == 31);

  msg ("This should be the last line before finishing this test.");
}

static void
thread32 (void *aux)
{
  t32 = thread_current ();

  ASSERT (t32->priority == 32);

  struct locks *locks = aux;

  lock_acquire (&locks->e); // block by main, donate 32 to main
  msg ("thread32: got lock e");

  ASSERT (tmain->priority == 31);
  ASSERT (t32->priority == 32);

  lock_release (&locks->e);

  lock_acquire (&locks->c); // no block

  thread_create ("thread33", 33, thread33, locks);

  ASSERT (tmain->priority == 31);
  ASSERT (t32->priority == 33);
  ASSERT (t33->priority == 33);

  lock_acquire (&locks->a); // no block
  msg ("thread32: got lock a");

  thread_create ("thread34", 34, thread34, locks);
  ASSERT (t32->priority == 34); // donated by thread34

  lock_acquire (&locks->b); // no block
  msg ("thread32: got lock b");

  thread_create ("thread35", 35, thread35, locks);
  ASSERT (t32->priority == 35); // donated by thread35

  lock_release (&locks->a); // do not continue thread34
  lock_release (&locks->b); // continue thread35
  lock_release (&locks->c); // continue thread35

  msg ("thread32: done");
}

static void
thread33 (void *aux)
{
  t33 = thread_current ();

  ASSERT (t33->priority == 33);

  struct locks *locks = aux;

  lock_acquire (&locks->c); // block by thread32,  donate 33 to thread32
  msg ("thread33: got lock c");
  lock_release (&locks->c);
  msg ("thread33: done");
}

static void
thread34 (void *aux)
{
  t34 = thread_current ();

  ASSERT (t34->priority == 34);

  struct locks *locks = aux;

  lock_acquire (&locks->a); // block by thread32,  donate 34 to thread32
  msg ("thread34: got lock a");
  lock_release (&locks->a);
  msg ("thread34: done");
}

static void
thread35 (void *aux)
{
  t35 = thread_current ();

  ASSERT (t35->priority == 35);

  struct locks *locks = aux;

  lock_acquire (&locks->b); // block by thread32, donate 35 to thread32
  msg ("thread35: got lock b");
  lock_release (&locks->b);
  msg ("thread35: done");
}
