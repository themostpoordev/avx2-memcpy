/* src/memcpy_avx2_final_lto.c — V6: the fused final kernel built with -flto.
 *
 * Knob-for-knob identical to src/memcpy_avx2_final_gcc.c; the ONLY difference is
 * the flag set (-flto at compile and at link, see the perf6 rules in the
 * Makefile). It is deliberately NOT part of the normal bench_<t> binaries: LTO
 * is a link-time property, and the honest control is a binary that contains both
 * this object and the non-LTO one, measured in the same windows. That binary is
 * bench/<t>_perf6, built by `make perf6probe-<t>`; see the Makefile.
 *
 * Expectation, recorded before measuring: the hot path is one function in one
 * translation unit with no cross-TU calls, so -flto has little to work with and
 * the measured delta is likely to sit inside the noise band. It is measured
 * anyway rather than argued away, because "probably nothing" is a prediction and
 * the whole point of this push is to replace predictions with numbers.
 */
#define TUNED_FN        memcpy_avx2_final_lto
#define TUNED_UNROLL    4
#define TUNED_ALIGNED   1
#define TUNED_PF_BYTES  128
#define TUNED_PF_CLASS  0
#define TUNED_SMALL_AVX 1
#define TUNED_PF_MIN    65536
#include "memcpy_avx2_tuned_impl.h"
