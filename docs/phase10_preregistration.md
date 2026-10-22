# Phase 10 — mixed-size benchmark (pre-registration)

**PRE-REGISTRATION.** Written and committed **before** any harness code was
written and before any mixed-size measurement was taken. The distributions, the
metric, the fairness rule, the acceptance criteria and the headline rule below
are fixed. If attempt 1 fails A1, the retry uses **D2, which is declared here**
— not a distribution chosen after seeing attempt 1.

## Why this phase exists

Every published win was measured **fixed-size**: the harness copies the same
size 128 times per batch, in a tight loop, on buffers allocated once and
touched once, single threaded, with the branch predictor trained on that one
size. That is the best case for a size-specialised kernel. A real program
calls `memcpy` with mixed sizes in sequence, so the predictor is not warmed to
one size and the cache is not dedicated to one size's working set.

The 8–64 B wins (+25% to +50% against glibc) are the ones most likely to
depend on that warm-up: they are 2–3 ns copies where the branch structure *is*
the kernel. This phase measures the mixed-size case and reports what it finds.

## Facts this is built from (not invented)

* Fixed-mode results: the Phase 7.4 campaign, committed under
  `docs/data/raw/phase7_p74_*.windows.log`, summarised in
  `docs/data/phase7_4_summary.csv` (42 rows: 14 sizes x 3 targets).
* The tier model ([METHODOLOGY.md §3](METHODOLOGY.md)): **Tier A floor 1.5%**,
  **Tier B floor 25%**. No new tiers are introduced.
* The harness: `bench/bench.c`; existing `--sizes` / `--iters` / `--reps` /
  `--budget` / `--impl` flags; `taskset -c 2`; `CLOCK_MONOTONIC_RAW`;
  `min_of_mins` as the primary statistic.
* The 14 measured sizes (the 13-point ladder 16 B…4 MiB plus the separately
  reported 8 B row): 8, 16, 32, 64, 128, 256, 512 B, 1 KiB, 4 KiB, 16 KiB,
  64 KiB, 256 KiB, 1 MiB, 4 MiB.

The fixed-mode baseline this phase is compared against (Phase 7.4, Δ_min vs
glibc, znver2 / znver3 / v3):

| size | Δ_min | tier | size | Δ_min | tier |
|---|--:|:--:|---|--:|:--:|
| 8 B | +25.49 / +25.49 / +38.93 | A/A/A | 1 KiB | −1.41 / −1.41 / −1.41 | B/A/A |
| 16 B | +38.02 / +38.02 / +50.31 | B/B/A | 4 KiB | +0.00 / +0.00 / +0.00 | B/A/A |
| 32 B | +25.49 / +25.10 / +25.45 | A/B/A | 16 KiB | −2.28 / −2.41 / −2.40 | A/A/A |
| 64 B | +25.10 / +25.49 / +25.49 | A/A/A | 64 KiB | −0.72 / +0.00 / −0.80 | A/A/A |
| 128 B | +10.11 / +10.11 / +10.11 | A/A/A | 256 KiB | −1.33 / +0.00 / +0.40 | B/B/B |
| 256 B | +8.41 / +8.41 / +8.41 | A/A/A | 1 MiB | +12.58 / +11.10 / +9.42 | A/B/B |
| 512 B | +14.59 / +14.59 / +14.59 | B/A/A | 4 MiB | +0.16 / +6.91 / +1.49 | B/B/B |

## Q1. The distribution

### D1 — attempt 1: uniform over the 14 measured sizes

Each of the 14 sizes is drawn with probability **1/14**. Concretely, the
sequence for one rep is a **Fisher–Yates shuffle (fixed seed) of a multiset
containing exactly `iters/14` copies of each of the 14 sizes** — so the order
is mixed (which is what puts pressure on the branch predictor) while the count
per size is exactly balanced (so the per-size numbers are comparable).

**Defence, and its limits.** I cannot defend any specific real-world
size distribution from evidence available in this repository, and I will not
invent one. What I *can* defend is this: the 14 sizes are the project's own
measured sizes; a uniform draw over them is the only choice that privileges no
size; and because the aggregate metric is **byte-weighted** (Q2), the large
sizes dominate it — so **D1 cannot flatter the kernel**, whose wins are
concentrated in the sizes that contribute almost no bytes. D1 is a *neutral*
distribution, not a claim about real workloads, and it is used precisely
because it is neutral.

### D2 — attempt 2, declared now: uniform over the 9 sizes ≤ 4 KiB

Each of {8, 16, 32, 64, 128, 256, 512 B, 1 KiB, 4 KiB} is drawn with
probability 1/9; the sequence is again a Fisher–Yates shuffle of an exactly
balanced multiset.

**Defence.** "Every `memcpy` call is at most 4 KiB" — a small-buffer
workload, and the shape an application program (copying small structures and
page-sized buffers) most plausibly has. This is the distribution most
*favourable* to the kernel, because its call count is dominated by the sizes
where the fixed-mode delta is +25% to +50%. It is declared here, before any
measurement, precisely so that the retry cannot be distribution-shopped: if D1
fails and D2 passes, the report must still show D1's numbers and the per-size
table (Q5).

**Neither distribution is changed after seeing results.** If both fail, the
answer is the answer.

## Q2. The metric

