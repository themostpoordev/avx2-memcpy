# Results — memcpy_fast (F4) against glibc on Zen 2

Every number below comes from the **committed** reference campaign
[`docs/data/raw/f128b_*.windows.log`](data/raw/) — 9 windows x 5 reps, pinned to
one core, glibc measured in the same process and the same windows — with tier
verdicts from [METHODOLOGY.md](METHODOLOGY.md). Two statistics are reported for
every row: `min` (the floor) and `median` (the typical). A row is called a win
only if it clears its floor under both.

## Reference campaign

Every certified number below is derived from
[`docs/data/raw/f128b_*.windows.log`](data/raw/) — **committed**, one log per
target. The campaign recorded the shipped kernel under the name
`avx2_final_f4_win` (`avx2_final` plus F4's `TUNED_WINDOW`); the shipped source
is `src/fast.c`.

The per-size summary is
[`docs/data/f128b_reference_summary.csv`](data/f128b_reference_summary.csv),
regenerated from the committed logs by `python3 scripts/f128b_summary.py`. The
repo's own analyser reproduces the same numbers straight from the log:

```sh
sh scripts/analyze.sh docs/data/raw/f128b_znver2.windows.log \
   avx2_final_f4_win avx2_final znver2 memcpy
```

Two statistics are reported for every row, and they mean different things:

* **Δ_min** — the *floor*. The fastest single sample observed, against glibc's
  own floor. This is the statistic the claim model was built around.
* **Δ_med** — the *typical*. The median of the 45 per-rep samples on each side.

A row is called a win only if it clears its floor under **both**.

## Certified wins (min and median both clear the floor)

| size | Δ_min (znver2 / znver3 / v3) | Δ_med (znver2 / znver3 / v3) | spread% | tier | verdict |
|--:|--:|--:|--:|:--:|:--|
| 16 B | **+44.75 / +38.02 / +38.02%** | **+50.00 / +37.62 / +37.62%** | 18.7% | B / A / A | WIN x3 |
| 32 B | **+25.45 / +25.45 / +25.49%** | **+25.10 / +25.10 / +25.10%** | 31.6% | B / A / A | WIN x3 |
| 64 B | **+25.49 / +25.49 / +25.10%** | **+25.10 / +25.10 / +25.10%** | 16.6% | A / A / A | WIN x3 |
| 512 B | **+16.83 / +16.83 / +15.71%** | **+16.83 / +15.71 / +15.71%** | 66.3% | A / A / A | WIN x3 |

**4 certified wins over glibc on the measured silicon.** 16 B and 32 B are Tier B
on znver2 — that cell's per-window spread exceeds 10%, so its floor is 25% rather
than 1.5% — and clear that floor under both statistics. 64 B and 512 B are Tier A
everywhere.

Two of these deserve a second look:

* **512 B is bimodal but not statistic-dependent.** 24 of 45 reps land in the fast
  mode (~5.79 ns) and 16 in the slow mode (~9.55 ns), so the spread is 66%. The
  floor and the median both land in the fast mode and Δ_min and Δ_med agree to
  the digit (+16.83%). The distribution is wide; the verdict does not move with
  the statistic.
* **32 B on znver2 is the tightest cell in the table**: Δ_med +25.10% against a
  25.00% floor. A certified win, but by 0.10 percentage points — and the
  margin is an artefact of the *floor*, not of the kernel. That cell is Tier B
  because **one window of nine** came in at 2.37 ns against 1.86–1.87 ns in the
  other eight, and the tier is a max/min ratio over per-window minima, so a
  single noisy window moves the floor from 1.5% to 25%. The kernel's own floor
  agrees to 0.5% across those eight windows. The 25% floor is doing its job —
  it is saying "this cell is noisy, so only a difference bigger than 25% is
  trustworthy" — and the difference measured is 25.1%. The row is real; the
  margin is thin; both are stated rather than smoothed.

## Certified loss (Tier A)

| size | Δ_min (znver2 / znver3 / v3) | Δ_med (znver2 / znver3 / v3) | spread% | tier | verdict |
|--:|--:|--:|--:|:--:|:--|
| 128 B | **−10.04 / −10.04 / −10.04%** | **−10.04 / −10.04 / −10.04%** | 68.6% | A x3 | LOSS x3 |

The kernel buys the 16–64 B band with one extra dispatch level, and 128 B pays
about one timer step for it. It is disclosed, not tuned away, and the loss is
identical under the floor and under the median.

**The cause is known and the fix is not free.** At exactly 128 B the window's
128-byte block pair has its head and tail blocks coincident, so the kernel
issues 8 loads and 8 stores for a 128 B copy where glibc issues 4 and 4 — this
kernel's 128 B copy costs *more* than its own 256 B copy, on all three
targets. Two Phase 7 experiments confirmed the cause and were still
**rejected**: giving that size the 64-byte pair is worth **+18.31%** there
(2.7969 ns against glibc's 3.1000 ns — ahead of glibc, for the first time),
but the first attempt cost 16% at 32 B and 64 B through a branch-target
alignment shift, and the second attempt — with the alignment restored, and
with 32 B and 64 B clean — cost 2.7% at 512 B, which is a certified row. The
full record, with the acceptance tests that were fixed before each measurement
ran, is in [NEGATIVE_RESULTS.md](NEGATIVE_RESULTS.md). The shipped kernel keeps
its window unchanged, and this loss stands.

## Measured, not claimed (Tier B — inside the 25% floor)

| size | Δ_min (znver2 / znver3 / v3) | Δ_med (znver2 / znver3 / v3) | spread% | tier | verdict |
|--:|--:|--:|--:|:--:|:--|
| 256 B | +8.41 / +8.41 / +8.41% | +8.41 / +8.41 / +8.41% | 68.6% | B / A / A | win on 2 of 3, `measured` on znver2 |
| 1 MiB | +7.95 / +10.67 / +8.28% | −2.67 / −1.36 / −1.81% | 17.9% | B x3 | `measured` x3 |

**256 B** is a win on znver3 and v3 (Tier A, 1.5% floor) and lands inside its 25%
floor on znver2 (Tier B) — the same three-target split the row has always had.

**1 MiB is the one headline row where the two statistics tell different stories,
and it matters.** At the floor the kernel is +8 to +11% ahead; at the median it is
level to 2.7% *behind*. Both sit inside the Tier-B 25% floor, so neither is a
claim — but a reader who takes only the floor number will overestimate the 1 MiB
result. The honest summary is: **at 1 MiB this kernel is level with glibc.**

**Unmeasurable:** 4 MiB. Builds sharing two of three knobs spread 21–30% in the
same windows, so nothing at that size is resolvable and nothing is claimed.

**Opt-in, `memcpy_mid` (1 KiB <= n <= 64 KiB):** **16 KiB +7.13% / +5.95% /
+7.13%**, Tier A on all three targets (193.0 ns against glibc's 210.2 ns on
znver2).[^record] Outside 16 KiB the band is level, which is why it is opt-in and
not the general path.

## Conditional — statistic-dependent rows

**There are none.** A row is labeled conditional when it is a win under one
statistic and a loss (or a tie) under the other. Across all 39 cells of
[`f128b_reference_summary.csv`](data/f128b_reference_summary.csv) exactly two
disagree, and neither is a headline row: 4 MiB on znver2 (tie at the floor,
+5.43% at the median) and 4 KiB on v3 (tie at the floor, −1.51% at the median).
4 MiB is unmeasurable and 4 KiB is inside the 1.5% floor either way.

The section stays because the case is real and the rule is written down in
[METHODOLOGY.md §11](METHODOLOGY.md#11-reproducibility-gate): a future campaign
that produces a genuinely statistic-dependent row must be labeled here, not
quietly resolved in favour of the more favourable statistic.

## Retracted

**Nothing is retracted.** A Phase 6 audit re-derived every published number from
the committed logs and found that all of them reproduce exactly. The earlier
concern — that the certified campaign's window logs were missing — was itself the
error: the campaign was in the tree all along, and the summary CSV that carried
its numbers was never committed. Both are now.

The retraction mechanism stays for the same reason the conditional section stays.
Rows retracted earlier keep their retraction paragraphs in
[NEGATIVE_RESULTS.md](NEGATIVE_RESULTS.md), and removing one is not permitted
([METHODOLOGY.md §11](METHODOLOGY.md#11-reproducibility-gate)).

## Method note

Every headline row above is reconstructable from the logs in
[`docs/data/raw/`](data/raw/): 3 targets, 9 windows x 5 reps, `migrated=0` on
every rep, glibc measured in the same process and the same windows. The summary
CSV is regenerated by `python3 scripts/f128b_summary.py`.

Two statistics are reported side by side for every row: **Δ_min**, the floor (the
fastest single sample, on each side), and **Δ_med**, the typical (the median of
the 45 per-rep samples). A row is a certified win only if it clears its floor
under both. Rows whose distribution is bimodal (spread > 50%) are reported with
both statistics, and if the two disagree the row is labeled conditional rather
than resolved in favour of the more favourable one. Nothing is retracted: every
published number reproduces from the committed log.

Rule going forward: no published row without a committed window log
([METHODOLOGY.md §11](METHODOLOGY.md#11-reproducibility-gate)).

[^record]: **Record-derived.** Certified at the time of measurement, on a quieter
    host window. Reproduction on the current host depends on host load — see
    [METHODOLOGY.md §9](METHODOLOGY.md#9-host-variance-the-floor-moves). The
    frozen CSVs in [`docs/data/`](data/) are the authoritative record.

## The shipped kernel, full ladder

`memcpy_fast` (`src/fast.c`, F4), gcc `-O3 -fno-crossjumping`, 32 B-aligned main
loop, unroll 4, +128 B prefetch gated at 64 KiB, a one-compare tiny path for
n <= 64, and a straight-line 128–256 B window.

| size | Δ_min (znver2 / znver3 / v3) | Δ_med (znver2 / znver3 / v3) | spread% | tier | verdict |
|--:|--:|--:|--:|:--:|:--|
| 16 B | **+44.75 / +38.02 / +38.02%** | **+50.00 / +37.62 / +37.62%** | 18.7 / 17.6 / 16.7 | B / A / A | WIN x3 |
| 32 B | **+25.45 / +25.45 / +25.49%** | **+25.10 / +25.10 / +25.10%** | 31.6 / 32.0 / 30.8 | B / A / A | WIN x3 |
| 64 B | **+25.49 / +25.49 / +25.10%** | **+25.10 / +25.10 / +25.10%** | 16.6 / 12.6 / 6.6 | A / A / A | WIN x3 |
| 128 B | −10.04 / −10.04 / −10.04% | −10.04 / −10.04 / −10.04% | 68.6 / 66.2 / 68.9 | A / A / A | **LOSS x3** (Tier A) |
| 256 B | +8.41 / +8.41 / +8.41% | +8.41 / +8.41 / +8.41% | 68.6 / 68.3 / 68.6 | B / A / A | WIN 2/3 (measured on znver2) |
| 512 B | **+16.83 / +16.83 / +15.71%** | **+16.83 / +15.71 / +15.71%** | 66.3 / 63.5 / 64.8 | A / A / A | WIN x3 |
| 1 KiB | −1.41 / −1.41 / −1.41% | −1.41 / −1.41 / −1.41% | 74.4 / 73.7 / 76.0 | A / A / A | measured x3 |
| 4 KiB | +0.00 / +0.00 / +0.00% | +0.00 / +0.00 / −1.51% | 26.6 / 25.1 / 28.6 | A / A / A | measured x3 (LOSS at median on v3) |
| 16 KiB | −2.28 / −1.19 / −1.31% | −3.48 / −1.16 / −1.16% | 60.2 / 57.5 / 54.8 | A / A / A | measured 2/3, one certified LOSS (znver2) |
| 64 KiB | +0.00 / −0.08 / −0.08% | +0.00 / +0.00 / +0.00% | 6.8 / 5.2 / 3.2 | A / A / A | measured x3 |
| 256 KiB | −0.19 / +0.75 / +1.88% | −2.32 / −1.04 / −0.85% | 8.9 / 10.2 / 9.8 | B / B / B | measured x3 (Tier B) |
| 1 MiB | +7.95 / +10.67 / +8.28% | −2.67 / −1.36 / −1.81% | 17.9 / 19.0 / 15.4 | B / B / B | measured x3 (Tier B) |
| 4 MiB | +1.10 / +0.93 / −4.98% | +5.43 / +0.36 / +2.10% | 51.0 / 67.2 / 65.6 | A / B / B | **not usable** (bimodal, verdicts disagree on znver2) |

`spread%` = (p99 − p25) / median of the 45 per-rep mins.
spread > 50% indicates a bimodal distribution — for those
rows, read both min and median; do not select the more
favorable one.

"measured" means inside the applicable floor. It is not a win, not a loss and
not a tie-breaker. From 1 KiB to 64 KiB the kernel is level with glibc — the
interesting part of that result is that glibc's loop is the same 4x32 B loop, so
there is nothing left to win there by unrolling harder.

## The ladder

`min_of_mins` in ns, znver2, from the frozen CSVs in
[`docs/data/`](data/). Each rung is one file in `src/rungs/`; the delta and tier
verdict for every cell of every target are in those CSVs, not in this table.

This table is the **phase-2 reference ladder**, not the shipped kernel: its
record is the committed phase-2 CSVs in `docs/data/`, and it is `min_of_mins`
only, with no median column. The shipped kernel's rows are in the table above
and are reconstructable from the committed logs in `docs/data/raw/`.

| size | naive | word | sse2 | avx2 | avx2_nt | asm | rep |
|--:|--:|--:|--:|--:|--:|--:|:--|
| 16 B | 6.6 | 7.2 | 7.2 | 2.2 | 2.2 | 2.2 | - |
| 32 B | 12.4 | 4.4 | 3.1 | 2.5 | 2.5 | 2.8 | - |
| 64 B | 22.2 | 7.5 | 3.4 | 5.3 | 5.3 | 5.6 | - |
| 128 B | 47.6 | 12.5 | 4.0 | 5.9 | 5.6 | 5.6 | - |
| 256 B | 87.6 | 23.1 | 5.9 | 7.1 | 6.8 | 6.8 | - |
| 512 B | 167.7 | 43.1 | 10.2 | 9.5 | 8.6 | 9.2 | - |
| 1 KiB | 328.0 | 88.1 | 20.2 | 14.9 | 14.5 | 13.3 | - |
| 4 KiB | 1618.1 | 328.1 | 80.1 | 51.9 | 51.3 | 43.2 | - |
| 16 KiB | 6555.0 | 1287.2 | 338.0 | 240.2 | 237.8 | 225.2 | - |
| 64 KiB | 26891.0 | 5119.0 | 1462.0 | 1262.0 | 1253.0 | 1262.0 | - |
| 256 KiB | 105770.0 | 20519.0 | 6322.0 | 5290.0 | 12394.0 | 5270.0 | - |
| 1 MiB | 431003.0 | 82275.0 | 28153.0 | 24567.0 | 50746.0 | 24066.0 | - |
| 4 MiB | 1815394.0 | 350461.0 | 126228.0 | 123122.0 | 208393.0 | 126078.0 | - |

`rep` (`rep movsb`) has no frozen CSV of its own — this silicon has neither ERMS
nor FSRM, so it is a size-gated also-ran and is documented in
[NEGATIVE_RESULTS.md](NEGATIVE_RESULTS.md). Note what the ladder says about
dispatch: no single rung is fastest at every size, and the two most defensible
general choices (`avx2`, `asm`) are 2.5x off `sse2` at 64 B. That gap is what
`experiments/hybrid/` tried to route around and could not.

The predecessor kernel, V9c, is kept in `src/reference/` and built by
`make reference`. It is the same kernel without the 128–256 B window: 32 B and
64 B at +12.9%, 256 B at −16%, 128 B at −20.1%, 512 B at +13.5%. That is the
window's contribution, stated as a trade rather than a free win.

## How the numbers were produced

`min_of_mins` (the smallest single-batch time over every rep in every window),
9 windows x 5 reps with no early stop, `taskset -c 2`, `migrated=0` required, and
a per-cell floor of `max(1.5%, measured null band)`. Every row additionally
reports `median_of_mins` (the median of the 45 per-rep samples) and the spread
`(p99 − p25) / median`; a row is a certified win only if it clears its floor
under both statistics. The full contract, the two-tier claim model, the
audit-width gate, the evidence policy and the reproducibility gate are in
[METHODOLOGY.md](METHODOLOGY.md). Verified, not assumed: every kernel is checked
byte-for-byte against an independent reference with canaries before it is timed.

## What made the wins possible

Three things, in order of contribution. **Small-size specialisation:** at 16–64 B
the copy is 2–3 ns, so the branch structure *is* the kernel; the shipped kernel
takes the whole 1–64 B range from one compare and copies it with straight-line
overlapping moves. **Per-size tuning instead of one loop:** the 512 B win comes
from the block-pair shape chosen for that class, and the 256 B win from a
dedicated 128–256 B window rather than from wider instructions — that window is
what separates F4 from its predecessor. **Measurement discipline:** every
candidate was measured in the same windows as the thing it replaced, so the
changes that survived are the ones that moved a floor, and the 128 B regression
is visible rather than hidden by a median.

## Scope and honesty notes

* **This tree is a curated subset of the experiment.** The public tree contains
  the shipped kernels and the seven-rung ladder. The shipped kernel's published
  rows **are** reconstructable: the reference campaign's raw window logs are
  committed under `docs/data/raw/`, and `scripts/f128b_summary.py` regenerates
  the summary CSV from them. The phase-2 ladder rows in "The ladder" below are
  the older campaign, whose record is the committed phase-2 CSVs in
  `docs/data/`; the raw logs behind those CSVs did not ship and are not
  reconstructable here. The candidate kernels that lost (V9b, V9c variants, the
  prefetch-window grid, the PGO/LTO probes) stayed on the VPS, by design —
  a record that only lists winners is a sales sheet, and
  [NEGATIVE_RESULTS.md](NEGATIVE_RESULTS.md) is the record of what lost.
* Numbers are silicon-specific: one Zen 2 host, `taskset -c 2`, glibc as
  measured in the same process. Your machine will differ; measure before you
  switch. The reference host itself is shared, and its floor moves with load —
  see [METHODOLOGY.md §9](METHODOLOGY.md#9-host-variance-the-floor-moves).
* A loss is reported with the same prominence as a win. 128 B is 10% behind
  glibc and stays that way in this release.
* **The floor is the floor, and the median is the truth.** Every row reports
  both. Where they disagree, the row says so: at 1 MiB the floor says +8 to
  +11% and the median says level to 2.7% behind, and the honest summary is that
  the kernel is level with glibc at that size.
