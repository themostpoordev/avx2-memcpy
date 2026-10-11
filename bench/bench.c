/* bench/bench.c — benchmark harness.
 *
 * Design rules baked in here:
 *
 * 1. Kernels are reached through a `volatile` function pointer. The call is
 *    opaque to the optimiser, so it can neither be inlined, folded, nor
 *    deleted. No per-iteration sink is needed (that would tax the timed
 *    region); the destination is checksummed once after the timed region.
 *
 * 2. Two clock domains, never mixed:
 *      wall : CLOCK_MONOTONIC_RAW per batch  -> median/min/p99 and GB/s
 *      core : PMU cycles counter (perf_event_open) -> cycles/byte
 *    cycles/byte NEVER comes from rdtsc. TSC is reported as a separate column
 *    (tsc_ticks_per_byte) precisely so the ~10% skew stays visible.
 *
 * 3. Small sizes are timed as a batch of K calls (~64 KiB of traffic, K in
 *    [1,1024]) so the ~20 ns clock_gettime cost cannot dominate a 16-byte
 *    measurement. The measured timer overhead is subtracted from every sample.
 *
 * 4. Cache warmup: the whole timed loop runs warm; measured sizes reuse
 *    buffers allocated once at startup and touched once, so no page faults are
 *    charged to a kernel.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/perf_event.h>   /* PERF_TYPE_*, PERF_COUNT_HW_* for our PMU sets */

#include "common.h"
#include "harness.h"
#include "pmu.h"

#ifndef TARGET_NAME
#define TARGET_NAME "unknown"
#endif

#define MAX_SIZE   ((size_t)256 * 1024 * 1024)
#define MAX_ITERS  4096
#define MAX_SAMPLES 4096

/* ------------------------------------------------------------------ state */
static volatile uint64_t g_sink;

static unsigned char *g_src, *g_dst, *g_ref;   /* 64B-aligned, +128B slack */
static size_t         g_max_size = MAX_SIZE;

static const size_t g_sizes[] = {
    16, 32, 64, 128, 256, 512, 1024, 4096,
    16384, 65536, 262144, 1048576, 4194304, 16777216, 67108864, 268435456
};
static const size_t g_sizes_quick[] = {
    64, 1024, 65536, 1048576, 16777216, 268435456
};

/* Event sets. MINI is used inside the timed sweep (one counter, so no
 * multiplexing can perturb cycles/byte). FULL is used by --pmu. */
