/* SPDX-License-Identifier: MIT */
/* src/common.h — shared declarations and macros for the kernels and harness.
 *
 * Every kernel here is a from-scratch implementation. No libc wrappers around
 * the functions we are reimplementing (memcpy/memset); only the harness may call
 * libc (clock_gettime, posix_memalign, mmap, ...).
 *
 * The two shipped kernels are declared in include/ (the public API); the rungs
 * used by the benchmark are declared here.
 */
#ifndef AVX2_MEM_COMMON_H
#define AVX2_MEM_COMMON_H

#include <stddef.h>
#include <stdint.h>

#include "memcpy_fast.h"
#include "memcpy_mid.h"

/* Zen-class cache line. Used for alignment, not assumed by the kernels. */
#define CACHELINE 64u

#define ALIGNED(n)   __attribute__((aligned(n)))
#define ALIGN_UP(x, a) (((x) + ((size_t)(a) - 1u)) & ~((size_t)(a) - 1u))
#define NOINLINE     __attribute__((noinline))
#define LIKELY(x)    __builtin_expect(!!(x), 1)
#define UNLIKELY(x)  __builtin_expect(!!(x), 0)

/* Compiler barrier: stops the optimiser reordering across a measurement edge. */
#define BARRIER() __asm__ volatile("" ::: "memory")

/* ---- golden reference ---------------------------------------------------- */
/* Byte-at-a-time, no vectorisation, independent of every rung. It is the
 * reference the harness's --verify pass checks every kernel against. */
void *memcpy_ref(void *dst, const void *src, size_t n) NOINLINE;
void *memset_ref(void *dst, int c, size_t n) NOINLINE;

/* ---- ladder: one file per rung under src/rungs/ -------------------------- */
void *memcpy_naive(void *dst, const void *src, size_t n);   /* rung 1: bytes        */
void *memcpy_word(void *dst, const void *src, size_t n);    /* rung 2: 8 B words    */
void *memcpy_sse2(void *dst, const void *src, size_t n);    /* rung 3: 128-bit      */
void *memcpy_avx2(void *dst, const void *src, size_t n);    /* rung 4: 256-bit      */
void *memcpy_avx2_nt(void *dst, const void *src, size_t n); /* rung 5: NT stores    */
void *memcpy_asm(void *dst, const void *src, size_t n);     /* rung 6: hand-written */
void *memcpy_rep(void *dst, const void *src, size_t n);     /* rung 7: rep movsb    */

/* ---- Phase 7 rungs: the F4 shape, one knob changed each ------------------- */
/* Both are built from src/fast_impl.h with the deliverable's own knobs, so
 * they differ from the shipped kernel in exactly one knob and nothing else.
 * Neither is a candidate for promotion without a fresh certification
 * campaign; see each file's header for its acceptance test. */
void *memcpy_avx2_f4win64(void *dst, const void *src, size_t n);  /* H1b: H=64 at n=128 */
void *memcpy_avx2_f4win129(void *dst, const void *src, size_t n); /* H1a control: no window at 128 */
void *memcpy_f4win64_aligned(void *dst, const void *src, size_t n); /* Phase 7.2: H1b shape, aligned labels */
void *memcpy_f4_baseline(void *dst, const void *src, size_t n);    /* Phase 7.4 control: the pre-promote F4 */
void *memcpy_pf_big(void *dst, const void *src, size_t n);        /* Phase 8.3 attempt 1: 4 KiB prefetch distance in the 512 KiB-2 MiB band */
void *memcpy_pf_ctl(void *dst, const void *src, size_t n);        /* Phase 8.3 CONTROL: the runtime-distance loop shape, distance left at 128 B */
void *memcpy_pf_cov2(void *dst, const void *src, size_t n);       /* Phase 8.3 attempt 2: a second prefetch pair at +192 in the 512 KiB-2 MiB band */
void *memcpy_pf_ctl2(void *dst, const void *src, size_t n);       /* Phase 8.3 attempt 2 CONTROL: the same shape, the second pair a duplicate */

/* ---- the matching memset ladder ------------------------------------------ */
void *memset_naive(void *dst, int c, size_t n);
void *memset_word(void *dst, int c, size_t n);
void *memset_sse2(void *dst, int c, size_t n);
void *memset_avx2(void *dst, int c, size_t n);
void *memset_avx2_nt(void *dst, int c, size_t n);
void *memset_asm(void *dst, int c, size_t n);

#endif /* AVX2_MEM_COMMON_H */
