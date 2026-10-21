/* src/memcpy_avx2_final_gcc_ofast.c — V6: -Ofast.
 *
 * Identical source and knobs to memcpy_avx2_final_gcc.c; the difference is the
 * per-rung flag in the Makefile (GCC_EXTRA_memcpy_avx2_final_gcc_ofast.c :=
 * -Ofast), so the measured delta is the flag and nothing else. -Ofast enables
 * -ffast-math; this kernel does integer byte movement only, so nothing in it can
 * be affected by the FP relaxations, and the harness's byte-for-byte verify has
 * to keep passing before any timing row is written anyway.
 */
#define TUNED_FN        memcpy_avx2_final_gcc_ofast
#define TUNED_UNROLL    4
#define TUNED_ALIGNED   1
#define TUNED_PF_BYTES  128
#define TUNED_PF_CLASS  0
#define TUNED_SMALL_AVX 1
#define TUNED_PF_MIN    65536
#include "memcpy_avx2_tuned_impl.h"
