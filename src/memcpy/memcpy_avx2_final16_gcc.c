/* src/memcpy_avx2_final16_gcc.c — V9 fused kernel, gcc, SMALL_AVX 0.
 *
 * The one open question in the fused kernel is the 32..63 B class, and this rung
 * is how it gets settled by measurement instead of by argument. The gcc-built
 * Kernel 4 body (avx2_gcc) reached 1.9 ns at 32 B in round 1 - the best 32 B
 * number in the whole screen, and 24% ahead of glibc - and its 32..63 B class is
 * four 16 B moves, not two 32 B moves. If that shape is what made it fast, this
 * rung finds it out on identical source with only TUNED_SMALL_AVX flipped.
 *
 * (The same pair exists in the other direction: memcpy_avx2_final_gcc has
 * TUNED_SMALL_AVX 1. The two differ in exactly one preprocessor constant.)
 */
#define TUNED_FN        memcpy_avx2_final16_gcc
#define TUNED_UNROLL    4
#define TUNED_ALIGNED   1
#define TUNED_PF_BYTES  128
#define TUNED_PF_CLASS  0
#define TUNED_SMALL_AVX 0
#define TUNED_PF_MIN    65536
#include "memcpy_avx2_tuned_impl.h"
