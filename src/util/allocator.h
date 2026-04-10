#ifndef SSE_ALLOCATOR_H_
#define SSE_ALLOCATOR_H_

#include "../arch/linux/errno.h"
#include "./types.h"
#include "string.h"
// extern void* alloca(size_t __size);
#undef alloca
#undef __alloca
#define alloca(size) __alloca(size)
#define __alloca(size) __builtin_alloca(size)
#include "../arch/memory.h"

#define ALLOCATE_STACK
// #define ALLOCATE_HEAP

#if defined(ALLOCATE_STACK)
#define _alloc(size) alloca(size)
#define _free(addr)
#undef ALLOCATE_STACK
#elif defined(ALLOCATE_HEAP)
#define _alloc(size) malloc(size)
#define _free(addr) free(addr)
#undef ALLOCATE_HEAP
#endif

/**
 * _memcpy
 */
static inline void* _memcpy(void* dest, const void* src, size_t size)
{
  return internal_memcpy(dest, src, size);
}

/*
 * _memset
 */
static inline void* _memset(void* s, const int32_t c, const size_t size)
{
  return internal_memset(s, c, size);
}

/*
 * _memset_s
 */
static inline int32_t _memset_s(void* s, const size_t smax, const int32_t c, const size_t n)
{
  return internal_memset_s(s, smax, c, n);
}

#endif
