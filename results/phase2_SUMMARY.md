# Phase 2 running summary — kernel ladder on Zen2-class silicon

Machine: 4-vCPU KVM guest, CPUID reports "AMD EPYC Processor" (family 17h model 01h)
but measured throughput is Zen 2 class (2x256b load/cyc, 1x256b store/cyc,
2x256b FADD/cyc), TSC 2.7947 GHz, L1d 32K, L2 512K, L3 8M, 7.7 GiB RAM shared.
Pin: `taskset -c 2`, CPU verified with `sched_getcpu()` inside the PMU window
(never migrated in any Phase 2 row: `migrated=0` in every CSV row).
Clock: `core_ghz` snapshot per run (2.99–3.14 GHz observed); `cycles_per_byte`
comes from the PMU cycles counter only, never from TSC.

## Ladder progress

| kernel | commit | verified | CSV |
|---|---|---|---|
| (reference) `ref_scalar` | `80fbc4e` | vs glibc, n=0..4096 | `phase2_naive_*.csv` |
| 1 `naive` | `2de3160` | **PASS** all 3 targets | `results/phase2_naive_{znver2,znver3,v3}.csv` |
| 2 `word` | — | not started | — |

Kernel 1 note: the harness/report infrastructure and the kernel landed in the
same commit (`2de3160`) because `git add -A` staged both. History was **not**
rewritten to split it (hard constraint). From kernel 2 on, staging is per-path,
so each kernel gets its own commit before it is benchmarked.

## Summary table

`vs_glibc` and `vs_prev` are median GB/s ratios; `best_size` is where that
kernel/target reaches its highest median GB/s. `cycles_per_byte` is PMU cycles.

| kernel | target | best_size | gbps @ best | cycles/byte @ best | gbps @ 4KB | c/B @ 4KB | vs_glibc (median, by class) | vs_prev (median, by class) | notes |
|---|---|---|---|---|---|---|---|---|---|
| naive | znver2 | 32 B | 2.49 | 1.386 | 2.18 | 1.459 | 0.044x..0.39x (**loses everywhere**) | 0.94x..1.61x (≈1.0, noise) | byte loop, 5 instr/byte |
| naive | znver3 | 64 B | 2.34 | 1.470 | 1.93 | 1.470 | 0.041x..0.23x (**loses everywhere**) | 0.88x..1.02x (≈1.0, noise) | identical codegen to znver2 |
| naive | v3 | 64 B | 1.83 | 1.694 | 2.02 | 1.520 | 0.039x..0.26x (**loses everywhere**) | 0.92x..1.07x (≈1.0, noise) | identical codegen to znver2 |
| glibc | znver2 | 4 KB | **69.57** | **0.0507** | 69.57 | 0.0507 | 1.000x | — | baseline; ifunc-resolved AVX path |

Naive vs glibc by size class (median GB/s, znver2): tiny 2.36 vs 6.05 (0.39x),
small 1.87 vs 42.59 (0.044x), mid 1.78 vs 44.34 (0.040x), big 1.62 vs 36.88
(0.044x), huge 1.75 vs 8.65 (0.20x). The measured gap ranges **2.6x (16 B) to
32x (4 KB)** on `min_ns`, which is the least noisy statistic.

## Incremental gain, kernel 1 vs previous

`naive` vs `ref_scalar` (the previous rung) is a deliberate control: the two
kernels are *the same algorithm in two different translation units*, and
`objdump` confirms identical instruction streams. Measured median ratios:
znver2 0.94–1.61x, znver3 0.88–1.02x, v3 0.92–1.07x — i.e. **1.0x within
noise**, which is exactly what a working harness must show. Two consequences:

* the delta measurement is not producing fake progress signals out of thin air;
* the per-class spread of a *null* comparison (0.88x–1.61x) bounds how large a
  claimed win has to be before it means anything at tiny sizes.

Honest verdict for kernel 1: **no optimization, no gain, and it must be beaten.**
Its value is that it establishes the floor and validates the pipeline.

