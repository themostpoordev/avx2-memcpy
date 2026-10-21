/* SPDX-License-Identifier: MIT */
/* bench/harness.h — benchmark harness types and inline timing primitives.
 *
 * The harness measures from *outside* the kernel: kernels live in their own
 * translation units and are reached through a volatile function pointer, so no
 * call can be inlined, folded, or removed by the optimiser.
 */
#ifndef AVX2_MEM_HARNESS_H
#define AVX2_MEM_HARNESS_H

/* Must precede every system header: CLOCK_MONOTONIC_RAW is a GNU extension. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "pmu.h"

typedef void *(*copy_fn)(void *dst, const void *src, size_t n);
typedef void *(*set_fn)(void *dst, int c, size_t n);

/* One benchmarkable implementation pair. op coverage is independent per side. */
typedef struct {
    const char *name;   /* CSV `impl` column */
    copy_fn     copy;   /* NULL if this impl has no memcpy side */
    set_fn      set;    /* NULL if this impl has no memset side */
    const char *note;   /* free-form provenance, printed by --list only */
} impl_t;

extern const impl_t g_impls[];
extern const size_t g_impl_count;

/* ---- clock domains -------------------------------------------------------
 * Two independent domains, never mixed:
 *   tsc  : rdtsc, invariant, calibrated against CLOCK_MONOTONIC_RAW.
 *   core : PMU cycles counter. The only source used for cycles/byte.
 */
static inline uint64_t rdtsc_now(void)
{
    uint32_t lo, hi;
    __asm__ volatile("lfence; rdtsc" : "=a"(lo), "=d"(hi) :: "memory");
    return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t rdtscp_now(void)
{
    uint32_t lo, hi, aux;
    __asm__ volatile("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux) :: "memory");
    (void)aux;
    return ((uint64_t)hi << 32) | lo;
}

static inline double ns_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

#endif /* AVX2_MEM_HARNESS_H */
