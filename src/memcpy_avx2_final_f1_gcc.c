/* src/memcpy_avx2_final_f1_gcc.c — F1: V9c with branch-layout hints.
 *
 * One knob against avx2_final_v2_gcc_noxj: TUNED_EXPECT 1, which marks the tiny
 * test UNLIKELY and the alignment test LIKELY (the harness hands the kernel
 * 64 B-aligned buffers, so that is the arrangement its rows actually take).
 *
 * The honest prediction, written before the measurement: ~0.00% at 128/256 B,
 * because neither hint is on a branch those sizes execute — the alignment test
 * lives after the n > 512 exit and the tiny test is not taken above 64 B. If the
 * number moves at 128/256 B it is code layout, not the hints.
 */
#define TUNED_FN         memcpy_avx2_final_f1
#define TUNED_UNROLL     4
#define TUNED_ALIGNED    1
#define TUNED_PF_BYTES   128
#define TUNED_PF_CLASS   0
#define TUNED_SMALL_AVX  0
#define TUNED_PF_MIN     65536
#define TUNED_SMALL_PATH 1
#define TUNED_EXPECT     1
#include "memcpy_avx2_tuned_impl.h"
