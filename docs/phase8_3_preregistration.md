# Phase 8.3 — 1 MiB variant (pre-registered)

**PRE-REGISTRATION.** Written and committed **before** any Phase 8.3 code was
written and before any measurement. The construction, the criteria, the
acceptance rules and the attempt budget are fixed here.

Phase 8.1 returned **VARIANT VIABLE** (`docs/phase8_1_mechanism.md`, commit
`912ed32`): the 1 MiB mode split is **kernel-driven** — the fast cluster
(23 053–25 819 ns, 12/45 reps on znver2) is exclusive to the kernels that
carry the software prefetch (`fast`, `f4_baseline`, `f4win64al`) and is absent
from `glibc`, `ref_scalar` and `naive`; it predates the promote; and it does
not correlate with the fast modes at other sizes (4–12 of 45, at or below
chance). glibc's own 1 MiB minima in the same windows never go below
26 370 ns.

## The construction (Phase 8.1 Q4)

A **size-gated longer prefetch distance** for the 1 MiB band.

The published symptom is a *median* problem, not a floor problem: the floor is
already a win (+12.58 / +11.10 / +9.42%) and the fast cluster already exists.
A longer prefetch distance would not touch the floor — the fast mode's data is
already in cache — but it would let the *typical* copy reach the same
91 GB/s, which is what moves the median.

The arithmetic behind the distance: the fast mode runs at 2 MiB of traffic in
23 053 ns = **91 GB/s**. At that rate 128 B of lookahead covers **1.4 ns**,
against a DRAM latency of tens of nanoseconds. **4096 B covers ~45 ns** — most
of a DRAM latency — and the prefetched line survives 4 KiB of streaming in a
32 KiB L1.

### It must be size-gated, and the band is fixed

The prefetch is live for every `n >= TUNED_PF_MIN` (64 KiB), so a global
`TUNED_PF_BYTES` change would also move the 64 KiB and 256 KiB rows — which
the P1 gate does not cover — and would touch 4 MiB, which is out of scope.

**The new distance applies only for `TUNED_PF_BIG_MIN <= n < TUNED_PF_BIG_MAX`,
with `TUNED_PF_BIG_MIN = 524288` (512 KiB) and `TUNED_PF_BIG_MAX = 2097152`
(2 MiB).** That band contains the 1 MiB size and excludes 64 KiB, 256 KiB and
4 MiB. The distance is chosen **once, before the loop**, exactly as the
existing `pf` selection is, so the hot loop is unchanged.

The knobs are added to `src/fast_impl.h` with a default of **off**
(`0`), so the shipped kernel's object code is bit-identical until a rung turns
them on.

## Criteria

* **P1** — every certified row (**8, 16, 32, 64, 128, 256, 512 B and 16 KiB**)
  unchanged within the null band versus the Phase 7.4 deliverable, measured in
  the same windows: `|Δ_variant − Δ_shipped| ≤ max(1.5%, null_band)`, per size
  per target. 16 KiB cannot be affected at all: it is below
  `TUNED_PF_MIN = 65536`, so the prefetch is off there.
* **P2** — the 1 MiB **floor** is preserved: the variant's Δ_min against glibc
  must not be worse than the Phase 7.4 reference on any target.
* **P3** — the 1 MiB **median** moves to **≥ +1.5%** (the Tier A floor, or the
  null band where that is larger) against glibc on **≥ 2 of 3** targets.

### A note on P2's literal wording, recorded before measuring

The brief states P2 as "1 MiB fast mode (min) preserved: ≥ +10% vs glibc on
all 3 targets". **The current kernel does not meet that literally, and has
never done so:** the Phase 7.4 reference floor is +12.58 / +11.10 / **+9.42%**
(v3 below 10%), and the Phase 8.2 campaign measured +10.57 / **+9.20%** /
+10.81% (znver3 below 10%). A variant that changed nothing would fail that
wording.

P2 is therefore applied as its stated intent — **the floor must not be traded
away** — namely: the variant's 1 MiB Δ_min must not be worse than the Phase 7.4
reference on any target, and the absolute floor values are reported. The
reference values are recorded here so the comparison is against a fixed
number, not against whatever the variant happens to produce.

## Stop conditions

* **S1** — if P1 fails → **REJECT**. No certified row is traded for a median.
* **S2** — if P2 fails → **REJECT**. The win at the floor is the asset; do not
  trade it for a median.
