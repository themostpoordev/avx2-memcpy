/* src/memcpy_avx2_tuned_impl.h — Phase 2.6 tuned AVX2 kernel body (V3-V9).
 *
 * One body, several rungs, so a difference between two measured rungs is a
 * difference in exactly the knob that changed. Knobs (all must be defined by the
 * wrapper before including this file):
 *
 *   TUNED_FN            the rung symbol
 *   TUNED_UNROLL        32 B blocks per main-loop iteration: 4, 6, 8 or 12
 *   TUNED_ALIGNED       1 = add a 32 B-aligned store/load main loop (V3), used when
 *                       BOTH pointers are 32 B aligned; 0 = unaligned only
 *   TUNED_PF_BYTES      fixed prefetch distance in bytes, 0 = no prefetch
 *   TUNED_PF_CLASS      1 = ignore TUNED_PF_BYTES and pick the distance from the
 *                       size class (V5): <4K +128, 4K-64K +256, 64K-1M +512, >1M +1024
 *   TUNED_SMALL_AVX     1 = the 32..63 B flat path uses two overlapping 32 B AVX2
 *                       moves (glibc's shape); 0 = four 16 B SSE moves (kernel 4's)
 *
 * Shape, and why it is this shape
 *   Every size class is a STRAIGHT-LINE block pair with no loop and no tail
 *   branch: copy [0, cls) and then copy the SAME number of bytes ending at n
 *   (i.e. [n-cls, n)). For n <= 2*cls those two blocks cover [0,n) and never
 *   write outside it; bytes in between may be written twice, which is legal
 *   here because src and dst do not overlap (the ladder's contract) - the same
 *   reason kernel 4's 16 B tail pair is legal. The second block is what removes
 *   the tail: there is no remainder to branch on (V8).
 *   Above the top class (512 B) the main loop advances in whole TUNED_UNROLL*32 B
 *   blocks, and the leftover (always < TUNED_UNROLL*32 B) is covered by copying
 *   the LAST full block again - one overlapping block, still no scalar tail.
 *
 * Correctness argument for the overlap tail (the bound that matters)
 *   The loop leaves i = n - r with 0 <= r < B, B = TUNED_UNROLL*32. The overlap
 *   block writes [n-B, n). It covers [i, n) iff n-B <= i, i.e. B >= r, which holds
 *   because r < B. And it writes nothing below 0 or above n-1. So the whole copy
 *   is inside [0,n) for every n >= B, which is the condition for entering the
 *   loop. bench.c's 64 B canaries on both sides are therefore untouched; verify
 *   probes n = 0..4096 with random offsets, so every class boundary and every
 *   overlap case is exercised, not argued.
 *
 * Zero-length and tiny n
 *   n == 0 touches nothing. Below 16 B the chain is the same 8/4/2/1 B
 *   overlapping pairs kernel 4 uses, which is the cheapest thing that is
 *   correct for every n in the class.
 */
#ifndef TUNED_FN
#error "define TUNED_FN"
#endif
#ifndef TUNED_UNROLL
#error "define TUNED_UNROLL"
#endif
#ifndef TUNED_ALIGNED
#error "define TUNED_ALIGNED"
#endif
#ifndef TUNED_PF_BYTES
#error "define TUNED_PF_BYTES"
#endif
#ifndef TUNED_PF_CLASS
#error "define TUNED_PF_CLASS"
#endif
#ifndef TUNED_SMALL_AVX
#error "define TUNED_SMALL_AVX"
#endif

#include <immintrin.h>
#include <stdint.h>
#include "common.h"

#define TUNED_BLOCK ((size_t)TUNED_UNROLL * 32u)

/* 32 B at src+off -> dst+off (unaligned) */
#define T_MOVE32(off)                                                        \
    do {                                                                     \
        __m256i v_ = _mm256_loadu_si256((const __m256i *)(s + (off)));        \
        _mm256_storeu_si256((__m256i *)(d + (off)), v_);                      \
    } while (0)

/* 16 B at src+off -> dst+off */
#define T_MOVE16(off)                                                        \
    do {                                                                     \
        __m128i v_ = _mm_loadu_si128((const __m128i *)(s + (off)));           \
        _mm_storeu_si128((__m128i *)(d + (off)), v_);                         \
    } while (0)

/* a flat block pair: [0,cnt*32) then [n-cnt*32,n) */
#define T_BLOCKPAIR(cnt)                                                     \
    do {                                                                     \
        for (size_t k_ = 0; k_ < (size_t)(cnt); k_++)                        \
            T_MOVE32(k_ * 32);                                               \
        for (size_t k_ = 0; k_ < (size_t)(cnt); k_++)                        \
            T_MOVE32(n - (size_t)(cnt) * 32 + k_ * 32);                      \
    } while (0)

