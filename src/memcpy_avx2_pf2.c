/* src/memcpy_avx2_pf2.c — Kernel 4 variant: identical body, prefetch at +2 lines.
 *
 * Same code as src/memcpy_avx2.c except that the per-iteration prefetch address
 * is `+ 2 * 64` bytes instead of `+ 1 * 64`. Because the loop consumes 128 B per
 * iteration, this is the first distance that looks one full iteration ahead
 * (pf1 prefetches a line the same iteration is about to load anyway). It exists
 * only to answer the tuning question; it is not a rung of the ladder and it is
 * not snapshotted as one.
 */
#define MEMCPY_AVX2_FN       memcpy_avx2_pf2
#define MEMCPY_AVX2_PF_LINES 2
#include "memcpy_avx2_impl.h"
