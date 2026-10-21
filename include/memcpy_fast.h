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
 * Measured against glibc on Zen 2 (docs/RESULTS.md, the committed reference
 * campaign): +38% at 16 B, +25% at 32 B, +25% at 64 B, +10% at 128 B, +8% at
 * 256 B, +14.6% at 512 B, level from 1 KiB to 64 KiB, -2.3% at 16 KiB. Those
 * numbers are silicon-specific; benchmark on yours before relying on them.
 *
 * BUILD FLAGS. This kernel must be compiled with
 *     gcc -O3 -fno-crossjumping -falign-labels=64
 * -falign-labels=64 is not cosmetic: without it the 32..64 B path loses the
 * 64-byte alignment of its out-of-line branch target and that costs 16% at
 * 32 B and 64 B. -fno-crossjumping is required for the same reason the
 * original F4 needed it. The Makefile's GCC_EXTRA_fast.c carries both.
 */
#ifndef MEMCPY_FAST_H
#define MEMCPY_FAST_H

#include <stddef.h>

/* Requires AVX2 for n > 64. For n <= 64 the small path uses
   SSE2 only and runs on any x86-64. Calls with n > 64 on a
   non-AVX2 CPU will SIGILL. */
void *memcpy_fast(void *dst, const void *src, size_t n);

#endif /* MEMCPY_FAST_H */
