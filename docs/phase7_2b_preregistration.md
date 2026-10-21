# Phase 7.2b — pre-registration: is the 512 B regression real or an artifact?

**Committed BEFORE any measurement in this pass.** No measurement for this pass
had been taken when this file was written. The only prior data it refers to is
the committed Phase 7.2 campaign (`docs/data/raw/phase7_f4al_<target>.windows.log`).

## The question

Phase 7.2's candidate — `src/rungs/f4win64_aligned.c` — turns the certified
128 B loss into a **win** (+18.31% against the shipped F4 at both the floor and
the median, identically on all three targets; +10.11% against glibc, the first
time 128 B is ahead of glibc in this project). It was nevertheless **rejected
under S3** because the regression gate failed at **512 B**: −2.70% against the
shipped F4 on znver2 and znver3 (−1.33% on v3, which passed).

The 512 B difference is two quanta of the reported per-copy time
(5.7891 ns → 5.9453 ns) on a path whose **instructions are byte-identical** in
all three builds; only its placement differs, because the knob's added code
moves the flat class chain that contains it. That makes it a candidate for
either a genuine layout effect or a measurement artifact.

**This pass decides which. It does not promote anything.**

## Rules

**R1.** If T1 shows the 512 B delta moving toward 0 (|delta| < 1.5% at higher
iters, on ≥ 2 of 3 targets) → the original −2.70% was a resolution artifact.
**PROPOSE PROMOTE**, to be confirmed by a full re-certification campaign. Do
not auto-promote in this pass.

**R2.** If T1 shows the −2.70% persisting at higher iters **and** T2 shows the
regression comes from **code shape** (the no-flag H1b build also shows ≈−2.70%)
→ the regression is **REAL**. The trade is genuine (128 B +18% for 512 B
−2.7%). Report both and present for a strategic decision. Do not auto-promote.

**R3.** If T1 shows the −2.70% persisting **and** T2 shows it comes from
**alignment-flag placement** (the no-flag H1b build shows 0.00% at 512 B) →
the alignment strategy for 128 B can be adjusted to avoid moving the flat class
chain. This is **attempt 2 for Phase 7.2**. Report and stop; the decision
whether attempt 2 is worth it is not taken in this pass.

**R4.** If the timer-resolution check (T3) shows the observed difference is
≤ the instrument's per-tick granularity, state that explicitly and classify the
result as **"below resolution — cannot distinguish from noise"** regardless of
the T1/T2 numbers.

## Experiments

### T3 — timer resolution (no measurement)

From the committed Phase 7.2 log: the TSC frequency, the per-tick period, the
observed F4-vs-candidate difference at 512 B, and that difference expressed in
ticks.

### T2 — isolate alignment from code shape

Compare the 512 B behaviour of the **no-flag** build against the shipped F4.
In this tree the no-flag build is `src/rungs/avx2_f4win64.c`
(`memcpy_avx2_f4win64`, registered as `f4win64`): the same source, the same
`TUNED_WIN64 1` knob, and **no** `-falign-labels=64`. It is byte-for-byte the
same code as `f4win64_aligned` minus the alignment flag, which this pass
verifies with `objdump` and reports as a diff. The candidate
(`f4win64_aligned`) and the no-flag build are therefore the two arms of the
comparison, and the shipped F4 is the baseline, all measured in the same
windows.

### T1 — higher-resolution measurement at 512 B

512 B only, all three targets, 9 windows × 5 reps, `taskset -c 2`, zero
migrated reps, null band (`naive` vs `ref_scalar`) measured in the same run at
the same iters, at **iters = 40000** (10× the standard 4000) and at
**iters = 4000** so the resolution effect is directly visible.

**Same windows across all impls:** all impls are measured inside one harness
process per window, interleaved rep-major by the harness itself, so an
interference burst is shared rather than charged to one kernel.

**On "the same process" for the two iters settings:** the harness takes one
`--iters` per invocation, so the 4000-iter and 40000-iter measurements are two
invocations run **back to back inside the same window**, sharing the host state
of that window. They are not one process, and this file says so rather than
claiming otherwise. The committed Phase 7.2 campaign (iters = 4000) is also
quoted for cross-reference, but it is a different host state and is labelled as
such.

## What this pass does NOT do

- It does not touch `src/fast.c`.
- It does not promote anything.
- It does not re-run the full 13-size campaign.
- It does not cherry-pick windows: the full 45-sample distributions are
  reported for every impl, every target, at both iters settings.
- It does not invent a tier or a statistic: the existing two-tier model and the
  existing min/median are used, and the null band is measured in the same run.
