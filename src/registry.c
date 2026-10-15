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

    { "asm", memcpy_asm, NULL,
      "kernel 6: hand-written AVX2 assembly (src/memcpy_asm.S), 32B aligned store path (vmovdqa) + unaligned path (vmovdqu), scalar <64B" },

    /* Phase 2.5: not a seventh ladder rung - a dispatcher over rungs 1-6. It
     * copies nothing itself; it routes each size to the rung that measured
     * fastest at that size (src/routing_table.h, generated from the Kernel 6
     * campaign's min_of_mins). glibc is never a routing target. */
    { "hybrid", memcpy_hybrid, NULL,
      "phase 2.5: size+target routing over rungs 1-6, table generated from measured min_of_mins" },

    /* ---- Phase 2.6: tuned AVX2 family (V1-V9) ---------------------------
     * These are not ladder rungs: they are the candidates of the performance
     * push, each one knob away from its neighbour, measured against glibc in the
     * same windows. The winner becomes the deliverable (memcpy_avx2_final). */
    { "avx2_gcc", memcpy_avx2_gcc, NULL,
      "V1: kernel 4 body, compiled by gcc 16.2.1 instead of clang (same source)" },
    { "avx2_t4", memcpy_avx2_t4, NULL,
      "V4: tuned body, unroll 4 (128 B/iter), flat overlap classes, no prefetch" },
    { "avx2_t8", memcpy_avx2_t8, NULL,
      "V4: tuned body, unroll 8 (256 B/iter), flat overlap classes, no prefetch" },
    { "avx2_t12", memcpy_avx2_t12, NULL,
      "V4: tuned body, unroll 12 (384 B/iter), flat overlap classes, no prefetch" },
    { "avx2_t8al", memcpy_avx2_t8al, NULL,
      "V3: tuned body, unroll 8, 32 B-aligned main loop when both pointers allow" },
    { "avx2_t8ap", memcpy_avx2_t8ap, NULL,
      "V5: tuned body, unroll 8, aligned path, fixed prefetch +128 B" },
    { "avx2_t8alp", memcpy_avx2_t8alp, NULL,
      "V5: tuned body, unroll 8, aligned path, per-class prefetch (+128/+256/+512/+1024)" },
    { "avx2_t4pf", memcpy_avx2_t4pf, NULL,
      "V5b: tuned body, unroll 4, aligned path, +128 B prefetch only at 64 KB and up" },

    /* ---- Phase 2 rest of the ladder -------------------------------------
    { "memset_sse2", memset_sse2, NULL, "kernel 8: memset ladder" },
    { "rep",     memcpy_rep,     memset_rep,     "rep movsb/stosb, size-gated" },
    { "memset_naive", ... }  ... reported as the memset ladder (kernel 8)
    ---------------------------------------------------------------------- */
};

const size_t g_impl_count = sizeof g_impls / sizeof g_impls[0];