**Primary — aggregate ns/byte.** `total_ns / total_bytes` over the whole run:
`total_ns` is the sum of the per-iteration times and `total_bytes` the sum of
the drawn sizes. This is byte-weighted: a size contributes in proportion to how
many *bytes* the distribution draws of it, not to how many calls. It is the
only aggregate that is comparable with the fixed mode, which reports a rate.

**Secondary — per-size ns/byte, bucketed by the drawn size.** For each size
`s`: `ns_per_byte(s) = (sum of the times of the iterations that drew s) /
(count(s) x s)`. Reported for every size that clears the count floor in Q4.

**Statistic.** As in the fixed mode, the primary statistic is **`min_of_mins`**:
the per-rep value (the aggregate, or a size bucket's rate) is computed per rep,
and the published figure is the **minimum over the 45 reps** (9 windows x 5
reps). The median across reps and the spread `(p99 − p25) / median` are
published beside it, exactly as the fixed mode does.

**Supplementary (declared; NOT part of the acceptance).** The **total-time
ratio**: the sum of all per-iteration times for our kernel against the same sum
for glibc, over the same sequence. This is the workload-level question — "is
this workload faster" — and it is call-count weighted rather than byte
weighted, so it can differ from the aggregate. It is reported so that the
headline cannot be read as more than the data supports; it does not enter A1.

## Q3. Fairness

* The size sequence is **pre-generated into an array** before any kernel runs.
* **The same array is fed to our kernel and to glibc.** One array per
  (window, rep); every implementation in that rep iterates that array in that
  order. The sequence is never re-drawn between implementations, and the
  array is regenerated (same fixed seed, advanced per rep) only when the next
  rep begins, so each rep has its own mixture but all implementations in a rep
  see the identical one.
* The **null band** is measured the same way: `naive` and `ref_scalar` iterate
  the same array in the same rep, so the band is a band of the same workload.
* The buffer discipline is the fixed mode's, applied **per iteration**: the
  64 B canary regions around the destination are re-armed before each copy and
  checked immediately after it, because the next iteration's size differs from
  the current one's and a canary written by a legitimate later copy must not
  be mistaken for an overwrite.
* The **time budget is disabled in mixed mode.** In fixed mode the budget
  truncates a case at 50 ms, which is how the 1 MiB and 4 MiB cases end up
  running 514–1639 of 4000 iterations. In mixed mode every implementation
  must run the *identical* full sequence, or the byte totals would differ
  between implementations and the comparison would not be on identical work.
* Same clock (`CLOCK_MONOTONIC_RAW`), same `taskset -c 2`, same
  `min_of_mins` definition.

## Q4. Acceptance

* **A1** — the aggregate mixed-mode ns/byte for F4 is **lower** than glibc's
  aggregate mixed-mode ns/byte by at least the **Tier A floor (1.5%)**, on the
  same run and the same sequence. A1 holds when it clears on **≥ 2 of the 3
  targets**; every per-target number is reported whether it clears or not.
* **A2** — a per-size mixed-mode delta against glibc is reported for every
  size that has **≥ 200 iterations in every rep**. D1 gives exactly
  `4000/14 = 285` per size per rep and D2 exactly `4000/9 = 444`, so every
  size clears the floor under both distributions; if a size somehow does not,
  it is reported as *not reportable* rather than given a number.

**Iteration count: 4000 per rep**, the same as the fixed mode's `--iters`.
**Reps: 9 windows x 5 reps**, the same shape as the fixed campaign, run **3
times** as three independent repetitions, so the run-to-run spread is visible.

## Q5. The headline rule (fixed before measuring, applied after)

* **A1 passes** → the aggregate win holds in mixed mode. The headline gains one
  sentence stating the aggregate mixed-mode result **and the distribution it
  was measured on**.
* **A1 fails** → the fixed-size wins do not generalise to mixed-size. The
  headline states that plainly: *"7 certified wins on fixed-size, warm-cache
  workloads; does not beat glibc in mixed-size workloads (distribution
  declared)."* The fixed-size table stays, clearly labelled.
* **Per size** — report which sizes' mixed-mode delta is ≥ 0% and which are
  < 0%. **Do not aggregate this into a "survives / fails" count that the data
  does not support.**
* **A precision this rule inherits from the metric (Q2).** If A1 fails but
  the supplementary total-time ratio is positive, the failure wording must
  say precisely what failed — that the **byte-weighted aggregate** is not
  better — and must not say "does not beat glibc in mixed-size workloads",
  which the total-time ratio could contradict. The headline states what the
  data shows and no more.

## Q6. Stop

* **S1** — if attempt 1 fails A1, one retry is allowed with a **different
  distribution**: **D2, declared above**. No third distribution exists.
* **S2** — three attempts do not exist.
* **S3** — if both attempts fail A1, accept the result and apply Q5's
  headline rule.

## What this phase may not do

* Only `bench/bench.c` changes. `src/fast.c` and `src/fast_impl.h` are not
  touched, so the shipped kernel's `.text` md5 must be unchanged on all three
  targets (0eb00867… on znver2/znver3, 368680ed… on v3).
* No new tier, no new statistic, no threshold moved after the fact.
* Every number in the report cites its log path; the mixed-mode runs write
  logs under `docs/data/raw/`.