## Codegen across the three targets (mandatory per kernel)

`scripts/codegen_diff.sh memcpy_naive src/memcpy_naive.c`

| target | instructions | distinct mnemonics | function bytes |
|---|---|---|---|
| znver2 | 11 | 10 | 33 |
| znver3 | 11 | 10 | 33 |
| v3 | 11 | 10 | 33 |

All three pairwise diffs are **empty** (identical instruction streams):
`results/codegen_diff_naive_znver2_vs_znver3.diff`,
`..._znver2_vs_v3.diff`, `..._znver3_vs_v3.diff`.
Instruction stream: `movzbl (%rsi,%rcx,1),%edi` + `mov %dil,(%rax,%rcx,1)` +
`inc`/`cmp`/`jne` per byte, with an early `test/je` for n == 0.

Hypothesis answered for rung 1: **the tuning target is irrelevant to a scalar
byte loop.** `-march=znver3` neither helps nor hurts; there is nothing for it to
vectorise. The three targets diverge only when the source contains a loop worth
vectorising (Phase 1 negative control: 6 vs 14 vs 10 vector instructions for the
same float loop). The target question will be answered for real from kernel 3
(SSE2) onward, where the source has intrinsics the compiler cannot “improve”
away.

## Measurement floor on this host (Phase 2 noise investigation)

**Status: kernel 2 is blocked pending your decision.** The 12/12 variance-flagged
result triggered a four-step noise investigation. Raw numbers, instrumented by
`--dump-reps`, `--noise` and `scripts/noise_floor.sh`.

### Step 1 — per-rep values, two impls interleaved in ONE process (64 B, reps=5)

| rep | ref_scalar `med_ns` | ref_scalar `min_ns` | naive `med_ns` | naive `min_ns` |
|--:|--:|--:|--:|--:|
| 0 | 32.92 | 29.72 | 43.88 | 33.54 |
| 1 | 33.10 | 25.73 | 34.23 | 25.72 |
| 2 | 31.34 | 25.72 | 33.75 | 30.38 |
| 3 | 33.39 | 25.72 | 35.06 | 25.73 |
| 4 | 41.22 | 29.60 | 39.47 | 28.13 |

Medians differ 6% between two kernels that are *byte-identical*; the `min_ns`
values hit the same 25.72 ns floor in both. So the spread is **not**
cross-process noise: it is inside a single process, interleaved.

### Step 2 — noise floor: same impl, same size, 20 runs back-to-back

| size | median-of-medians spread | median-of-mins spread | null-kernel overhead |
|--:|--:|--:|--:|
| 64 B | **37.7%** | 32.1% | 1.71 ns = 4.8% of impl |
| 4 KB | **45.9%** | 20.2% | 1.25 ns = 0.06% |
| 64 KB | **30.4%** | 19.7% | below 40 ns timer floor |
| 1 MB | **25.4%** | 35.5% | below timer floor |

More samples do **not** fix it (64 B medians: 40.7% at `--iters 100`, 96.5% at
1000, 31.1% at 4000) — more samples just draw more of the interference
population. The global minimum over all runs, however, is reproducible to
**0.04%** across independent experiments (25.7217 / 25.7227 / 25.7324 / 25.7227 ns
at 64 B in four different runs).

### Step 3 — host contention

`/proc/stat` `steal` field is **0 in every sample** (cpu2 and aggregate) and PSI
shows `some avg10` 1.0–2.0%, `full` 0.00; loadavg 1.14–2.07 on 4 vCPUs with
`2/293` runnable. The guest's accounting therefore **does not expose** the
interference, and PSI does not explain a 25–45% median spread. Across three
measurements 60 s apart, medians drifted (spread 9.7% / 24.4% / 42.0%) while
global mins moved only 26.5/27.3/26.5 µs — i.e. **slow temporal drift, not
random jitter**.

### Step 4 — harness overhead, isolated with a null kernel

`--noise` measures an empty kernel through the identical call path, batch loop
and timer:

