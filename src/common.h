/* src/common.h — shared declarations, alignment macros, kernel prototypes.
 *
 * Project rule: every kernel in src/ is a from-scratch implementation. No libc
 * wrappers around the functions we are reimplementing (memcpy/memset). Only the
 * benchmark harness may call libc (clock_gettime, posix_memalign, mmap, ...).
 */
#ifndef AVX2_MEM_COMMON_H
#define AVX2_MEM_COMMON_H

#include <stddef.h>
#include <stdint.h>

/* Zen-class cache line. Used for alignment, not assumed in kernels. */
#define CACHELINE 64u

#define ALIGNED(n)   __attribute__((aligned(n)))
#define ALIGN_UP(x, a) (((x) + ((size_t)(a) - 1u)) & ~((size_t)(a) - 1u))
#define NOINLINE     __attribute__((noinline))
#define LIKELY(x)    __builtin_expect(!!(x), 1)
#define UNLIKELY(x)  __builtin_expect(!!(x), 0)

/* Compiler barrier: stops the optimiser reordering across a measurement edge. */
#define BARRIER() __asm__ volatile("" ::: "memory")

/* ------------------------------------------------------------------------- */
/* Kernel prototypes.
 *
 * Phase 1 ships ONLY ref_scalar (byte-at-a-time). It exists for two reasons:
 *   1. it lets the harness be validated end-to-end before any real kernel
 *      exists (build-the-harness-first rule), and
 *   2. it is the golden reference for the --verify correctness pass that every
 *      later kernel is checked against.
 *
 * Phase 3 kernels are declared here as they land (one line per kernel). The
 * Makefile picks up new files under src/ automatically.
 */
void *memcpy_ref(void *dst, const void *src, size_t n) NOINLINE;
void *memset_ref(void *dst, int c, size_t n) NOINLINE;

/* --- Phase 2 kernel ladder (one file per kernel in src/) ------------------ */
void *memcpy_naive(void *dst, const void *src, size_t n);   /* kernel 1 */
void *memcpy_word(void *dst, const void *src, size_t n);    /* kernel 2 */
void *memcpy_sse2(void *dst, const void *src, size_t n);    /* kernel 3 */
void *memcpy_avx2(void *dst, const void *src, size_t n);    /* kernel 4 */
void *memcpy_avx2_pf2(void *dst, const void *src, size_t n); /* kernel 4, prefetch +2 lines */
void *memcpy_avx2_pf4(void *dst, const void *src, size_t n); /* kernel 4, prefetch +4 lines */
void *memcpy_avx2_nt(void *dst, const void *src, size_t n);  /* kernel 5, non-temporal stores */
void *memcpy_asm(void *dst, const void *src, size_t n);      /* kernel 6, hand-written AVX2 asm */
void *memcpy_rep(void *dst, const void *src, size_t n);      /* kernel 7, size-gated rep movsb */

/* --- Kernel 8: the memset ladder (mirror of the memcpy rungs) ------------- */
void *memset_naive(void *dst, int c, size_t n);              /* kernel 8, rung 1 */
void *memset_word(void *dst, int c, size_t n);               /* kernel 8, rung 2 */
void *memset_sse2(void *dst, int c, size_t n);               /* kernel 8, rung 3 */
void *memset_avx2(void *dst, int c, size_t n);               /* kernel 8, rung 4 */
void *memset_avx2_nt(void *dst, int c, size_t n);            /* kernel 8, rung 5, NT above 1 MiB */
void *memset_asm(void *dst, int c, size_t n);                /* kernel 8, rung 6, hand-written AVX2 asm */

/* --- Phase 2.5: hybrid dispatcher ----------------------------------------
 * Routes each call to the rung that measured fastest at that size, per target.
 * The table is generated data (src/routing_table.h, from
 * scripts/gen_routing_table.sh); this rung contains no copy loop of its own.
 */
void *memcpy_hybrid(void *dst, const void *src, size_t n);

/* --- Phase 2.6: tuned AVX2 family (V1-V9) --------------------------------
 * One parameterized body (src/memcpy_avx2_tuned_impl.h); each rung here differs
 * from its neighbours in exactly the knob named in its file. avx2_gcc is the
 * Kernel 4 body compiled by gcc instead of clang (V1).
 */
void *memcpy_avx2_gcc(void *dst, const void *src, size_t n);   /* V1, gcc build of kernel 4 */
void *memcpy_avx2_t4(void *dst, const void *src, size_t n);    /* V4, unroll 4  */
void *memcpy_avx2_t8(void *dst, const void *src, size_t n);    /* V4, unroll 8  */
void *memcpy_avx2_t12(void *dst, const void *src, size_t n);   /* V4, unroll 12 */
void *memcpy_avx2_t8al(void *dst, const void *src, size_t n);  /* V3, unroll 8 + aligned stores */
void *memcpy_avx2_t8ap(void *dst, const void *src, size_t n);  /* V5, + fixed prefetch 128 B */
void *memcpy_avx2_t8alp(void *dst, const void *src, size_t n); /* V5, + per-class prefetch */
void *memcpy_avx2_t4pf(void *dst, const void *src, size_t n);  /* V5b, + prefetch gated at 64 KB */

