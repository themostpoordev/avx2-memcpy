# Methodology — how a number becomes a claim here

This project's only currency is a measurement you can re-run. Everything in
[RESULTS.md](RESULTS.md) passed the rules below; anything that did not pass is
reported as measured, as a loss, or not at all.

## 1. Measure the floor, not the median

Each timed rep records the fastest single batch inside it (`min_ns`, timer
overhead subtracted) and the median. On a shared host the medians move 20–43%
between windows while the minimum is stable to ~0.1% in the middle of the size
range — a minimum is a property of the code plus the machine, a median is a
property of whatever else ran.

So the signal is **`min_of_mins`**: the smallest `min_ns` observed across every
rep in every window for one `(impl, target, op, size)`. Medians and p99 are
recorded and published, and are never used in a claim.

## 2. Repetition and pinning

* **5 reps x 9 windows per cell, no early stop.** `min_of_mins` only decreases
  as windows accumulate, so a campaign that stops early is not comparable to one
  that ran to the cap. Nine windows is cheap.
* Every measurement is pinned (`taskset -c 2` on the reference host) and each
  rep samples its own CPU inside the timed region. A rep that migrated is
  dropped and counted; a campaign with migrations is not used.
* Reps for the compared implementations are interleaved inside one process, so
  an interference burst is shared rather than charged to one kernel.
* `gbps` is informational only: an idle clock inflates it.

## 3. The claim model: floor first, then tier

Every cell gets a **floor**, and a delta inside its floor is `measured` — not a
win, not a loss, not a tie-breaker.

1. **Base floor 1.5%.** Kept and published as `rule3` so that places where the
   flat floor was too tight stay auditable.
2. **Measured null band.** `naive` and `ref_scalar` are the same byte loop with
   byte-identical instruction streams. Their difference, measured **in the same
   windows at the same size**, is the instrument's own noise floor. The
   applicable floor is `max(1.5%, band)`; where the band is 20%+, a 1.5% "win"
   would be pure artefact.
3. **Tier assignment.** A cell whose per-window minimum spread is <= 10% is
   Tier A and is judged against `max(1.5%, band)`. A cell above that spread is
   Tier B and is judged against a flat 25% floor. Tier B exists so that a noisy
   cell can still say something without being able to claim a 3% win.
4. **Both verdicts are published side by side** in every CSV. When they
   disagree, the band-aware verdict wins.

A delta that does not clear its floor is written down as `measured`. That word is
load-bearing: it means "this instrument cannot tell these two apart", which is
often the honest result.

## 4. Verify first, then time

No kernel is benchmarked until it passes, on the exact binary being measured
(`make verify`):

* byte-for-byte equality against an independent golden reference for
  `n = 0..4096` with randomised source/destination offsets, then the full size
  list;
* **a fixed set of offset pairs per size, covering every alignment class the
  kernel branches on** — in particular both pointers 32-byte aligned, which is
  the only condition under which the aligned fast path is taken, and the
  "one pointer aligned, one not" case the alignment test exists for. Three
  random pairs per size reach the aligned path for 4 of 4096 pairs, i.e. for
  10 of the 4097 sizes; the fixed pairs make it 4097 of 4097;
* 64 B canaries on both sides of the destination, so an overlapping tail that
  writes outside `[0, n)` fails;
* a PMU-cycles cross-check, because `cycles/byte` is derived from raw cycles and
  the raw counter is published alongside it.

Two limits, stated rather than implied: the largest size (256 MiB) is compared
by content only, because that allocation has no room for canaries; and
`make gate` is a *separate*, static check (it reads the sources and the built
objects and never executes a kernel), so a green gate is not a statement about
the copy semantics.

## 4a. What a fixed-size result is, and what it is not

Every fixed-size number this project publishes is the **floor of a warm
predictor**: the harness copies the same size in a tight loop, on buffers
allocated once and touched once, single threaded, with the branch predictor
trained on that one size, and it reports the minimum over 45 reps. The minimum
is a meaningful floor because a cell's per-rep spread is a few percent.

That is the best case for a size-specialised kernel. A real program calls
`memcpy` with mixed sizes, so the predictor is re-learned per call and the
destination is not the one the previous call left in cache.

**The mixed-size measurement is the honest comparator for a real workload,
and it costs more than the statistic it needs.** In mixed mode a cell's
per-rep spread is 178–222% rather than a few percent, so the minimum over 45
reps stops being a floor and becomes an extreme order statistic; the
representative number is the **median over the reps**, and the per-call timing
resolves a copy no shorter than about 1 KiB on this host (a clock pair costs
~40 ns and an 8 B copy takes ~2.7 ns). The predictor cost is therefore
reported **per size**, with the sizes the instrument cannot resolve marked as
unmeasured rather than as failures.

