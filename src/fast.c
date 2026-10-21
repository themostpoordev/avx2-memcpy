/* SPDX-License-Identifier: MIT */
/* src/fast.c — F4: the shipped memcpy. Builds `memcpy_fast`
 * (include/memcpy_fast.h).
 *
 * F4 is V9c plus TUNED_WINDOW 1: a dedicated straight-line window for
 * 128..256 B, which is
 *
 *     if ((size_t)(n - 128) <= 128u) { T_BLOCKPAIR(4); return dst; }
 *
 * placed before the flat-class chain. One unsigned compare, then a block pair
 * with no further branch — the shape the original fused kernel had at these
 * sizes and that the small-path builds lost.
 *
 * Measured against glibc with this build (see docs/RESULTS.md): +38..45% at
 * 16 B, +25.4% at 32 B, +25.5% at 64 B, −10.0% at 128 B, +8.4% at 256 B on two
 * of three targets, +16.8% at 512 B, level from 1 KiB to 64 KiB, +8..11% at
 * 1 MiB. The 128 B deficit was stated as a ceiling before the campaign ran, and
 * it is reported as a certified loss rather than tuned away.
 *
 * Compile with gcc and -fno-crossjumping: without the flag gcc merges the heads
 * of T_BLOCKPAIR(2) and T_BLOCKPAIR(4), and the 256 B class loses the straight
 * line this rung exists to provide.
 */
#define TUNED_FN         memcpy_fast
#define TUNED_UNROLL     4
#define TUNED_ALIGNED    1
#define TUNED_PF_BYTES   128
#define TUNED_PF_CLASS   0
#define TUNED_SMALL_AVX  0
#define TUNED_PF_MIN     65536
#define TUNED_SMALL_PATH 1
#define TUNED_WINDOW     1
#include "fast_impl.h"
