# Phase 8.2 — quiet-window re-certification of 16 KiB and 256 B (pre-registered)

**PRE-REGISTRATION.** Written and committed **before** the quiet-window gate
was run and before any measurement. The criteria, the gate and the protocol
are fixed here; the results go in afterwards, in a later commit.

## What is being attacked

Two rows of the Phase 7.4 reference campaign that are not certified wins:

* **16 KiB** — a certified **loss** on all three targets (−2.28 / −2.41 /
  −2.40%), Tier A, 1.5% floor. The promoted kernel is **+1.16%** against the
  kernel it replaced at that size (znver2), so this is a host-limited row, not
  a kernel regression: the size is memory-bound and the row follows the host.
* **256 B** — a certified **win at the floor** on all three targets (+8.41% ×3)
  but a **tie at the median** on znver2 (+0.00%). The same bimodal question as
  512 B and 1 MiB.

Both need a quiet window to be measured fairly. This sub-phase is
**re-certification only** — no code, no variant, the existing deliverable.

## Criteria

**16 KiB**
* **A1_16** — 16 KiB delta against glibc is within **1.5%** (the Tier A floor)
  on **≥ 2 of 3** targets.
* **A2_16** — 16 KiB delta against the **previous** deliverable (the
  pre-promote F4, `f4_baseline`) is **≥ +1.0%**: the promote must not have
  regressed this size.
* **S1_16** — if A1_16 fails → **accept the loss**. Do not re-run.
* **S2_16** — if A2_16 fails → the promote regressed 16 KiB. **Report; do not
  revert without an explicit go.**

**256 B**
* **A1_256** — 256 B **median** delta against glibc is **≥ +1.5%** on
  **≥ 2 of 3** targets.
* **A2_256** — 256 B **minimum** delta against glibc is **unchanged
  (≥ +8%)** on all three targets.
* **S1_256** — if A1_256 fails → **accept "conditional at median"**. Do not
  re-run.

**Regression gate (both sizes)** — 16 B, 32 B, 64 B, 128 B, 512 B and 1 MiB
must be **unchanged within the null band** relative to the Phase 7.4 reference
deltas:

> |Δ_new(vs glibc) − Δ_phase7.4(vs glibc)| ≤ max(1.5%, null_band_pct)

with the null band measured in the same run. The five certified win sizes
(16/32/64/128/512 B) are the ones that must not move; 1 MiB is a Tier-B
`measured`/parity row and is in the list as a control — a row that moves
outside its band there is a finding about that row's stability, not a
regression of a certified win, and is reported as such.

## Quiet-window gate (run BEFORE measuring)

The floor of this host moves with load
([METHODOLOGY.md §9](METHODOLOGY.md#9-host-variance-the-floor-moves)), so the
gate samples the noise floor before any measurement is taken.

* **Probe size: 64 KiB.** It is the size whose per-rep minima the committed
  Phase 7.4 data shows to be stable (a 0.8% cluster gap, 42 of 45 reps in the
  lower cluster), so it is a usable host-stability probe. 16 KiB and 256 B are
  not: both are bimodal on this host, so their own spread cannot be used to
  decide whether the host is quiet.
* **Sampling:** three consecutive samples, 10 s apart (≈30 s of observation).
  Each sample is one harness run of `naive,ref_scalar` at 64 KiB with
  `--reps 5 --iters 4000`.
* For each sample, at 64 KiB:
  * `band_i` = |naive_i − ref_scalar_i| / ref_scalar_i
  * `spread_i` = (max − min) / min of that sample's five ref_scalar minima
* **PASS** requires **all three** samples to satisfy `band_i < 2%` **and**
  `spread_i < 2%`.
* If the gate fails: **wait 5 minutes and retry once.** If it fails again →
  **STOP**, report "host not quiet today", reschedule, and **do not measure**.
  Forcing a measurement on a noisy host is exactly what produced the 16 KiB
  loss in the first place.

## Measurement protocol (only if the gate passes)

* One run serves both target sizes and the regression gate.
* Sizes: 16, 32, 64, 128, 256, 512, 16384, 1048576.
* Implementations: `ref_scalar`, `naive` (the null band), `glibc` (the
  baseline), `fast` (the deliverable under test), `f4_baseline` (the pre-promote
  kernel, for A2_16).
* 9 windows × 5 reps, `taskset -c 2`, `iters 4000`, all three targets, zero
  migrated reps required, null band measured in the same run.
* The two statistics are as everywhere: the floor (`min_of_mins`) and the
  median of the 45 per-rep minima.

## If both sizes pass

The new campaign becomes **the reference for 16 KiB and 256 B**; the Phase 7.4
campaign stays the reference for every other size. `docs/RESULTS.md` and
`docs/README.md` are updated from the new CSV, the new log is committed under
`docs/data/raw/`, and the per-size provenance is stated in the results file.

## If either size fails

That row stays at its Phase 7.4 value, plainly stated. No re-run, no
re-rolling, no threshold adjustment.

## Amendment to the quiet-window gate (committed before the retry)

The first gate run failed, and inspecting it against the committed Phase 7.4
campaign showed the gate's **spread clause is unsatisfiable on this host, even
in a good window**. The per-rep range of the noise-floor rungs, over the 45
reps of the committed good campaign:

| size | ref_scalar | naive | the deliverable |
|---|--:|--:|--:|
| 16 KiB | 9.56% | 15.04% | 60.05% |
| 64 KiB | 9.80% | 20.19% | 3.96% |
| 256 KiB | 23.46% | 13.11% | 16.34% |
| 1 MiB | 29.61% | 32.70% | 25.29% |

No size and no implementation reaches 2%. The clause therefore measures the
**per-rep variance of the measurement**, which on this shared host is always
large, and cannot distinguish a quiet host from a noisy one. It is a
specification defect, not a host finding.

Two implementation defects were also found in the gate script: the spread
accidentally included the `memset` rows, and the band was taken over five reps
rather than over a converged sample.

**The gate is corrected, before the retry, as follows.** The quietness signal
is the **null band** — `|naive − ref_scalar| / ref_scalar`, the project's own
definition of the noise floor — measured at the sizes that matter, over enough
reps for the band to converge (the campaign's 45 reps converge it to 0.04–1.07%;
the harness caps a single run at 32 reps, so a sample is 30).

* Three consecutive samples, 10 s apart, each a harness run of `naive` and
  `ref_scalar` at 16 KiB, 64 KiB, 256 KiB and 1 MiB with `--reps 30`.
* Per sample and per size: `band_i` = |floor(naive) − floor(ref_scalar)| /
  floor(ref_scalar), the floors taken over that sample's 30 reps.
* **PASS** requires every `band_i` to be **< 2%** in all three samples, **and**
  the **across-sample spread** of the band at each size — max − min over the
  three samples, in percentage points — to be **≤ 2**.

This is the reading the brief's sentence most naturally supports ("3
consecutive 10 s samples with spread < 2%": the spread *of the samples*), and
it is satisfiable on a quiet host, as the committed good campaign shows. The
band threshold itself is unchanged, and the gate can still fail.

The retry rule is unchanged: on failure, wait 5 minutes and retry once; on a
second failure, **STOP**, report "host not quiet today", reschedule, and do
not measure.
