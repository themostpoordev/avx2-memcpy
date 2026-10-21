/* SPDX-License-Identifier: MIT */
/* src/memcpy_avx2_mid_impl.h — Phase 3 mid-range kernel (1 KiB .. 64 KiB).
 *
 * WHY A SEPARATE KERNEL
 *   The Phase 2.6 deliverable is level with glibc at 1 KiB-64 KiB (1 KiB -1.41%,
 *   4 KiB 0.00%, 16 KiB -1.19/-2.28%, 64 KiB 0.00/-0.08%) while winning at 16-512 B
 *   and at 1 MiB. The mid range is the last untapped band, and it is untapped for a
 *   structural reason: the deliverable's main loop is one loop serving 512 B to
 *   4 MiB, with the prefetch gated OFF below 64 KB and a 128 B (unroll 4) body.
 *   At 1-64 KiB the data is L1/L2-resident, so the limit is not memory latency but
 *   the number of instructions and split-line stores per byte. This kernel is that
 *   loop specialised: 256 B per iteration, prefetch on, and a head peel that puts
 *   every 32 B store on a 64 B boundary.
 *
 * KNOBS (defined by the wrapper, one per rung, so a measured delta is that knob)
 *   MID_FN      the rung symbol
 *   MID_UNROLL  32 B blocks per main-loop iteration: 4 or 8 (256 B at 8)
 *   MID_PF1     prefetch distance for both streams, in bytes from the block start
 *   MID_PF2     distance of the SECOND source stream, 0 = one stream only
 *   MID_PEEL    1 = peel dst to a 64 B boundary before the main loop
 *   MID_NTPF    1 = prefetchnta on the source when n >= MID_NT_SIZE (Phase 3 P3)
 *   MID_NT_SIZE threshold for the NTA source prefetch, default 256 KiB
 *
 * PREFETCH DISTANCES ARE IN BLOCKS, NOT IN LINES - deliberately
 *   The brief for P1/P2 says "+2 and +4 cache lines". With a 256 B block those
 *   two distances fall INSIDE the block the loop is already loading, so as
 *   literally specified they would prefetch bytes the very next instructions
 *   read: a uop with no possible effect. The two-stream idea is kept and the
 *   distances are expressed as one and two BLOCKS ahead (MID_PF1 = BLOCK,
 *   MID_PF2 = 2*BLOCK), which for unroll 4 is the brief's +2/+4 lines and for
 *   unroll 8 is +4/+8. The literal +128/+256 pair on the unroll-8 body is rung
 *   `mid_pfnear`, so the question is measured rather than argued about either way.
 *
 * SHAPE
 *   n < 1024 ......... the deliverable's own body, inlined (see below)
 *   n >= 1024 ........ head peel to a 64 B dst boundary, 256 B/iter loop,
 *                      one overlapping 256 B block as the tail
 *
 *   The below-1 KiB path is not reinvented: this header includes
 *   memcpy_avx2_tuned_impl.h with the F4 deliverable's exact knob set under an
 *   internal static-inline name, so sizes 0..1023 are the certified 16-512 B
 *   code and any delta this rung shows at those sizes is dispatch/code layout,
 *   not a different algorithm. That is also what keeps the 512 B win (Tier A,
 *   +16.8%) out of the blast radius of a mid-range experiment.
 *
 * HEAD PEEL, AND WHY IT IS THE P4 ANSWER
 *   dst is advanced to its next 64 B boundary with a bounded overlapping copy of
 *   r = (-dst) & 63 bytes, r in [0,63]. Every 32 B store in the main loop then
 *   starts on a 64 B boundary, so no store straddles a cache line: on Zen2 a
 *   split-line store occupies the store port for two cycles and the port is this
 *   loop's ceiling (1x32 B/cycle). The peel is NOT an alignment dispatch: there
 *   is one loop, not an aligned and an unaligned copy of it, and no branch picks
 *   between them - which is the F2/F3 lesson from the 128/256 B pass, where
 *   moving the alignment decision around cost more than it saved. Rung
 *   `mid_nopeel` is the same loop without the peel, so the peel's value at
 *   64 B-aligned harness buffers is measured, not assumed.
 *
 * CORRECTNESS (the bounds that matter)
 *   - Peel: r in [0,63]; the r bytes are copied by an overlapping pair whose both
 *     ends lie inside [0,r), so nothing below dst or above dst+r-1 is written.
 *     The main loop then starts at dst+r and copies m = n-r bytes.
 *   - Loop: j advances by BLOCK while j+BLOCK <= m, so every store is inside
 *     [r, r+m) = [r, n).
 *   - Tail: one block at offset r + m - BLOCK, which needs m >= BLOCK. The mid
 *     path is only entered for n >= 1024 and BLOCK <= 256, so m = n-r >= 961.
 *     It covers [r+j, n) because the loop left m-j < BLOCK, i.e. m-BLOCK <= j.
 *     Its bytes are already correct in dst; rewriting them from src is legal
 *     because src and dst do not overlap (the ladder's contract) and they are
 *     copied from the same offsets.
 *   - n == 0 and every n < 1024 never reach this code at all.
 *   `--verify` walks n = 0..4096 at random src/dst offsets, so the peel, the
 *   block boundary and the overlap tail are all exercised before any timing row.
 *
 * WHY UNALIGNED MOVES EVEN THOUGH THE PEEL ALIGNS dst
 *   Only dst is peeled; src keeps whatever alignment the caller gave it, and the
 *   harness calls with random relative offsets (so the two can never both be
 *   assumed aligned). vmovdqu on an aligned address is the same instruction cost
 *   as vmovdqa on Zen2, so there is nothing to win by selecting encodings and one
 *   precondition fewer to get wrong.
 */
