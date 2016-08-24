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

#if SIZE_MAX > (1ULL << 40)
#define VPX_MAX_ALLOCABLE_MEMORY (1ULL << 40)
#else
// For 32-bit targets keep this below INT_MAX to avoid valgrind warnings.
#define VPX_MAX_ALLOCABLE_MEMORY ((1ULL << 31) - (1 << 16))
#endif

// Returns 0 in case of overflow of nmemb * size.
static int check_size_argument_overflow(uint64_t nmemb, uint64_t size) {
  const uint64_t total_size = nmemb * size;
  if (nmemb == 0) return 1;
  if (size > VPX_MAX_ALLOCABLE_MEMORY / nmemb) return 0;
  if (total_size != (size_t)total_size) return 0;

  return 1;
}

void *vpx_memalign(size_t align, size_t size) {
  const uint64_t total_size = (uint64_t)size + align - 1 + ADDRESS_STORAGE_SIZE;
  void *addr, *x = NULL;

  if (!check_size_argument_overflow(1, total_size)) return NULL;

  addr = malloc((size_t)total_size);

  if (addr) {
    x = align_addr((unsigned char *)addr + ADDRESS_STORAGE_SIZE, (int)align);
    /* save the actual malloc address */
    ((size_t *)x)[-1] = (size_t)addr;
  }

  return x;
}

void *vpx_malloc(size_t size) { return vpx_memalign(DEFAULT_ALIGNMENT, size); }

void *vpx_calloc(size_t num, size_t size) {
  void *x;

  if (!check_size_argument_overflow(num, size)) return NULL;

  x = vpx_memalign(DEFAULT_ALIGNMENT, num * size);

  if (x) memset(x, 0, num * size);

  return x;
}

void *vpx_realloc(void *memblk, size_t size) {
  void *addr, *new_addr = NULL;
  int align = DEFAULT_ALIGNMENT;

  /*
  The realloc() function changes the size of the object pointed to by
  ptr to the size specified by size, and returns a pointer to the
  possibly moved block. The contents are unchanged up to the lesser
  of the new and old sizes. If ptr is null, realloc() behaves like
  malloc() for the specified size. If size is zero (0) and ptr is
  not a null pointer, the object pointed to is freed.
  */
  if (!memblk)
    new_addr = vpx_malloc(size);
  else if (!size)
    vpx_free(memblk);
  else {
    const uint64_t total_size =
        (uint64_t)size + align - 1 + ADDRESS_STORAGE_SIZE;
    addr = (void *)(((size_t *)memblk)[-1]);
    memblk = NULL;

    if (!check_size_argument_overflow(1, total_size)) return NULL;

    new_addr = realloc(addr, size + align + ADDRESS_STORAGE_SIZE);

    if (new_addr) {
      addr = new_addr;
      new_addr =
          (void *)(((size_t)((unsigned char *)new_addr + ADDRESS_STORAGE_SIZE) +
                    (align - 1)) &
                   (size_t)-align);
      /* save the actual malloc address */
      ((size_t *)new_addr)[-1] = (size_t)addr;
    }
  }

  return new_addr;
}

void vpx_free(void *memblk) {
  if (memblk) {
    void *addr = (void *)(((size_t *)memblk)[-1]);
    free(addr);
  }
}

#if CONFIG_VP9_HIGHBITDEPTH
void *vpx_memset16(void *dest, int val, size_t length) {
  size_t i;
  uint16_t *dest16 = (uint16_t *)dest;
  for (i = 0; i < length; i++) *dest16++ = val;
  return dest;
}
#endif  // CONFIG_VP9_HIGHBITDEPTH
