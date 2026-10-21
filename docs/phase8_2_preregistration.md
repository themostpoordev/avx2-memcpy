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