* call + loop + address setup = **1.71 ns per call** at 64 B (4.8% of the
  measured 35.8 ns) — not the source of the spread;
* timer overhead = 40 ns per sample, amortised to 0.039 ns/call at batch=1024;
* the null kernel's own medians still vary **20.1%** run-to-run while touching
  no memory at all ⇒ the jitter is imposed on the vCPU from outside, not
  produced by the memory system or the harness.

Additionally the **core clock itself drifts**: `--calib` measured 2.7468, 2.8837,
3.03, 3.11 and 3.14 GHz in different runs (PMU cycles ÷ wall seconds, so this is
not an IPC artefact). That is a 14% range and it is uncontrollable (no cpufreq
sysfs). Consequence: **ns/GB/s cannot be compared across time; the PMU
cycles/byte counter can.**

### Null-comparison band (the bar a real claim must clear)

Two provably identical kernels (`naive` vs `ref_scalar`, identical objdump
streams), interleaved, reps=9 — any "speedup" below these numbers is an artefact:

| size | global-min ratio | best-median ratio | best cycles/byte ratio |
|--:|--:|--:|--:|
| 16 B | +41.6% | +61.7% | +62.4% |
| 64 B | +0.2% | +26.4% | +11.4% |
| 128 B | +18.3% | +6.3% | +7.5% |
| 256 B | +31.8% | −1.7% | +4.1% |
| 512 B | **−4.4%** | −9.7% | −7.2% |
| 1 KB | **−3.8%** | −9.3% | −7.9% |
| 4 KB | **−2.9%** | −8.5% | −11.0% |

Two things fall out of this table. Below 512 B the band is 4–62% depending on
statistic, so **no kernel comparison at those sizes is resolvable**. From 512 B
up, the **global minimum is the tightest estimator (2.9–4.4%)** and, notably,
the sign is consistent within each size across all three statistics — an
alignment effect, not noise: identical source can differ ~10% purely from where
its loop lands.

### Live re-measurement (post-fix, same session, `bench_znver2`)

Re-run after the three harness fixes below, so every number here comes from the
current binary. Where it disagrees with the tables above, this supersedes them.

**Step 1 — prescribed command `--quick --impl naive --impl ref_scalar --reps 5`,
n=64, both impls in one process, interleaved.**

| rep | naive `med_ns` | naive `min_ns` | ref_scalar `med_ns` | ref_scalar `min_ns` |
|--:|--:|--:|--:|--:|
| 0 | 30.19 | 24.41 | 40.67 | 27.87 |
| 1 | 42.05 | 32.20 | 33.63 | 29.37 |
| 2 | 40.13 | 30.58 | 31.42 | 22.23 |
| 3 | 30.03 | 25.71 | 36.22 | 29.73 |
| 4 | 33.37 | 25.72 | 35.62 | 29.02 |

Medians 30.0–42.1 ns inside naive alone (40% within one process); the two
identical kernels sit 6.3% apart on median-of-medians and 9.8% apart on global
min. So the spread is **inside** a process: host contention or core state, not
cross-process drift.

**Step 2 — noise floor, 20 back-to-back runs, same code path.**

| size | median-of-medians spread | median-of-mins spread | null-kernel overhead |
|--:|--:|--:|--:|
| 64 B | 41.2% | 40.1% | 1.86 ns = 4.89% of impl |
| 4 KB | 36.3% | 24.1% | 1.25 ns = 0.054% |
| 64 KB | 40.4% | 11.4% | **0.000 ns** (below 40 ns timer floor) |
| 1 MB | 35.7% | 15.9% | **0.000 ns** (below timer floor) |

**Step 3 — contention, three runs 60 s apart (4 KB, 20 runs each).**

