#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

#define __user

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// bool user_has_access (uint8_t __user *, size_t);
bool kernel_has_access (uint8_t __user *, size_t);

#endif /* userprog/syscall.h */
