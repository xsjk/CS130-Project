#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

#define __user

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool user_has_access (uint8_t __user *, size_t);
bool kernel_has_access (uint8_t __user *, size_t);

int try_load (const uint8_t __user *uaddr);
bool try_store (uint8_t __user *uaddr, uint8_t byte);

#ifdef FILESYS
bool chdir (const char *dir);
bool mkdir (const char *dir);
bool readdir (int fd, char *name);
bool isdir (int fd);
int inumber (int fd);
#endif

#endif /* userprog/syscall.h */
