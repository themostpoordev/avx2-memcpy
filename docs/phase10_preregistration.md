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

## Amendment after the code review (committed before any measurement)

A code review of the harness change found four defects that would have made
the numbers mean the wrong thing. All four are fixed; the criteria above are
unchanged. The amendments are implementation details, not changes of what is
measured.

1. **The canary pass wrote outside the allocation.** The canary base was
   `g_dst`, but the allocation's slack is *after* the returned pointer, so the
   lower canary region was 64 bytes outside the mapping — overwriting glibc's
   mmap chunk header on every iteration. The canary base is now
   `g_dst + VCAN`, as `verify_large()` already does, and the buffers are freed
   on the mixed exit path (the leak was hiding the corruption).
2. **No warm-up.** The implementation that ran first in the registry paid a
   cold-predictor and cold-cache penalty the others did not: the aggregate
   moved by 5.7 points and the 8 B per-size delta by 44 points **with a sign
   flip** between the two orders. Every implementation now runs the sequence
   once, untimed, before its timed pass, so all of them start from the same
   predictor and cache state. A `--mixed-reverse` diagnostic flag was added so
   the order sensitivity can be re-measured.
3. **The timer overhead was taken from the fixed mode's batch calibration.**
   That constant is subtracted once per *batch* there (up to 1024 copies), so
   its per-copy error is `e/1024`; subtracting it per *call* makes it `e`,
   1024x larger, and the in-situ cost of a clock pair is not the cost of a
   clock pair in a tight loop. A null kernel is now timed through the same
   call path, inside the same loop, every 64 iterations, and the median of
   those is the overhead that applies. It is common to every implementation in
   a rep, so it cancels in the deltas.
4. **The per-copy residual was clamped at zero.** Clamping is a non-linear
   transform whose bias depends on the true copy time, so it distorts the
   comparison between two kernels. The residual is now left unclamped and the
   count of negative residuals is reported: a negative residual means the
   clock's jitter exceeded the copy, which is the honest reading at the
   smallest sizes, where a 2.7 ns copy is measured on a ~40 ns clock.

Also: the mixed path now runs the **fixed-mode `--verify`** for every selected
implementation before the campaign, so the mixed run is not the only
correctness gate; the size set is checked against the allocation; `--mixed`
warns that it ignores `--sizes` and `--quick`; and the real iteration counts
are recorded: **3990 per rep for D1** (285 copies of each of the 14 sizes) and
**3996 for D2** (444 copies of each of the 9), because the sequence length is
rounded down to a multiple of the size set so the multiset stays exactly
balanced. The per-size counts are as declared; the sequence length is 3990
rather than 4000, and the report states it.

**What this mode cannot do, and does not claim to do.** A clock pair costs
~40 ns on this host and an 8 B copy takes ~2.7 ns, so the 8–64 B per-size
rates are a measurement of the clock's jitter around the copy, not of the
copy. The negative-residual count is reported for every run, and the per-size
numbers must be read with it. The aggregate is byte-diluted by the same
overhead and is not affected: the overhead is a constant per call, common to
every implementation and every size, so it cancels in the deltas.

## Results

Campaigns: `docs/data/raw/phase10_d1_<target>_r<rep>.windows.log` and
`docs/data/raw/phase10_d2_<target>_r<rep>.windows.log` (9 windows x 5 reps per
window, three independent repetitions, per target; 3990 iterations per rep for
D1, 3996 for D2). Summaries: `docs/data/phase10_d1_summary.csv` and
`docs/data/phase10_d2_summary.csv`, produced by
`scripts/phase10_analyze.py`.

### A1 verdict: FAIL on both attempts

| | znver2 | znver3 | v3 |
|---|--:|--:|--:|
| D1, aggregate floor delta, per repetition | −15.29 / +1.42 / +4.60% | −5.42 / −0.16 / −2.95% | +5.64 / +2.92 / +1.29% |
| D1, aggregate floor delta, median of the 3 | +1.42% | −2.95% | +2.92% |
| D1, **total-time** ratio, median of the 3 | +1.26% | +4.38% | +6.02% |
| D2, aggregate floor delta, per repetition | +25.84 / +215.12 / −596.06% | +28.16 / +27.84 / +15.64% | +50.52 / +18.22 / +50.71% |

A1 (the aggregate floor delta ≥ +1.5% on ≥ 2 of 3 targets) is **not met**:
one target of three clears it on D1, and the D2 numbers are not a verdict at
all (a +215% and a −596% in the same distribution are not a measurement of a
kernel).

**Why the pre-registered statistic does not survive the mixed workload.** In
fixed mode the per-rep spread of a cell is a few percent, so the minimum over
45 reps is a meaningful floor. In mixed mode **glibc's own per-rep spread of
the aggregate is 178–222%** — the byte-weighted aggregate is dominated by the
4 MiB copies, and 4 MiB is the size this project already calls unmeasurable
(117–197% spread). A minimum over 45 samples of a 200%-spread quantity is an
extreme order statistic, not a floor, and the run-to-run swing of the D1
aggregate (−15.29% to +5.64% for the same kernel, the same seeds, the same
tree) is larger than the 1.5% acceptance threshold. D2 is worse in a different
way: every size is ≤ 4 KiB, so the ~40 ns per-call clock is ~80% of the
measured time and the aggregate measures the clock.

