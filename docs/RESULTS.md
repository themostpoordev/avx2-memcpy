# Results — memcpy_fast against glibc on Zen 2

## Reference campaigns

Every number below comes from a **committed** reference campaign
([`docs/data/raw/`](data/raw/)) — 9 windows x 5 reps per target, `taskset -c 2`,
`migrated=0` on every rep, glibc measured in the same process and the same
windows. Two campaigns are in play, and every row says which one it comes from:

| campaign | log | reference for |
|---|---|---|
| **phase7_4** | [`phase7_p74_<target>.windows.log`](data/raw/) | every size except 16 KiB |
| **phase8_2** | [`phase7_p82_<target>.windows.log`](data/raw/) | the 16 KiB row (re-certified in a quiet window) |

The per-size summaries are
[`docs/data/phase7_4_summary.csv`](data/phase7_4_summary.csv) and
[`docs/data/phase8_2_summary.csv`](data/phase8_2_summary.csv), regenerated
from those logs by `python3 scripts/phase7_4_summary.py` and
`python3 scripts/phase8_2_summary.py`. The tables below are generated from the
CSVs by `python3 scripts/phase7_4_tables.py` and checked against them by
`python3 scripts/check_published.py`, so no published number is typed by hand.

Two statistics are reported for every row, and they mean different things:

* **Δ_min** — the *floor*. The fastest single sample observed, against glibc's
  own floor. This is the statistic the claim model is built around.
* **Δ_med** — the *typical*. The median of the 45 per-rep samples.

A row is a certified win only if it clears its floor under **both**.

The shipped kernel is the **Phase 7.4 promote**: the F4 shape plus the 128 B
case that gives `n == 128` a 64-byte block pair (four loads and four stores,
no redundancy) inside the 128–256 B window, compiled with `-falign-labels=64`
so the 32–64 B path keeps the 64-byte alignment of its out-of-line branch
target. Its `.text` md5 is `0eb00867a4688a618d0e4329195c188d` on znver2 and
znver3 and `368680ed8dac9be06d0c1d382b5dfc6c` on v3.

The promote was measured against the kernel it replaced in the same windows
(`f4_baseline` in the campaign, the pre-promote F4 verbatim). The trade it makes
is documented in [NEGATIVE_RESULTS.md](NEGATIVE_RESULTS.md): **128 B is won,
512 B loses 2.24 points of margin and stays a certified win.** The mechanism is
in `docs/phase7_3a_mechanism.md`: the window body that dispatches the 64-byte
pair sits *before* the flat class chain, so the branch that adds it displaces
the chain, and the 512 B path's position in the code stream moves with it.

## Certified wins (Tier A, both min and median clear the floor)

| size | Δ_min (znver2 / znver3 / v3) | Δ_med (znver2 / znver3 / v3) | spread% | tier | verdict | reference |
|--:|--:|--:|--:|:--:|:--:|:--|
| 8 B | **+25.49** / **+25.49** / **+38.93** | **+30.67** / **+24.67** / **+39.95** | 3.2/4.7/5.4 | A/A/A | WIN x3 | phase7_4 |
| 16 B | **+38.02** / **+38.02** / **+50.31** | **+27.44** / **+27.44** / **+50.00** | 16.5/16.6/18.3 | B/B/A | WIN x3 | phase7_4 |
| 32 B | **+25.49** / **+25.10** / **+25.45** | **+5.05** / **+25.10** / **+25.10** | 22.7/29.6/30.6 | A/B/A | WIN x3 | phase7_4 |
| 64 B | **+25.10** / **+25.49** / **+25.49** | **+19.19** / **+25.10** / **+25.10** | 11.6/11.2/11.6 | A/A/A | WIN x3 | phase7_4 |
| 128 B | **+10.11** / **+10.11** / **+10.11** | **+45.22** / **+10.11** / **+10.11** | 61.3/60.6/61.6 | A/A/A | WIN 2/3 | phase7_4 |
| 512 B | **+14.59** / **+14.59** / **+14.59** | **+6.28** / **+13.47** / -34.90 | 39.5/60.5/38.0 | B/A/A | WIN 2/3 | phase7_4 |

