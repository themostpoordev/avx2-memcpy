/* src/memcpy_sse2.c — KERNEL 3 of the ladder: 16B blocks, unrolled 4x (SSE2).
 *
 * Rung 2 moved one 64-bit word per iteration. This rung moves 16 bytes per
 * instruction via _mm_loadu_si128/_mm_storeu_si128 and unrolls the hot loop 4x so
 * one iteration covers a full 64-byte Zen cache line. SSE2 is the first rung
 * whose width depends on the register file rather than the GPRs, and it is
 * baseline x86-64, so no -mavx/-msse flag gates it: it compiles everywhere this
 * project builds and, unlike the AVX rungs that follow, carries no
 * frequency/power side effects on the core.
 *
 * Contract and assumptions
 *   - Non-overlapping src/dst, as in rungs 1 and 2.
 *   - Any alignment. `loadu`/`storeu` are the unaligned forms; both accept a
 *     completely unaligned address by definition, so there is no alignment
 *     precondition and no aligned-store `movdqa` fault to avoid. The cast to
 *     `__m128i *` is the sanctioned intrinsic interface: clang defines `__m128i`
 *     with `may_alias`, and the loads/stores are compile-time single instructions
 *     that cannot be reordered into a strict-aliasing violation. No byte of the
 *     source or destination is ever accessed as any other C type.
 *   - No overrun: the 64-byte loop requires `i + 64 <= n` and the 16-byte loop
 *     requires `i + 16 <= n`, so every 16-byte store lands fully inside dst,
 *     which is 16-byte granular by construction. The scalar tail writes exactly
 *     the remaining 0-15 bytes. Nothing is written past dst[n-1].
 *   - n == 0 touches nothing: the < 32 fast path does not execute for n == 0.
 *
 * Small-size rule (< 32 bytes must not enter the SSE loop)
 *   n < 32 takes the byte path. Rationale: one 16-byte load/store pair for a
 *   16-byte copy is not obviously a win over 16 byte-pairs once the
 *   branch-to-vector-path and the register transfer are counted, and for n < 16
 *   the vector loop cannot complete a single iteration at all. Whether 32 is the
 *   right crossover is measured, not assumed: kernel 3 is benchmarked against
 *   rung 2 (word) and rung 1 (naive) at 16/32/64/128 B with the band-aware floor
 *   applied, and the crossover is reported as measured.
 *
 * Every load and store, justified:
 *   - four 16B `loadu` per 64B block  : one per 16B lane of the block; issued as
 *                                       four independent loads so the core can
 *                                       keep two 16B loads in flight per cycle.
 *   - four 16B `storeu` per 64B block : one per lane, in the same order as the
 *                                       loads, so stores retire in address order.
 *   - one 16B load/store pair per remaining 16B : handles 16-63 byte remainders
 *                                       without falling straight to bytes.
 *   - byte pairs for the last 0-15 bytes : the only form that cannot overrun.
 *
 * Known loss, stated up front: there is still no head-alignment prologue (the
 * destination is written wherever it happens to point) and the unroll is a
 * straight 4x with no software prefetch. Both are deliberate for this rung;
 * `_mm256` width and prefetch arrive with rung 4.
 */
#include "common.h"
#include <emmintrin.h>

void *memcpy_sse2(void *dst, const void *src, size_t n)
{
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    size_t i = 0;

    /* ---- fast path: < 32 bytes stays on byte copies ---------------------- */
    if (n < 32) {
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
        for (; i < n; i++)
            d[i] = s[i];
        return dst;
    }

    /* ---- 64-byte blocks: 4 x 16B, one cache line per iteration ----------- */
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
    for (; i + 64 <= n; i += 64) {
        __m128i v0 = _mm_loadu_si128((const __m128i *)(const void *)(s + i));
        __m128i v1 = _mm_loadu_si128((const __m128i *)(const void *)(s + i + 16));
        __m128i v2 = _mm_loadu_si128((const __m128i *)(const void *)(s + i + 32));
        __m128i v3 = _mm_loadu_si128((const __m128i *)(const void *)(s + i + 48));
        _mm_storeu_si128((__m128i *)(void *)(d + i),      v0);
        _mm_storeu_si128((__m128i *)(void *)(d + i + 16), v1);
        _mm_storeu_si128((__m128i *)(void *)(d + i + 32), v2);
        _mm_storeu_si128((__m128i *)(void *)(d + i + 48), v3);
    }

    /* ---- remaining whole 16B blocks -------------------------------------- */
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
    for (; i + 16 <= n; i += 16) {
        __m128i v = _mm_loadu_si128((const __m128i *)(const void *)(s + i));
        _mm_storeu_si128((__m128i *)(void *)(d + i), v);
    }

    /* ---- scalar tail: 0-15 bytes, cannot overrun ------------------------ */
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
    for (; i < n; i++)
        d[i] = s[i];

    return dst;
}
