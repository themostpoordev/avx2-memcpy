/* SPDX-License-Identifier: MIT */
/* src/memcpy_naive.c — KERNEL 1 of the ladder: byte-by-byte loop.
 *
 * This is the floor of the ladder and exists to be beaten. It is deliberately
 * the simplest formulation in C11 that satisfies the memcpy contract: one byte
 * load, one byte store, per byte, in increasing address order.
 *
 * Contract and assumptions
 *   - Non-overlapping src/dst (memmove is out of scope for now). There is no
 *     overlap check here on purpose: an overlap check would be a branch that
 *     this kernel is supposed to pay for and the vector kernels to avoid.
 *   - `dst` and `src` may have any alignment: 1-byte accesses have no
 *     alignment requirement, so no unaligned load/store UB exists here.
 *   - No strict-aliasing violation: every access goes through
 *     `unsigned char *`, which is permitted to alias any object type.
 *   - n == 0 must not touch memory (memcpy contract); the loop below does
 *     nothing in that case, and the early return makes that explicit.
 *
 * Small-size rule (mandatory < 32 B fast path)
 *   The rule says "handle < 32 bytes without entering the vector loop". This
 *   kernel has no vector loop at all, so the rule holds by construction: every
 *   size, including every size below 32, takes the same 1-byte path and there
 *   is no SIMD entry cost to bypass. The interesting question ("is the SIMD
 *   fast path actually faster below 64 B?") is a measurement, and it is
 *   reported for the vector kernels once they exist.
 *
 * Vectorisation is disabled three ways (this must not silently become an AVX2
 * kernel - that mistake already cost one bogus validation run in Phase 1):
 *   1. src/memcpy_naive.c is listed in SCALAR_SRCS in the Makefile, which adds
 *      -fno-vectorize -fno-slp-vectorize -fno-unroll-loops;
 *   2. the pragmas below, which survive a manual compile;
 *   3. `make audit`, which fails if vector instructions appear in this TU.
 */
#include "common.h"

void *memcpy_naive(void *dst, const void *src, size_t n)
{
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    if (UNLIKELY(n == 0))
        return dst;                       /* 0-byte copy: touch nothing */

#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];                      /* 1B load src[i] -> 1B store dst[i] */

    return dst;
}