void *TUNED_FN(void *dst, const void *src, size_t n)
{
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    /* ---------------- flat classes: no loop, no tail branch -------------- */
    if (n < 512) {
        if (n >= 256)      { T_BLOCKPAIR(8); }
        else if (n >= 128) { T_BLOCKPAIR(4); }
        else if (n >= 64)  { T_BLOCKPAIR(2); }
#if TUNED_SMALL_AVX
        else if (n >= 32)  { T_BLOCKPAIR(1); }          /* 2 x 32 B overlapping  */
#else
        else if (n >= 32)  {                            /* 4 x 16 B (kernel 4)    */
            T_MOVE16(0); T_MOVE16(16); T_MOVE16(n - 32); T_MOVE16(n - 16);
        }
#endif
        else if (n >= 16)  { T_MOVE16(0); T_MOVE16(n - 16); }
        else if (n >= 8)   {
            uint64_t a, b;
            __builtin_memcpy(&a, s, 8); __builtin_memcpy(&b, s + n - 8, 8);
            __builtin_memcpy(d, &a, 8); __builtin_memcpy(d + n - 8, &b, 8);
        } else if (n >= 4) {
            uint32_t a, b;
            __builtin_memcpy(&a, s, 4); __builtin_memcpy(&b, s + n - 4, 4);
            __builtin_memcpy(d, &a, 4); __builtin_memcpy(d + n - 4, &b, 4);
        } else if (n >= 2) {
            uint16_t a, b;
            __builtin_memcpy(&a, s, 2); __builtin_memcpy(&b, s + n - 2, 2);
            __builtin_memcpy(d, &a, 2); __builtin_memcpy(d + n - 2, &b, 2);
        } else if (n == 1) {
            d[0] = s[0];
        }
        return dst;
    }

    /* ---------------- main loop: 512 B and up ---------------------------- */
    size_t i = 0;

#if TUNED_PF_CLASS
    /* V5: one distance per size class, chosen once, before the loop. The
     * prefetch stream is what keeps a store-bound loop from stalling on L2. */
    size_t pf;
    if (n < 4096)        pf = 128;
    else if (n < 65536)  pf = 256;
    else if (n < 1048576) pf = 512;
    else                 pf = 1024;
#else
    size_t pf = (size_t)TUNED_PF_BYTES;
#endif

#if TUNED_ALIGNED
    /* V3: one test of the low bits of both pointers. The harness allocates
     * 64 B-aligned buffers, so this branch is 100% predicted in practice; it is
     * still a real runtime check, not an assumption, and the unaligned fallback
     * is the same loop with unaligned accesses. */
    if ((((uintptr_t)s | (uintptr_t)d) & 31u) == 0u) {
        for (; i + TUNED_BLOCK <= n; i += TUNED_BLOCK) {
            if (pf) {
                _mm_prefetch((const char *)(s + i + pf), _MM_HINT_T0);
                _mm_prefetch((const char *)(d + i + pf), _MM_HINT_T0);
            }
            for (size_t k = 0; k < (size_t)TUNED_UNROLL; k++) {
                __m256i v = _mm256_load_si256((const __m256i *)(s + i + k * 32));
                _mm256_store_si256((__m256i *)(d + i + k * 32), v);
            }
        }
    } else
#endif
    {
        for (; i + TUNED_BLOCK <= n; i += TUNED_BLOCK) {
            if (pf) {
                _mm_prefetch((const char *)(s + i + pf), _MM_HINT_T0);
                _mm_prefetch((const char *)(d + i + pf), _MM_HINT_T0);
            }
            for (size_t k = 0; k < (size_t)TUNED_UNROLL; k++) {
                __m256i v = _mm256_loadu_si256((const __m256i *)(s + i + k * 32));
                _mm256_storeu_si256((__m256i *)(d + i + k * 32), v);
            }
        }
    }

    /* V8: the tail is one overlapping block, not a cascade. n - TUNED_BLOCK is a
     * multiple of nothing in particular - it is simply where the last full block
     * starts, and the loop guaranteed n - i < TUNED_BLOCK, so [n-BLOCK, n)
     * covers [i, n). */
    for (size_t k = 0; k < (size_t)TUNED_UNROLL; k++)
        T_MOVE32(n - TUNED_BLOCK + k * 32);

    return dst;
}