static const pmu_event_desc EV_MINI[] = {
    { "cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES },
};
/* The full counter set is measured in ROUNDS of at most PMU_MAX_GROUP (4)
 * events, each round running the workload once. Reason (measured, Phase 1,
 * .recon/pmugsize.c): on this host a 5+ event group reports 0.3-1% running
 * time and returns all zeros instead of multiplexing. Each round therefore
 * starts a fresh, fully-scheduled measurement of identical work, and the
 * per-round running% is printed so the composition is auditable. */
#define PMU_HW(c, nm)  { (nm), PERF_TYPE_HARDWARE, (c) }
#define PMU_HWC(cid, op, res, nm) \
    { (nm), PERF_TYPE_HW_CACHE, \
      (unsigned long long)(cid) | ((op) << 8) | ((res) << 16) }
#define PMU_RAW(c, nm) { (nm), PERF_TYPE_RAW, (c) }

static const pmu_event_desc ROUND0[] = {          /* core pipeline */
    PMU_HW(PERF_COUNT_HW_CPU_CYCLES,        "cycles"),
    PMU_HW(PERF_COUNT_HW_INSTRUCTIONS,      "instructions"),
    PMU_HWC(PERF_COUNT_HW_CACHE_L1D, PERF_COUNT_HW_CACHE_OP_READ,
            PERF_COUNT_HW_CACHE_RESULT_ACCESS, "l1d_loads"),
    PMU_HWC(PERF_COUNT_HW_CACHE_L1D, PERF_COUNT_HW_CACHE_OP_READ,
            PERF_COUNT_HW_CACHE_RESULT_MISS,   "l1d_load_misses"),
};
static const pmu_event_desc ROUND1[] = {          /* cache + branches */
    PMU_HW(PERF_COUNT_HW_CACHE_REFERENCES,    "cache_references"),
    PMU_HW(PERF_COUNT_HW_CACHE_MISSES,        "cache_misses"),
    PMU_HW(PERF_COUNT_HW_BRANCH_INSTRUCTIONS, "branch_instructions"),
    PMU_HW(PERF_COUNT_HW_BRANCH_MISSES,       "branch_misses"),
};
static const pmu_event_desc ROUND2[] = {          /* TLB + Zen raw */
    PMU_HWC(PERF_COUNT_HW_CACHE_DTLB, PERF_COUNT_HW_CACHE_OP_READ,
            PERF_COUNT_HW_CACHE_RESULT_MISS, "dtlb_read_miss"),
    PMU_RAW(0xFF60, "l2_request_g1_all"),          /* Zen l2_request_g1 all umasks */
    PMU_RAW(0x0076, "retired_ops"),                /* Zen ex_ret_ops */
};
static const pmu_event_desc *const ROUNDS[]    = { ROUND0, ROUND1, ROUND2 };
static const size_t              ROUND_LEN[]   = { 4, 4, 3 };
#define N_ROUNDS (sizeof ROUNDS / sizeof ROUNDS[0])

/* Observations collected across rounds, for the derived ratios. */
#define OBS_MAX 32
static struct { const char *name; uint64_t val; } g_obs[OBS_MAX];
static int g_obs_n;

static void obs_set(const char *name, uint64_t v)
{
    for (int i = 0; i < g_obs_n; i++)
        if (!strcmp(g_obs[i].name, name)) { g_obs[i].val = v; return; }
    if (g_obs_n < OBS_MAX) {
        g_obs[g_obs_n].name = name;
        g_obs[g_obs_n].val = v;
        g_obs_n++;
    }
}

static uint64_t obs_get(const char *name)
{
    for (int i = 0; i < g_obs_n; i++)
        if (!strcmp(g_obs[i].name, name))
            return g_obs[i].val;
    return 0;
}

/* ------------------------------------------------------------------ options */
typedef struct {
    size_t warmup_max;
    size_t iters_max;
    size_t iters_min;
    double budget_s;      /* per (impl,op,size) timed-loop budget */
    double warm_budget_s;
    int    verify;
    int    quick;
    int    thp;
    const char *impl_filter;
    const char *mode;
    const char *arg1, *arg2, *arg3;
    unsigned reps;        /* >= 3 per the measurement rules */
} opts_t;

static opts_t g_opt = {
    .warmup_max = 10, .iters_max = 100, .iters_min = 3,
    .budget_s = 1.0, .warm_budget_s = 0.5,
    .verify = 1, .quick = 0, .thp = 0, .impl_filter = NULL,
    .mode = "all", .arg1 = NULL, .arg2 = NULL, .arg3 = NULL,
    .reps = 3,
};

/* ------------------------------------------------------------------ timing */
static double g_timer_overhead_ns = 0.0;
static double g_tsc_ghz = 0.0;         /* invariant TSC, calibrated */
static double g_core_ghz = 0.0;        /* PMU-cycles/TSC-ticks * tsc_ghz */

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* Cost of a back-to-back pair of ns_now() calls, subtracted from samples. */
static double calib_timer_overhead(void)
{
    double s[1001];
    for (int i = 0; i < 1001; i++) {
        double a = ns_now();
        double b = ns_now();
        s[i] = b - a;
    }
    qsort(s, 1001, sizeof s[0], cmp_double);
    return s[500];                      /* median */
}

static double calib_tsc_ghz(void)
{
    double best = 0.0;
    for (int r = 0; r < 5; r++) {
        uint64_t t0 = rdtsc_now();
        double   n0 = ns_now();
        /* ~20 ms of wall time */
        double target = n0 + 20e6;
        while (ns_now() < target)
            ;
        double   n1 = ns_now();
        uint64_t t1 = rdtsc_now();
        double ghz = (double)(t1 - t0) / (n1 - n0);
        if (ghz > best)
            best = ghz;                 /* fastest = no preemption inside */
    }
    return best;
}

/* Core clock from the PMU: count core cycles over a TSC-measured wall interval.
 * Reported for information only; cycles/byte always uses raw PMU cycles.
 * Raw inputs are printed so the ratio is auditable rather than asserted. */
static double g_calib_cyc = 0, g_calib_tsc = 0, g_calib_wall_ns = 0;

static double calib_core_ghz(void)
{
    pmu_t p;
    if (!pmu_init(&p, EV_MINI, 1))
        return 0.0;

    volatile uint64_t acc = 0;
    double w0 = ns_now();
    pmu_start(&p);
    uint64_t t0 = rdtsc_now();
    for (uint64_t i = 0; i < 200000000ull; i++) {
        acc += i;
        BARRIER();
    }
    uint64_t t1 = rdtsc_now();
    pmu_stop(&p);
    double w1 = ns_now();

    uint64_t cyc = pmu_delta(&p, "cycles");
    pmu_report_multiplex(&p, "calib");
    pmu_close(&p);
    g_calib_cyc = (double)cyc;
    g_calib_tsc = (double)(t1 - t0);
    g_calib_wall_ns = w1 - w0;
    if (!cyc || t1 == t0)
        return 0.0;
    return g_tsc_ghz * (double)cyc / (double)(t1 - t0);
}

/* ------------------------------------------------------------------ buffers */
static void free_bufs(void)
{
    free(g_src); free(g_dst); free(g_ref);
    g_src = g_dst = g_ref = NULL;
}

static int alloc_once(size_t bytes, int need_ref)
{
    void *a = NULL, *b = NULL, *c = NULL;
    if (posix_memalign(&a, CACHELINE, bytes + 128) != 0) return 0;
    if (posix_memalign(&b, CACHELINE, bytes + 128) != 0) { free(a); return 0; }
    if (need_ref && posix_memalign(&c, CACHELINE, bytes + 128) != 0) {
        free(a); free(b); return 0;
    }
    g_src = a; g_dst = b; g_ref = c;
    return 1;
}

static int setup_buffers(int need_ref)
{
    size_t want = MAX_SIZE;
    while (want >= 4096) {
        if (alloc_once(want, need_ref)) {
            g_max_size = want;
            /* First touch: page faults must never land inside a measurement. */
            memset(g_src, 0xA5, want);
            memset(g_dst, 0x5A, want);
            if (g_ref) memset(g_ref, 0, want);
            if (g_opt.thp) {
                madvise(g_src, want, MADV_HUGEPAGE);
                madvise(g_dst, want, MADV_HUGEPAGE);
                if (g_ref) madvise(g_ref, want, MADV_HUGEPAGE);
            }
            return 1;
        }
        want /= 2;
    }
    return 0;
}

/* ------------------------------------------------------------------ measurement */
typedef struct {
    double med_ns, min_ns, p99_ns, gbps, gbps_wall, cpb, tsc_cpb;
    unsigned batch, iters;
    uint64_t cycles, tsc_ticks;
    int cpu_a, cpu_b, migrated;   /* sched_getcpu() sampled INSIDE the PMU window */
} res_t;

static unsigned pick_batch(size_t n)
{
    if (n == 0) return 1;
    size_t b = 65536 / n;              /* ~64 KiB of copy traffic per sample */
    if (b < 1) b = 1;
    if (b > 1024) b = 1024;
    return (unsigned)b;
}

static void run_case(const impl_t *im, int op, void *dst, const void *src,
                     size_t n, res_t *out)
{
    static double samples[MAX_SAMPLES];
    pmu_t p;

    memset(out, 0, sizeof *out);

    copy_fn volatile cfn = im->copy;
    set_fn  volatile sfn = im->set;
    if ((op == 0 && cfn == NULL) || (op == 1 && sfn == NULL)) {
        out->batch = 0;
        return;
    }

    unsigned batch = pick_batch(n);
    unsigned ni = (unsigned)(g_opt.iters_max > MAX_SAMPLES ? MAX_SAMPLES
                                                          : g_opt.iters_max);

    /* ---- warmup (budget-aware: a 256 MiB scalar copy is ~0.2 s per call) */
    {
        double t0 = ns_now();
        size_t warm = 0;
        while (warm < g_opt.warmup_max) {
            for (unsigned k = 0; k < batch; k++) {
                if (op == 0) cfn(dst, src, n);
                else         sfn(dst, (int)k, n);
            }
            warm++;
            if (ns_now() - t0 >= g_opt.warm_budget_s * 1e9)
                break;
        }
    }

    /* ---- timed loop: PMU window and wall window cover the same work ----- */
    if (!pmu_init(&p, EV_MINI, 1))
        fprintf(stderr, "harness: PMU unavailable, cycles/byte will be -1\n");

    pmu_start(&p);
    out->cpu_a = sched_getcpu();          /* pinning proof: sampled inside the region */
    uint64_t tsc0 = rdtsc_now();
    double   n0   = ns_now();

    unsigned iters = 0;
    while (iters < ni) {
        double a = ns_now();
        for (unsigned k = 0; k < batch; k++) {
            if (op == 0) cfn(dst, src, n);
            else         sfn(dst, (int)k, n);
        }
        double b = ns_now();
        double per = (b - a - g_timer_overhead_ns) / (double)batch;
        samples[iters++] = per > 0.0 ? per : 0.0;
        /* budget_s is seconds; b - n0 is nanoseconds. Getting this wrong made
         * every case stop after iters_min samples in the first validation run. */
        if (b - n0 >= g_opt.budget_s * 1e9 && iters >= g_opt.iters_min)
            break;
    }

    double   n1   = ns_now();
    uint64_t tsc1 = rdtsc_now();
    out->cpu_b = sched_getcpu();          /* still inside the PMU window */
    out->migrated = (out->cpu_a != out->cpu_b);
    pmu_stop(&p);

    out->cycles    = pmu_delta(&p, "cycles");
    out->tsc_ticks = tsc1 - tsc0;
    pmu_report_multiplex(&p, "harness");
    pmu_close(&p);

    /* Keep the destination alive without taxing the timed region. n == 0 is
     * legal for memcpy/memset, so never index the buffers in that case. */
    if (n) {
        g_sink ^= ((volatile unsigned char *)dst)[0];
        g_sink ^= ((const volatile unsigned char *)src)[0];
        g_sink ^= ((volatile unsigned char *)dst)[n - 1];
    } else {
        g_sink ^= (uint64_t)(uintptr_t)dst;
    }

    if (iters == 0) iters = 1;
    qsort(samples, iters, sizeof samples[0], cmp_double);

    out->iters = iters;
    out->batch = batch;
    out->med_ns = samples[iters / 2];
    out->min_ns = samples[0];
    {
        size_t rank = (size_t)(0.99 * (double)iters + 0.999999);
        if (rank == 0) rank = 1;
        if (rank > iters) rank = iters;
        out->p99_ns = samples[rank - 1];
    }
    double total_bytes = (double)n * (double)batch * (double)iters;
    out->gbps      = (double)n / out->med_ns;             /* bytes/ns == GB/s (10^9) */
    out->gbps_wall = total_bytes / (n1 - n0);
    out->cpb       = out->cycles ? (double)out->cycles / total_bytes : -1.0;
    out->tsc_cpb   = (double)out->tsc_ticks / total_bytes;
}

/* ------------------------------------------------------------------ verify
 * Rules implemented here:
 *   - the reference is glibc, i.e. the thing we are trying to beat, not our
 *     own scalar code;
 *   - exhaustive 0..4096 bytes, because off-by-one and tail bugs live at small
 *     and odd sizes;
 *   - randomised src/dst offsets, because unaligned entry paths are exactly
 *     where vector kernels break;
 *   - canary bytes on BOTH sides of dst: a vector kernel that writes a whole
 *     32B block past the end silently corrupts memory, and every benchmark
 *     after that is a lie;
 *   - ref_scalar is crossed against glibc first, so a harness bug cannot be
 *     mistaken for a kernel bug.
 */
#define VMAXN    4096u
#define VCAN     64u
#define VCAN_BYTE 0xA7u

static unsigned char vsrc[VMAXN + 2 * VCAN + 128] ALIGNED(64);
static unsigned char vdst[VMAXN + 2 * VCAN + 128] ALIGNED(64);
static unsigned char vexp[VMAXN + 2 * VCAN + 128] ALIGNED(64);
static unsigned char vref[VMAXN + 2 * VCAN + 128] ALIGNED(64);

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;   /* fixed seed: reproducible */

static size_t rng_offset(void)
{
    uint64_t x = g_rng;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    g_rng = x;
    return (size_t)((x * 0x2545F4914F6CDD1Dull) & 63u);
}

static int fail(const impl_t *im, int op, size_t n, size_t so, size_t dof,
                const char *what, size_t at, unsigned char exp,
                unsigned char got)
{
    fprintf(stderr, "VERIFY FAIL %s %s n=%zu src_off=%zu dst_off=%zu %s at=%zu "
                    "exp=%02x got=%02x\n",
            im->name, op ? "memset" : "memcpy", n, so, dof, what, at, exp, got);
    return 1;
}

/* Canary regions: [d-VCAN, d) and [d+n, d+n+VCAN). */
static int canary_check(const impl_t *im, int op, size_t n, size_t so, size_t dof,
                        const unsigned char *d)
{
    for (size_t i = 1; i <= VCAN; i++) {
        if (d[-(ptrdiff_t)i] != VCAN_BYTE)
            return fail(im, op, n, so, dof, "UNDERWRITE", i,
                        VCAN_BYTE, d[-(ptrdiff_t)i]);
        if (d[n + i - 1] != VCAN_BYTE)
            return fail(im, op, n, so, dof, "OVERWRITE", n + i - 1,
                        VCAN_BYTE, d[n + i - 1]);
    }
    return 0;
}

static int verify_small(const impl_t *im)
{
    for (size_t n = 0; n <= VMAXN; n++) {
        for (int t = 0; t < 3; t++) {
            size_t so = rng_offset(), dof = rng_offset();
            unsigned char *s = vsrc + VCAN + so;
            unsigned char *d = vdst + VCAN + dof;
            for (size_t i = 0; i < n; i++)
                s[i] = (unsigned char)((n * 31u + i * 7u + 13u) & 0xffu);

            if (im->copy) {
                memcpy(vexp, s, n);         /* glibc expectation */
                memcpy_ref(vref, s, n);     /* independent golden model */
                if (n && memcmp(vref, vexp, n))
                    return fail(im, 0, n, so, dof, "HARNESS-BUG(glibc!=ref)",
                                0, vexp[0], vref[0]);
                memset(d - VCAN, VCAN_BYTE, VCAN);
                memset(d + n, VCAN_BYTE, VCAN);
                memset(d, 0xEE, n);
                im->copy(d, s, n);
                for (size_t i = 0; i < n; i++)
                    if (d[i] != vexp[i])
                        return fail(im, 0, n, so, dof, "BYTE", i, vexp[i], d[i]);
                if (canary_check(im, 0, n, so, dof, d)) return 1;
            }
            if (im->set) {
                int val = 0x5A;
                memset(vexp, 0, n);
                memset(vexp, val, n);        /* glibc expectation */
                memset_ref(vref, val, n);
                if (n && memcmp(vref, vexp, n))
                    return fail(im, 1, n, so, dof, "HARNESS-BUG(glibc!=ref)",
                                0, vexp[0], vref[0]);
                memset(d - VCAN, VCAN_BYTE, VCAN);
                memset(d + n, VCAN_BYTE, VCAN);
                memset(d, 0x00, n);
                im->set(d, val, n);
                for (size_t i = 0; i < n; i++)
                    if (d[i] != vexp[i])
                        return fail(im, 1, n, so, dof, "BYTE", i, vexp[i], d[i]);
                if (canary_check(im, 1, n, so, dof, d)) return 1;
            }
        }
    }
    return 0;
}

static int verify_large(const impl_t *im)
{
    const size_t *sizes = g_opt.quick ? g_sizes_quick : g_sizes;
    size_t nsizes = g_opt.quick ? sizeof g_sizes_quick / sizeof g_sizes_quick[0]
                                : sizeof g_sizes / sizeof g_sizes[0];
    for (size_t si = 0; si < nsizes; si++) {
        size_t n = sizes[si];
        if (n > g_max_size) continue;
        /* Keep 64B of slack on both sides of dst for the canary when the
         * allocation allows it; otherwise fall back to a plain compare. */
        int use_canary = (n + 2 * VCAN + 64 <= g_max_size);
        size_t so = rng_offset(), dof = rng_offset();
        unsigned char *s = use_canary ? g_src + VCAN + so : g_src;
        unsigned char *d = use_canary ? g_dst + VCAN + dof : g_dst;
        unsigned char *e = g_ref;
        for (size_t i = 0; i < n; i++)
            s[i] = (unsigned char)((i * 7u + 13u) & 0xffu);

        if (im->copy) {
            memcpy(e, s, n);
            memset(d, 0xEE, n);
            if (use_canary) {
                memset(d - VCAN, VCAN_BYTE, VCAN);
                memset(d + n, VCAN_BYTE, VCAN);
            }
            im->copy(d, s, n);
            for (size_t i = 0; i < n; i++)
                if (d[i] != e[i])
                    return fail(im, 0, n, so, dof, "BYTE", i, e[i], d[i]);
            if (use_canary && canary_check(im, 0, n, so, dof, d)) return 1;
        }
        if (im->set) {
            int val = 0x5A;
            memset(e, val, n);
            memset(d, 0x00, n);
            if (use_canary) {
                memset(d - VCAN, VCAN_BYTE, VCAN);
                memset(d + n, VCAN_BYTE, VCAN);
            }
            im->set(d, val, n);
            for (size_t i = 0; i < n; i++)
                if (d[i] != e[i])
                    return fail(im, 1, n, so, dof, "BYTE", i, e[i], d[i]);
            if (use_canary && canary_check(im, 1, n, so, dof, d)) return 1;
        }
    }
    return 0;
}

static int verify_impl(const impl_t *im)
{
    int bad = verify_small(im);
    if (bad) return bad;
    bad = verify_large(im);
    if (!bad)
        fprintf(stderr, "verify: %s OK (glibc reference, n=0..%u random offsets,"
                        " canary %uB both sides, plus size list)\n",
                im->name, VMAXN, VCAN);
    return bad;
}

/* ---- rep aggregation -----------------------------------------------------
 * Rule: >= 3 repetitions per measurement, median across reps, and a > 5%
 * spread on the median must be flagged (and never dressed up).
 */
#define MAX_REPS 32

typedef struct {
    double med_ns, min_ns, p99_ns, gbps, gbps_wall, cpb, tsc_cpb, spread_pct;
    unsigned batch, iters, reps;
    uint64_t cycles;
    int cpu, migrated, flagged;
} agg_t;

/* ------------------------------------------------------------------ output */
static void csv_header(void)
{
    printf("impl,target,op,size_bytes,batch,iters,reps,median_ns,min_ns,p99_ns,"
           "gbps,gbps_wall,cycles_per_byte,tsc_ticks_per_byte,rep_spread_pct,"
           "flag,cpu,migrated,tsc_ghz,core_ghz,cycles_total\n");
}

static void csv_row(const impl_t *im, const char *op, size_t n, const agg_t *a)
{
    printf("%s,%s,%s,%zu,%u,%u,%u,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.3f,%d,%d,%d,%.5f,%.5f,%llu\n",
           im->name, TARGET_NAME, op, n, a->batch, a->iters, a->reps,
           a->med_ns, a->min_ns, a->p99_ns, a->gbps, a->gbps_wall,
           a->cpb, a->tsc_cpb, a->spread_pct, a->flagged, a->cpu, a->migrated,
           g_tsc_ghz, g_core_ghz, (unsigned long long)a->cycles);
}

static double med_d(double *v, unsigned n)
{
    if (!n) return 0.0;
    qsort(v, n, sizeof v[0], cmp_double);
    return v[n / 2];
}

static int cmp_u64v(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static void measure_reps(const impl_t *im, int op, size_t n, agg_t *o)
{
    double meds[MAX_REPS], gbps[MAX_REPS], cpbs[MAX_REPS], tscs[MAX_REPS];
    double p99s[MAX_REPS], walls[MAX_REPS];
    uint64_t cycs[MAX_REPS];
    unsigned reps = g_opt.reps ? g_opt.reps : 1;
    unsigned iters = 0, batch = 0;
    double mins = 0.0;
    int cpu = -1, migrated = 0;

    memset(o, 0, sizeof *o);
    if (reps > MAX_REPS) reps = MAX_REPS;

    for (unsigned r = 0; r < reps; r++) {
        res_t res;
        run_case(im, op, g_dst, g_src, n, &res);
        if (res.batch == 0) { o->batch = 0; return; }   /* impl lacks this op */
        meds[r] = res.med_ns;
        gbps[r] = res.gbps;
        cpbs[r] = res.cpb;
        tscs[r] = res.tsc_cpb;
        p99s[r] = res.p99_ns;
        walls[r] = res.gbps_wall;
        cycs[r] = res.cycles;
        if (r == 0 || res.min_ns < mins) mins = res.min_ns;
        if (res.iters > iters) iters = res.iters;
        batch = res.batch;
        cpu = res.cpu_b;
        if (res.migrated) migrated = 1;
    }

    o->reps = reps;
    o->iters = iters;
    o->batch = batch;
    o->min_ns = mins;
    o->med_ns = med_d(meds, reps);
    o->p99_ns = med_d(p99s, reps);
    o->cpb = med_d(cpbs, reps);
    o->tsc_cpb = med_d(tscs, reps);
    o->gbps_wall = med_d(walls, reps);
    o->gbps = med_d(gbps, reps);
    qsort(cycs, reps, sizeof cycs[0], cmp_u64v);
    o->cycles = cycs[reps / 2];
    /* gbps[] is sorted now (median call sorted it) */
    o->spread_pct = o->gbps > 0.0
                  ? 100.0 * (gbps[reps - 1] - gbps[0]) / o->gbps : 0.0;
    o->cpu = cpu;
    o->migrated = migrated;
    o->flagged = (o->spread_pct > 5.0);
}

static int want_impl(const impl_t *im)
{
    return !g_opt.impl_filter || strcmp(g_opt.impl_filter, im->name) == 0;
}

static const impl_t *find_impl(const char *name)
{
    for (size_t i = 0; i < g_impl_count; i++)
        if (strcmp(g_impls[i].name, name) == 0)
            return &g_impls[i];
    return NULL;
}

static int parse_op(const char *s)
{
    if (!strcmp(s, "memcpy") || !strcmp(s, "copy")) return 0;
    if (!strcmp(s, "memset") || !strcmp(s, "set"))  return 1;
    return -1;
}

static const char *op_name(int op) { return op == 0 ? "memcpy" : "memset"; }

/* ------------------------------------------------------------------ modes */
static int mode_all(void)
{
    const size_t *sizes = g_opt.quick ? g_sizes_quick : g_sizes;
    size_t nsizes = g_opt.quick ? sizeof g_sizes_quick / sizeof g_sizes_quick[0]
                                : sizeof g_sizes / sizeof g_sizes[0];

    fprintf(stderr, "harness: target=%s clock: tsc=%.5f GHz core=%.5f GHz "
                    "(ratio %.4f) timer_overhead=%.2f ns max_size=%zu MiB\n",
            TARGET_NAME, g_tsc_ghz, g_core_ghz,
            g_tsc_ghz > 0 ? g_core_ghz / g_tsc_ghz : 0.0,
            g_timer_overhead_ns, g_max_size >> 20);

    csv_header();
    unsigned nflagged = 0, nmigrated = 0, nrows = 0;
    for (size_t ii = 0; ii < g_impl_count; ii++) {
        const impl_t *im = &g_impls[ii];
        if (!want_impl(im))
            continue;
        if (g_opt.verify && verify_impl(im))
            return 1;                       /* no benchmarking of broken code */
        for (int op = 0; op < 2; op++) {
            if (op == 0 && !im->copy) continue;
            if (op == 1 && !im->set)  continue;
            for (size_t si = 0; si < nsizes; si++) {
                size_t n = sizes[si];
                if (n > g_max_size) continue;
                agg_t a;
                measure_reps(im, op, n, &a);
                if (a.batch == 0) continue;
                csv_row(im, op_name(op), n, &a);
                nrows++;
                if (a.flagged) {
                    nflagged++;
                    fprintf(stderr, "VARIANCE FLAG %s %s n=%zu reps=%u "
                                    "spread=%.2f%% on median gbps=%.4f "
                                    "(>5%% -> remeasure or treat as noisy)\n",
                            im->name, op_name(op), n, a.reps, a.spread_pct,
                            a.gbps);
                }
                if (a.migrated) {
                    nmigrated++;
                    fprintf(stderr, "MIGRATION %s %s n=%zu cpu %d -> %d\n",
                            im->name, op_name(op), n, a.cpu, a.cpu);
                }
                fflush(stdout);
            }
        }
    }
    fprintf(stderr, "harness: %u rows, %u variance-flagged, %u migrated\n",
            nrows, nflagged, nmigrated);
    return 0;
}

static int mode_one(int full_pmu)
{
    const impl_t *im = find_impl(g_opt.arg1);
    if (!im) { fprintf(stderr, "no such impl: %s\n", g_opt.arg1); return 2; }
    int op = parse_op(g_opt.arg2);
    if (op < 0) { fprintf(stderr, "op must be memcpy|memset\n"); return 2; }
    size_t n = (size_t)strtoull(g_opt.arg3, NULL, 0);
    if (n == 0) { fprintf(stderr, "bad size\n"); return 2; }
    if (n > g_max_size) {
        fprintf(stderr, "size %zu exceeds max buffer %zu\n", n, g_max_size);
        return 2;
    }

    if (!full_pmu) {
        /* --one is the single-shot form used by scripts/run_perf.sh; keep it to
         * one rep so the caller's runtime stays predictable. */
        unsigned saved = g_opt.reps;
        g_opt.reps = 1;
        agg_t a;
        measure_reps(im, op, n, &a);
        g_opt.reps = saved;
        csv_header();
        csv_row(im, op_name(op), n, &a);
        return 0;
    }

    if ((op == 0 && !im->copy) || (op == 1 && !im->set)) {
        fprintf(stderr, "impl %s has no %s side\n", im->name, op_name(op));
        return 2;
    }

    /* One measurement per round; each round owns a <= PMU_MAX_GROUP group so
     * it is actually scheduled. Rounds are separate runs of identical work, so
     * ratios built from different rounds stay comparable. */
    unsigned batch = pick_batch(n);
    double  bytes  = (double)n * batch;
    double  wall_ns_total = 0.0;

    printf("impl=%s target=%s op=%s size_bytes=%zu batch=%u\n",
           im->name, TARGET_NAME, op_name(op), n, batch);

    for (size_t r = 0; r < N_ROUNDS; r++) {
        pmu_t p;
        if (!pmu_init(&p, ROUNDS[r], ROUND_LEN[r])) {
            fprintf(stderr, "pmu: round %zu unavailable\n", r);
            continue;
        }
        for (int w = 0; w < 5; w++)
            for (unsigned k = 0; k < batch; k++) {
                if (op == 0) im->copy(g_dst, g_src, n);
                else         im->set(g_dst, (int)k, n);
            }
        pmu_start(&p);
        double a = ns_now();
        for (unsigned k = 0; k < batch; k++) {
            if (op == 0) im->copy(g_dst, g_src, n);
            else         im->set(g_dst, (int)k, n);
        }
        double b = ns_now();
        pmu_stop(&p);
        wall_ns_total += b - a;

        printf("round%zu events_opened=%d reliable=%d running_pct=%.3f "
               "wall_ns=%.0f gbps=%.4f\n",
               r, p.n, p.reliable,
               p.enabled ? 100.0 * (double)p.running / (double)p.enabled : 0.0,
               b - a, bytes / (b - a));
        for (int i = 0; i < p.n; i++) {
            uint64_t v = pmu_delta(&p, p.name[i]);
            printf("counter %-20s %llu  per_byte=%.6f\n", p.name[i],
                   (unsigned long long)v, (double)v / bytes);
            obs_set(p.name[i], v);
        }
        pmu_report_multiplex(&p, "pmu");
        pmu_close(&p);
    }

    uint64_t cyc = obs_get("cycles");
    uint64_t ins = obs_get("instructions");
    uint64_t cr  = obs_get("cache_references");
    uint64_t cm  = obs_get("cache_misses");
    uint64_t bi  = obs_get("branch_instructions");
    uint64_t bm  = obs_get("branch_misses");
    uint64_t l1  = obs_get("l1d_loads");
    uint64_t l1m = obs_get("l1d_load_misses");
    uint64_t dt  = obs_get("dtlb_read_miss");
    uint64_t l2  = obs_get("l2_request_g1_all");
    uint64_t rp  = obs_get("retired_ops");

    printf("wall_ns_total=%.1f  gbps_wall=%.4f\n", wall_ns_total,
           bytes * (double)N_ROUNDS / wall_ns_total);
    printf("derived ipc=%.4f cycles_per_byte=%.4f cyc_per_op=%.2f\n",
           cyc ? (double)ins / (double)cyc : 0.0,
           cyc ? (double)cyc / bytes : -1.0,
           cyc ? (double)cyc / (double)batch : -1.0);
    if (l1)  printf("derived l1d_load_miss_rate=%.6f\n", (double)l1m / (double)l1);
    if (cr)  printf("derived cache_miss_rate=%.6f (generic l2-ish)\n", (double)cm / (double)cr);
    if (bi)  printf("derived branch_miss_rate=%.6f\n", (double)bm / (double)bi);
    printf("derived dtlb_miss_per_byte=%.6f  l2_req_per_byte=%.6f  "
           "retired_ops_per_cycle=%.4f\n",
           (double)dt / bytes, (double)l2 / bytes,
           cyc ? (double)rp / (double)cyc : 0.0);
    return 0;
}

static int mode_align(int twod, size_t n, int step)
{
    fprintf(stderr, "align%d: size=%zu step=%d\n", twod ? 2 : 1, n, step);
    printf("impl,target,op,size_bytes,src_off,dst_off,median_ns,gbps,"
           "cycles_per_byte\n");
    for (size_t ii = 0; ii < g_impl_count; ii++) {
        const impl_t *im = &g_impls[ii];
        if (!want_impl(im) || !im->copy) continue;
        if (twod) {
            for (int so = 0; so < 64; so += step) {
                for (int dof = 0; dof < 64; dof += step) {
                    res_t r;
                    run_case(im, 0, g_dst + dof, g_src + so, n, &r);
                    printf("%s,%s,memcpy,%zu,%d,%d,%.4f,%.4f,%.4f\n",
                           im->name, TARGET_NAME, n, so, dof,
                           r.med_ns, r.gbps, r.cpb);
                }
            }
        } else {
            for (int off = 0; off < 64; off++) {
                res_t r;
                run_case(im, 0, g_dst, g_src + off, n, &r);
                printf("%s,%s,memcpy,%zu,%d,0,%.4f,%.4f,%.4f\n",
                       im->name, TARGET_NAME, n, off, r.med_ns, r.gbps, r.cpb);
            }
            for (int off = 0; off < 64; off++) {
                res_t r;
                run_case(im, 0, g_dst + off, g_src, n, &r);
                printf("%s,%s,memcpy,%zu,0,%d,%.4f,%.4f,%.4f\n",
                       im->name, TARGET_NAME, n, off, r.med_ns, r.gbps, r.cpb);
            }
        }
        fflush(stdout);
    }
    return 0;
}

static void report_pinning(void)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof set, &set) == 0)
        fprintf(stderr, "harness: affinity covers %d cpu(s), running on cpu %d "
                        "-> use taskset -c N\n",
                CPU_COUNT(&set), sched_getcpu());
}

