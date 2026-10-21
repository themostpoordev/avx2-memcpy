/* src/memcpy_avx2_final_gcc_noplt.c — V6: -fno-plt.
 *
 * This rung calls nothing, so -fno-plt should be exactly zero - the interesting
 * part is whether it measures zero. A flag that shows a "win" on a function that
 * makes no calls would be a noise artifact, and this rung is the control that can
 * say so. (The harness calls the rung through a function pointer either way, so
 * the PLT is not on this kernel's path.)
 */
#define TUNED_FN        memcpy_avx2_final_gcc_noplt
#define TUNED_UNROLL    4
#define TUNED_ALIGNED   1
#define TUNED_PF_BYTES  128
#define TUNED_PF_CLASS  0
#define TUNED_SMALL_AVX 1
#define TUNED_PF_MIN    65536
#include "memcpy_avx2_tuned_impl.h"
