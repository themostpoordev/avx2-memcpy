/* SPDX-License-Identifier: MIT */
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
/* Optional: only prefetch at or above this size (V5, take 2). 0 = always. The
 * first V5 attempt used one distance per size class and lost at 4 MiB to a flat
 * +128 B (see the report); the screening data also showed prefetch HURTING at
 * 4 KB (41.9 ns without it, 47.6 ns with). So the knob that matters is not the
 * distance but whether to prefetch at all, and the answer is size-dependent. */
#ifndef TUNED_PF_MIN
#define TUNED_PF_MIN 0
#endif

/* Optional (V9b): take the n <= 64 classes from a SINGLE compare, before the
 * flat-class chain, and copy them with kernel 4's shape.
 *
 * Why it is a knob and not always on: with it off, this header compiles to the
 * exact bytes every rung of the final1 campaign was measured on, so the V9b rung
 * is one flag away from its control and the measured difference is that flag.
 *
 * The chain below is six compares deep at 16 B (512, 256, 128, 64, 32, 16). On a
 * copy that costs ~7 cycles end to end that is a real fraction, and the final1
 * campaign shows it: 3.1 ns at 16-64 B against 2.2 ns for the same moves reached
 * from a short chain (kernel 4) and 1.9 ns for the PGO build, which pushes the
 * same cold classes out of the hot path. The block skips the chain entirely for
 * n <= 64 and copies what the chain would have copied.
 *
 * Correctness is the argument used everywhere else in this file: each pair of
 * moves covers [0,sz) and [n-sz,n) with n-sz >= 0, so nothing is touched outside
 * [0,n); src and dst do not overlap; and `--verify` walks every n in 0..4096, so
 * a wrong edge in here cannot survive the gate. */
#ifndef TUNED_SMALL_PATH
#define TUNED_SMALL_PATH 0
#endif

/* Optional (F1): branch-layout hints on the two dispatch branches.
 *
 * The 128/256 B deficit of the small-path builds is NOT caused by these
 * branches - the alignment test is only reached at n > 512 and the tiny test is
 * only taken at n <= 64 - but F1 was asked for and has to be priced rather than
 * argued about. With this at 1 the tiny test is marked UNLIKELY (so the tiny
 * block is laid out off the hot fall-through) and the alignment test is marked
 * LIKELY, which is the arrangement the harness's 64 B-aligned buffers produce. */
#ifndef TUNED_EXPECT
#define TUNED_EXPECT 0
#endif

/* Optional (F4): a straight-line kernel for the 128..256 B window, reached by
 * ONE unsigned compare before the flat-class chain.
 *
 * This is the only one of the four F attempts that is aimed at the mechanism the
 * disassembly shows: at 128 B the small-path builds run the tiny test, the 512 B
 * test, two 32 B moves, a 256 B test and a jmp before finishing, where the
 * original fused kernel ran two tests and a straight-line block pair. A class
 * that is entered from one compare and then has no branches at all cannot pay
 * that dispatch.
 *
 * Correctness: the window is 128 <= n <= 256, and T_BLOCKPAIR(4) covers [0,128)
 * + [n-128,n) with n-128 >= 0, so nothing outside [0,n) is touched; n < 128 and
 * n > 256 fall through to the existing code. */
#ifndef TUNED_WINDOW
#define TUNED_WINDOW 0
#endif

/* Optional (Phase 7, rung-only): the window's LOWER edge. 128 is the shipped
 * value. 129 moves the edge up by one size, so n = 128 falls through to the
 * flat class chain and reaches the (64, 128] class's H = 64 block pair through
 * four extra compares. That is the H1a CONTROL rung; see
 * src/rungs/avx2_f4win129.c. It exists to price the compares, and it is never
 * a promotion candidate. */
#ifndef TUNED_WIN_LO
#define TUNED_WIN_LO 128
#endif

/* Optional (Phase 7, rung-only): inside the window, give n == 128 the H = 64
 * block pair, which tiles that size exactly (4 loads + 4 stores) instead of
 * the H = 128 pair, whose head and tail blocks are the same four blocks at
 * n = 128 (8 loads + 8 stores for a 128 B copy).
 *
 * Default 0: the shipped kernel is unchanged, and the shipped object's .text
 * md5 is unchanged with it. See src/rungs/avx2_f4win64.c for the measurement
 * and for the acceptance criteria that were fixed before the measurement ran. */
#ifndef TUNED_WIN64
#define TUNED_WIN64 0
#endif

#if TUNED_EXPECT
#define T_UNLIKELY(c) (UNLIKELY(c))
#define T_LIKELY(c)   (LIKELY(c))
#else
#define T_UNLIKELY(c) (c)
#define T_LIKELY(c)   (c)
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

