/* SPDX-License-Identifier: MIT */
/* src/memcpy_avx2_impl.h — shared body of the Kernel 4 (AVX2) rung.
 *
 * Three variants are built from this one body, differing only in prefetch
 * distance, so the prefetch experiment cannot accidentally change anything else:
 *
 *   src/memcpy_avx2.c       MEMCPY_AVX2_PF_LINES 1   prefetch s/d + i +  64 B
 *   src/memcpy_avx2_pf2.c   MEMCPY_AVX2_PF_LINES 2   prefetch s/d + i + 128 B
 *   src/memcpy_avx2_pf4.c   MEMCPY_AVX2_PF_LINES 4   prefetch s/d + i + 256 B
 *
 * `memcpy_avx2` (PF_LINES 1) is the rung of the ladder; pf2/pf4 exist only to
 * answer "what should the distance be", and the report gives all three.
 *
 * Shape
 *   - n >= 64: 4x32 B unrolled loop (128 B = two cache lines per iteration),
 *     then a 32 B loop, then a tail for the remaining < 32 B.
 *   - n <  64: the small-size fast path, which never enters the AVX2 loop.
 *     Correction, measured rather than intended: its effective width is NOT the
 *     128 bits this source asks for at every size. For 32 <= n < 64 the source
 *     writes four 16 B accesses (0, 16, n-32, n-16) and clang's SLP vectorizer
 *     merges the two adjacent pairs into 32 B ymm accesses - the same class of
 *     merge that made the "SSE2" rung compile to AVX at 43f01dc. Evidence in the
 *     built object (build/<target>/src/memcpy_avx2.o): 14 ymm references, four
 *     of them in this branch, while 16 <= n < 32 is genuinely 16 B (xmm).
 *     Correctness is unaffected - each merged access covers [0,32) or [n-32,n),
 *     both inside [0,n) for n >= 32, so bench.c's 64 B canaries cannot be touched
 *     and verify passes at every n - but the monotonicity sentence that used to
 *     stand here ("rung 3's widest access is 16 B, rung 4's is 32 B" below 64 B)
 *     was wrong as compiled and is retired: at 32..63 B this rung IS 32 B wide.
 *     Whether to force 128-bit there is a Kernel 5 question, because changing it
 *     invalidates the measured Kernel 4 campaign and its frozen snapshots.
 *
 * The tail, and why it is not a byte loop
 *   The remainder after the loops is < 32 B. For 16 <= r < 32 it is copied by
 *   two 16 B accesses, at offset 0 and at offset r-16, which OVERLAP each other;
 *   below 16 B the same idea uses 8/4/2 B accesses, and below 2 B a single byte.
 *   Two properties make this correct here:
 *     1. every access lies inside [0, n) of both buffers (the second access of a
 *        pair starts at r-sz >= 0), so nothing is written past dst[n-1] and the
 *        64 B canaries in bench.c's verify cannot be touched;
 *     2. src and dst do not overlap (this ladder copies, it does not move), so
 *        writing the same byte twice - once as "head", once as "tail" - is
 *        harmless and the two may be issued in either order.
 *   The price is that a pair of accesses may read and write a few bytes twice.
 *   For a copy the alternative (a byte loop) costs up to 15 byte-pairs for the
 *   same bytes, which is the worse trade at these sizes; the report measures
 *   where that stops being true rather than asserting it.
 *
 * Prefetch
 *   One hinted pair per 128 B iteration, `_mm_prefetch(..., _MM_HINT_T0)`, for
 *   the source and the destination stream at `+ MEMCPY_AVX2_PF_LINES * 64` bytes
 *   from the iteration start. Read against what the loop has already consumed
 *   (the four loads cover [i, i+128)), the three variants are therefore:
 *     pf1 -> the second line of the block being loaded right now (no lookahead)
 *     pf2 -> the first line of the next block (one iteration ahead)
 *     pf4 -> the first line two blocks ahead (two iterations ahead)
 *   Prefetching dst as well as src is deliberate: the store stream still needs
 *   ownership, and a write prefetch is how that request gets issued early. A
 *   prefetch address may legally point past the end of the mapping - prefetch
 *   hints never fault, so no bounds check is needed or wanted here.
 *
 * Bytes at a time, after the 43f01dc lesson
 *   `_mm256_loadu_si256` / `_mm256_storeu_si256` only: there is no alignment
 *   test anywhere in this kernel, so there is no alignment branch to mispredict
 *   and no 32-byte-aligned store that could assume what it does not have. H1 of
 *   the Kernel-4 diagnostics checked exactly this, by counting mask tests: zero.
 */
#ifndef MEMCPY_AVX2_FN
#error "define MEMCPY_AVX2_FN before including memcpy_avx2_impl.h"
#endif
#ifndef MEMCPY_AVX2_PF_LINES
#error "define MEMCPY_AVX2_PF_LINES before including memcpy_avx2_impl.h"
#endif

#include <immintrin.h>
#include "common.h"

#define MEMCPY_AVX2_PF_BYTES ((size_t)MEMCPY_AVX2_PF_LINES * 64u)

