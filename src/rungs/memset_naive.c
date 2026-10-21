/* SPDX-License-Identifier: MIT */
/* src/memset_naive.c — KERNEL 8, rung 1: byte-by-byte fill.
 *
 * The floor of the memset ladder, mirroring kernel 1 of the memcpy ladder: one
 * byte store per byte, increasing addresses, nothing vectorised (this file is in
 * SCALAR_SRCS, so clang cannot turn it into an AVX2 fill behind our back).
 *
 * Contract
 *   - dst may have any alignment: a byte store has no alignment requirement.
 *   - Writes exactly [dst, dst+n). Nothing past dst[n-1], which is what bench.c's
 *     64 B canaries check.
 *   - n == 0 touches nothing (the loop does nothing, and the early return says so).
 *   - The value is the *byte* (unsigned char)c, as the C standard requires; the
 *     harness passes values where that distinction is observable (0x00, 0xFF and
 *     a random byte), so a kernel that filled with a word would fail --verify.
 */
#include "common.h"

void *memset_naive(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    unsigned char  v = (unsigned char)c;

    if (UNLIKELY(n == 0))
        return dst;

#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
    for (size_t i = 0; i < n; i++)
        d[i] = v;

    return dst;
}
