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
