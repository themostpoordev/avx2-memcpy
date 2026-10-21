/* src/memcpy_avx2_final_gcc_unroll.c — V6: -funroll-loops.
 *
 * The loop is already hand-unrolled TUNED_UNROLL times; -funroll-loops lets gcc
 * unroll it again (and the flat classes with it). Whether that helps a store-bound
 * loop or just adds uops is a measurement, not a preference - hence the rung.
 */
#define TUNED_FN        memcpy_avx2_final_gcc_unroll
#define TUNED_UNROLL    4
#define TUNED_ALIGNED   1
#define TUNED_PF_BYTES  128
#define TUNED_PF_CLASS  0
#define TUNED_SMALL_AVX 1
#define TUNED_PF_MIN    65536
#include "memcpy_avx2_tuned_impl.h"