| run | wall clock | loadavg | `steal` cpu2 / agg | core_ghz | med-of-medians | spread | min-of-mins |
|--:|--:|--:|--:|--:|--:|--:|--:|
| 1 | 20:45:28 | 1.70 | 0 / 0 | 2.847 | 2235.5 ns | 43.4% | 1291.8 ns |
| 2 | 20:46:31 | 1.49 | 0 / 0 | 2.730 | 1986.9 ns | 39.0% | 1291.8 ns |
| 3 | 20:47:34 | 1.52 | 0 / 0 | 3.063 | 2503.5 ns | **3.7%** | 1745.8 ns |

`steal` is exactly 0 in every sample on both cpu2 and aggregate, and PSI `some`
stays 1–2% with `full` 0.00, yet loadavg sits at 1.5–1.7 on 4 vCPUs with
`1/291` runnable while nothing of ours is running. The guest's accounting simply
does not expose the interference. Run 3 is the diagnostic find: **a window that
is internally steady (3.7%) is 26% slower than a window that is internally
chaotic (43%)** — so "quiet" and "fast" are not the same window, and neither
medians nor mins are stable across windows.

**Step 4 — harness overhead.** Call + batch loop + address setup = **1.86 ns** per
call at 64 B (4.89% of the measurement, a constant offset, not noise); 1.25 ns at
4 KB (0.054%); unresolvable at ≥64 KB. The timer costs 40 ns per sample,
amortised to 0.039 ns/call at batch=1024 and 40 ns/call at batch=1 (0.15% at
64 KB, 0.008% at 1 MB). So harness overhead is **not** the source of the spread:
the null kernel, which touches no memory, still varies 20% run-to-run.

**Step 3b — `/proc/loadavg` and `/proc/stat` before *and after* each run, with the
delta consumed *during* the window** (the explicitly requested before/after
form; rep count 20, 4 KB, unpinned):

| run | clock | window | loadavg before→after | other CPU ticks in-window agg / cpu2 (100/s) | steal | med-of-medians | spread | min-of-mins |
|--:|--:|--:|--:|--:|--:|--:|--:|--:|
| 1 | 20:55:55 | 2.92 s | 1.75→1.69 | **480 / 78** | 0 | 2571.75 ns | 20.0% | 1293.69 ns |
| 2 | 20:56:58 | 1.95 s | 1.74→1.74 | **318 / 31** | 0 | 2413.31 ns | 35.0% | 1292.44 ns |
| 3 | 20:58:00 | 2.98 s | 1.39→1.52 | **485 / 156** | 0 | 2157.19 ns | 33.6% | 1791.50 ns |

480 and 485 ticks in a ~3 s window is 4.8 s of CPU time consumed by *other*
processes on a 4-vCPU guest — about 1.6 vCPUs busy for the whole measurement while
our own loadavg contribution is ~0. So the loadavg of 1.5–1.7 is not a stale
average: foreign work is genuinely running throughout. `steal` stays **0**, which
is why the guest accounting cannot see it.

**Pinning test (performance-jō rule 2).** The harness reports
`affinity covers 4 cpu(s), running on cpu N -> use taskset -c N`, so steps 1–4
above ran *unpinned*; this closes that gap. Same command, 4 KB, 20 runs/window,
three windows back-to-back:

| config | median spread per window | min spread per window | min-of-mins |
|--|--|--|--|
| `taskset -c 2` (pinned) | 31.9% / 18.2% / 31.3% | 35.0% / 35.3% / 37.4% | 1292.44 / 1292.44 / 1291.81 ns |
| unpinned | 35.7% / 31.3% / 20.9% | 28.4% / 18.5% / 41.7% | 1293.06 / 1620.56 / 1292.44 ns |

**Pinning does not reduce the noise** — the two distributions are the same.
Migration is therefore ruled out as the cause, and the interference is imposed
on the vCPU from the host regardless of which vCPU we occupy. The CSV confirms
the rule-2 requirement is met where it can be: `taskset -c 2 ./bench_bench_znver2
--one ...` emits `cpu=2, migrated=0`, i.e. `sched_getcpu()` is read inside the
measured region.

