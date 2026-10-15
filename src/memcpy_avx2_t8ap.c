/* src/memcpy_avx2_t8ap.c — Phase 2.6 V5: unroll 8, fixed prefetch distance +128 B
 * (the Kernel 4 distance) with the V3 aligned path, so the class-based variant
 * t8alp has a fixed-distance control measured in the same windows. */
#define TUNED_FN       memcpy_avx2_t8ap
#define TUNED_UNROLL   8
#define TUNED_ALIGNED  1
#define TUNED_PF_BYTES 128
#define TUNED_PF_CLASS 0
#define TUNED_SMALL_AVX 1
#include "memcpy_avx2_tuned_impl.h"
