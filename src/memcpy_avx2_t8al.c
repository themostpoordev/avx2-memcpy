/* src/memcpy_avx2_t8al.c — Phase 2.6 V3: unroll 8 + 32 B-aligned main loop when
 * both pointers are 32 B aligned (the harness's buffers are 64 B aligned). */
#define TUNED_FN       memcpy_avx2_t8al
#define TUNED_UNROLL   8
#define TUNED_ALIGNED  1
#define TUNED_PF_BYTES 0
#define TUNED_PF_CLASS 0
#define TUNED_SMALL_AVX 1
#include "memcpy_avx2_tuned_impl.h"