#ifndef MID_FN
#error "define MID_FN"
#endif
#ifndef MID_UNROLL
#error "define MID_UNROLL"
#endif
#ifndef MID_PF1
#error "define MID_PF1"
#endif
#ifndef MID_PF2
#error "define MID_PF2"
#endif
#ifndef MID_PEEL
#error "define MID_PEEL"
#endif
#ifndef MID_NTPF
#define MID_NTPF 0
#endif
#ifndef MID_NT_SIZE
#define MID_NT_SIZE 262144
#endif
/* Size below which the head peel is skipped (0 = always peel).
 *
 * The peel is only worth its cost when the loop runs long enough for the store
 * lines to matter; at 1 KiB the measurement says it is not (see the p3mid
 * campaign: the peeled rung is 4.22% behind glibc at 1 KiB while the unpeeled one
 * is 1.41% ahead, on all three targets). This knob is that finding turned into a
 * threshold, and the rung that uses it declares the size it starts at. */
#ifndef MID_PEEL_MIN
#define MID_PEEL_MIN 0
#endif

#include <immintrin.h>
#include <stdint.h>
#include "common.h"

/* ---- the below-1 KiB body: the Phase 2.6 deliverable, unchanged ----------- */
/* A prior `static inline` declaration gives the including body internal linkage,
 * so the tuned impl's out-of-line definition becomes an inlined one and no extra
 * symbol reaches the binary (checked with nm before this rung was timed). */
static inline void *mid_small(void *dst, const void *src, size_t n);
#define TUNED_FN         mid_small
#define TUNED_UNROLL     4
#define TUNED_ALIGNED    1
#define TUNED_PF_BYTES   128
#define TUNED_PF_CLASS   0
#define TUNED_SMALL_AVX  0
#define TUNED_PF_MIN     65536
#define TUNED_SMALL_PATH 1
#define TUNED_WINDOW     1
#include "fast_impl.h"

#define MID_BLOCK ((size_t)MID_UNROLL * 32u)

/* 32 B at src+off -> dst+off */
#define M_MOVE32(off)                                                        \
    do {                                                                     \
        __m256i mv_ = _mm256_loadu_si256((const __m256i *)(s + (off)));       \
        _mm256_storeu_si256((__m256i *)(d + (off)), mv_);                     \
    } while (0)

