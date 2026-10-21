/* SPDX-License-Identifier: MIT */
/* src/ref_scalar.c — byte-at-a-time reference. NOT a performance kernel.
 *
 * Purpose:
 *   - validates the harness before any real kernel exists (Phase 1), and
 *   - provides the golden model for the harness --verify pass.
 *
 * It is deliberately the slowest possible formulation so that a harness bug
 * (e.g. measuring the wrong buffer, or a kernel nobody actually called) shows
 * up as an absurd GB/s number rather than as a plausible one.
 *
 * Loads/stores: one byte load from src, one byte store to dst, per byte.
 *
 * VECTORISATION IS EXPLICITLY DISABLED, and not only here: the Makefile puts
 * these files in SCALAR_SRCS and adds -fno-vectorize -fno-slp-vectorize
 * -fno-unroll-loops. This is not theoretical - without it clang -O3
 * -march=znver2 rewrote memcpy_ref into a 4x32B `vmovups` loop and the
 * "byte-at-a-time reference" measured 30-70 GB/s, which is impossible for a
 * byte loop and which silently corrupted the first harness validation run.
 * The `#pragma clang loop` lines below make the intent survive a manual
 * compile too. `make audit` fails loudly if vector instructions reappear here.
 */
#include "common.h"

void *memcpy_ref(void *dst, const void *src, size_t n)
{
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];                       /* 1B load + 1B store per iteration */
    return dst;
}

void *memset_ref(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    unsigned char  v = (unsigned char)c;
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
    for (size_t i = 0; i < n; i++)
        d[i] = v;                          /* 1B store per iteration */
    return dst;
}
