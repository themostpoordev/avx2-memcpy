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

### Recommendation (your decision rule put this in the >5% branch on medians)

1. **Primary statistic: global min of `min_ns` and its PMU `cycles_per_byte`**,
   over ≥11 reps, for sizes ≥512 B. Floor ≈ 5%; require deltas to exceed it.
2. **Sizes <512 B: no cross-kernel claims** except for differences >2x.
3. **Report `cycles/byte`, not GB/s, as the primary unit** — the clock drifts 14%.
4. Medians and p99 are informational only; the harness keeps flagging >5% spreads.
5. All comparisons stay interleaved/paired inside one process (already implemented).

* `p99_ns` is recorded for information only and is used to claim nothing.

## Artifacts

* `results/phase2_naive_{znver2,znver3,v3}.csv` — 80 rows each (16 sizes x
  {ref_scalar, glibc, naive} x {memcpy, memset where applicable}), 21 columns.
* `results/phase2_naive_{znver2,znver3,v3}.log` — harness stderr: clock snapshot,
  variance flags, migration count.
* `results/phase2_naive_analysis.txt` — `scripts/analyze_phase2.sh` output.
* `results/codegen_diff_naive_*.diff` — pairwise codegen diffs.
* `results/phase2_perf_pass.log` — PMU validation pass (`make perf`).
