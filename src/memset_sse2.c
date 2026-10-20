/* src/memset_sse2.c — KERNEL 8, rung 3: 16 B stores, unrolled 4x (SSE2).
 *
 * 16 bytes per store via _mm_set1_epi8 broadcast, four stores per 64 B cache
 * line. Built with -mno-avx -mno-avx2 (SSE2_SRCS) so the rung really is 128-bit
 * wide: with AVX2 enabled clang merges adjacent 16 B stores into 32 B ymm stores
 * and this rung would silently become rung 4 - the same defect kernel 3 of the
 * memcpy ladder had, caught the same way (make audit + audit-width).
 *
 * Contract
 *   - Any alignment: storeu has no alignment requirement, so no prologue and no
 *     aligned-store fault path is needed.
 *   - No overrun: the 64 B loop needs i + 64 <= n, the 16 B loop needs
 *     i + 16 <= n, and the byte tail writes exactly the remainder.
 *   - < 32 B stays on the byte path (ladder rule); the crossover is measured.
 *   - n == 0 touches nothing.
 */
#include "common.h"
#include <emmintrin.h>

void *memset_sse2(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    size_t i = 0;

    if (n < 32) {
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
        for (; i < n; i++)
            d[i] = (unsigned char)c;
        return dst;
    }

    const __m128i v = _mm_set1_epi8((char)(unsigned char)c);

#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
    for (; i + 64 <= n; i += 64) {
        _mm_storeu_si128((__m128i *)(void *)(d + i),      v);
        _mm_storeu_si128((__m128i *)(void *)(d + i + 16), v);
        _mm_storeu_si128((__m128i *)(void *)(d + i + 32), v);
        _mm_storeu_si128((__m128i *)(void *)(d + i + 48), v);
    }
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
    for (; i + 16 <= n; i += 16)
        _mm_storeu_si128((__m128i *)(void *)(d + i), v);

#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
    for (; i < n; i++)
        d[i] = (unsigned char)c;

    return dst;
}
