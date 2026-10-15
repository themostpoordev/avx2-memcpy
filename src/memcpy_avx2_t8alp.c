/* src/memcpy_avx2_t8alp.c — Phase 2.6 V5: unroll 8 + V3 aligned path + per-class
 * prefetch (V5): <4 KB +128 B, 4-64 KB +256 B, 64 KB-1 MB +512 B, >1 MB +1024 B. */
#define TUNED_FN       memcpy_avx2_t8alp
#define TUNED_UNROLL   8
#define TUNED_ALIGNED  1
#define TUNED_PF_BYTES 0
#define TUNED_PF_CLASS 1
#define TUNED_SMALL_AVX 1
#include "memcpy_avx2_tuned_impl.h"
