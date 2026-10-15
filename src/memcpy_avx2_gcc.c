/* src/memcpy_avx2_gcc.c — Phase 2.6 V1: the SAME Kernel 4 body as src/memcpy_avx2.c
 * (memcpy_avx2_impl.h, prefetch +1 line), compiled by gcc 16.2.1 instead of
 * clang. Nothing else differs, so a measured difference between "avx2" and
 * "avx2_gcc" in one window set is the compiler, not the source.
 *
 * Built by the Makefile's gcc rule (GCC_SRCS), which passes
 *   gcc -O3 -march=znver2 -mtune=znver2 -mavx2 -mfma
 * (plus the same -fno-builtin and -std flags as the clang rungs, so neither
 * compiler is allowed to call libc's memcpy for us).
 */
#define MEMCPY_AVX2_FN       memcpy_avx2_gcc
#define MEMCPY_AVX2_PF_LINES 1
#include "memcpy_avx2_impl.h"
