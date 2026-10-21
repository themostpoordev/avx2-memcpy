/* src/memset_word.c — KERNEL 8, rung 2: 8-byte stores + byte tail.
 *
 * One 64-bit store per iteration, obtained by broadcasting the byte into a
 * uint64_t once and storing that word. No SIMD: this file is in SCALAR_SRCS.
 *
 * Contract
 *   - Any alignment. The word store is issued through __builtin_memcpy, which
 *     compiles to a plain unaligned MOV on this silicon; a cast to uint64_t*
 *     would be both an aliasing and an alignment violation in C11.
 *   - No overrun: the word loop runs only while i + 8 <= n, and the byte tail
 *     writes exactly the remainder. Nothing outside [dst, dst+n) is written.
 *   - < 32 B stays on the byte path, matching the ladder's small-size rule. The
 *     crossover is measured (this rung is benchmarked against rung 1 at 16 B and
 *     against glibc at every size), not assumed.
 *   - n == 0 touches nothing.
 *   - The fill value is the byte, not the int: v is truncated once, before the
 *     broadcast, so the 0x00/0xFF/random-byte rows are all exact.
 */
#include "common.h"

void *memset_word(void *dst, const int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    unsigned char  b = (unsigned char)c;
    size_t i = 0;

    if (n < 32) {
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
        for (; i < n; i++)
            d[i] = b;
        return dst;
    }

    uint64_t w = (uint64_t)b * 0x0101010101010101ull;   /* byte broadcast */
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
    for (; i + 8 <= n; i += 8)
        __builtin_memcpy(d + i, &w, 8);

#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
    for (; i < n; i++)
        d[i] = b;

    return dst;
}