**Read the tier column as the measurement's stability, not the claim's.** Tier A
means the per-window spread is ≤ 10%, so the floor is `max(1.5%, the null
band)`; Tier B means the row is bimodal, the floor is 25%, and only a wide
margin can clear it. 16 B is Tier B on znver2 and znver3 and still clears
(+38.02% and +38.02% against a 25% floor). **128 B and 512 B clear their floor
on two of the three targets**: on v3 the null band at 128 B is 18.35%, and on
znver2 the 512 B cell is Tier B with a 25% floor — so +10.11% and +14.59% read
`measured` there. The deltas themselves are identical on all three targets;
the floors are not.

**128 B is the win that took the most work.** It was a certified loss of
−10.04% before the promote and is now **+10.11%** on every target — the first
time this kernel is ahead of glibc at that size. The cause was structural: at
exactly 128 B the window's 128-byte block pair had its head and tail blocks
coincident, so the kernel issued eight loads and eight stores for a 128 B copy
where glibc issues four and four. Its own 128 B copy used to cost *more* than
its 256 B copy.

**512 B's median is bimodal, and that is a property of the host.** The shipped
kernel's own 512 B median reads 5.7891 ns in the Phase 7.2b campaign and
9.4688 ns in the reference one — the same kernel, the same size, the same host,
two campaigns — while its floor reads 5.7891 ns in both. The −34.90% median on
v3 is which mode that rep's samples landed in, not a regression: see
[How the numbers were produced](#how-the-numbers-were-produced) and
[NEGATIVE_RESULTS.md](NEGATIVE_RESULTS.md).

**8 B is reported too.** It is not on the 13-point ladder, but the knob moves
the `n <= 15` class, so the size is measured and reported rather than left
unsaid.

## Certified wins at the floor, conditional at the median

**256 B: a certified win at the floor on all three targets — +8.41% ×3 — and a
tie at the median.**

| size | Δ_min (znver2 / znver3 / v3) | Δ_med (znver2 / znver3 / v3) | spread% | tier | verdict | reference |
|--:|--:|--:|--:|:--:|:--:|:--|
| 256 B | **+8.41** / **+8.41** / **+8.41** | **+0.00** / **+8.41** / **+8.41** | 42.6/67.9/69.8 | A/A/A | WIN x3 at the floor | phase7_4 |

The Phase 8.2 quiet-window campaign re-measured this size and left the row
where it was: the floor is **+8.41% on all three targets**, identical to the
Phase 7.4 value, and the median is +0.00 / −0.63 / +0.00% — so the
pre-registered criterion (median ≥ +1.5% on ≥ 2 of 3 targets) was **not met**,
the row keeps its Phase 7.4 reference, and it is documented here as a win at
the floor and a tie at the median. The spread (43–68%) says why: the row is
bimodal, and the same two modes the 1 MiB row shows are visible here.

## Parity at the median

**1 MiB: a win at the floor, parity at the median — permanently.**

| size | Δ_min (znver2 / znver3 / v3) | Δ_med (znver2 / znver3 / v3) | spread% | tier | verdict | reference |
|--:|--:|--:|--:|:--:|:--:|:--|
| 1 MiB | **+12.58** / **+11.10** / **+9.42** | -1.96 / -2.02 / -2.29 | 11.3/15.9/6.1 | A/B/B | WIN 1/3 at the floor | phase7_4 |

**The mechanism is documented in
[NEGATIVE_RESULTS.md](NEGATIVE_RESULTS.md).** At 1 MiB the copy runs at either
the loop's **store-port ceiling** (~32 B/cycle, 91 GB/s) or the
**memory-hierarchy rate** (~27 B/cycle, 76 GB/s) — and glibc only ever gets
the second. The kernel reaches the first in roughly a third of its reps
(14–15 of 45 in the reference campaign; 6–18 of 45 across the four campaigns
that measured it), by 13–16% when it does, and is 6–9% behind in the reps
where it does not. So the floor is a win and the median is a tie: **the kernel
is never consistently faster at this size, and never consistently slower
either.**

The floor is a **best-of-45-lottery-draws** statistic, and its value moves by
about ±2 points from campaign to campaign — the four campaigns that measured
the shipped kernel here read +12.58 / +11.10 / +9.42%, +10.57 / +9.20 /
+10.81%, +10.08 / +11.92 / +11.08% and +9.16 / +8.50 / +10.60%. The **verdict**
does not move: all twelve cells are wins at the floor (the lowest +8.50%) and
all twelve medians are between −1.55% and −2.32%, i.e. parity.

**Two variants were attempted and both failed** (a longer prefetch distance,
then a wider prefetch coverage), each pre-registered, each with a shape
control, each measured in its own campaign. The prefetch distance cost 2–4% at
the floor; the extra coverage moved the median by +0.37 / +0.81 / −0.07
points, i.e. nothing. Every remaining mechanism on the list is closed by
evidence. The full record is in
[`docs/phase8_3_preregistration.md`](phase8_3_preregistration.md) and
[`docs/phase8_1_mechanism.md`](phase8_1_mechanism.md).

## Unmeasurable

**4 MiB.** The per-window spread is 117% / 197% / 99% — the campaign's worst
cell by a wide margin — so nothing at that size is resolvable and nothing is
claimed. The floor reads +0.16 / +6.91 / +1.49%, which is the spread talking,
not a result.

## Certified losses

**None.**

16 KiB was a certified loss of −2.28 / −2.41 / −2.40% in the Phase 7.4
campaign. It was re-certified in the Phase 8.2 quiet-window campaign and is
now a **tie**: −1.19 / −1.19 / −0.12% at the floor, inside the 1.5% floor on
all three targets. That was not caused by any code change — the promoted
kernel was already **+1.16 / +0.12 / +1.05% faster** than the kernel it
replaced at that size. The Phase 7.4 number was a host artifact: the size is
memory-bound and the row follows the host.

The two rows that were losses before Phase 8 are now: **128 B a certified win**
(+10.11%) and **16 KiB a tie** (−1.19 / −1.19 / −0.12%).

## Method note

Every row is reconstructable from a committed window log in
[`docs/data/raw/`](data/raw/). `min_of_mins` — the floor, the smallest
single-batch time over every rep in every window — is the primary statistic;
the median of the 45 per-rep samples and the spread `(p99 − p25) / median` are
published beside it, and a row whose two statistics disagree is labelled
conditional rather than resolved in favour of the more favourable one. Every
candidate was measured against the kernel it would replace, in the same
windows, on the same host state; the acceptance criteria were written down and
committed **before** the measurement, and each size got at most two attempts.
The pre-registration rule, the two-tier claim model, the width gate, the
evidence policy and the reproducibility gate are in
[METHODOLOGY.md §11](METHODOLOGY.md#11-reproducibility-gate).

## The shipped kernel, full ladder

| size | Δ_min (znver2 / znver3 / v3) | Δ_med (znver2 / znver3 / v3) | spread% | tier | verdict |
|--:|--:|--:|--:|:--:|:--|
| 16 B | **+38.02** / **+38.02** / **+50.31** | **+27.44** / **+27.44** / **+50.00** | 16.5/16.6/18.3 | B/B/A | WIN x3 |
| 32 B | **+25.49** / **+25.10** / **+25.45** | **+5.05** / **+25.10** / **+25.10** | 22.7/29.6/30.6 | A/B/A | WIN x3 |
| 64 B | **+25.10** / **+25.49** / **+25.49** | **+19.19** / **+25.10** / **+25.10** | 11.6/11.2/11.6 | A/A/A | WIN x3 |
| 128 B | **+10.11** / **+10.11** / **+10.11** | **+45.22** / **+10.11** / **+10.11** | 61.3/60.6/61.6 | A/A/A | WIN 2/3 |
| 256 B | **+8.41** / **+8.41** / **+8.41** | **+0.00** / **+8.41** / **+8.41** | 42.6/67.9/69.8 | A/A/A | WIN x3 |
| 512 B | **+14.59** / **+14.59** / **+14.59** | **+6.28** / **+13.47** / -34.90 | 39.5/60.5/38.0 | B/A/A | WIN 2/3 |
| 1 KiB | -1.41 / -1.41 / -1.41 | **+2.42** / **+1.61** / -71.87 | 44.7/42.7/43.2 | B/A/A | measured x3 |
| 4 KiB | **+0.00** / **+0.00** / **+0.00** | **+0.00** / -22.69 / -22.69 | 22.3/21.7/21.2 | B/A/A | measured x3 |
| 16 KiB | -1.19 / -1.19 / -0.12 | -1.56 / -3.85 / -2.32 | 39.6/38.9/4.2 | A/B/B | **measured x3** (a tie) |
| 64 KiB | -0.72 / **+0.00** / -0.80 | -0.79 / -1.58 / **+0.79** | 3.6/3.6/4.5 | A/A/A | measured x3 |
| 256 KiB | -1.33 / **+0.00** / **+0.40** | -2.28 / -1.21 / -1.55 | 7.8/7.8/10.9 | B/B/B | measured x3 |
| 1 MiB | **+12.58** / **+11.10** / **+9.42** | -1.96 / -2.02 / -2.29 | 11.3/15.9/6.1 | A/B/B | WIN 1/3 |
| 4 MiB | **+0.16** / **+6.91** / **+1.49** | **+4.47** / **+6.39** / **+1.82** | 117.1/196.9/99.2 | B/B/B | measured x3 |

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

**One warning about the median at 512 B and below.** The per-rep distribution at
those sizes is bimodal: a fast mode and a slow mode, ~471 ns apart per
128-copy batch, and which mode a rep lands in is a property of the *host*, not
of the kernel. The shipped kernel's own 512 B median reads 5.7891 ns in the
Phase 7.2b campaign and 9.4688 ns in the reference one — the same kernel, the
same size, the same host, two campaigns. Its floor reads 5.7891 ns in both.
That is why the claim model is built on the floor and why the median is
reported beside it rather than instead of it.

## What made the wins possible

Four things, in order of contribution. **Small-size specialisation:** at 16–64 B
the copy is 2–3 ns, so the branch structure *is* the kernel; the shipped kernel
takes the whole 1–64 B range from one compare and copies it with straight-line
overlapping moves. **The right block size at the class boundary:** the ladder's
rule is that a class window is `(H, 2H]` and the block pair uses `H` = the
class's lower bound, so the two blocks tile `[0, n)` exactly at `n = 2H`. The
128–256 B window used `H = 128` over a range that starts at 128, so the one
size where the ladder's own rule bites — 128 B — got a pair whose head and
tail blocks coincided. Giving that size `H = 64` is the 128 B win, and it is
worth +20 points at that size. **Per-size tuning instead of one loop:** the
512 B win comes from the block-pair shape chosen for that class, and the
256 B win from a dedicated 128–256 B window rather than from wider
instructions. **Measurement discipline:** every candidate was measured in the
same windows as the thing it replaced, so the changes that survived are the
ones that moved a floor, and the 128 B regression was visible rather than hidden
by a median.

## Scope and honesty notes

* **This tree is a curated subset of the experiment.** The public tree contains
  the shipped kernels and the seven-rung ladder. The shipped kernel's published
  rows **are** reconstructable: the reference campaigns' raw window logs are
  committed under `docs/data/raw/`, and `scripts/phase7_4_summary.py` and
  `scripts/phase8_2_summary.py` regenerate the summary CSVs from them and
  `scripts/phase7_4_tables.py` regenerates the tables in this file. The
  phase-2 ladder rows below are the older campaign, whose record is the
  committed phase-2 CSVs in `docs/data/`; the raw logs behind those CSVs did
  not ship and are not reconstructable here. The candidate kernels that lost
  (V9b, V9c variants, the prefetch-window grid, the PGO/LTO probes) stayed on
  the VPS, by design — a record that only lists winners is a sales sheet, and
  [NEGATIVE_RESULTS.md](NEGATIVE_RESULTS.md) is the record of what lost.
* Numbers are silicon-specific: one Zen 2 host, `taskset -c 2`, glibc as
  measured in the same process. Your machine will differ; measure before you
  switch. The reference host itself is shared, and its floor moves with load —
  see [METHODOLOGY.md §9](METHODOLOGY.md#9-host-variance-the-floor-moves).
* A loss is reported with the same prominence as a win. There are no certified
  losses left, and the row that was one — 16 KiB — was re-certified rather than
  argued away: it is a tie because a quiet-window measurement says so.
* **The floor is the floor, and the median is the truth.** Every row reports
  both. Where they disagree, the row says so: at 1 MiB the floor says +9 to
  +13% and the median says parity, and the honest summary is that the kernel
  is level with glibc at that size.
* **Nothing was retracted.** The rows that changed changed because a new
  measurement said so, and both the old and the new numbers are in the record:
  128 B was a loss in the Phase 6 campaign and is a win now, 16 KiB was a loss
  in the Phase 7.4 campaign and is a tie now, and each transition has its
  campaign committed under `docs/data/raw/`.

[^record]: **Record-derived.** Certified at the time of measurement, on a quieter
    host window. Reproduction on the current host depends on host load — see
    [METHODOLOGY.md §9](METHODOLOGY.md#9-host-variance-the-floor-moves). The
    committed window logs in [`docs/data/raw/`](data/) are the authoritative
    record.