void *MID_FN(void *dst, const void *src, size_t n)
{
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    /* ---- below the range under attack: the certified small-size code ------ */
    if (n < 1024)
        return mid_small(dst, src, n);

    /* ---- head peel: bounded, never leaves [0,r) --------------------------- */
    size_t r = 0;
#if MID_PEEL
    /* MID_PEEL_MIN is a compile-time threshold, so a rung that always peels must
     * not carry a `n >= 0` test: it would be dead code and a -Wtype-limits
     * warning on a TU whose whole point is that its instruction stream is what
     * the neighbour's is not. */
#if MID_PEEL_MIN > 0
    if (n >= (size_t)MID_PEEL_MIN)
#endif
        r = (size_t)(0u - (uintptr_t)d) & 63u;
    if (r >= 32) {
        M_MOVE32(0);
        M_MOVE32(r - 32);
    } else if (r >= 16) {
        __m128i v0 = _mm_loadu_si128((const __m128i *)(const void *)(s));
        __m128i v1 = _mm_loadu_si128((const __m128i *)(const void *)(s + r - 16));
        _mm_storeu_si128((__m128i *)(void *)(d), v0);
        _mm_storeu_si128((__m128i *)(void *)(d + r - 16), v1);
    } else if (r >= 8) {
        uint64_t a, b;
        __builtin_memcpy(&a, s, 8);
        __builtin_memcpy(&b, s + r - 8, 8);
        __builtin_memcpy(d, &a, 8);
        __builtin_memcpy(d + r - 8, &b, 8);
    } else if (r) {
        for (size_t k = 0; k < r; k++)
            d[k] = s[k];
    }
#endif

    /* ---- main loop: MID_UNROLL x 32 B per iteration ----------------------- */
    const size_t m = n - r;
    size_t       j = 0;

#if MID_NTPF
    /* P3: above 256 KiB the source is a stream that will not be reused, so NTA
     * asks for it in L1 without polluting L2/L3. Below it, T0 - a stream that
     * does fit in L2 is worth keeping there. The destination stream stays T0 in
     * both cases: the earlier NT-STORE rung lost badly on this host, and this
     * knob is only about the read side. */
    if (n >= (size_t)MID_NT_SIZE) {
        for (; j + MID_BLOCK <= m; j += MID_BLOCK) {
            _mm_prefetch((const char *)(s + j + MID_PF1), _MM_HINT_NTA);
            _mm_prefetch((const char *)(d + j + MID_PF1), _MM_HINT_T0);
#if MID_PF2
            /* The second source stream is MID_PF2 = 2*BLOCK ahead of the block
             * start, so at the last iteration it points up to MID_BLOCK bytes
             * past the end of the copy. A prefetch of an address outside the
             * buffer is harmless on x86, but forming a pointer more than one
             * past the end of the object is undefined behaviour in C, so the
             * offset is clamped to the copy length. The clamp is a compare
             * against a value already live in the loop condition, and the
             * branch is perfectly predicted. */
            size_t pf2 = j + MID_PF2;
            if (pf2 < m)
                _mm_prefetch((const char *)(s + pf2), _MM_HINT_NTA);
#endif
            for (size_t k = 0; k < (size_t)MID_UNROLL; k++)
                M_MOVE32(r + j + k * 32);
        }
    } else
#endif
    {
        for (; j + MID_BLOCK <= m; j += MID_BLOCK) {
            _mm_prefetch((const char *)(s + j + MID_PF1), _MM_HINT_T0);
            _mm_prefetch((const char *)(d + j + MID_PF1), _MM_HINT_T0);
#if MID_PF2
            /* Clamped for the same reason as the NTA branch above. */
            size_t pf2 = j + MID_PF2;
            if (pf2 < m)
                _mm_prefetch((const char *)(s + pf2), _MM_HINT_T0);
#endif
            for (size_t k = 0; k < (size_t)MID_UNROLL; k++)
                M_MOVE32(r + j + k * 32);
        }
    }

    /* ---- tail: the last whole block again, never a scalar cascade -------- */
    /* m >= MID_BLOCK is guaranteed by n >= 1024 and MID_UNROLL <= 8, which is the
     * condition for both offsets below to lie inside [0,n). */
    if (j < m)
        for (size_t k = 0; k < (size_t)MID_UNROLL; k++)
            M_MOVE32(n - MID_BLOCK + k * 32);

    return dst;
}
