#ifndef THREADS_INIT_H
#define THREADS_INIT_H

#include <debug.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <threads/pte.h>

/* Page directory with kernel mappings only. */
extern union entry_t *init_page_dir;

#endif /* threads/init.h */
