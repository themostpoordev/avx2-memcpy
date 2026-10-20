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

    /* ---- Phase 2.6 V9/V6: the fused final and its flag variants -----------
     * avx2_final is the fused kernel (V1-V8 winners). The clang and gcc builds
     * differ in the compiler only, so the pair measures V1 on the final kernel.
     * final16_gcc flips exactly one knob against final_gcc: the 32..63 B class
     * is four 16 B moves instead of two 32 B moves (V8's open question). The
     * three _gcc_<flag> rungs are final_gcc plus one compiler flag each (V6). */
    { "avx2_final", memcpy_avx2_final, NULL,
      "V9: fused AVX2 - unroll 4, 32 B-aligned path, prefetch +128 gated at 64 KB, flat overlap classes, no tail branch (clang)" },
    { "avx2_final_gcc", memcpy_avx2_final_gcc, NULL,
      "V9: identical source and knobs to avx2_final, built by gcc 16.2.1" },
    { "avx2_final16_gcc", memcpy_avx2_final16_gcc, NULL,
      "V9: as avx2_final_gcc with the 32..63 B class as four 16 B moves (only TUNED_SMALL_AVX differs)" },
    { "avx2_final_v2_gcc", memcpy_avx2_final_v2_gcc, NULL,
      "V9b: as avx2_final_gcc with n <= 64 taken from one compare (only TUNED_SMALL_PATH differs)" },
    { "avx2_final_v2_gcc_noxj", memcpy_avx2_final_v2_gcc_noxj, NULL,
      "V9c: identical to avx2_final_v2_gcc, built with -fno-crossjumping (only the flag differs)" },

    /* ---- the 128/256 B pass (F1-F4) --------------------------------------
     * Four attempts at the two certified losses, each one knob away from
     * avx2_final_v2_gcc_noxj so the pair delta is the attempt. One pass only;
     * whatever they do, the deficit is reported as it stands after it. */
    { "avx2_final_f1", memcpy_avx2_final_f1, NULL,
      "F1: V9c + __builtin_expect on the tiny and alignment branches" },
    { "avx2_final_f3_notest", memcpy_avx2_final_f3_notest, NULL,
      "F3: V9c with the alignment test and aligned loop removed (bounds the alignment-contract idea; verifiable form)" },
    { "avx2_final_f4_win", memcpy_avx2_final_f4_win, NULL,
      "F4: V9c + a straight-line 128..256 B window reached by one compare" },
    { "avx2_final_ifunc", memcpy_avx2_final_ifunc, NULL,
      "F2: the same body behind an IFUNC symbol (measures the indirection a load-time resolver costs)" },
    { "avx2_final_gcc_ofast", memcpy_avx2_final_gcc_ofast, NULL,
      "V6: avx2_final_gcc + -Ofast" },
    { "avx2_final_gcc_unroll", memcpy_avx2_final_gcc_unroll, NULL,
      "V6: avx2_final_gcc + -funroll-loops" },
    { "avx2_final_gcc_noplt", memcpy_avx2_final_gcc_noplt, NULL,
      "V6: avx2_final_gcc + -fno-plt (control: this rung makes no calls)" },
    { "avx2_final_noomit", memcpy_avx2_final_noomit, NULL,
      "V6: fused final with -fno-omit-frame-pointer forced ON (so a slowdown here = the worth of omitting it)" },

#ifdef PERF6_PROBE
    /* ---- Phase 2.6 V6/V7 probe-only rungs --------------------------------
     * These two rows exist ONLY in the probe binary (bench/<t>_perf6), which is
     * built with -DPERF6_PROBE so this table appends them and the same link pulls
     * in their variant objects (Makefile: perf6probe-<t>). Both are the fused
     * final kernel with exactly one thing changed - the compiler flag set - and
     * they sit here, beside their non-flagged twin, so the comparison is
     * interleaved inside one process (protocol rule 4) in the same windows. A
     * flag measured in a binary of its own would be a different-windows
     * comparison, which this project does not make claims from. */
    { "avx2_final_lto", memcpy_avx2_final_lto, NULL,
      "V6: fused final compiled AND linked with -flto (probe binary only)" },
    { "avx2_final_pgo", memcpy_avx2_final_pgo, NULL,
      "V7: fused final rebuilt with -fprofile-use over the harness's own ladder (probe binary only)" },