The results are in [`docs/phase10_preregistration.md`](phase10_preregistration.md)
and the *Mixed-size workloads* section of
[RESULTS.md](RESULTS.md#mixed-size-workloads--where-the-fixed-size-wins-do-not-carry-over).
In short: the fixed-size wins do not carry over. At 4 KiB–1 MiB the kernel is
27–55% behind glibc in a mixed stream (0 of 9 target x repetition cells), and
at 512 B and below the measurement cannot resolve the copy at all, so those
wins are untested rather than refuted.

Two controls keep that negative result honest: a diagnostic single-size
sequence shows the per-call timing is fair (the two kernels agree to 0.1% at
1 KiB and at 4 KiB, so the loss is caused by the interleaving and not by the
instrument), and the implementation order no longer moves the result (0.6
points apart, after the warm-up fix that made it 5.7 points apart).

`make gate` runs before every campaign. A campaign whose tree fails the gate
writes no timing row at all.

## 5. The width gate

The harness is not the only check on the code; the object is checked too.
`make gate` disassembles every kernel translation unit, finds the widest
data-movement instruction that touches memory, and compares it with the width the
rung declares in the audit table. A mismatch fails the build. This catches the
class of bug where the vectoriser silently widens a "byte" loop — which is
exactly what happened once, and why `naive`, `word` and `ref_scalar` are compiled
with vectorisation disabled and are asserted to contain zero vector
instructions. It also fails on any AVX-512 (EVEX) encoding, because the
AVX2-only targets would SIGILL.

The same gate checks the dispatcher in `experiments/hybrid/` structurally: a
dispatch TU may contain no data movement of its own, and every symbol it calls
must be a width-gated rung.

## 6. Evidence policy

* The **frozen CSVs** in [`docs/data/`](data/) are the record. They are the
  `min_of_mins` tables the analysis produced, committed byte-for-byte.
* The raw window logs the **shipped kernel's** rows were derived from are
  committed under [`docs/data/raw/`](data/raw/), and
  `scripts/f128b_summary.py` regenerates the summary CSV from them. A log
  nobody can re-derive is not evidence, and a repo full of last night's output
  is not a record — which is why the reference campaign's logs ship, and the
  phase-2 ladder's logs (whose record is the committed CSVs) do not.
* A claim that is not in the CSVs and not in [RESULTS.md](RESULTS.md) does not
  exist. No number in the prose was typed in by hand from memory; every one is
  copied from an analysed log.

## 7. What this methodology rules out

* Comparing a kernel measured today with a baseline measured last week.
* Reporting a win from a median, a best-of-n, or a `gbps` figure.
* Selecting the winning size classes after the fact.
* Quoting a number from a different build of the same source and calling it the
  same kernel (three builds sharing two of three knobs spread 12% at 4 MiB — that
  size is unmeasurable here as a result, and is reported that way).
* Any claim whose tier is B being written as a win.

## 8. Reproducing a number

```sh
make clean && make -j$(nproc)     # build the harness and the three targets
make gate                         # audits + width gate; must pass
make bench -- --verify            # correctness pass on the exact binaries
taskset -c 2 ./bench/bench_znver2 --impl glibc,fast --sizes 16,64,512 \
    --reps 5 --iters 4000         # timed rows: one CSV line per cell
```

`--reps 5` is the protocol minimum. A single window is a screening run, not a
claim: the per-window spread is what decides the tier, and one window cannot
measure it.

## 9. Host variance: the floor moves

This is a shared VPS, not a lab bench. The measurement floor drifts with host
load, and it drifts on both sides of a comparison. On 2026-10-21, five
back-to-back runs of the same binary at 512 B put glibc's own `min_of_mins`
between 6.96 and 11.27 ns, while our kernel's floor moved with it; a later run of
the same sizes put both back near parity. Nothing in the code changed — the
`.text` md5 of the shipped object was identical across all of it.

The certified numbers were measured on quieter windows, on fixed-size
workloads, and the frozen CSVs in
`docs/data/` are the authoritative record of those windows. Two consequences:

* **A single-run comparison on a loaded host proves nothing**, in either
direction. Cross-session comparisons (today's kernel against last week's
baseline) are invalid by construction; only deltas measured in the same windows
count.
* **Reproduction requires a quiet host.** If a re-run disagrees with
`docs/data/`, check the per-window spread first: the row that moved is usually
the one whose baseline moved, not the kernel. Rows labelled *record-derived* in
[RESULTS.md](RESULTS.md) are exactly the ones where this was observed.

## 10. The public tree is a subset

The public tree contains the shipped kernels (`src/fast.c`, `src/mid.c`), the
seven-rung reference ladder (`src/rungs/`), one documented-loss experiment
(`experiments/hybrid/`) and the predecessor kernel as a non-default reference
(`src/reference/`, `make reference`). The frozen CSVs in `docs/data/` record the full ladder
that was run on the reference host, which included many more candidate kernels
and tuning knobs. The **shipped kernel's** rows are regenerable: the reference
campaign's raw window logs are committed under `docs/data/raw/` and
`scripts/f128b_summary.py` rebuilds the summary CSV from them. The **phase-2
ladder** rows are not: the sources for most of those candidates, and the raw
logs behind the phase-2 CSVs, stayed on the machine that ran the campaign, and
the CSVs are the frozen record of what was measured, not a build product of this
repository. See §11 for the rule that governs what may be published.

## 11. Reproducibility gate

**Rule 1 — No published performance row without a committed window log.** Every
number in RESULTS.md must be reconstructable from a log under
`docs/data/raw/`.

**Rule 2 — Bimodal sizes (spread > 50%) report both min and median.** A row that
is a win under one statistic and a loss under the other is labeled conditional,
not certified.

**Rule 3 — Retractions are permanent.** A row that does not reproduce keeps its
retraction paragraph in RESULTS.md and NEGATIVE_RESULTS.md. Removing a
retraction is not permitted.

**Rule 4 — The reference campaign (f128b) is the only committed campaign for
F4.** If a future campaign supersedes it, it is committed the same way
(`docs/data/raw/`), and the RESULTS.md headline is rewritten against the new
reference.

*Scope of Rule 1.* It governs the shipped kernel's published performance rows.
The phase-2 reference ladder in RESULTS.md ("The ladder") is context, not a
claim about the shipped kernel; its record is the committed phase-2 CSVs in
`docs/data/`, and it is `min_of_mins` only. Its raw logs did not ship.

*Current state.* The reference campaign is committed and every shipped-kernel row
reproduces from it. No row is currently conditional and none is retracted; both
sections are kept in RESULTS.md so that the next one that is has somewhere to
go.
