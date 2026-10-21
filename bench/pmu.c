/* SPDX-License-Identifier: MIT */
/* bench/pmu.c — perf_event_open(2) group wrapper (our own mini-profiler).
 *
 * Linux perf_event read layout with PERF_FORMAT_GROUP |
 * PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING.
 * Straight from include/uapi/linux/perf_event.h, the times come BEFORE the
 * values when the GROUP format is selected:
 *
 *   struct {
 *       u64 nr;             // number of events in the group (leader included)
 *       u64 time_enabled;   // ns
 *       u64 time_running;   // ns
 *       struct { u64 value; } values[nr];   // [0] == group leader
 *   };
 *
 * This ordering was verified empirically on this host (.recon/pmudump.c): with
 * a 1-event group, buf[1] == buf[2] == wall clock in ns, and buf[3] is the
 * cycle count. Getting this wrong is not a loud failure — it silently returns
 * a *time* where a *count* is expected (here a 2.8x undercount of cycles/byte),
 * which is why the harness cross-checks cycles against TSC and wall clock.
 * Without PERF_FORMAT_GROUP the layout is the shorter {value, enabled, running}.
 *
 * If the group is wider than the hardware can schedule at once the kernel
 * time-multiplexes it, and every count must be scaled by enabled/running.
 * `p->scale` is that factor; pmu_delta() applies it. The ratio is reported so
 * no scaled number is ever presented as if it were exact.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>

#include "pmu.h"

static long perf_event_open_sys(struct perf_event_attr *attr, pid_t pid,
                                int cpu, int group_fd, unsigned long flags)
{
    return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

int pmu_init(pmu_t *p, const pmu_event_desc *events, size_t count)
{
    memset(p, 0, sizeof *p);
    p->lead = -1;
    p->scale = 1.0;

    if (count > PMU_MAX_EVENTS)
        count = PMU_MAX_EVENTS;

    for (size_t i = 0; i < count; i++) {
        struct perf_event_attr pe;
        memset(&pe, 0, sizeof pe);
        pe.type = events[i].type;
        pe.size = sizeof pe;
        pe.config = events[i].config;
        pe.disabled = 1;                 /* enabled explicitly by pmu_start() */
        pe.inherit = 0;
        pe.exclude_kernel = 1;           /* user-space benchmark only */
        pe.exclude_hv = 1;
        pe.read_format = PERF_FORMAT_GROUP
                       | PERF_FORMAT_TOTAL_TIME_ENABLED
                       | PERF_FORMAT_TOTAL_TIME_RUNNING;

        int group_fd = (p->n == 0) ? -1 : p->lead;
        long fd = perf_event_open_sys(&pe, 0, -1, group_fd, 0);
        if (fd < 0) {
            fprintf(stderr, "pmu: '%s' unavailable errno=%d (%s)\n",
                    events[i].name, errno, strerror(errno));
            continue;
        }
        if (p->n == 0)
            p->lead = (int)fd;
        p->fd[p->n] = (int)fd;
        p->name[p->n] = events[i].name;
        p->n++;
    }

    p->ok = (p->n > 0);
    return p->ok;
}

void pmu_start(pmu_t *p)
{
    if (!p->ok)
        return;
    if (ioctl(p->lead, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) < 0)
        fprintf(stderr, "pmu: RESET failed: %s\n", strerror(errno));
    if (ioctl(p->lead, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) < 0)
        fprintf(stderr, "pmu: ENABLE failed: %s\n", strerror(errno));
    for (int i = 0; i < p->n; i++)
        p->snap[i] = 0;                  /* reset zeroed every counter */
}

void pmu_stop(pmu_t *p)
{
    if (!p->ok)
        return;

    if (ioctl(p->lead, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) < 0)
        fprintf(stderr, "pmu: DISABLE failed: %s\n", strerror(errno));

    unsigned long long buf[3 + PMU_MAX_EVENTS];
    ssize_t got = read(p->lead, buf, sizeof buf);
    if (got < (ssize_t)(sizeof(unsigned long long) * 4)) {
        fprintf(stderr, "pmu: short read (%zd bytes)\n", got);
        p->ok = 0;
        return;
    }

    unsigned long long nr = buf[0];
    if (nr > (unsigned long long)p->n)
        nr = (unsigned long long)p->n;

    p->enabled = buf[1];
    p->running = buf[2];

    /* values start at index 3 (after nr, time_enabled, time_running) */
    for (unsigned long long i = 0; i < nr; i++)
        p->val[i] = buf[3 + i];

    p->multiplexed = (p->running < p->enabled);
    p->scale = (p->running > 0) ? (double)p->enabled / (double)p->running
                                : 1.0;
    /* A group that never actually runs must not be reported as a measurement.
     * Measured on this host: a >4-event group reports ~0.3-1% running time and
     * all-zero counts instead of multiplexing, so anything below 99.9% running
     * is treated as broken and its counts are discarded. */
    p->reliable = (p->enabled > 0 &&
                   p->running * 1000ull >= p->enabled * 999ull);
}

uint64_t pmu_delta(const pmu_t *p, const char *name)
{
    int i = pmu_index(p, name);
    if (i < 0)
        return 0;
    /* Refuse to return a number the hardware did not actually count. */
    if (!p->reliable)
        return 0;
    unsigned long long d = p->val[i] - p->snap[i];
    if (p->multiplexed)
        d = (unsigned long long)((double)d * p->scale);
    return d;
}

int pmu_index(const pmu_t *p, const char *name)
{
    for (int i = 0; i < p->n; i++)
        if (strcmp(p->name[i], name) == 0)
            return i;
    return -1;
}

const char *pmu_name_at(const pmu_t *p, int i)
{
    return (i >= 0 && i < p->n) ? p->name[i] : NULL;
}

void pmu_report_multiplex(const pmu_t *p, const char *tag)
{
    if (!p->ok)
        return;
    if (!p->reliable)
        fprintf(stderr, "%s: group UNRELIABLE enabled=%llu running=%llu "
                        "(%llu events; this host supports at most "
                        "%d per group) -> counts discarded\n",
                tag, p->enabled, p->running,
                (unsigned long long)p->n, PMU_MAX_GROUP);
    else if (p->multiplexed)
        fprintf(stderr, "%s: group MULTIPLEXED enabled=%llu running=%llu "
                        "scale=%.4f (counts are scaled -> treat as estimates)\n",
                tag, p->enabled, p->running, p->scale);
}

void pmu_close(pmu_t *p)
{
    for (int i = 0; i < p->n; i++)
        if (p->fd[i] >= 0)
            close(p->fd[i]);
    p->n = 0;
    p->ok = 0;
    p->lead = -1;
}
