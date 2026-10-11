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
2. **Repetition (RATIFIED, amendment 2 in §3b): 5 reps per window and
   ALWAYS 9 windows** per `(impl, target, op, size)`. There is **no early stop**:
   the 0.5 % stability rule was measured to quit before a deep floor is reached
   (`min_of_mins` only ever decreases as windows accumulate, so campaigns that
   stop at different window counts are not comparable). Nine windows is cheap.
   After the 9th window, a key whose per-window `min_ns` spread exceeds **10 %**
   is marked **`UNSTABLE` and carries no claim** — the label is propagated into
   the CSV, the analysis file and the report; an unstable key is never averaged
   in or quietly reported as a win.
3. **Claim floor (RATIFIED, amendment 1 in §3a): the applicable floor is
   `max(1.5 %, measured_null_band(target, op, size))`.** `measured_null_band` is
the difference between the two identical byte loops (`naive` vs `ref_scalar`)
measured **in the same windows** at that same size, so the floor is per
`(target, op, size)` and cannot be assumed. **Both verdicts are published in
every report and every CSV:**

   * `rule3_verdict` — the mandated flat 1.5 % comparison, kept visible so the
     places where the flat floor was wrong stay auditable;
   * `band_verdict` — the comparison against `max(1.5 %, band)`.

   **When the two disagree, `band_verdict` wins.** A claim below the applicable
   floor is reported as **within-noise** and is not a win, a loss, or a
tie-breaker. Rule 3 was originally justified by `min_of_mins` being stable to
0.1 %; that holds only in the middle of this box's size range, which is exactly
why the band is now measured rather than assumed.
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

### 3a. Amendment 1 — band-aware claim floor (RATIFIED)

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
`max(1.5%, measured band)`. **RATIFIED: the band-aware verdict is the claim rule,
and where the two disagree the band verdict wins.** The rule-3 verdict stays in
every table and every CSV so the sizes where the flat floor was wrong remain
auditable. Nothing is suppressed.

### 3b. Amendment 2 — always run to the 9-window cap (RATIFIED)

Rule 2's original stop is met too early in practice. Evidence: znver2's 4 KB key
was flat for six consecutive windows and then improved **20.05%** at window 9
(1617.4 -> 1293.1 ns), and v3 stopped at window 5 on a 0.04% change. Because
`min_of_mins` only ever decreases as windows accumulate, campaigns that stop at
different window counts are not comparable. Cross-target and cross-kernel
campaigns therefore **always** run to the 9-window cap (`RUN_TO_CAP=1`, which is
now the default path for every campaign) and publish the per-key rule-2 verdict.
A key whose per-window `min_ns` spread exceeds **10%** after 9 windows is marked
`UNSTABLE` and carries no claim — the znver2 256 KB case is the first example.
An `UNSTABLE` key is never averaged in, and the label is propagated into the CSV,
the analysis file and the report.

### 3c. Two-tier claim model (RATIFIED) — tier assignment and the Tier B floor

Amendment 1 (band-aware floor) and amendment 2 (`UNSTABLE` = no claim) contradict
each other on real data: Kernel 3 passes the band floor at 12 of 13 sizes while
11-12 of those same sizes are `UNSTABLE`. Neither rule wins. The resolution is a
two-tier model, where tier assignment uses the same per-window spread that rule 2
already computes:

* **Tier A — spread ≤10 %.** Claim floor = `max(1.5 %, measured_null_band(size))`.
  Publish `rule3_verdict` (against 1.5 %) and `band_verdict` (against the band);
  **`band_verdict` is authoritative for Tier A sizes.**
* **Tier B — spread >10 %.** Do **not** use `null_band`: at these sizes it
  under-states the uncertainty. Claim floor = **25 % flat**. A delta >25 % is
  **claimed**; a delta ≤25 % is reported as **"measured, not claimed"**. The
  spread value is always published so a reader can see why the size is Tier B.

Rationale: `UNSTABLE` means the fine-grain floor is unusable at that size, not
that the size is unmeasurable. Large effects stay detectable and claimable —
Kernel 3's +54…77 % gains exceed 25 % and are therefore claims, not mere
measurements. Every per-target CSV carries `tier`, `tier_floor_pct` and the tier
verdicts alongside the rule-3 and band verdicts, so no claim can be traced to a
floor that was not published.

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

`mo_measure.sh` enforces the window count (always 9) and the pinning;
`mo_analyze.sh` applies the band-aware claim floor, publishes both verdicts and
the per-key `UNSTABLE` label, and writes `results/phase2_<kernel>_<target>.csv`.
Neither script post-processes a number by hand.

## 7. Committed CSV snapshots (the frozen record)

`results/*.csv` stays gitignored: those files are rewritten by every run and
committing them directly would pollute history. Finalized kernels are therefore
snapshotted:

```sh
make snapshot        # results/phase2_<impl>_<target>.csv -> results/committed/...
```

`results/committed/*.csv` is intentionally **not** ignored. Rules:

* only a kernel that has passed verify and completed its benchmark is
  snapshotted — a snapshot is a finalization event, not a checkpoint;
* **snapshots are immutable.** `make snapshot` refuses to overwrite an existing
  committed file that differs from the current run and says so, because a frozen
  record that silently changes is not a record. Re-running the same measurement
  is a no-op (`cmp -s` equal); a genuinely different result needs a new name and
  an explanation, not an overwrite;
* snapshotted so far: `phase2_naive_{znver2,znver3,v3}.csv` and
  `phase2_word_{znver2,znver3,v3}.csv` (commit `phase2: snapshot committed CSVs
  for naive and word`).