* **S3** — if P3 fails → **attempt 2**, with a *different* mechanism from
  Phase 8.1 Q3's candidate list. If attempt 2 also fails → **accept parity at
  1 MiB**, document it in NEGATIVE_RESULTS.md, and close Phase 8.

**Attempt budget: two, and the second one is nearly unusable.** Phase 8.1 Q3
ranked the candidates: (1) the software prefetch, (2) cache/TLB residency,
(3) the aligned main loop and unroll 4, (4) prefetch distance, (5) NT store
paths — ruled out by inspection, the shipped kernel has none and the NT rung
lost by 69–372%. Attempt 1 is (4), the construction above. If attempt 1 fails,
attempt 2 must come from (2) or (3). And note, from Phase 8.1: the L3/LLC
counters this host would need to confirm (2) **do not exist here**, so a failed
attempt will not say *why* — only that the distance was not the limiter.

## Protocol

* 9 windows x 5 reps, `taskset -c 2`, `iters 4000`, all three targets, zero
  migrated reps, null band measured in the same run.
* Sizes: 8, 16, 32, 64, 128, 256, 512, 16384, 1048576.
* Implementations: `ref_scalar`, `naive` (the band), `glibc` (the baseline),
  `fast` (the shipped Phase 7.4 deliverable, the P1/P2 reference),
  `f4_baseline` (the pre-promote kernel), and the new variant.
* The variant is built as a **rung** (`src/rungs/`), registered next to `fast`,
  and code-reviewed before it is measured.
* The shipped `src/fast.c` is **not** touched. A promote happens only if P1,
  P2 and P3 all pass, and then it needs the full Phase 7.4 treatment: a new
  reference campaign, a new published md5, a rewritten headline.

## Standing rules for this sub-phase

No installs. No history rewrite. No re-rolling the reference campaign. 4 MiB
is not touched. Do not touch certified paths beyond what this file specifies.
Report full distributions, not selected windows. Every number cites its log
path.

## Amendment after the code review (committed before the campaign)

A code review of the rung (before any measurement) found that the construction
has a **confound**, and two things are added because of it.

**I-1 — the loop body is not the shipped loop's.** The band test is outside
the loop, as designed, but because the distance becomes a *runtime* value GCC
re-schedules the prefetch inside the iteration: the offset becomes a register
base and an extra `lea` appears. So `pf_big` measures **distance + loop shape**,
not the distance alone. The two false comments that claimed otherwise (in
`src/rungs/pf_big.c` and `src/fast_impl.h`) have been corrected.

**The control: `src/rungs/pf_ctl.c`.** The same runtime-distance shape, the
same band, and the **distance left at 128 B** — so it is semantically the
shipped kernel, compiled with the loop shape the new knob forces. It prices
the two effects separately:

| comparison | what it prices |
|---|---|
| `pf_ctl` vs `fast` | the loop shape alone |
| `pf_big` vs `pf_ctl` | the distance alone (4096 B vs 128 B) |

This is the same discipline Phase 7.2 used when its control showed "knob, not
flag". `pf_ctl` is not a candidate and cannot be promoted; it exists only to
be compared.

**The campaign therefore measures three kernels** (`fast`, `pf_big`, `pf_ctl`)
plus `glibc`, `ref_scalar` and `naive`. P1, P2 and P3 are evaluated against
`pf_big`, and the decomposition is reported alongside.

**Also from the review, and carried into the campaign:**

* the band test adds three instructions to the `n > 512` path, so P1's
  "unchanged" for 16 KiB means "within the null band", **not**
  "instruction-identical". 16 KiB's loop body is unchanged and the prefetch is
  off below 64 KiB; the cost is ~1–2 cycles out of ~650 for a 16 KiB copy.
* the band edges are a deliberate cliff (n = 2 MiB − 1 gets 4096 B, n = 2 MiB
  gets 128 B). No campaign size sits on an edge.
* the prefetch now reaches ~4 KiB past the end of the buffer at 1 MiB.
  `PREFETCHT0` never faults and never writes, and the harness's buffers are
  256 MiB, so nothing is touched; formally it is the same pointer-arithmetic
  past the end that the shipped kernel already does at 128 B, widened 32x.
* `scripts/phase8_3_accept.py` — the evaluator — is written **before** the
  campaign, as the review required.

**Sizes measured:** 8, 16, 32, 64, 128, 256, 512 B, 16 KiB, 64 KiB, 256 KiB and
1 MiB. 64 KiB and 256 KiB are not in the P1 gate, but they are the two rows the
loop-shape change could touch, so they are measured and reported. **4 MiB is
not measured** — it is out of scope.

