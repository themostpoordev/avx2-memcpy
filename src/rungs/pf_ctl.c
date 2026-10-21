/* SPDX-License-Identifier: MIT */
/* src/rungs/pf_ctl.c — Phase 8.3 CONTROL: the runtime-distance LOOP SHAPE
 * with the distance left at 128 B.
 *
 * Why this rung exists. Phase 8.3 attempt 1 (src/rungs/pf_big.c) changes the
 * prefetch distance from a compile-time constant to a runtime value, chosen
 * once before the loop. That is not free: with a runtime distance GCC
 * re-schedules the prefetch in the loop body (the offset becomes a register
 * base and an extra lea appears in the iteration), so pf_big measures
 * DISTANCE + LOOP SHAPE, not the distance alone. A code review of the rung
 * caught that, before any measurement.
 *
 * This rung is the control: the same runtime-distance shape, the same band,
 * and the same 128 B distance the shipped kernel uses. It is therefore
 * SEMANTICALLY the shipped kernel - the copy it performs is identical - but
 * compiled with the loop shape the new knob forces. Comparing
 *
 *     pf_ctl  vs fast   = the price of the loop shape alone
 *     pf_big  vs pf_ctl = the price of the distance alone
 *
 * separates the two effects, which is the same discipline Phase 7.2 used when
 * its control showed "knob, not flag".
 *
 * The criteria, the band and the attempt budget are fixed in
 * docs/phase8_3_preregistration.md.
 */
#define TUNED_FN         memcpy_pf_ctl
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

/* The same shape as pf_big, with the distance left at the shipped value. */
#define TUNED_PF_BIG_BYTES 128
#define TUNED_PF_BIG_MIN   524288
#define TUNED_PF_BIG_MAX   2097152

#include "fast_impl.h"