### What does resolve: the per-size rows

The representative mixed-mode number is the **median over the 45 reps** of each
target, not the minimum: the minimum is not meaningful when the per-rep spread
is two orders of magnitude larger than in fixed mode. Medians over the 9
target x repetition cells, with the fixed-mode delta for comparison:

| size | mixed-mode delta (znver2 / znver3 / v3) | median | cells ≥ 0 | fixed-mode delta | |
|---|--:|--:|--:|--:|---|
| 8 B | +8.76 / +37.02 / +13.19% | +14.96% | 7 of 9 | +25.49% | clock-bound |
| 16 B | +20.52 / +43.56 / +22.42% | +22.42% | 9 of 9 | +38.02% | clock-bound |
| 32 B | −6.00 / +25.97 / +13.59% | +14.53% | 6 of 9 | +25.49% | clock-bound |
| 64 B | +11.23 / +36.61 / +22.61% | +22.66% | 9 of 9 | +25.10% | clock-bound |
| 128 B | +0.02 / +13.04 / −23.91% | +0.02% | 5 of 9 | +10.11% | clock-bound |
| 256 B | −27.00 / +0.42 / −16.66% | −16.66% | 2 of 9 | +8.41% | clock-bound |
| 512 B | +0.35 / −15.66 / −36.15% | −15.66% | 3 of 9 | +14.59% | clock-bound |
| 1 KiB | +12.38 / +11.65 / +2.13% | +11.65% | 8 of 9 | −1.41% | |
| 4 KiB | −56.03 / −51.49 / −54.79% | **−55.09%** | **0 of 9** | +0.00% | |
| 16 KiB | −31.28 / −33.81 / −31.08% | **−31.83%** | **0 of 9** | −2.28% | |
| 64 KiB | −35.62 / −36.03 / −29.97% | **−32.23%** | **0 of 9** | −0.72% | |
| 256 KiB | −27.32 / −26.74 / −25.88% | **−26.74%** | **0 of 9** | −1.33% | |
| 1 MiB | −26.87 / −27.64 / −24.07% | **−27.04%** | **0 of 9** | +12.58% | |
| 4 MiB | +7.32 / +8.58 / +12.50% | +9.61% | 9 of 9 | +0.16% | unmeasurable size |

"clock-bound" means the per-copy time is not measurable at that size: a clock
pair costs ~40 ns here and an 8 B copy takes ~2.7 ns, so the per-size rate at
those sizes is the clock's jitter around the copy. **Those six rows are not
reportable**, and the question they were meant to answer — whether the 8–64 B
wins depend on a warm predictor — is **unanswered by this instrument**, not
answered "no".

### Two controls that make the negative result credible

1. **The per-call timing is fair.** A diagnostic sequence of a single size
   (`--mixed-size N`, not one of the declared distributions) gives, at 1 KiB,
   17.96 ns/copy for glibc and 17.66 for the kernel, and at 4 KiB 58.33 and
   58.27 — agreement to 0.1%, and the same shapes as the fixed-mode numbers
   plus the residual overhead bias. So the loss below is caused by the
   *interleaving*, not by the instrument.
2. **The implementation order no longer moves the result.** The same D1
   sequence, run with the implementations in registry order and reversed, gives
   aggregate deltas of +11.96% and +11.33% (0.6 points apart; before the
   warm-up fix the two orders were 5.7 points apart and the 8 B per-size delta
   flipped sign).

### What the mechanism is, and what it is not

The size that carries the most bytes in D1 is 4 MiB, and 4 MiB is the size
this project already calls unmeasurable, so the byte-weighted aggregate is
largely a statement about 4 MiB. The rows that resolve — 1 KiB to 1 MiB — say
the kernel is 27–55% behind glibc in a mixed stream, unanimously (0 of 9 cells
at each of the five sizes). The mechanism is not isolated: this host exposes
no L3/LLC events, so "the cold destination and a cold predictor cost this
kernel more than they cost glibc" is an **inference**, while the measurement is
the nine cells.

### Q5 applied

A1 is not met, so the fixed-size wins do not generalise to a mixed-size
workload, and the headline says so. The wording states precisely what failed:
the mixed-size **aggregate** is not a verdict (it is dominated by the
unmeasurable 4 MiB copies, and the pre-registered minimum-over-45-reps floor
is not a meaningful statistic at a 200% per-rep spread), and where the
mixed-size measurement does resolve — 4 KiB to 1 MiB — the kernel is **27–55%
behind glibc**. The sizes at and below 512 B are reported as unmeasured, not as
failures.
