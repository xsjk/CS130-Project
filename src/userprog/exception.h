#ifndef USERPROG_EXCEPTION_H
#define USERPROG_EXCEPTION_H

/* Page fault error code bits that describe the cause of the exception.  */
#define PF_P 0x1  /* 0: not-present page. 1: access rights violation. */
#define PF_W 0x2  /* 0: read, 1: write. */
#define PF_U 0x4  /* 0: kernel, 1: user process. */
#define PF_R 0x8  /* 0: reserved bit violation. */
#define PF_I 0x10 /* 0: error not caused by an instruction fetch. */
#define PF_X 0x20 /* 0: error not caused by a protection violation. */

extern long long page_fault_cnt;

void exception_init (void);
void exception_print_stats (void);

#endif /* userprog/exception.h */
