/* SPDX-License-Identifier: MIT */
/* src/rungs/pf_ctl2.c — Phase 8.3 attempt 2 CONTROL: the coverage rung's
 * loop SHAPE, without the extra coverage.
 *
 * This is the same code as src/rungs/pf_cov2.c — same band, same instructions,
 * same register pressure — but the second prefetch pair is at +128, i.e. a
 * DUPLICATE of the first pair. The hardware drops a prefetch to a line it is
 * already fetching, so the extra instruction costs a slot and nothing else:
 * this rung has pf_cov2's shape and pf_cov2's instruction count, and none of
 * pf_cov2's extra coverage.
 *
 * Comparing
 *     pf_ctl2 vs fast   = the price of the loop shape alone
 *     pf_cov2 vs pf_ctl2 = the price of the extra COVERAGE alone
 *
 * separates the two, which is the same discipline Phase 7.2 used when its
 * control showed "knob, not flag", and the same discipline attempt 1 used.
 *
 * It is NOT a candidate and cannot be promoted. It exists only to be compared,
 * and only if P3 passes is it worth measuring at all.
 *
 * The criteria, the band and the attempt budget are fixed in
 * docs/phase8_3_preregistration.md.
 */
#define TUNED_FN         memcpy_pf_ctl2
#define TUNED_UNROLL     4
#define TUNED_ALIGNED    1
#define TUNED_PF_BYTES   128
#define TUNED_PF_CLASS   0
#define TUNED_SMALL_AVX  0
#define TUNED_PF_MIN     65536
#define TUNED_SMALL_PATH 1
#define TUNED_WINDOW     1
#define TUNED_WIN_LO     128
#define TUNED_WIN64      1

/* The same shape as pf_cov2, with the second pair a duplicate of the first. */
#define TUNED_PF_BIG_BYTES  128
#define TUNED_PF_BIG2_BYTES 128
#define TUNED_PF_BIG_MIN    524288
#define TUNED_PF_BIG_MAX    2097152

#include "fast_impl.h"
