/* SPDX-License-Identifier: MIT */
/* src/memcpy_word.c — KERNEL 2 of the ladder: 8-byte chunks + byte tail.
 *
 * Rung 1 copied one byte per iteration. This rung copies one 64-bit word per
 * iteration, which is the cheapest step up that changes the instruction count
 * per byte without entering SIMD. It is deliberately NOT vectorised: this file
 * is listed in SCALAR_SRCS, so clang cannot turn the word loop into an AVX2
 * loop and make rung 2 silently become rung 4.
 *
 * Contract and assumptions
 *   - Non-overlapping src/dst (memmove is out of scope), same as rung 1.
 *   - Any alignment: every access below is either a byte or an 8-byte access
 *     performed through __builtin_memcpy, which compiles to a plain unaligned
 *     load/store on this silicon. No aligned-store UB is possible.
 *   - No strict-aliasing violation: the word is transferred into and out of a
 *     uint64_t object by __builtin_memcpy, so no pointer of one type is used to
 *     access an object of another type.
 *   - n == 0 must not touch memory. The < 32 fast path does nothing for n == 0.
 *   - No overrun: the word loop runs only while i + 8 <= n, so the 8-byte store
 *     is always fully inside dst. The tail loop writes exactly the remaining
 *     bytes. Nothing is ever written past dst[n-1].
 *
 * Why __builtin_memcpy and not a cast or a plain memcpy call
 *   - A cast to uint64_t* would be an unaligned, aliasing access: UB in C11 even
 *     though it happens to work on x86-64. `may_alias` typedefs would fix that
 *     but are a clang extension; __builtin_memcpy is standard C and clang
 *     lowers an 8-byte one to a single MOV.
 *   - A plain `memcpy(&w, s + i, 8)` would be an actual call to libc here,
 *     because the Makefile compiles every kernel with -fno-builtin-memcpy so
 *     that the compiler can never synthesise the very function we are
 *     reimplementing. The __builtin_ form is unaffected by that flag and is
 *     still inlined. (Verified in the codegen diff: the hot loop is MOV only.)
 *
 * Small-size rule (< 32 bytes must not enter the word loop)
 *   The byte loop below handles every n < 32. Rationale: for n < 8 the word loop
 *   cannot complete a single word, and for 8 <= n < 32 it would run 1-3
 *   iterations while still paying the loop's counter and branch; below 32 bytes
 *   the copy is three words plus a tail at most, so the general path buys
 *   nothing. Whether this crossover is actually in the right place is a
 *   measurement, not an argument — kernel 2 is benchmarked at 16/32/64 B against
 *   rung 1 and against glibc, and the result is reported as measured.
 *
 * Known loss, stated up front: the byte tail is up to 7 byte-pairs per call, and
 * there is no head-alignment prologue. Both are deliberate for this rung and are
 * what rungs 3+ exist to fix.
 */
#include "common.h"

void *memcpy_word(void *dst, const void *src, size_t n)
{
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    size_t i = 0;

    /* ---- fast path: < 32 bytes stays on byte copies ---------------------- */
    if (n < 32) {
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
        for (; i < n; i++)
            d[i] = s[i];                  /* 1B load + 1B store, n < 32 */
        return dst;
    }

    /* ---- 8-byte word loop ------------------------------------------------ */
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
    for (; i + 8 <= n; i += 8) {
        uint64_t w;
        __builtin_memcpy(&w, s + i, 8);   /* 8B load  from src[i .. i+7] */
        __builtin_memcpy(d + i, &w, 8);   /* 8B store to   dst[i .. i+7] */
    }

    /* ---- tail: fewer than 8 bytes remain --------------------------------- */
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
    for (; i < n; i++)
        d[i] = s[i];                      /* 1B load + 1B store per tail byte */

    return dst;
}