static void usage(void)
{
    fprintf(stderr,
        "usage: bench [mode] [options]\n"
        "  (default)                     CSV sweep: all impls x memcpy/memset x all sizes\n"
        "  --one <impl> <op> <size>      single CSV row\n"
        "  --pmu <impl> <op> <size>      full PMU counter set + derived ratios\n"
        "  --align [size]                src and dst offset sweeps 0..63\n"
        "  --align2d [size] [step]       src x dst offset grid\n"
        "  --calib                       clock-domain calibration only\n"
        "  --verify                      correctness pass only\n"
        "  --list                        list implementations\n"
        "options: --quick --iters N --warmup N --budget SEC --reps N (>=3) --thp\n"
        "         --no-verify --impl NAME\n");
}

int main(int argc, char **argv)
{
    const char *pos[3] = { NULL, NULL, NULL };
    int npos = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--quick"))      g_opt.quick = 1;
        else if (!strcmp(a, "--thp"))   g_opt.thp = 1;
        else if (!strcmp(a, "--no-verify")) g_opt.verify = 0;
        else if (!strcmp(a, "--iters") && i + 1 < argc)   g_opt.iters_max = (size_t)atoi(argv[++i]);
        else if (!strcmp(a, "--reps") && i + 1 < argc)    g_opt.reps = (unsigned)atoi(argv[++i]);
        else if (!strcmp(a, "--warmup") && i + 1 < argc)  g_opt.warmup_max = (size_t)atoi(argv[++i]);
        else if (!strcmp(a, "--budget") && i + 1 < argc)  g_opt.budget_s = atof(argv[++i]);
        else if (!strcmp(a, "--impl") && i + 1 < argc)    g_opt.impl_filter = argv[++i];
        else if (a[0] == '-' && a[1] == '-')              g_opt.mode = a;
        else if (npos < 3)                                pos[npos++] = a;
        else { usage(); return 2; }
    }
    g_opt.arg1 = pos[0]; g_opt.arg2 = pos[1]; g_opt.arg3 = pos[2];
    /* `bench <impl> <op> <size>` is shorthand for --one (used by run_perf.sh). */
    if (!strcmp(g_opt.mode, "all") && npos == 3)
        g_opt.mode = "--one";
    if (g_opt.iters_min > g_opt.iters_max) g_opt.iters_min = g_opt.iters_max;

    if (!strcmp(g_opt.mode, "--help")) { usage(); return 0; }

    if (!strcmp(g_opt.mode, "--list")) {
        printf("target=%s impls:\n", TARGET_NAME);
        for (size_t i = 0; i < g_impl_count; i++)
            printf("  %-12s copy=%s set=%s  %s\n", g_impls[i].name,
                   g_impls[i].copy ? "yes" : "no",
                   g_impls[i].set ? "yes" : "no",
                   g_impls[i].note ? g_impls[i].note : "");
        return 0;
    }

    report_pinning();

    int need_ref = g_opt.verify || !strcmp(g_opt.mode, "--verify");
    if (!setup_buffers(need_ref)) {
        fprintf(stderr, "harness: buffer allocation failed\n");
        return 1;
    }

    g_timer_overhead_ns = calib_timer_overhead();
    g_tsc_ghz = calib_tsc_ghz();
    g_core_ghz = calib_core_ghz();

    /* Guard rail: a PMU that is emulated, multiplexed, or read with the wrong
     * field layout produces a plausible-looking but wrong core/TSC ratio. A
     * real core sits within roughly 0.5x..2x of its invariant TSC. */
    if (g_core_ghz > 0.0) {
        double ratio = g_core_ghz / g_tsc_ghz;
        if (ratio < 0.5 || ratio > 2.0)
            fprintf(stderr,
                    "harness: WARNING core/tsc ratio %.4f is implausible "
                    "(cycles=%.0f tsc=%.0f wall_ns=%.0f) -> PMU counter suspect, "
                    "cycles/byte below is NOT trustworthy\n",
                    ratio, g_calib_cyc, g_calib_tsc, g_calib_wall_ns);
    }

    int rc = 0;
    if (!strcmp(g_opt.mode, "all"))            rc = mode_all();
    else if (!strcmp(g_opt.mode, "--one"))     rc = mode_one(0);
    else if (!strcmp(g_opt.mode, "--pmu"))     rc = mode_one(1);
    else if (!strcmp(g_opt.mode, "--align")) {
        size_t n = g_opt.arg1 ? (size_t)strtoull(g_opt.arg1, NULL, 0) : 4096;
        rc = mode_align(0, n, 1);
    }
    else if (!strcmp(g_opt.mode, "--align2d")) {
        size_t n = g_opt.arg1 ? (size_t)strtoull(g_opt.arg1, NULL, 0) : 4096;
        int step = g_opt.arg2 ? atoi(g_opt.arg2) : 8;
        if (step < 1 || step > 64) step = 8;
        rc = mode_align(1, n, step);
    }
    else if (!strcmp(g_opt.mode, "--calib")) {
        printf("tsc_ghz=%.5f core_ghz=%.5f core_to_tsc_ratio=%.4f "
               "timer_overhead_ns=%.3f\n",
               g_tsc_ghz, g_core_ghz,
               g_tsc_ghz > 0 ? g_core_ghz / g_tsc_ghz : 0.0,
               g_timer_overhead_ns);
        printf("calib_core_raw: pmu_cycles=%.0f tsc_ticks=%.0f wall_ns=%.0f "
               "cyc_from_wall=%.5f GHz tsc_from_wall=%.5f GHz\n",
               g_calib_cyc, g_calib_tsc, g_calib_wall_ns,
               g_calib_wall_ns > 0 ? g_calib_cyc / g_calib_wall_ns : 0.0,
               g_calib_wall_ns > 0 ? g_calib_tsc / g_calib_wall_ns : 0.0);
    }
    else if (!strcmp(g_opt.mode, "--verify")) {
        for (size_t i = 0; i < g_impl_count; i++)
            if (want_impl(&g_impls[i]) && verify_impl(&g_impls[i])) rc = 1;
    }
    else { usage(); rc = 2; }

    fprintf(stderr, "harness: cpu at exit=%d sink=%llx\n", sched_getcpu(),
            (unsigned long long)g_sink);
    free_bufs();
    return rc;
}
