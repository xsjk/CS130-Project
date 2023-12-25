#ifndef VM_SUPPLEMENTAL_PAGE_TABLE_H
#define VM_SUPPLEMENTAL_PAGE_TABLE_H

#include "threads/palloc.h"
#include <hash.h>

bool user_stack_growth (void *fault_addr, void *esp);

#endif /* vm/supplemental_page_table.h */