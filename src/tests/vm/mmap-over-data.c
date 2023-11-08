/* Verifies that mapping over the data segment is disallowed. */

#include "tests/lib.h"
#include "tests/main.h"
#include <round.h>
#include <stdint.h>
#include <syscall.h>

static char x;

void
test_main (void)
{
  uintptr_t x_page = ROUND_DOWN ((uintptr_t)&x, 4096);
  int handle;

  CHECK ((handle = open ("sample.txt")) > 1, "open \"sample.txt\"");
  CHECK (mmap (handle, (void *)x_page) == MAP_FAILED,
         "try to mmap over data segment");
}
