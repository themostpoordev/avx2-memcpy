/* SPDX-License-Identifier: MIT */
/* src/rungs/f4_baseline.c — CONTROL: the shipped F4 as it was BEFORE the
 * Phase 7.4 promote.
 *
 * This file is the pre-promote src/fast.c verbatim, with only TUNED_FN changed.
 * It exists so that the promoted kernel and the kernel it replaces can be
 * measured in the SAME windows, in the same process, on the same host state:
 * the Phase 7.4 acceptance criteria (A1-A3) are all "delta vs shipped F4",
 * and a relative comparison in shared windows is the only way to measure them
 * against a host whose floor moves.
 *
 * It is built by the gcc rule with -fno-crossjumping and WITHOUT
 * -falign-labels=64: the alignment flag is what the promoted kernel adds, and
 * a baseline with it would be the candidate rather than the baseline.
 *
 * Its .text md5 must equal the pre-promote shipped F4's - 0b2d0837e25a53b2570ef33a6606edfa
 * on znver2 and znver3, 71c9cefef2d67698267abc118fe55e1a on v3 - or it is not
 * the same kernel and the comparison is void.
 */
#define TUNED_FN         memcpy_f4_baseline
#define TUNED_UNROLL     4
#define TUNED_ALIGNED    1
#define TUNED_PF_BYTES   128
#define TUNED_PF_CLASS   0
#define TUNED_SMALL_AVX  0
#define TUNED_PF_MIN     65536
#define TUNED_SMALL_PATH 1
#define TUNED_WINDOW     1
#include "fast_impl.h"