/* --- Phase 2.6 V9: fused final candidates --------------------------------
 * V1-V8 winners combined into one kernel. The clang and gcc builds share a knob
 * set so the pair measures the compiler on the FINAL kernel (V1's verdict was
 * taken on Kernel 4); final16_gcc flips only the 32..63 B class width (V8); the
 * last three are the V6 flag variants of the gcc build.
 */
void *memcpy_avx2_final(void *dst, const void *src, size_t n);         /* V9, clang */
void *memcpy_avx2_final_gcc(void *dst, const void *src, size_t n);     /* V9, gcc */
void *memcpy_avx2_final16_gcc(void *dst, const void *src, size_t n);   /* V9, gcc, 16 B small path */
void *memcpy_avx2_final_gcc_ofast(void *dst, const void *src, size_t n);  /* V6: -Ofast */
void *memcpy_avx2_final_gcc_unroll(void *dst, const void *src, size_t n); /* V6: -funroll-loops */
void *memcpy_avx2_final_gcc_noplt(void *dst, const void *src, size_t n);  /* V6: -fno-plt */
void *memcpy_avx2_final_noomit(void *dst, const void *src, size_t n);     /* V6: -fno-omit-frame-pointer */
void *memcpy_avx2_final_v2_gcc(void *dst, const void *src, size_t n);    /* V9b: one-compare small path */
void *memcpy_avx2_final_v2_gcc_noxj(void *dst, const void *src, size_t n); /* V9c: V9b + -fno-crossjumping */
void *memcpy_avx2_final_f1(void *dst, const void *src, size_t n);        /* F1: V9c + branch hints */
void *memcpy_avx2_final_f3_notest(void *dst, const void *src, size_t n); /* F3: V9c - alignment test */
void *memcpy_avx2_final_f4_win(void *dst, const void *src, size_t n);    /* F4: V9c + 128..256 B window */
void *memcpy_avx2_final_ifunc(void *dst, const void *src, size_t n);     /* F2: same body via IFUNC */

/* --- Phase 3 P1: the mid-range kernel (1 KiB .. 64 KiB) -------------------
 * One body (src/memcpy_avx2_mid_impl.h) with one knob changed per rung, so the
 * measured pair delta is that knob. mid is the candidate; the other five price
 * unroll 8 against unroll 4 (u4), two streams against one (pf1) and against the
 * brief's literal +2/+4 line distances (pfnear), the head peel (nopeel) and the
 * NTA source prefetch above 256 KiB (ntpf).
 */
void *memcpy_avx2_mid(void *dst, const void *src, size_t n);        /* P1: unroll 8, 2 streams, peel */
void *memcpy_avx2_mid_u4(void *dst, const void *src, size_t n);     /* P1 control: unroll 4 */
void *memcpy_avx2_mid_pf1(void *dst, const void *src, size_t n);    /* P2: one stream */
void *memcpy_avx2_mid_pfnear(void *dst, const void *src, size_t n); /* P2: +2/+4 lines */
void *memcpy_avx2_mid_nopeel(void *dst, const void *src, size_t n); /* P4: no head peel */
void *memcpy_avx2_mid_ntpf(void *dst, const void *src, size_t n);   /* P3: NTA src prefetch >= 256 KiB */
void *memcpy_avx2_mid_pm(void *dst, const void *src, size_t n);     /* P1b: peel gated at 4 KiB (candidate) */
void *memcpy_avx2_mid_u4pf1(void *dst, const void *src, size_t n);  /* P1 diagnostic: unroll 4, one stream */
void *memcpy_avx2_mid_asm(void *dst, const void *src, size_t n);    /* P5: hand-scheduled twin of mid_pm (>1 KiB only) */

/* Probe-only rungs: -flto and PGO are link-time properties, so they are built
 * into bench/<t>_perf6 (see the Makefile's perf6 rules) and only that binary
 * defines PERF6_PROBE. The declarations are guarded so the normal binaries can
 * never reference an object they do not link. */
#ifdef PERF6_PROBE
void *memcpy_avx2_final_lto(void *dst, const void *src, size_t n);       /* V6: -flto */
void *memcpy_avx2_final_pgo(void *dst, const void *src, size_t n);       /* V7: PGO */
#endif

#endif /* AVX2_MEM_COMMON_H */
