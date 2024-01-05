#include "page.h"
#include "frame.h"
#include "swap.h"

#include "filesys/off_t.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"

#define STACK_MAX (1 << 23) // 8MB

bool
user_stack_growth (void *fault_addr, void *esp)
{
  void *upage = pg_round_down (fault_addr);
  bool writable = true;
  bool success = true;

  struct fte *fte = cur_frame_table_find (upage);

  if (fte == NULL)
    {
      // try stack growth
      if (fault_addr >= esp - 32 && upage >= PHYS_BASE - STACK_MAX)
        {
          struct fte *new_fte = fte_create (upage, writable);
          ASSERT (new_fte != NULL);
        }
      else
        {
          success = false;
        }
    }
  else
    {
      switch (fte->type)
        {
        case SPTE_FRAME:
          PANIC (" frame should not exist, otherwise won't raise page fault.");
          break;
        case SPTE_SWAP:
        case SPTE_FILE:
          fte_unevict (fte);
          break;
        default:
          PANIC ("should not reach here");
          break;
        }
    }

  return success;
}