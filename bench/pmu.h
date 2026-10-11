/* bench/pmu.h — our own perf_event_open(2) wrapper.
 *
 * The `perf` CLI is not installed on this host and no packages may be
 * installed, so the repo carries its own minimal profiler. perf_event_open
 * itself works here (verified in Phase 0), including raw AMD events.
 */
#ifndef AVX2_MEM_PMU_H
#define AVX2_MEM_PMU_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stddef.h>
#include <stdint.h>

#define PMU_MAX_EVENTS 16

typedef struct {
    const char        *name;
    unsigned           type;    /* PERF_TYPE_HARDWARE / _HW_CACHE / _RAW */
    unsigned long long config;
} pmu_event_desc;

typedef struct {
    int      n;                            /* events successfully opened */
    int      fd[PMU_MAX_EVENTS];
    const char *name[PMU_MAX_EVENTS];
    unsigned long long snap[PMU_MAX_EVENTS];   /* value at pmu_start() */
    unsigned long long val[PMU_MAX_EVENTS];    /* value at pmu_stop()  */
    int      lead;                         /* group leader fd */
    int      ok;                           /* 1 if the group is usable */
    int      multiplexed;                  /* 1 if time_running < time_enabled */
    int      reliable;                     /* 1 if the group actually ran ~100% */
    double   scale;                        /* enabled/running scaling factor */
    unsigned long long enabled, running;   /* from the last pmu_stop() */
} pmu_t;

/* Counter slots available on this host, measured in Phase 0/1: groups of <= 4
 * event run at 100%; groups of >= 5 collapse to ~0.3-1% running time and
 * return all zeros instead of multiplexing. Never exceed PMU_MAX_GROUP. */
#define PMU_MAX_GROUP 4

/* Open `events` as one group on the calling thread.
 * Returns 1 on success (at least one event, `p->ok == 1`), 0 if the whole
 * group is unavailable. Per-event failures are skipped and reported in
 * p->name[] only for the events that opened. */
int pmu_init(pmu_t *p, const pmu_event_desc *events, size_t count);

/* Reset+enable the group and snapshot counters. */
void pmu_start(pmu_t *p);

/* Disable the group, read it, and compute the multiplexing scale factor. */
void pmu_stop(pmu_t *p);

/* Scaled delta of a named event since pmu_start(); 0 if absent. */
uint64_t pmu_delta(const pmu_t *p, const char *name);

/* Index of a named event (or -1) / name of index i (or NULL). */
int      pmu_index(const pmu_t *p, const char *name);
const char *pmu_name_at(const pmu_t *p, int i);

void pmu_close(pmu_t *p);

/* Print the raw multiplexing ratio to stderr, prefixed by `tag`. */
void pmu_report_multiplex(const pmu_t *p, const char *tag);

#endif /* AVX2_MEM_PMU_H */
