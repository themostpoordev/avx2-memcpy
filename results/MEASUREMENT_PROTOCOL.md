# Measurement protocol — the contract for every Phase 2 (and later) claim

Status: **in force** from Kernel 2 onward. This document is the contract. Any
deviation from it must be flagged in the report that deviates, and if the
deviation changes a conclusion, the conclusion is void until remeasured.

Host: shared 4-vCPU guest, Zen-2-class silicon, 2.79474 GHz invariant TSC.
No cpufreq sysfs, so the core clock is uncontrollable (2.73–3.06 GHz observed).

---

## 1. Why medians are not usable here (the evidence)

* **Host contention is real and invisible to the guest.** With the destination
  buffer untouched by anything else, `/proc/loadavg` sits at 1.4–1.75 on 4 vCPUs
  while nothing of ours runs, and the `/proc/stat` delta *during* a ~3 s
  measurement window was **480–485 busy ticks aggregate (~1.6 vCPUs of foreign
  work) with `steal` exactly 0** and PSI `full` 0.00. The guest's accounting
  simply does not expose the interference.
* **Pinning cannot fix it.** `taskset -c 2` vs unpinned, 4 KB, 20 runs/window,
  three windows: median spreads 31.9/18.2/31.3 % vs 35.7/31.3/20.9 %; min
  spreads 35.0/35.3/37.4 % vs 28.4/18.5/41.7 %. The distributions are the same,
  so migration is ruled out and the interference is host-imposed.
* **The noise is window-structured.** One window can be internally steady
  (median spread 3.7 %) while another is 43 % chaotic — and the steady window can
  be the *slower* one. So neither "quiet" nor "fast" can be assumed.
* **Two byte-identical kernels differ by up to 34 %** on best-median GB/s at
  small sizes and 4–11 % at large ones when measured in separate windows. That
  number is the artefact band, not a result.

## 2. Why `min_of_mins` is the signal

`min_of_mins` = the single smallest `min_ns` observed across **all reps in all
windows** for a given `(impl, target, op, size)`. Each rep's `min_ns` is the
fastest single batch inside that rep's timed region, on a pinned core, with the
timer overhead subtracted.

Across 9 windows at 4 KB, `min_of_mins` came out at **1291.81–1293.06 ns — 0.1 %
apart** in 6 of them, while the medians over those same windows moved 20–43 %.
The minimum is a stable, reproducible property of this box. The medians are
contaminated. We therefore measure the floor and compare floors.

## 3. The rules

1. **Primary metric: `cycles/byte` from the rep that achieved `min_of_mins`.**
   Cycles come from the PMU (`PERF_COUNT_HW_CPU_CYCLES`), never from rdtsc/TSC.
   The paired `min_ns` and its `cycles/byte` come from the same timed region, so
   they describe the same execution.
2. **Repetition: 5 reps per window, minimum 3 windows** per
   `(impl, target, op, size)`. Keep adding windows until `min_of_mins` is stable
   to **≤0.5 %** across the last two windows. **Cap: 9 windows.**
3. **Claim floor: Δ`min_of_mins` must exceed 1.5 %** on the same
   `(target, op, size)` measured the same way. Anything ≤1.5 % is reported as
   **within-noise** and is not a win, a loss, or a tie-breaker.
   The floor is 1.5 % rather than the ~3.5 % that the ns-median noise would
   justify, because `min_of_mins` is stable to 0.1 % and the floor must still
   cover the 0.5 % window-stability tolerance plus cross-window drift.
4. **Reps are interleaved across impls inside one process** (harness feature), so
   an interference burst is shared between the compared kernels instead of being
   charged to whichever one ran during it.
5. **All measurements pinned: `taskset -c 2`**, and every rep must evidence
   `cpu=2 migrated=0` (the harness samples `sched_getcpu()` *inside* the timed
   region). Migration makes a rep unusable and it is dropped and reported.
6. **Median and p99 are informational columns only.** They are recorded, they are
   never used in a claim, and the harness keeps flagging >5 % median spreads.
7. **`gbps` is reported for information**; it is inflatable by an idle clock and
   is not comparable across sessions. ns and cycles/byte are.
8. **`cycles/byte` is derived from raw PMU cycles and total bytes**
   (`n × batch × iters`); the raw `cycles_total` column is always published so
   the division can be checked.