## Attempt 1 result — REJECTED (S2 and S3)

Campaign: `docs/data/raw/phase7_p83_<target>.windows.log`, 9 windows x 5 reps,
4455 REPS lines per target, **zero migrated reps**.

| criterion | result | verdict |
|---|---|---|
| **P1** the certified rows unchanged within the null band | every cell inside its limit (the largest move is 16 KiB on v3 at −1.18%, inside its 1.50% limit) | **PASS** |
| **P2** the 1 MiB floor preserved | +8.12 / +7.56 / +7.36% against the Phase 7.4 reference's +12.58 / +11.10 / +9.42% | **FAIL on all three** |
| **P3** the 1 MiB median moved to ≥ +1.5% | −4.35 / −3.61 / −2.80% — 0 of 3 | **FAIL** |

**The decomposition is the informative part.** At 1 MiB:

| effect | floor | median |
|---|--:|--:|
| the loop shape (`pf_ctl` vs `fast`) | +0.79 / −0.38 / −1.69% | −0.11 / −0.04 / −0.48% |
| the distance (`pf_big` vs `pf_ctl`) | −2.74 / −3.97 / −2.03% | −2.25 / −1.62 / −1.10% |

The **loop shape is free** — the runtime-distance re-scheduling costs nothing
measurable, which is exactly what the control was for. The **4096 B distance
is actively harmful**: it costs 2–4% at the floor and 1–2% at the median, on
all three targets. The construction's premise (that 128 B covers too little of
the copy's latency) is wrong in practice: a 32× longer lookahead fetches lines
that are evicted before they are used, and the bandwidth it wastes is charged
to the copy.

## Attempt 2 — pre-registration: prefetch COVERAGE in the 1 MiB band

Attempt 1 tested the prefetch's **distance**. Attempt 2 tests its
**coverage**, which is a different change and is aimed at the cause attempt 1
identified.

**The mechanism, from the attempt-1 campaign.** At 1 MiB the copy moves 2 MiB
of traffic. Two rates appear in the data, and they are not a host effect:

* **32 B/cycle** (2 MiB in ~23 000 ns = 91 GB/s) — the loop's **store-port
  ceiling** on this core. This is the fast mode.
* **27 B/cycle** (2 MiB in ~27 700 ns = 76 GB/s) — the **memory-hierarchy**
  rate, and what the median shows for every kernel, ours and glibc's alike.

The gap between them is the loads not being covered in time. The shipped
kernel's software prefetch covers **one** 64 B line of the next 128 B
iteration (a T0 prefetch at +128 brings in the line containing it); the other
line is left to the hardware prefetcher. When the software prefetch arrives in
time the copy runs at the store ceiling; when it does not, the copy is
bandwidth-bound at 76 GB/s. The fast mode is the first case, and it is a
**per-(kernel, rep) lottery**: in the attempt-1 campaign `fast`, `pf_ctl`,
`pf_big` and `f4_baseline` each won it in 3–7 of their 45 reps, at
**different** reps, while `glibc` never won it in any of its 135.

