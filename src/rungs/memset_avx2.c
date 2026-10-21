/* SPDX-License-Identifier: MIT */
/* src/memset_avx2.c — KERNEL 8, rung 4: 32 B stores, unrolled 4x (AVX2).
 *
 * The memset counterpart of memcpy kernel 4: broadcast the byte into one ymm
 * register and issue four 32 B stores per 128 B iteration (two cache lines), then
 * a 32 B remainder loop, then a sub-32 B tail made of overlapping 16/8/4/2/1 B
 * stores.
 *
 * Contract
 *   - Any alignment: `_mm256_storeu_si256` has no alignment requirement. There is
 *     no aligned store anywhere in this TU, so there is no alignment branch to
 *     mispredict and nothing that can fault on an odd pointer.
 *   - Overlapping tail writes are legal for a fill: every byte written is the
 *     same byte, so writing one twice is invisible. Each tail store lies inside
 *     [0, n) of dst (the second of a pair starts at n-sz >= 0), so the canaries
 *     are untouched.
 *   - No overrun: the 128 B loop needs i + 128 <= n; the 32 B loop needs
 *     i + 32 <= n; the tail covers exactly the < 32 B remainder with
 *     overlapping pairs, whose union is [i, n).
 *   - < 64 B takes the overlap tail directly (no loop), which is the same shape
 *     kernel 4 of the memcpy ladder uses below 64 B.
 *   - n == 0: all comparisons are false, nothing is written.
 */
#include "common.h"
#include <immintrin.h>

void *memset_avx2(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const __m256i v = _mm256_set1_epi8((char)(unsigned char)c);
    size_t i = 0;

    for (; i + 128 <= n; i += 128) {
        _mm256_storeu_si256((__m256i *)(void *)(d + i),      v);
        _mm256_storeu_si256((__m256i *)(void *)(d + i + 32), v);
        _mm256_storeu_si256((__m256i *)(void *)(d + i + 64), v);
        _mm256_storeu_si256((__m256i *)(void *)(d + i + 96), v);
    }
    for (; i + 32 <= n; i += 32)
        _mm256_storeu_si256((__m256i *)(void *)(d + i), v);

    size_t r = n - i;                      /* 0 <= r < 32 */
    if (r >= 16) {
        _mm_storeu_si128((__m128i *)(void *)(d + i),      _mm256_castsi256_si128(v));
        _mm_storeu_si128((__m128i *)(void *)(d + n - 16), _mm256_castsi256_si128(v));
    } else if (r >= 8) {
        uint64_t w = (uint64_t)(unsigned char)c * 0x0101010101010101ull;
        __builtin_memcpy(d + i, &w, 8);
        __builtin_memcpy(d + n - 8, &w, 8);
    } else if (r >= 4) {
        uint32_t w = (uint32_t)(unsigned char)c * 0x01010101u;
        __builtin_memcpy(d + i, &w, 4);
        __builtin_memcpy(d + n - 4, &w, 4);
    } else if (r >= 2) {
        uint16_t w = (uint16_t)((unsigned char)c * 0x0101u);
        __builtin_memcpy(d + i, &w, 2);
        __builtin_memcpy(d + n - 2, &w, 2);
    } else if (r == 1) {
        d[i] = (unsigned char)c;
    }

    _mm256_zeroupper();
    return dst;
}
