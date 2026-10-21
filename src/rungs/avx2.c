/* SPDX-License-Identifier: MIT */
/* src/memcpy_avx2.c — KERNEL 4 of the ladder: 32 B AVX2 blocks, unrolled 4x.
 *
 * Rung 3 copies 16 B per access, rung 4 copies 32 B, and the only intended
 * difference between the two rungs is that width (plus the prefetch pair, which
 * this variant places at +1 cache line). The body lives in
 * src/memcpy_avx2_impl.h because the prefetch-distance variants pf2 and pf4 are
 * built from the same text; anything that shows up as a difference between
 * rung 3 and rung 4 in the codegen diff is therefore width, unroll or prefetch,
 * not a rewrite.
 *
 * This variant is the primary rung: it is the one compared against sse2, the
 * one whose diff is saved to results/codegen_diff_avx2_*.diff, and the one
 * snapshotted as results/committed/phase2_avx2_*.csv. pf2/pf4 are reported as
 * a prefetch-distance comparison, not as separate rungs.
 */
#define MEMCPY_AVX2_FN       memcpy_avx2
#define MEMCPY_AVX2_PF_LINES 1
#include "avx2_impl.h"
