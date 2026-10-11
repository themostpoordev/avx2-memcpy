/* src/memcpy_avx2_pf4.c — Kernel 4 variant: identical body, prefetch at +4 lines.
 *
 * Same code as src/memcpy_avx2.c except that the per-iteration prefetch address
 * is `+ 4 * 64` = +256 B, i.e. two 128 B iterations ahead. Exists only for the
 * prefetch-distance comparison; not a rung, not snapshotted as one.
 */
#define MEMCPY_AVX2_FN       memcpy_avx2_pf4
#define MEMCPY_AVX2_PF_LINES 4
#include "memcpy_avx2_impl.h"
