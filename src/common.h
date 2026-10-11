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

#endif /* AVX2_MEM_COMMON_H */
