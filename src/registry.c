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
#include "harness.h"
#include "common.h"

const impl_t g_impls[] = {
    { "ref_scalar", memcpy_ref, memset_ref,
      "byte-at-a-time golden reference; not a performance kernel" },

    /* ---- Phase 3 additions go here, one row per kernel -------------------

    { "naive",    memcpy_naive,    memset_naive,    "byte loop" },
    { "word",     memcpy_word,     memset_word,     "8B chunks + tail" },
    { "sse2",     memcpy_sse2,     memset_sse2,     "16B x4 unrolled" },
    { "avx2",     memcpy_avx2,     memset_avx2,     "32B x4 unrolled, cached store" },
    { "avx2_nt",  memcpy_avx2_nt,  memset_avx2_nt,  "32B x4, non-temporal, size-gated" },
    { "nasm",     memcpy_nasm,     memset_nasm,     "hand-written clang-integrated asm" },
    { "rep",      memcpy_rep,      memset_rep,      "rep movsb/stosb, size-gated" },
    { "glibc",    memcpy,          memset,          "glibc 2.44 ifunc baseline" },
    ---------------------------------------------------------------------- */
};

const size_t g_impl_count = sizeof g_impls / sizeof g_impls[0];
