/* src/memcpy_avx2_t4.c — Phase 2.6 V4 unroll sweep: TUNED_UNROLL 4 (128 B/iter). */
#define TUNED_FN       memcpy_avx2_t4
#define TUNED_UNROLL   4
#define TUNED_ALIGNED  0
#define TUNED_PF_BYTES 0
#define TUNED_PF_CLASS 0
#define TUNED_SMALL_AVX 1
#include "memcpy_avx2_tuned_impl.h"
