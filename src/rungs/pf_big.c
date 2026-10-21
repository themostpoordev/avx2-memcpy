/* SPDX-License-Identifier: MIT */
/* src/rungs/pf_big.c — Phase 8.3 attempt 1: a LONGER prefetch distance for the
 * 1 MiB band.
 *
 * The construction, from Phase 8.1 Q4
 * (docs/phase8_1_mechanism.md, commit 912ed32):
 *
 *   The 1 MiB mode split is KERNEL-driven. The fast cluster (23 053-25 819 ns
 *   on znver2, 12 of 45 reps) is exclusive to the kernels that carry the
 *   software prefetch - fast, f4_baseline, f4win64al - and is absent from
 *   glibc, ref_scalar and naive. It predates the promote (f4_baseline shows
 *   it) and it does not correlate with the fast modes at other sizes (4-12 of
 *   45, at or below chance). glibc's own 1 MiB minima in the same windows
 *   never go below 26 370 ns.
 *
 *   The published symptom is a MEDIAN problem, not a floor problem: the floor
 *   is already a win (+12.58/+11.10/+9.42%) and the fast cluster already
 *   exists. A longer prefetch distance would not touch the floor - the fast
 *   mode's data is already in cache - but it could let the TYPICAL copy reach
 *   the same 91 GB/s, which is what moves the median.
 *
 *   The arithmetic: the fast mode moves 2 MiB of traffic in 23 053 ns = 91
 *   GB/s. At that rate 128 B of lookahead covers 1.4 ns, against a DRAM
 *   latency of tens of nanoseconds. 4096 B covers ~45 ns - most of a DRAM
 *   latency - and the prefetched line survives 4 KiB of streaming in a 32 KiB
 *   L1.
 *
 * The band is FIXED by the pre-registration (docs/phase8_3_preregistration.md):
 *   TUNED_PF_BIG_MIN = 524288 (512 KiB), TUNED_PF_BIG_MAX = 2097152 (2 MiB)
 * so that 64 KiB, 256 KiB and 4 MiB are NOT touched - the prefetch is live
 * for every n >= TUNED_PF_MIN (64 KiB), so a global distance change would
 * move rows the P1 gate does not cover and would reach 4 MiB, which is out of
 * scope. The distance is chosen once, before the loop - but the loop BODY is
 * not byte-for-byte the shipped loop: with a runtime distance GCC re-schedules
 * the prefetch (the offset becomes a register base and an extra lea appears in
 * the iteration). A measurement of this rung therefore measures DISTANCE +
 * LOOP SHAPE, which is why src/rungs/pf_ctl.c exists: the same runtime shape
 * with the distance left at 128 B, so the shape can be priced separately.
 * (A code review caught this before any measurement.)
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
 *   S3  if P3 fails -> attempt 2, a DIFFERENT mechanism from Phase 8.1 Q3's
 *       list; if that also fails, accept parity at 1 MiB and close Phase 8
 *
 * 16 KiB cannot be affected: it is below TUNED_PF_MIN, so the prefetch is off
 * there entirely.
 *
 * This file changes ONLY the prefetch distance inside the band. Everything
 * else - the size classes, the 128 B window, the H=64 pair at n == 128, the
 * unroll, the aligned loop - is the shipped kernel's.
 */
#define TUNED_FN         memcpy_pf_big
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

/* Phase 8.3 attempt 1: the longer distance, inside the fixed band. */
#define TUNED_PF_BIG_BYTES 4096
#define TUNED_PF_BIG_MIN   524288
#define TUNED_PF_BIG_MAX   2097152

#include "fast_impl.h"