9. Every result records its own `core_ghz` snapshot; a fixed clock is never
   assumed.
10. A claim of a speedup we did not measure with PMU cycles is forbidden.

### 3a. PENDING AMENDMENT — the 1.5% floor is not safe at every size

Rule 3 as approved assumes `min_of_mins` is stable to 0.1% everywhere. Measured
over 9 windows x 5 reps, that holds for the middle of the size range and **fails
at the extremes**, so the artefact band is now measured inside every campaign
instead of assumed. `naive` (kernel 1) and `ref_scalar` hold the same byte loop
with byte-identical instruction streams (`results/codegen_diff_naive_*.diff`), so
their difference *is* the band at that size, in those same windows:

| size | znver2 | znver3 | v3 |
|--:|--:|--:|--:|
| 16 B | 12.52% | 0.01% | 4.40% |
| 64 B | 10.90% | 0.86% | 1.17% |
| 128 B-1 MB | 0.00-0.06% | 0.15-4.56% | 0.00-1.23% |
| 512 B | 0.05% | **25.33%** | 0.00% |
| 4 KB | 0.05% | 0.15% | **25.15%** |
| 4 MB | 4.76% | 4.00% | 1.75% |

Where the band is 21-25%, a 1.5% "win" is pure artefact. `scripts/mo_analyze.sh`
therefore publishes **both** verdicts side by side and never hides the gap: the
mandated rule-3 verdict at 1.5%, and a **band-aware** verdict at
`max(1.5%, measured band)`. Every claim in a kernel report quotes the band-aware
verdict until this amendment is either ratified or rejected. Nothing is
suppressed: both columns are in every per-target CSV.

### 3b. PENDING AMENDMENT — the 0.5% early stop can miss a deep floor

Rule 2's stop is met too early in practice. Evidence: znver2's 4 KB key was flat
for six consecutive windows and then improved **20.05%** at window 9
(1617.4 -> 1293.1 ns), and v3 stabilised at window 5 on a 0.04% change. Because
`min_of_mins` only ever decreases as windows accumulate, campaigns that stop at
different window counts are not comparable. Cross-target and cross-kernel
campaigns therefore run to the 9-window cap (`RUN_TO_CAP=1`) and publish the
per-key rule-2 verdict, marking keys that are still moving as `UNSTABLE` rather
than averaging them in. An `UNSTABLE` key carries no claim.

## 4. What this protocol rules out

* Comparing NS-across sessions: the core clock moved 2.73→3.06 GHz between
  windows, a 12 % swing, so any cross-session ns comparison is
  frequency-confounded. `cycles/byte` is not.
* Comparing windows: only `min_of_mins` over ≥3 windows may be compared.
* Any claim from a single run, however tempting the number.

## 5. Known limits of this box (documented, not hidden)

* L3/LLC PMU events are unavailable on this vPMU, and a group wider than 4 events
  never schedules. Cache-boundary statements are labelled inference, not
  measurement.
* `retired_ops` (raw 0x0076) reads 1.49–1.62/byte against `instructions` 5.0/byte
  on this masked CPU; it is used for no claim.
* No `perf` CLI, `hyperfine`, `gnuplot` or `nasm`; no installs permitted.
  Substitutes: our own `perf_event_open` wrapper, the harness timers, clang's
  integrated assembler, and (planned) a pure-C SVG writer.
* The instrument itself has a floor: harness call+loop+setup costs 1.86 ns per
  call at 64 B (4.89 % of that measurement) and is unresolvable at ≥64 KB. A
  claimed effect at small sizes must exceed this offset, and the offset is
  constant rather than noise.

## 6. Reproducing any number in a Phase 2 report

```sh
make                                              # 3 targets, 0 warnings
scripts/mo_measure.sh <target> <impls> memcpy <sizes> <log>   # ≤9 windows, pinned
scripts/mo_analyze.sh <log> <kernel> <prev> <target> [op]   # report + CSV
```

`mo_measure.sh` enforces the window count and the ≤0.5 % stability stop;
`mo_analyze.sh` applies the 1.5 % claim floor and writes
`results/phase2_<kernel>_<target>.csv`. Neither script post-processes a number by
hand.
