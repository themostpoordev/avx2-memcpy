/* SPDX-License-Identifier: MIT */
/* src/rungs/pf_cov2.c — Phase 8.3 attempt 2: prefetch COVERAGE in the 1 MiB
 * band.
 *
 * The shipped kernel's software prefetch covers ONE 64 B line of the next
 * 128 B iteration (a T0 prefetch at +128 brings in the line containing it);
 * the other line is left to the hardware prefetcher. At 1 MiB the copy then
 * runs at either the loop's store-port ceiling (32 B/cycle, 91 GB/s — the fast
 * mode) or the memory-hierarchy rate (27 B/cycle, 76 GB/s — the median), and
 * which one it gets is a per-(kernel, rep) lottery.
 *
 * This rung covers BOTH lines of the next iteration in software: a second
 * prefetch pair at +192, inside the same band (512 KiB <= n < 2 MiB). The
 * first pair stays at the shipped +128 — attempt 1's 4096 B distance is NOT
 * carried over; it cost 2-4%.
 *
 * The band is FIXED by the pre-registration (docs/phase8_3_preregistration.md):
 *   TUNED_PF_BIG_MIN = 524288 (512 KiB), TUNED_PF_BIG_MAX = 2097152 (2 MiB)
 * so that 64 KiB, 256 KiB, 2 MiB and 4 MiB are NOT touched, and the prefetch
 * is off below TUNED_PF_MIN (64 KiB) anyway, so no certified row can be
 * affected at all. The second prefetch's offset is loop-invariant, so the
 * compiler unswitches on it and the path outside the band keeps the shipped
 * loop.
 *
 * CRITERIA, fixed in docs/phase8_3_preregistration.md BEFORE this file was
 * written:
 *   P1  every certified row (8/16/32/64/128/256/512 B, 16 KiB) unchanged
 *       within the null band vs the Phase 7.4 deliverable, same windows
 *   P2  the 1 MiB floor is preserved: not worse than the Phase 7.4 reference
 *       (+12.58/+11.10/+9.42%) on any target
 *   P3  the 1 MiB median moves to >= +1.5% vs glibc on >= 2 of 3 targets
 *   S1  if P1 fails -> REJECT (no certified row is traded for a median)
 *   S2  if P2 fails -> REJECT (the win at the floor is the asset)
 *   S3  if P3 fails -> accept parity at 1 MiB and close Phase 8. There is no
 *       attempt 3.
 *
 * The control is src/rungs/pf_ctl2.c: the same shape with the second pair at
 * +128, i.e. a duplicate of the first — the same instruction count, no extra
 * coverage — so the loop shape can be priced separately. It is built and
 * measured before any promote, and only if P3 passes.
 */
#define TUNED_FN         memcpy_pf_cov2
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

/* The shipped distance, the fixed band, and the second pair at +192. */
#define TUNED_PF_BIG_BYTES  128
#define TUNED_PF_BIG2_BYTES 192
#define TUNED_PF_BIG_MIN    524288
#define TUNED_PF_BIG_MAX    2097152

#include "fast_impl.h"
