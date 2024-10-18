/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "vpx_mem.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/vpx_mem_intrnl.h"
#include "vpx/vpx_integer.h"

#if !defined(VPX_MAX_ALLOCABLE_MEMORY)
#if SIZE_MAX > (1ULL << 40)
#define VPX_MAX_ALLOCABLE_MEMORY (1ULL << 40)
#else
// For 32-bit targets keep this below INT_MAX to avoid valgrind warnings.
#define VPX_MAX_ALLOCABLE_MEMORY ((1ULL << 31) - (1 << 16))
#endif
#endif

// Returns 0 in case of overflow of nmemb * size.
static int check_size_argument_overflow(uint64_t nmemb, uint64_t size) {
  const uint64_t total_size = nmemb * size;
  if (nmemb == 0) return 1;
  if (size > VPX_MAX_ALLOCABLE_MEMORY / nmemb) return 0;
  if (total_size != (size_t)total_size) return 0;

  return 1;
}

#if defined(__CHERI_PURE_CAPABILITY__)
static uintptr_t *get_malloc_address_location(void *const mem) {
  return ((uintptr_t *)mem) - 1;
#else   // !__CHERI_PURE_CAPABILITY__
static size_t *get_malloc_address_location(void *const mem) {
  return ((size_t *)mem) - 1;
#endif  // !__CHERI_PURE_CAPABILITY__
}

static uint64_t get_aligned_malloc_size(size_t size, size_t align) {
  return (uint64_t)size + align - 1 + ADDRESS_STORAGE_SIZE;
}

static void set_actual_malloc_address(void *const mem,
                                      const void *const malloc_addr) {
#if defined(__CHERI_PURE_CAPABILITY__)
  uintptr_t *const malloc_addr_location = get_malloc_address_location(mem);
  *malloc_addr_location = (uintptr_t)malloc_addr;
#else   // !__CHERI_PURE_CAPABILITY__
  size_t *const malloc_addr_location = get_malloc_address_location(mem);
  *malloc_addr_location = (size_t)malloc_addr;
#endif  // !__CHERI_PURE_CAPABILITY__
}

static void *get_actual_malloc_address(void *const mem) {
#if defined(__CHERI_PURE_CAPABILITY__)
  uintptr_t *const malloc_addr_location = get_malloc_address_location(mem);
#else   // !__CHERI_PURE_CAPABILITY__
  size_t *const malloc_addr_location = get_malloc_address_location(mem);
#endif  // !__CHERI_PURE_CAPABILITY__
  return (void *)(*malloc_addr_location);
}

void *vpx_memalign(size_t align, size_t size) {
  void *x = NULL, *addr;
  const uint64_t aligned_size = get_aligned_malloc_size(size, align);
  if (!check_size_argument_overflow(1, aligned_size)) return NULL;

  addr = malloc((size_t)aligned_size);
  if (addr) {
#if __has_builtin(__builtin_align_up)
    x = __builtin_align_up((unsigned char *)addr + ADDRESS_STORAGE_SIZE, align);
#else
    x = align_addr((unsigned char *)addr + ADDRESS_STORAGE_SIZE, align);
#endif
    set_actual_malloc_address(x, addr);
  }
  return x;
}

void *vpx_malloc(size_t size) { return vpx_memalign(DEFAULT_ALIGNMENT, size); }

void *vpx_calloc(size_t num, size_t size) {
  void *x;
  if (!check_size_argument_overflow(num, size)) return NULL;

  x = vpx_malloc(num * size);
  if (x) memset(x, 0, num * size);
  return x;
}

void vpx_free(void *memblk) {
  if (memblk) {
    void *addr = get_actual_malloc_address(memblk);
    free(addr);
  }
}
