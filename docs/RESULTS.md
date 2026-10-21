# Results — memcpy_fast (F4) against glibc on Zen 2

Every number below is `min_of_mins` over 9 windows x 5 reps, pinned to one core,
benchmarked against the same process's glibc, with tier verdicts from
[METHODOLOGY.md](METHODOLOGY.md). Nothing here is a median, a vendor claim or a
best-of run.

## Headline

**4 certified wins over glibc on the measured silicon** (Tier A on at least two
of the three targets; 16 B is Tier B on znver2, where it clears that cell's 25%
floor and is still a win):

| size | znver2 | znver3 | v3 | tier |
|--:|--:|--:|--:|:--|
| 16 B | **+44.75%** | **+38.02%** | **+38.02%** | B on znver2 (25% floor), A on znver3/v3 |
| 32 B | **+25.45%** | **+25.45%** | **+25.49%** | A on znver3/v3, B on znver2 |
| 64 B | **+25.49%** | **+25.49%** | **+25.10%** | A x3 |
| 512 B | **+16.83%** | **+16.83%** | **+15.71%** | A x3 |

**1 certified loss on all three targets:** 128 B **−10.04%** (Tier A, 1.5% floor).
The kernel buys the 16–64 B band with one extra dispatch level, and 128 B pays
about one timer step for it. It is disclosed, not tuned away.

**Tier-B measured** — above the 1.5% floor, below Tier B's 25% floor, so not
claims.[^record]

| size | delta | targets |
|--:|--:|:--|
| 256 B | **+8.41%** | win on 2 of 3 (znver2 lands inside its floor and is `measured`) |
| 1 MiB | **+7.95 / +10.67 / +8.28%** | all three, Tier B |

**Unmeasurable:** 4 MiB. Builds sharing two of three knobs spread 21–30% in the
same windows, so nothing at that size is resolvable and nothing is claimed.

**Opt-in, `memcpy_mid` (1 KiB <= n <= 64 KiB):** **16 KiB +7.13% / +5.95% /
+7.13%**, Tier A on all three targets (193.0 ns against glibc's 210.2 ns on
znver2).[^record] Outside 16 KiB the band is level, which is why it is opt-in and
not the general path.

[^record]: **Record-derived.** Certified at the time of measurement, on a quieter
    host window. Reproduction on the current host depends on host load — see
    [METHODOLOGY.md §9](METHODOLOGY.md#9-host-variance-the-floor-moves). The
    frozen CSVs in [`docs/data/`](data/) are the authoritative record.

## The shipped kernel, full ladder

`memcpy_fast` (`src/fast.c`, F4), gcc `-O3 -fno-crossjumping`, 32 B-aligned main
loop, unroll 4, +128 B prefetch gated at 64 KiB, a one-compare tiny path for
n <= 64, and a straight-line 128–256 B window.

| size | znver2 | znver3 | v3 | verdict |
|--:|:--|:--|:--|:--|
| 16 B | **+44.75%** | **+38.02%** | **+38.02%** | WIN x3 |
| 32 B | **+25.45%** | **+25.45%** | **+25.49%** | WIN x3 |
| 64 B | **+25.49%** | **+25.49%** | **+25.10%** | WIN x3 |
| 128 B | −10.04% | −10.04% | −10.04% | **LOSS x3** (Tier A) |
| 256 B | +8.41% | **+8.41%** | **+8.41%** | WIN 2/3 (measured on znver2) |
| 512 B | **+16.83%** | **+16.83%** | **+15.71%** | WIN x3 |
| 1 KiB | −1.41% | −1.41% | −1.41% | measured x3 |
| 4 KiB | 0.00% | 0.00% | 0.00% | measured x3 |
| 16 KiB | −2.28% | −1.19% | −1.31% | measured 2/3, one certified LOSS (znver2) |
| 64 KiB | 0.00% | −0.08% | −0.08% | measured x3 |
| 256 KiB | −0.19% | +0.75% | +1.88% | measured x3 (Tier B) |
| 1 MiB | +7.95% | +10.67% | +8.28% | measured x3 (Tier B) |
| 4 MiB | +1.10% | +0.93% | −4.98% | **not usable** (spread 21–30%) |

"measured" means inside the applicable floor. It is not a win, not a loss and
not a tie-breaker. From 1 KiB to 64 KiB the kernel is level with glibc — the
interesting part of that result is that glibc's loop is the same 4x32 B loop, so
there is nothing left to win there by unrolling harder.

## The ladder

`min_of_mins` in ns, znver2, from the frozen CSVs in
[`docs/data/`](data/). Each rung is one file in `src/rungs/`; the delta and tier
verdict for every cell of every target are in those CSVs, not in this table.

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
a per-cell floor of `max(1.5%, measured null band)`. The full contract, the
two-tier claim model, the audit-width gate and the evidence policy are in
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
  the shipped kernels and the seven-rung ladder. The frozen CSVs in `docs/data/`
  record the full VPS campaign; they cannot be regenerated from the public tree
  alone, and the raw window logs they came from stayed on the VPS.
* Numbers are silicon-specific: one Zen 2 host, `taskset -c 2`, glibc as
  measured in the same process. Your machine will differ; measure before you
  switch. The reference host itself is shared, and its floor moves with load —
  see [METHODOLOGY.md §9](METHODOLOGY.md#9-host-variance-the-floor-moves).
* A loss is reported with the same prominence as a win. 128 B is 10% behind
  glibc and stays that way in this release.
