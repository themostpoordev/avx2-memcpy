/* SPDX-License-Identifier: MIT */
/* src/rungs/avx2_f4win129.c — Phase 7 rung H1a (CONTROL ONLY, NEVER PROMOTED).
 *
 * The shipped F4 shape with ONE knob changed: the window's lower edge moves
 * from 128 to 129, so n = 128 no longer enters the window and falls through to
 * the flat class chain, where the (64, 128] class's H = 64 block pair copies it
 * — the same four loads and four stores the H1b rung reaches with one extra
 * compare, but reached through FOUR extra compares instead.
 *
 * WHY THIS RUNG EXISTS
 *   To price the two competing costs at 128 B:
 *
 *     - H1b (src/rungs/avx2_f4win64.c): perfect tiling, one extra compare.
 *     - H1a (this file): perfect tiling, four extra compares.
 *
 *   The V9c history says the compares are not free: V9c, which had no window
 *   at all and so reached the H = 64 shape through the chain, measured -20.1%
 *   at 128 B — worse than the shipped kernel's -10.04%, even though both copy
 *   the same bytes with the same four loads and four stores. If H1a loses to
 *   H1b here, the V9c history is confirmed on this host and the H1b shape is
 *   the one worth promoting. If H1a wins, something in that history does not
 *   hold here, and the result needs explaining before anything is promoted.
 *
 *   Either way this is a MEASUREMENT, not a candidate.
 *
 * NOT TO BE PROMOTED
 *   Even if this rung wins, it must NOT go into src/fast.c. Its win would
 *   come from a shape that the V9c history says is fragile: the same code
 *   reached through a longer dispatch chain. The acceptance test and the stop
 *   condition for the Phase 7 128 B work live in src/rungs/avx2_f4win64.c, and
 *   they name this file as a control that is never promotable.
 *
 *   The shipped kernel keeps its 128..256 B window either way unless H1b
 *   passes the acceptance test in src/rungs/avx2_f4win64.c and a fresh
 *   certification campaign is committed (METHODOLOGY.md §11 rule 4).
 */
#define TUNED_FN         memcpy_avx2_f4win129
#define TUNED_UNROLL     4
#define TUNED_ALIGNED    1
#define TUNED_PF_BYTES   128
#define TUNED_PF_CLASS   0
#define TUNED_SMALL_AVX  0
#define TUNED_PF_MIN     65536
#define TUNED_SMALL_PATH 1
#define TUNED_WINDOW     1
#define TUNED_WIN_LO     129
#define TUNED_WIN64      0
#include "fast_impl.h"
