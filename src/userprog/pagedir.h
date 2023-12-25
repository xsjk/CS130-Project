#ifndef USERPROG_PAGEDIR_H
#define USERPROG_PAGEDIR_H

#include <stdbool.h>
#include <stdint.h>
#include <threads/pte.h>

union entry_t *pagedir_create (void);
void pagedir_destroy (union entry_t *pd);
bool pagedir_set_page (union entry_t *pd, void *upage, void *kpage, bool rw);
void *pagedir_get_page (union entry_t *pd, const void *upage);
void pagedir_clear_page (union entry_t *pd, void *upage);
bool pagedir_is_dirty (union entry_t *pd, const void *upage);
void pagedir_set_dirty (union entry_t *pd, const void *upage, bool dirty);
bool pagedir_is_accessed (union entry_t *pd, const void *upage);
void pagedir_set_accessed (union entry_t *pd, const void *upage,
                           bool accessed);
void pagedir_activate (union entry_t *pd);

#endif /* userprog/pagedir.h */
