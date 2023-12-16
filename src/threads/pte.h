#ifndef THREADS_PTE_H
#define THREADS_PTE_H

#include "threads/vaddr.h"

/* Functions and macros for working with x86 hardware page
   tables.

   See vaddr.h for more generic functions and macros for virtual
   addresses.

   Virtual addresses are structured as follows:

    31                  22 21                  12 11                   0
   +----------------------+----------------------+----------------------+
   | Page Directory Index |   Page Table Index   |    Page Offset       |
   +----------------------+----------------------+----------------------+
*/

union addr_t
{
  struct
  {
    uint32_t offset : 12;
    uint32_t pt_idx : 10;
    uint32_t pd_idx : 10;
  };
  void *ptr;
};

/* Page table index (bits 12:21). */
#define PTSHIFT PGBITS                   /* First page table bit. */
#define PTBITS 10                        /* Number of page table bits. */
#define PTSPAN (1 << PTBITS << PGBITS)   /* Bytes covered by a page table. */
#define PTMASK BITMASK (PTSHIFT, PTBITS) /* Page table bits (12:21). */

/* Page directory index (bits 22:31). */
#define PDSHIFT (PTSHIFT + PTBITS)       /* First page directory bit. */
#define PDBITS 10                        /* Number of page dir bits. */
#define PDMASK BITMASK (PDSHIFT, PDBITS) /* Page directory bits (22:31). */

/* Obtains page table index from a virtual address. */
static inline unsigned
pt_no (const void *va)
{
  return ((uintptr_t)va & PTMASK) >> PTSHIFT;
}

/* Obtains page directory index from a virtual address. */
static inline uintptr_t
pd_no (const void *va)
{
  return (uintptr_t)va >> PDSHIFT;
}

/* Page directory and page table entries.

   For more information see the section on page tables in the
   Pintos reference guide chapter, or [IA32-v3a] 3.7.6
   "Page-Directory and Page-Table Entries".

   PDEs and PTEs share a common format:

   31                                 12 11                     0
   +------------------------------------+------------------------+
   |         Physical Address           |         Flags          |
   +------------------------------------+------------------------+

   In a PDE, the physical address points to a page table.
   In a PTE, the physical address points to a data or code page.
   The important flags are listed below.
   When a PDE or PTE is not "present", the other flags are
   ignored.
   A PDE or PTE that is initialized to 0 will be interpreted as
   "not present", which is just fine. */

union entry_t
{
  struct
  {
    uint32_t present : 1;
    uint32_t writable : 1;
    uint32_t user : 1;
    uint32_t _reserved_0 : 2;
    uint32_t accessed : 1;
    uint32_t dirty : 1;
    uint32_t _reserved_1 : 2;
    uint32_t available : 3;
    uint32_t physical_address : 20;
  };
  uint32_t val;
};

/* Returns a PDE that points to page table PT. */
static inline union entry_t
pde_create (uint32_t *pt)
{
  ASSERT (pg_ofs (pt) == 0);
  union entry_t pde = { .val = vtop (pt) };
  pde.present = 1;
  pde.writable = 1;
  pde.user = 1;
  return pde;
}

/* Returns a pointer to the page table that page directory entry
   PDE, which must "present", points to. */
static inline uint32_t *
pde_get_pt (union entry_t pde)
{
  ASSERT (pde.present);
  return ptov (pde.physical_address << 12);
}

/* Returns a PTE that points to PAGE.
   The PTE's page is readable.
   If WRITABLE is true then it will be writable as well.
   The page will be usable only by ring 0 code (the kernel). */
static inline union entry_t
pte_create_kernel (void *page, bool writable)
{
  ASSERT (pg_ofs (page) == 0);
  union entry_t pte = { .val = vtop (page) };
  pte.present = 1;
  pte.writable = writable;
  return pte;
}

/* Returns a PTE that points to PAGE.
   The PTE's page is readable.
   If WRITABLE is true then it will be writable as well.
   The page will be usable by both user and kernel code. */
static inline union entry_t
pte_create_user (void *page, bool writable)
{
  union entry_t pte = pte_create_kernel (page, writable);
  pte.user = 1;
  return pte;
}

/* Returns a pointer to the page that page table entry PTE points
   to. */
static inline void *
pte_get_page (union entry_t pte)
{
  return ptov (pte.physical_address << 12);
}

#endif /* threads/pte.h */
