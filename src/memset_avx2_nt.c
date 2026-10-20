/* src/memset_avx2_nt.c — KERNEL 8, rung 5: non-temporal stores above a gate.
 *
 * A fill never reads the destination, so it is the one operation where NT stores
 * cost nothing in extra traffic: the write-allocate reads that a normal store
 * triggers are pure waste, and `vmovntdq` skips them. That is the opposite of the
 * memcpy NT rung, where the source still has to be read.
 *
 * Alignment is now a real precondition, not a preference: `vmovntdq` faults on a
 * destination that is not 32 B aligned. So this rung aligns dst explicitly:
 *   1. below NT_MIN                       -> memset_avx2's body (inlined shape)
 *   2. head: unaligned 32 B stores until (dst+i) is 32 B aligned
 *   3. body: 4 x 32 B vmovntdq per 128 B
 *   4. tail: ordinary overlapping stores back in cached memory
 *   5. one sfence at the end, so the NT stores are ordered before the call returns
 *      (the harness may read the buffer immediately afterwards; without the fence
 *      the stores are only guaranteed visible "eventually").
 *
 * Contract
 *   - No overrun anywhere: step 2 writes only while i + 32 <= n, step 3 needs
 *     i + 128 <= n, and step 4 covers [i, n) with overlapping pairs.
 *   - n == 0 touches nothing, and the sfence path is not reached.
 *   - The gate NT_MIN is 1 MiB, above the measured L3 (8 MiB is shared, so a
 *     1 MiB buffer is already streaming past the private caches but still inside
 *     the shared one). It is a knob the campaign reports the number for, not a
 *     tuned value: the rung exists to answer whether NT helps a fill at all here.
 */
#include "common.h"
#include <immintrin.h>

#define NT_MIN (1024u * 1024u)

void *memset_avx2_nt(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const __m256i v = _mm256_set1_epi8((char)(unsigned char)c);
    size_t i = 0;

    if (n < NT_MIN) {
        /* Same shape as memset_avx2: cached stores for anything that fits. */
        for (; i + 128 <= n; i += 128) {
            _mm256_storeu_si256((__m256i *)(void *)(d + i),      v);
            _mm256_storeu_si256((__m256i *)(void *)(d + i + 32), v);
            _mm256_storeu_si256((__m256i *)(void *)(d + i + 64), v);
            _mm256_storeu_si256((__m256i *)(void *)(d + i + 96), v);
        }
        for (; i + 32 <= n; i += 32)
            _mm256_storeu_si256((__m256i *)(void *)(d + i), v);
    } else {
        /* ---- align dst to 32 B with ordinary stores ---------------------- */
        for (; i + 32 <= n && (((uintptr_t)(d + i)) & 31u) != 0u; i += 32)
            _mm256_storeu_si256((__m256i *)(void *)(d + i), v);

        /* ---- NT body ----------------------------------------------------- */
        for (; i + 128 <= n; i += 128) {
            _mm256_stream_si256((__m256i *)(void *)(d + i),      v);
            _mm256_stream_si256((__m256i *)(void *)(d + i + 32), v);
            _mm256_stream_si256((__m256i *)(void *)(d + i + 64), v);
            _mm256_stream_si256((__m256i *)(void *)(d + i + 96), v);
        }
        for (; i + 32 <= n; i += 32)
            _mm256_stream_si256((__m256i *)(void *)(d + i), v);

        _mm_sfence();
    }

    /* ---- cached tail: 0..31 bytes, overlapping pairs -------------------- */
    size_t r = n - i;
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