#endif

    /* ---- Kernel 7 and the Kernel 8 memset ladder -------------------------
     * K7 is evidence, not a promotion candidate: this host has no ERMS/FSRM, so
     * rep movsb is microcoded and the prediction is that it loses everywhere
     * below the bandwidth cliff. The memset ladder mirrors the memcpy one rung
     * for rung, so each pair (naive/naive, word/word, sse2/sse2, avx2/avx2,
     * nt/nt, asm/asm) is a same-shape comparison across the two operations. */
    { "rep", memcpy_rep, NULL,
      "K7: rep movsb for n >= 256 B, kernel 2's word loop below the gate (no ERMS/FSRM on this host)" },
    { "memset_naive", NULL, memset_naive,
      "K8 rung 1: byte-at-a-time fill" },
    { "memset_word", NULL, memset_word,
      "K8 rung 2: 8-byte stores + byte tail" },
    { "memset_sse2", NULL, memset_sse2,
      "K8 rung 3: 16-byte stores, unrolled 4x (forced 128-bit wide)" },
    { "memset_avx2", NULL, memset_avx2,
      "K8 rung 4: 32-byte stores, unrolled 4x, overlapping tail" },
    { "memset_avx2_nt", NULL, memset_avx2_nt,
      "K8 rung 5: non-temporal stores above 1 MiB, with a dst-alignment prologue and sfence" },
    { "memset_asm", NULL, memset_asm,
      "K8 rung 6: hand-written AVX2 fill, same shape as rung 4" },

    /* ---- Phase 3 P1: the mid-range push (1 KiB .. 64 KiB) -----------------
     * Not ladder rungs: six candidates for the one band the deliverable is only
     * level at. All six route n < 1024 to the Phase 2.6 deliverable's own body,
     * so the pair deltas below are confined to 1 KiB and up and the certified
     * 16-512 B wins are not in play. Reference row for every delta: `mid`
     * against glibc, and each variant against `mid` for its own knob. */
    { "avx2_mid", memcpy_avx2_mid, NULL,
      "P1: dedicated 1 KiB-64 KiB kernel - 256 B/iter (unroll 8), 64 B head peel, two prefetch streams (one and two blocks ahead)" },
    { "avx2_mid_u4", memcpy_avx2_mid_u4, NULL,
      "P1 control: as avx2_mid at unroll 4 (128 B/iter), streams scaled to one and two blocks ahead (only MID_UNROLL/MID_PF differ)" },
    { "avx2_mid_pf1", memcpy_avx2_mid_pf1, NULL,
      "P2: as avx2_mid with the second prefetch stream removed (only MID_PF2 differs)" },
    { "avx2_mid_pfnear", memcpy_avx2_mid_pfnear, NULL,
      "P2: as avx2_mid with the brief's literal distances, +128/+256 B (only MID_PF1/MID_PF2 differ)" },
    { "avx2_mid_nopeel", memcpy_avx2_mid_nopeel, NULL,
      "P4: as avx2_mid with the head peel removed - the measured value of explicit cache-line split handling (only MID_PEEL differs)" },
    { "avx2_mid_ntpf", memcpy_avx2_mid_ntpf, NULL,
      "P3: as avx2_mid with prefetchnta on the source at 256 KiB and up (only MID_NTPF differs)" },
    { "avx2_mid_pm", memcpy_avx2_mid_pm, NULL,
      "P1 take 2: as avx2_mid with the head peel gated at 4 KiB (only MID_PEEL_MIN differs) - the candidate if it wins" },
    { "avx2_mid_u4pf1", memcpy_avx2_mid_u4pf1, NULL,
      "P1 diagnostic: unroll 4 with ONE prefetch stream (MID_PF2 = 0) - prices prefetch density against block size for the u4 anomaly" },
    { "avx2_mid_disp", memcpy_avx2_mid_disp, NULL,
      "Task 1: one symbol - the uncertified-untouched deliverable below 4 KiB, avx2_mid_pm at 4 KiB and up (two tail calls)" },
    { "avx2_mid_asm", memcpy_avx2_mid_asm, NULL,
      "P5: hand-scheduled asm twin of avx2_mid_pm (same peel/loop/tail); n < 1024 tail-calls the C deliverable, so its sub-1 KiB rows are not asm" },
};

const size_t g_impl_count = sizeof g_impls / sizeof g_impls[0];