**The construction.** In the 1 MiB band only, issue a **second prefetch pair**
at **+192**, so both 64 B lines of the next iteration are covered by software
instead of one. Everything else is the shipped kernel: the first pair stays at
+128, the band stays 512 KiB ≤ n < 2 MiB, the distance knob keeps the shipped
128 B value (attempt 1's 4096 B is *not* carried over — it was harmful).

**The control.** The second prefetch's address is a runtime value, so the band
loop is re-scheduled. `src/rungs/pf_ctl2.c` is the control: the same shape with
the second pair at **+128**, i.e. a duplicate of the first pair — the same
instruction count, no extra coverage — so the shape is priced separately. It
is built and measured **before any promote**, and only if P3 passes.

**Criteria — unchanged from the pre-registration above.**

* **P1** every certified row (8/16/32/64/128/256/512 B, 16 KiB) unchanged
  within the null band versus the Phase 7.4 deliverable, same windows. The
  band starts at 512 KiB and the prefetch is off below 64 KiB, so **no
  certified row can be affected at all** — this is a structural precondition,
  not a hope.
* **P2** the 1 MiB floor preserved: not worse than the Phase 7.4 reference
  (+12.58 / +11.10 / +9.42%) on any target.
* **P3** the 1 MiB median moved to ≥ +1.5% (the Tier A floor, or the null band
  where larger) against glibc on ≥ 2 of 3 targets.

**Stop conditions — unchanged.** S1 → REJECT if P1 fails. S2 → REJECT if P2
fails; the win at the floor is the asset. S3 → **this is the last attempt**: if
P3 fails, **accept parity at 1 MiB**, document it in NEGATIVE_RESULTS.md, and
close Phase 8. There is no attempt 3.

**Why this is the last attempt, stated before measuring.** Every other
mechanism on Phase 8.1 Q3's list is closed by evidence:

* the software prefetch's distance — the alignment-unroll grid, and attempt 1;
* the aligned main loop and the unroll (4/6/8/12) — the alignment-unroll grid:
  "only unroll 4 with the aligned loop and a gated +128 B prefetch survived";
* `PREFETCHW` on the destination — "no size at which it wins";
* NT stores — the ladder rung already writes **full 64 B lines** (four
  `_mm256_stream_si256` per 128 B iteration), so its 17–22 GB/s is not a
  granularity artifact, and the sweep found "no size at which NT wins on any
  target" (a 69–372% loss);
* L2/L3 residency and page-boundary crossing — not kernel-actionable: the
  working set's residency is host state, and the buffers are already
  `MADV_HUGEPAGE`d.

So attempt 2 is the last lever, and it is aimed at the identified cause.

## Attempt 2 result — REJECTED (S2 and S3). 1 MiB is closed as parity.

Campaign: `docs/data/raw/phase7_p832_<target>.windows.log`, 9 windows x 5 reps,
3960 REPS lines per target, **zero migrated reps**.

| criterion | result | verdict |
|---|---|---|
| **P1** the certified rows unchanged within the null band | every cell inside its limit; the largest move is 16 KiB on znver3 at +1.07%, inside its 1.50% limit | **PASS** |
| **P2** the 1 MiB floor preserved | +10.31 / **+8.50** / +10.90% against the Phase 7.4 reference's +12.58 / +11.10 / +9.42% | **FAIL on 2 of 3** |
| **P3** the 1 MiB median moved to ≥ +1.5% | −2.14 / −1.85 / −2.32% — 0 of 3 | **FAIL** |

**The decomposition.** At 1 MiB:

| effect | floor | median |
|---|--:|--:|
| the loop shape (`pf_ctl2` vs `fast`) | +1.91 / +1.52 / +1.60% | −0.44 / −0.63 / −0.11% |
| the extra coverage (`pf_cov2` vs `pf_ctl2`) | −0.77 / −1.53 / −1.29% | **+0.37 / +0.81 / −0.07%** |

**The extra coverage did essentially nothing to the median** — +0.37, +0.81 and
−0.07 points, i.e. inside the noise — and cost 0.8–1.5 points at the floor.
The shape change is again free at the median. The candidate's median
(−2.14 / −1.85 / −2.32%) is the shipped kernel's median (−2.07 / −2.03 /
−2.14%): **the coverage hypothesis is falsified.** Covering both lines of the
next iteration in software does not stop the loads from stalling, which means
the hardware prefetcher was already covering them and the stall is elsewhere.

**S3 fires. 1 MiB is accepted as parity and Phase 8 is closed.** There is no
attempt 3, and there is no mechanism left to attempt it with: every entry on
Phase 8.1's Q3 list is now closed by evidence, the two attempts here
(distance, coverage) are both measured, and the mechanism analysis says the
median is the memory-hierarchy's rate for a 2 MiB working set — 27 B/cycle,
which is what glibc gets too.

### What the four campaigns say about the published 1 MiB row

The shipped kernel's 1 MiB floor against glibc, in all four campaigns that
measured it:

| campaign | znver2 | znver3 | v3 |
|---|--:|--:|--:|
| phase7_4 (the reference) | +12.58% | +11.10% | +9.42% |
| phase8_2 | +10.57% | +9.20% | +10.81% |
| phase8_3 attempt 1 | +10.08% | +11.92% | +11.08% |
| phase8_3 attempt 2 | +9.16% | +8.50% | +10.60% |

The **value** moves by about ±2 points from campaign to campaign — it is a
best-of-45 lottery draws, and the lottery is the fast mode. The **verdict**
does not: every one of the twelve cells is a win at the floor (the lowest is
+8.50%) and every one of the twelve medians is between −1.55% and −2.32%,
i.e. parity. That is the row's honest shape, and it is what
[NEGATIVE_RESULTS.md](NEGATIVE_RESULTS.md) records.
