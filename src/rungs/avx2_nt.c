/* SPDX-License-Identifier: MIT */
/* src/memcpy_avx2_nt.c — Kernel 5: AVX2 with non-temporal stores.
 *
 * The body lives in memcpy_avx2_nt_impl.h; this file only fixes the parameters:
 *
 *   prefetch distance  +2 cache lines, inherited from Kernel 4's tuning, where
 *                      +2 won the 64 B-1 KB band on all three targets and was
 *                      not beaten there by +1 or +4.
 *   NT threshold       256 KB by default. The final value is chosen by
 *                      scripts/nt_threshold_probe.sh, which sweeps sizes with the
 *                      threshold forced low and reports the GB/s inflection.
 */
#include <stddef.h>

#define MEMCPY_AVX2_NT_FN        memcpy_avx2_nt
#define MEMCPY_AVX2_NT_PF_LINES  2

#include "avx2_nt_impl.h"
