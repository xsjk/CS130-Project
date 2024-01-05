#ifndef __LIB_STDDEF_H
#define __LIB_STDDEF_H

#if defined(__cplusplus)
#define NULL nullptr
#else
#define NULL ((void *)0)
#endif

#define offsetof(TYPE, MEMBER) ((size_t) & ((TYPE *)0)->MEMBER)

/* GCC predefines the types we need for ptrdiff_t and size_t,
   so that we don't have to guess. */
typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef __SIZE_TYPE__ size_t;

#define MAX(a, b) ((a > b) ? a : b)
#define MIN(a, b) ((a < b) ? a : b)

#endif /* lib/stddef.h */