/* A flat block pair: H = cnt*32 bytes at each end, [0,H) then [n-H,n).
 *
 * The class windows are (H, 2H], so the two blocks cover [0,n) because the
 * second one starts at n-H <= H. Choosing H as the class's LOWER bound is what
 * makes this cheap at the sizes that matter: at n = 2H the two blocks tile [0,n)
 * exactly, with no byte copied twice and no more moves than the copy needs. The
 * ladder sizes (32, 64, 128, 256, 512 B) are all 2H of their class, so they are
 * the zero-redundancy points.
 *
 * Two earlier versions of this block were wrong in opposite directions, which is
 * why the comment is long:
 *   - v1 used H = the class UPPER bound at both ends, i.e. it copied 2H bytes for
 *     every n in [H,2H): correct, but 2x the bytes at n = H. The screening
 *     campaign priced that at 64-512 B (+10%..+57% behind glibc) where the move
 *     count, not the bytes, sets the time.
 *   - v2 halved H but kept the window, leaving sizes above the midpoint with an
 *     uncovered gap (n = 300 -> [0,128) + [172,300) misses [128,172)).
 *     `--verify` failed on all 21 rung x target combinations immediately, which
 *     is the cheapest possible way to learn it: the check walks n = 0..4096 with
 *     random offsets, so a gap inside any class cannot hide.
 *   - v3 (this one) keeps the windows and restores H to the lower bound, so the
 *     coverage inequality n <= 2H holds by construction and the redundancy is
 *     zero at the top of each class. */
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

#if TUNED_SMALL_PATH
    /* ---- V9b: the tiny classes, reached in ONE compare ------------------- */
    if (T_UNLIKELY(n <= 64)) {
        if (n < 16) {
            if (n >= 8) {
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
        if (n < 32) { T_MOVE16(0); T_MOVE16(n - 16); return dst; }
        T_MOVE16(0); T_MOVE16(16); T_MOVE16(n - 32); T_MOVE16(n - 16);
        return dst;
    }
#endif

#if TUNED_WINDOW
    /* ---- F4: 128..256 B, one compare, then no branches at all ------------ */
    if ((size_t)(n - (size_t)TUNED_WIN_LO) <= (size_t)(256u - (size_t)TUNED_WIN_LO)) {
#if TUNED_WIN64
        /* Phase 7 rung H1b: at n == 128 the H = 128 block pair's head and tail
         * are the same four 32 B blocks, so the copy would issue 8 loads and
         * 8 stores for 128 bytes. The H = 64 pair tiles n = 128 exactly:
         * [0,64) + [n-64,n) = [0,128), 4 loads and 4 stores, no overlap. */
        if (n == 128u) {
            T_BLOCKPAIR(2);
            return dst;
        }
#endif
        T_BLOCKPAIR(4);
        return dst;
    }
#endif

    /* ---------------- flat classes: no loop, no tail branch -------------- */
    if (n <= 512) {
        if (n > 256)       { T_BLOCKPAIR(8); }          /* H = 256, exact at 512  */
        else if (n > 128)  { T_BLOCKPAIR(4); }          /* H = 128, exact at 256  */
        else if (n > 64)   { T_BLOCKPAIR(2); }          /* H =  64, exact at 128  */
#if TUNED_SMALL_AVX
        else if (n > 32)   { T_BLOCKPAIR(1); }          /* H =  32, exact at 64   */
#else
        else if (n > 32)   {                            /* 4 x 16 B (kernel 4)    */
            T_MOVE16(0); T_MOVE16(16); T_MOVE16(n - 32); T_MOVE16(n - 16);
        }
#endif
        else if (n > 16)   { T_MOVE16(0); T_MOVE16(n - 16); }   /* H = 16, exact at 32 */
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
    if (T_LIKELY((((uintptr_t)s | (uintptr_t)d) & 31u) == 0u)) {
        for (; i + TUNED_BLOCK <= n; i += TUNED_BLOCK) {
            if (pf && n >= (size_t)TUNED_PF_MIN) {
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
            if (pf && n >= (size_t)TUNED_PF_MIN) {
                _mm_prefetch((const char *)(s + i + pf), _MM_HINT_T0);
                _mm_prefetch((const char *)(d + i + pf), _MM_HINT_T0);
            }
            for (size_t k = 0; k < (size_t)TUNED_UNROLL; k++) {
                __m256i v = _mm256_loadu_si256((const __m256i *)(s + i + k * 32));
                _mm256_storeu_si256((__m256i *)(d + i + k * 32), v);
            }
        }
    }

    /* V8: the tail is one overlapping block, not a cascade. n - TUNED_BLOCK is
     * simply where the last full block starts, and the loop guaranteed
     * n - i < TUNED_BLOCK, so [n-BLOCK, n) covers [i, n).
     *
     * The `if (i < n)` is worth its branch: when the size is a whole number of
     * blocks (every power-of-two ladder size with unroll 4/8/16) the remainder is
     * zero and the overlap block would copy up to 512 B that the loop has already
     * copied - 25% of a 2 KB copy, 12% of a 4 KB copy. The branch is 100%
     * predicted in the iters loop (the size is constant per case), so it costs a
     * uop and saves a whole block of stores. */
    if (i < n)
        for (size_t k = 0; k < (size_t)TUNED_UNROLL; k++)
            T_MOVE32(n - TUNED_BLOCK + k * 32);

    return dst;
}
