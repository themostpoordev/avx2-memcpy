/* src/registry.c — the benchmark implementation table.
 *
 * Adding a kernel is a one-line change here plus its new file under src/;
 * the Makefile globs sources, so no build-system edit is needed.
 *
 * Phase 1 contains only the scalar reference (harness validation + golden
 * model for --verify). Phase 3 appends one row per kernel per the requested
 * ladder: naive -> word -> sse2 -> avx2 -> avx2_nt -> asm -> rep_movsb, and the
 * matching memset ladder.
 */
/* _GNU_SOURCE must be defined before the FIRST system header in this TU:
 * harness.h needs CLOCK_MONOTONIC_RAW, and a system header included before
 * harness.h would lock the feature set in place. Keep this at the very top. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <string.h>                 /* the `glibc` baseline entry below */

#include "harness.h"
#include "common.h"

const impl_t g_impls[] = {
    { "ref_scalar", memcpy_ref, memset_ref,
      "byte-at-a-time golden reference; independent of every kernel" },

    { "glibc", memcpy, memset,
      "glibc 2.44 ifunc-resolved baseline (the thing we are trying to beat)" },

    /* ---- Phase 2 ladder: one row appended per kernel, in ladder order ---- */
    { "naive", memcpy_naive, NULL,
      "kernel 1: byte-by-byte loop, 1B load + 1B store per byte" },

    { "word", memcpy_word, NULL,
      "kernel 2: 8B chunks + byte tail, <32B byte fast path" },

    { "sse2", memcpy_sse2, NULL,
      "kernel 3: 16B x4 unrolled, _mm_loadu_si128/_mm_storeu_si128, scalar tail" },

    { "avx2", memcpy_avx2, NULL,
      "kernel 4: 32B x4 unrolled, _mm256_loadu_si256/_mm256_storeu_si256, prefetch +1 line" },

    { "avx2_pf2", memcpy_avx2_pf2, NULL,
      "kernel 4 variant: identical body, prefetch distance +2 cache lines" },

    { "avx2_pf4", memcpy_avx2_pf4, NULL,
      "kernel 4 variant: identical body, prefetch distance +4 cache lines" },

    { "avx2_nt", memcpy_avx2_nt, NULL,
      "kernel 5: as kernel 4 above the NT threshold, _mm256_stream_si256 + sfence, cached below it" },

    /* ---- Phase 2 rest of the ladder -------------------------------------
    { "memset_sse2", memset_sse2, NULL, "kernel 8: memset ladder" },
    { "asm",     memcpy_asm,     memset_asm,     "hand-written clang-integrated asm (.S)" },
    { "rep",     memcpy_rep,     memset_rep,     "rep movsb/stosb, size-gated" },
    { "memset_naive", ... }  ... reported as the memset ladder (kernel 8)
    ---------------------------------------------------------------------- */
};

const size_t g_impl_count = sizeof g_impls / sizeof g_impls[0];
