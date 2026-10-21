/* SPDX-License-Identifier: MIT */
/* include/memcpy_fast.h — the shipped memcpy.
 *
 * memcpy_fast copies n bytes from src to dst and returns dst.
 *
 * Contract: identical to memcpy for non-overlapping src and dst. Any alignment
 * of either pointer, any n including 0. Reads and writes exactly [0, n) - the
 * implementation's overlapping tail copies stay inside the destination, which is
 * checked on every build by the harness's --verify pass (n = 0..4096, randomised
 * offsets, 64 B canaries on both sides).
 *
 * Measured against glibc on Zen 2 (docs/RESULTS.md): +38% at 16 B, +13% at 32 B,
 * +13% at 64 B, +13.5% at 512 B, level from 1 KiB to 64 KiB, -20% at 128 B.
 * Those numbers are silicon-specific; benchmark on yours before relying on them.
 */
#ifndef MEMCPY_FAST_H
#define MEMCPY_FAST_H

#include <stddef.h>

/* Requires AVX2 for n > 64. For n <= 64 the small path uses
   SSE2 only and runs on any x86-64. Calls with n > 64 on a
   non-AVX2 CPU will SIGILL. */
void *memcpy_fast(void *dst, const void *src, size_t n);

#endif /* MEMCPY_FAST_H */