**What *is* reproducible: the floor.** `min_of_mins_ns` landed at 1291.81–1293.06
ns in **6 of the 9 windows** above and in 3 more windows of step 3 (0.1% apart)
— while medians over the same windows moved 20–43%. The true minimum is a
stable, reproducible property of this box; the medians are contaminated.

*Caveat on `--one`/`--pmu`:* they emit a single non-replicated row (`reps=1`,
`rep_spread_pct=0.000`), so they satisfy neither the 3-rep rule nor rule 3. Only
the sweep mode aggregates interleaved reps. Use `--one` for the counter set,
never for a timing claim.

**Null-comparison band, current binary, reps=9.** Ratios of two provably
identical kernels; anything smaller than these is an artefact.

| size | global-min ratio | best-median ratio | best cycles/byte ratio |
|--:|--:|--:|--:|
| 16 B | +19.1% | +19.0% | +20.6% |
| 32 B | +5.0% | −4.4% | −0.0% |
| 64 B | +8.5% | −7.8% | −5.0% |
| 128 B | +18.4% | −16.7% | −6.3% |
| 256 B | +22.0% | +24.0% | +12.6% |
| 512 B | +0.6% | +34.4% | +12.1% |
| 1 KB | +0.3% | +28.6% | +20.9% |
| 4 KB | −20.1% | +0.2% | +0.5% |
| 16 KB | −0.9% | −0.1% | −1.4% |
| 64 KB | +1.1% | +0.1% | +3.4% |
| 256 KB | −0.0% | +7.5% | +2.4% |
| 1 MB | +0.1% | +3.2% | +1.9% |
| 4 MB | −0.4% | +7.1% | +0.3% |

Note 4 KB: the global-min ratio reads −20.1% while median and cycles/byte read
+0.2%/+0.5% — a single run's global min can itself be fooled, so a min only
counts once it has been reproduced across repeated quiet windows.

### Recommendation

1. **Primary statistic: `cycles/byte` (PMU cycles, not TSC)** — it is the only
   metric that survives the 2.73–3.06 GHz core-clock drift measured here, and it
   is the tightest of the three at large sizes.
2. **≥16 KB: claims allowed above a 3.5% floor**, taken from the best of ≥2
   repeated quiet windows (a window is quiet when its own median spread <5%).
3. **4 KB–16 KB: floor ≈ 5%, treat 4 KB as 20%** (one statistic blew up 20% there).
4. **<4 KB: no cross-kernel claims below 2x** — the band is 5–34%.
5. Medians and p99 are informational only; the harness keeps flagging >5% spreads,
   and we now also flag a run whose spread is <5% as a *usable* window.
6. All comparisons stay interleaved/paired inside one process (implemented).

### Harness defects found and fixed during this investigation

1. `bench/bench.c` interleaved-rep indexing (`rr[r*ncases+c]` vs `rr[c*reps+r]`)
   — cases read each other's samples (commit `a6f32ea`).
2. Repeating `--impl` silently kept only the last value, so the prescribed step-1
   command measured one kernel and looked like two. Repeats now append; comma
   lists still work (commit `a6f32ea`).
3. `--noise` and `--one` with a missing positional argument dereferenced NULL and
   segfaulted (rc=139) instead of printing usage. Now rc=2 + usage (commit `a6f32ea`).
4. `scripts/run_perf.sh` printed the shell's affinity mask as if it were the
   measurement's (Phase 1).

* `p99_ns` is recorded for information only and is used to claim nothing.

## Artifacts

* `results/phase2_naive_{znver2,znver3,v3}.csv` — 80 rows each (16 sizes x
  {ref_scalar, glibc, naive} x {memcpy, memset where applicable}), 21 columns.
* `results/phase2_naive_{znver2,znver3,v3}.log` — harness stderr: clock snapshot,
  variance flags, migration count.
* `results/phase2_naive_analysis.txt` — `scripts/analyze_phase2.sh` output.
* `results/codegen_diff_naive_*.diff` — pairwise codegen diffs.
* `results/phase2_perf_pass.log` — PMU validation pass (`make perf`).
