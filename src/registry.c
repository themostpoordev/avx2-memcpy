/* SPDX-License-Identifier: MIT */
/* src/registry.c — the benchmark implementation table.
 *
 * One row per function the harness may measure. The table is deliberately small:
 * the two shipped kernels, the golden reference, glibc as the baseline, and the
 * seven memcpy / six memset rungs of the ladder. Adding a kernel is one line
 * here plus its file under src/ (the Makefile globs sources, so there is no
 * build-system edit per kernel).
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

#ifdef HYBRID_PROBE
/* Built only into experiments/hybrid/bench_hybrid (see the Makefile's
 * `experiments` target). The dispatcher is a documented loss; it is shipped so
 * the loss can be re-measured, not as a candidate. */
void *memcpy_hybrid(void *dst, const void *src, size_t n);
#endif

const impl_t g_impls[] = {
    { "ref_scalar", memcpy_ref, memset_ref,
      "byte-at-a-time golden reference; independent of every kernel" },

    { "glibc", memcpy, memset,
      "glibc ifunc-resolved baseline (the thing we are trying to beat)" },

    /* ---- the two shipped kernels (public API: include/) ------------------ */
    { "fast", memcpy_fast, NULL,
      "deliverable (F4): one-compare tiny path, 128-256 B straight-line window, "
      "32 B AVX2 loop above 512 B, gcc -O3 -fno-crossjumping -falign-labels=64, "
      "+128 B prefetch at 64 KiB and up" },

    /* The pre-promote F4, kept as the control for the Phase 7.4 comparison.
     * It is registered IMMEDIATELY AFTER "fast" on purpose: the harness runs
     * rep-major across all cases, so two kernels that sit next to each other
     * in this table are the two least positionally biased against each
     * other, and the Phase 7.4 acceptance criteria are all relative to it. */
    { "f4_baseline", memcpy_f4_baseline, NULL,
      "Phase 7.4 CONTROL: the pre-promote F4, verbatim but for the symbol, so "
      "the promoted kernel and the kernel it replaces can be measured in the "
      "same windows" },

    { "mid", memcpy_mid, NULL,
      "opt-in 1 KiB-64 KiB kernel: 256 B/iteration, three prefetch streams, head "
      "peel gated at 4 KiB. Certifies +6-7% at 16 KiB; NOT part of the fast path" },

    /* ---- Phase 7 rungs: the F4 shape with one knob changed ----------------- */
    { "f4win64", memcpy_avx2_f4win64, NULL,
      "Phase 7 H1b: F4 with an explicit H=64 case at n=128 inside the window. "
      "PRIMARY CANDIDATE - acceptance test in src/rungs/avx2_f4win64.c" },

    { "f4win129", memcpy_avx2_f4win129, NULL,
      "Phase 7 H1a CONTROL: F4 with the window's lower edge at 129, so n=128 "
      "falls through to the class chain. Never promotable - see "
      "src/rungs/avx2_f4win64.c" },

    { "f4win64al", memcpy_f4win64_aligned, NULL,
      "Phase 7.2: F4 with the H=64 case at n=128 AND -falign-labels=64, so the "
      "32..64 B branch target is 64-byte aligned. Pre-registered criteria in "
      "src/rungs/f4win64_aligned.c" },

    /* ---- the ladder (src/rungs/) ---------------------------------------- */
    { "naive", memcpy_naive, NULL,
      "rung 1: byte-by-byte loop, 1 B load + 1 B store per byte" },

    { "word", memcpy_word, NULL,
      "rung 2: 8 B chunks + byte tail, <32 B byte fast path" },

    { "sse2", memcpy_sse2, NULL,
      "rung 3: 16 B x4 unrolled, _mm_loadu_si128/_mm_storeu_si128, scalar tail" },

    { "avx2", memcpy_avx2, NULL,
      "rung 4: 32 B x4 unrolled, unaligned 256-bit moves, overlapping tail" },

    { "avx2_nt", memcpy_avx2_nt, NULL,
      "rung 5: non-temporal stores above a fixed size gate, with sfence" },

    { "asm", memcpy_asm, NULL,
      "rung 6: hand-written AVX2 (clang integrated assembler), same shape as rung 4" },

    { "rep", memcpy_rep, NULL,
      "rung 7: size-gated rep movsb (no ERMS/FSRM on this silicon - see "
      "docs/NEGATIVE_RESULTS.md)" },

    { "memset_naive", NULL, memset_naive, "memset ladder rung 1: bytes" },
    { "memset_word", NULL, memset_word, "memset ladder rung 2: 8 B words" },
    { "memset_sse2", NULL, memset_sse2, "memset ladder rung 3: 128-bit" },
    { "memset_avx2", NULL, memset_avx2, "memset ladder rung 4: 256-bit" },
    { "memset_avx2_nt", NULL, memset_avx2_nt, "memset ladder rung 5: NT above 1 MiB" },
    { "memset_asm", NULL, memset_asm, "memset ladder rung 6: hand-written AVX2" },

#ifdef HYBRID_PROBE
    { "hybrid", memcpy_hybrid, NULL,
      "Phase 2.5 dispatcher over the ladder: 51 cells, 22 losses. Documented "
      "loss; see experiments/hybrid/README.md" },
#endif
};

const size_t g_impl_count = sizeof g_impls / sizeof g_impls[0];