void *MEMCPY_AVX2_FN(void *dst, const void *src, size_t n)
{
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    /* ---- main path: 64 B and up ----------------------------------------- */
    if (n >= 64) {
        size_t i = 0;

        /* 128 B per iteration: four 32 B loads + four 32 B stores */
        for (; i + 128 <= n; i += 128) {
            _mm_prefetch((const char *)(s + i + MEMCPY_AVX2_PF_BYTES), _MM_HINT_T0);
            _mm_prefetch((const char *)(d + i + MEMCPY_AVX2_PF_BYTES), _MM_HINT_T0);

            __m256i v0 = _mm256_loadu_si256((const __m256i *)(s + i));
            __m256i v1 = _mm256_loadu_si256((const __m256i *)(s + i + 32));
            __m256i v2 = _mm256_loadu_si256((const __m256i *)(s + i + 64));
            __m256i v3 = _mm256_loadu_si256((const __m256i *)(s + i + 96));

            _mm256_storeu_si256((__m256i *)(d + i),      v0);
            _mm256_storeu_si256((__m256i *)(d + i + 32), v1);
            _mm256_storeu_si256((__m256i *)(d + i + 64), v2);
            _mm256_storeu_si256((__m256i *)(d + i + 96), v3);
        }

        /* remaining whole 32 B blocks */
        for (; i + 32 <= n; i += 32) {
            __m256i v = _mm256_loadu_si256((const __m256i *)(s + i));
            _mm256_storeu_si256((__m256i *)(d + i), v);
        }

        /* tail: 0 <= n - i < 32 */
        size_t r = n - i;
        if (r >= 16) {
            __m128i a = _mm_loadu_si128((const __m128i *)(s + i));
            __m128i b = _mm_loadu_si128((const __m128i *)(s + i + r - 16));
            _mm_storeu_si128((__m128i *)(d + i), a);
            _mm_storeu_si128((__m128i *)(d + i + r - 16), b);
        } else if (r >= 8) {
            uint64_t a, b;
            __builtin_memcpy(&a, s + i, 8);
            __builtin_memcpy(&b, s + i + r - 8, 8);
            __builtin_memcpy(d + i, &a, 8);
            __builtin_memcpy(d + i + r - 8, &b, 8);
        } else if (r >= 4) {
            uint32_t a, b;
            __builtin_memcpy(&a, s + i, 4);
            __builtin_memcpy(&b, s + i + r - 4, 4);
            __builtin_memcpy(d + i, &a, 4);
            __builtin_memcpy(d + i + r - 4, &b, 4);
        } else if (r >= 2) {
            uint16_t a, b;
            __builtin_memcpy(&a, s + i, 2);
            __builtin_memcpy(&b, s + i + r - 2, 2);
            __builtin_memcpy(d + i, &a, 2);
            __builtin_memcpy(d + i + r - 2, &b, 2);
        } else if (r == 1) {
            d[i] = s[i];
        }
        return dst;
    }

    /* ---- small-size fast path: never enters the AVX2 loop ----------------
     * 32 <= n < 64 needs FOUR 16 B accesses, not two: a pair covers [0,sz) and
     * [n-sz,n), which over the whole range n (here 32..63) is complete only from
     * sz = 32 upwards. The first version of this file used the 16 B pair for the
     * whole n >= 16 range and verify caught it at n=33: byte 16 was never
     * written (nothing covers [16, 32) when n-sz = 17). Two pairs - offsets 0 and
     * 16, then n-32 and n-16 - cover [0,32) and [n-32,n), which is all of [0,n)
     * for every n <= 64, and no access leaves [0,n).
     */
    if (n >= 32) {
        __m128i a = _mm_loadu_si128((const __m128i *)s);
        __m128i b = _mm_loadu_si128((const __m128i *)(s + 16));
        __m128i c = _mm_loadu_si128((const __m128i *)(s + n - 32));
        __m128i e = _mm_loadu_si128((const __m128i *)(s + n - 16));
        _mm_storeu_si128((__m128i *)d, a);
        _mm_storeu_si128((__m128i *)(d + 16), b);
        _mm_storeu_si128((__m128i *)(d + n - 32), c);
        _mm_storeu_si128((__m128i *)(d + n - 16), e);
    } else if (n >= 16) {
        __m128i a = _mm_loadu_si128((const __m128i *)s);
        __m128i b = _mm_loadu_si128((const __m128i *)(s + n - 16));
        _mm_storeu_si128((__m128i *)d, a);
        _mm_storeu_si128((__m128i *)(d + n - 16), b);
    } else if (n >= 8) {
        uint64_t a, b;
        __builtin_memcpy(&a, s, 8);
        __builtin_memcpy(&b, s + n - 8, 8);
        __builtin_memcpy(d, &a, 8);
        __builtin_memcpy(d + n - 8, &b, 8);
    } else if (n >= 4) {
        uint32_t a, b;
        __builtin_memcpy(&a, s, 4);
        __builtin_memcpy(&b, s + n - 4, 4);
        __builtin_memcpy(d, &a, 4);
        __builtin_memcpy(d + n - 4, &b, 4);
    } else if (n >= 2) {
        uint16_t a, b;
        __builtin_memcpy(&a, s, 2);
        __builtin_memcpy(&b, s + n - 2, 2);
        __builtin_memcpy(d, &a, 2);
        __builtin_memcpy(d + n - 2, &b, 2);
    } else if (n == 1) {
        d[0] = s[0];
    }
    /* n == 0 falls through here and touches nothing, as the contract requires */
    return dst;
}
