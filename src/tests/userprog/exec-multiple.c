/* Executes and waits for multiple child processes. */

#include "tests/lib.h"
#include "tests/main.h"
#include <syscall.h>

void
test_main (void)
{
  wait (exec ("child-simple"));
  wait (exec ("child-simple"));
  wait (exec ("child-simple"));
  wait (exec ("child-simple"));
}
