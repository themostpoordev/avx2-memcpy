/* src/memcpy_avx2_final_f4_gcc.c — F4: a dedicated 128..256 B window.
 *
 * One knob against avx2_final_v2_gcc_noxj: TUNED_WINDOW 1, which adds
 *
 *     if ((size_t)(n - 128) <= 128u) { T_BLOCKPAIR(4); return dst; }
 *
 * before the flat-class chain. One unsigned compare, then a straight-line block
 * pair with no further branch - the shape the original fused kernel had at these
 * sizes and the small-path builds lost.
 *
 * Ceiling, stated before measuring: the original fused kernel was 3.3 ns at
 * 128 B and 3.4 ns at 256 B, against glibc's 3.1 and 3.7. So the best this rung
 * can do at 128 B is parity-minus-one-step (-5% vs glibc) and at 256 B parity
 * with the fused kernel (+8.4% vs glibc, which was a Tier-B "measured" row).
 * It cannot turn 128 B into a certified win, and the report will not pretend
 * otherwise.
 */
#define TUNED_FN         memcpy_avx2_final_f4_win
#define TUNED_UNROLL     4
#define TUNED_ALIGNED    1
#define TUNED_PF_BYTES   128
#define TUNED_PF_CLASS   0
#define TUNED_SMALL_AVX  0
#define TUNED_PF_MIN     65536
#define TUNED_SMALL_PATH 1
#define TUNED_WINDOW     1
#include "memcpy_avx2_tuned_impl.h"
