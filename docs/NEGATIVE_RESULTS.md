# Negative results

Every idea below was built or written, measured, and lost. They are here because a
tuning record that only lists wins is a sales sheet. Each entry says what was
tried, what the measurement said, and whether the code ships.

## Hybrid dispatcher — ships, in `experiments/hybrid/`

A size-class dispatch layer over the ladder, with the table generated from
measured `min_of_mins` rather than picked by hand. Over 51 cells (17 sizes x 3
targets) the tier verdicts were **2 wins, 22 losses, 27 unresolvable**. The
64 B–20 KB band lost on all three targets despite routing each size to our own
best rung, because the gap to glibc there is kernel quality, not routing. It is
the clearest negative result in the project: dispatch was glibc's strategy, we
did it with better data, and it did not close the gap.

## `PREFETCHW` on the destination — not shipped

The idea: prime the destination line in Exclusive state so the store does not pay
read-for-ownership. The variant was generated from the shipped body by string
substitution, with a control build that compiles the knob out; the control was
instruction-identical to the shipped kernel, so the pair delta is the
`prefetchw`. Result: **no size at which it wins**, and it competes with the
stores for the same resources. The mechanism on Zen 2 is that a full-line write
already merges in the write-combining buffer, so there is no ownership traffic
left to remove. Verdict: the shipped kernel keeps only the source-prefetch, as a
guard, and no `prefetchw` variant exists.

## `IFUNC` resolver — not shipped (the rung was measured, then dropped)

An IFUNC variant was built and measured in the same campaign as the plain kernel.
Its deltas were identical to two decimal places at every size, because **a
resolver never sees the arguments**: it cannot inspect `n`, `dst` or `src`, so it
can only choose on CPU features — which is exactly what glibc's loader already
does, with one extra indirection on the way in. Verdict: IFUNC is not a tuning
tool for this kernel, and the plain build ships.

## Non-temporal stores as a general choice — ships only as a ladder rung

`src/rungs/avx2_nt.c` is a real rung with a fixed size gate. A sweep with the gate
forced down to 64 B found **no size at which NT wins on any target**: throughput
was flat at 17–22 GB/s from 512 KiB to 4 MiB while the cached rung scaled to
34–81 GB/s, a 69–372% loss. At 4 MiB, where the `sfence` is negligible against a
~100 µs copy, it was still ~1.9x slower. The cause is an inference — this host
exposes no L3/LLC events, so the "bandwidth-capped streaming write path"
explanation is labelled as inference while the loss is the measurement. Verdict:
the rung keeps an experimental 256 KiB gate so the effect stays measurable; the
shipped kernel never uses NT stores.

## `rep movsb` — ships only as a ladder rung

Byte-string moves are the right answer on silicon with ERMS/FSRM. This silicon
has neither, so the rung is an also-ran at every size and adds an alignment
prologue the vector paths do not need. Verdict: it exists to keep the ladder
complete; it is not a candidate.

## Hand-written assembly against the compiler — ships as a rung, lost as an idea

`src/rungs/asm.S` is a full hand-written AVX2 loop. Its object is **byte-identical
across all three targets** (one md5), which is a nice property and also the
problem: the compiler's version adapts per target and is smaller (121 bytes of
instructions against 155 in the tight loop). Measured, the hand-written stream
does not beat the compiler's at any size where it matters. Verdict: the compiler
wins the codegen argument; the asm rung stays as the control that proves it.

## PGO and LTO — not shipped

PGO's apparent 25% win at 16–32 B turned out to be the **entry chain**, not the
compiler: the same improvement was reproduced statically with one compare and no
profile in the build (1.5 ns against PGO's 1.9 ns). `-flto` was neutral almost
everywhere, regressed 16 B by 9.7% and 128 B by 6.3%, flipped sign at 32 B across
targets, and produced one certified win (512 B, one target) — not enough to
demand a link-time build. Verdict: neither is justified, and the probe TUs that
measured them are not part of the public build.

## Alignment-unroll grid — not shipped

The grid varied unroll factor (4/6/8/12), the 32 B-aligned main loop, and prefetch
distance across sizes. Only unroll 4 with the aligned loop and a gated +128 B
prefetch survived; unroll 8/12 and the aligned loop never produced a certified
result that the simpler shape did not already have, and `-funroll-loops` on its
own cost up to 19%. Verdict: the shipped kernel is the simplest point in the
grid that cleared the floors.

## The 128 B block-pair shape — hypothesis CONFIRMED, candidate REJECTED

The shipped kernel is a certified loss at 128 B (−10.04% at both the floor and
the median, on all three targets). The cause is structural, and the Phase 7
campaign confirmed it.

**The defect.** The class ladder's rule is that a class window is `(H, 2H]` and
the block pair uses `H` = the class's *lower* bound, so the two blocks tile
`[0, n)` exactly at `n = 2H`. For the `(64, 128]` class that is `H = 64`, and
it tiles n = 128 exactly. But the F4 window covers `128 <= n <= 256` with
`T_BLOCKPAIR(4)` — `H = 128` — and it is tested *before* the flat class chain.
At n = 128 the window's head block and tail block are therefore the same four
32 B blocks: the kernel issues **8 loads and 8 stores for a 128 B copy** where
glibc issues 4 and 4.

**The signature.** This kernel's 128 B copy (3.4238 ns) costs *more* than its
own 256 B copy (3.4023 ns) on all three targets — half the bytes, the same
work — while glibc's 128 B copy (3.1113 ns) is 16% cheaper than its 256 B copy
(3.7148 ns).

**The experiment.** Two rungs were built from the shipped body with one knob
changed each, and measured against the shipped F4 in the same windows, in the
same process, on the same host state. The acceptance test was written into
`src/rungs/avx2_f4win64.c`'s header and committed (`82c330d`) **before** any
measurement ran.

| rung | shape at n = 128 | 128 B vs F4 | regression gate | verdict |
|---|---|--:|---|---|
| `f4win64` (H1b, primary) | H = 64 inside the window, one extra compare | **+18.31%** | **FAIL** — 32/64 B −16.3..−16.9%, 512 B −2.7% | rejected |
| `f4win129` (H1a, control) | H = 64 via the class chain, four extra compares | +9.13% | PASS | never promotable by construction |

The hypothesis is **confirmed**: giving n = 128 the H = 64 pair is worth
**+18.31%** at that size, identically on znver2, znver3 and v3 (2.8 ns against
F4's 3.4 ns). The certified −10.04% loss was caused by the coincident blocks.

**And the primary candidate is still rejected**, because the pre-registered
regression gate failed. The cause of the regression is *not* the knob: on
znver2 and znver3 the 16..64 B code is byte-identical to the shipped kernel's —
the same instructions at the same offsets `0x0..0x34` — so the knob cannot be
responsible there. (On v3 the knob's own codegen differs — 20 instructions with
no `lea` in the shipped kernel against 21 with `lea` in both knob builds — so
the v3 regression is confounded between the knob and the alignment, and is
reported as such.) What changed is the **alignment of the out-of-line 32..64 B
branch target**: in the shipped kernel it sits at `0x1c0`, which is 64-byte
aligned; in the rung it moved to `0x1a0`, which is 32-byte aligned but *not*
64-byte aligned (`0x1a0` = 416 = 6·64 + 32), while the `n <= 15` path took the
64-byte-aligned `0x180` instead. A 16% swing on a 2 ns copy, from alignment
alone — the same layout sensitivity the F2/F3 work already documented
("moving the alignment decision around cost more than it saved").

**Consequence.** The shipped kernel keeps its 128..256 B window unchanged, and
its certified 128 B loss stands. Nothing was promoted: promoting would have
traded a certified 128 B loss for certified 32 B and 64 B losses, and the
gate exists precisely to refuse that trade. The campaign logs are committed
under [`docs/data/raw/`](data/raw/) and the acceptance summary is
[`docs/data/phase7_f4win_acceptance_summary.csv`](data/phase7_f4win_acceptance_summary.csv).

The next experiment, if there is one, is a separate one with its own criteria
fixed in advance: keep the H = 64 semantics at n = 128 *and* restore the
64-byte alignment of the 32..64 B branch target, then re-run this same gate.
That was not attempted here.

## The 128 B alignment fix (Phase 7.2) — REJECTED by the regression gate, S3

Phase 7.1 confirmed the 128 B cause (+18.31% at 128 B when that size gets the
64-byte block pair) and rejected the candidate because the 16–64 B path lost
its 64-byte branch-target alignment. Phase 7.2 added that alignment back and
re-ran the same gate. **The 128 B fix works. The candidate is still rejected**,
on a different size, for a different reason — and the reason is measured, not
argued.

### The pre-registered criteria (verbatim, `src/rungs/f4win64_aligned.c`)

> **P1.** ALIGNMENT PRECONDITION. For each target t in {znver2, znver3, v3} and
> for the LINKED binary bench/bench_<t>: every direct branch target of
> memcpy_f4win64_aligned reachable when n <= 64 has an absolute address A with
> (A & 0x3f) == 0. Each of those addresses is the first instruction of a basic
> block… If any target fails, REJECT WITHOUT MEASURING.
>
> **P2.** INSTRUCTION IDENTITY. The 16..64 B path… of memcpy_f4win64_aligned is
> instruction-identical… to the same region of memcpy_avx2_f4win64… which is the
> same source with the same knob and WITHOUT -falign-labels=64.
>
> **A1.** 128 B delta vs F4 >= -2% (both min and median).
>
> **A2.** For each of 16, 32, 64, 256, 512 B and 1 MiB, on every target: the
> delta against F4 must satisfy |delta| <= max(1.5%, null_band_pct)… AND the
> tier verdict against F4 must not be LOSS. No certified row may be worse than
> -5% against F4, whatever the band.
>
> **A3.** The .text md5 of build/<t>/src/fast.o is unchanged per target.
>
> **S1.** If P1 fails after one alignment attempt -> REJECT, do not measure.
> **S1b.** If P2 fails on any target -> REJECT without measuring.
> **S2.** If A1 fails on attempt 1 -> one retry allowed with a different
> alignment strategy. If attempt 2 fails -> REJECT.
> **S3.** If A2 fails on either attempt -> REJECT. Do not trade certified rows
> for 128 B.

Committed as `200a852` **before** the campaign; the measurement was
`docs/data/raw/phase7_f4al_<target>.windows.log` (9 windows × 5 reps,
`taskset -c 2`, `iters 4000`, zero migrated reps), and the per-size summary is
`docs/data/phase7_f4al_acceptance_summary.csv`.

### The measured deltas (candidate vs the shipped F4, same windows)

| size | znver2 | znver3 | v3 | band (znver2) | A2 |
|---|--:|--:|--:|--:|:--|
| 16 B | +0.00% | +0.00% | +0.00% | 0.00% | pass |
| 32 B | +0.00% | +0.00% | −0.53% | 0.00% | pass |
| 64 B | +0.00% | +0.47% | +0.00% | 0.00% | pass |
| 256 B | +0.00% | +0.00% | +0.00% | 0.00% | pass |
| **512 B** | **−2.70%** | **−2.70%** | −1.33% | 0.00% | **FAIL (znver2, znver3)** |
| 1 MiB | −0.38% | +0.59% | +0.64% | 5.16% | pass |

**A1 PASS on all three targets**: 128 B is **+18.31%** against F4, min *and*
median, identical on znver2, znver3 and v3 (2.7969 ns against F4's 3.4238 ns).
Against glibc it is a **WIN** — 2.7969 ns against glibc's 3.1000 ns, +10.11%.
**For the first time in this project, 128 B is ahead of glibc rather than
behind it.**

**A3 PASS on all three targets**: the shipped F4 `.text` md5 is unchanged
(`0b2d0837…` znver2/znver3, `71c9cefe…` v3; sha256
`cc9d462ebe8452cdbe34838f17656e9d0eb3336abe3db0eaf6a009389d8cc2f4`).

**A2 FAILED on znver2 and znver3, at 512 B, by 1.20 percentage points** — the
measured −2.70% against a 1.50% floor (the null band there computes as 0.00%).

### What the alignment fix actually fixed

The regressions that sank the Phase 7.1 attempt are **gone**: 32 B and 64 B went
from −16.3%…−16.9% to +0.00% / +0.47% / −0.53%. The alignment strategy did
exactly what it was supposed to do, and P1 confirmed the precondition held on
all three targets before a single measurement was taken.

### Why 512 B still fails — and it is NOT the alignment flag

The campaign also carried the **H1b control**: the same knob, *no* alignment
flag, measured in the same windows. H1b shows the **identical −2.70%** at 512 B
on znver2 and znver3. So the 512 B shift is caused by the **knob**, not by
`-falign-labels=64` — by the code the knob adds to the window body moving the
flat class chain, which is where the 512 B path lives. The alignment flag cannot
fix it, because it was never the cause.

The raw numbers are **two timer ticks of identical code**: F4's 512 B floor is
5.7891 ns and both knob builds' is 5.9453 ns — a 0.156 ns difference — with the
512 B path's instructions byte-identical in all three builds. The null band at
512 B computes as 0.00% because `naive` and `ref_scalar` landed on the same
timer value, so the band rule falls back to the protocol's 1.5% floor, which two
ticks exceeds.

### Consequence

**Rejected, per S3.** `src/fast.c` was never edited. The shipped kernel keeps
its 128–256 B window and its certified 128 B loss stands. The 512 B row would
have shrunk from +16.83% to about +14.6% against glibc — still a certified win,
but smaller — and the gate exists precisely to refuse a quiet trade of that
kind. The headline is unchanged.

**What is not settled.** The 128 B win is real and reproduced on all three
targets at both statistics; the only thing between it and the shipped kernel is
a two-tick placement shift at 512 B caused by the knob's own code growth. A
strategy that adds the n == 128 case *without* moving the flat class chain
would likely pass. That would be a further attempt under a new
pre-registration, and it is not attempted here.

## Alignment sensitivity — quantified (Phase 7)

A 32-byte shift in the branch-target alignment of the 32..64 B path — the
target moving from `0x1c0` (64-byte aligned) to `0x1a0` (32-byte aligned, *not*
64-byte aligned) — produced **−16.87% at 32 B and 64 B** in an otherwise
instruction-identical code path. This is the sharpest quantified alignment
sensitivity in the project. F2/F3 had observed the effect qualitatively;
Phase 7 measures it.

The mechanism is now pinned down: on znver2 and znver3 the 16..64 B code was
byte-identical to the shipped kernel's — the same instructions at the same
offsets `0x0..0x34` — so the only difference was where the out-of-line
32..64 B block sat, and that alone cost 16% on a 2 ns copy. (On v3 the knob's
own codegen also differs, so the v3 figure is confounded between the knob and
the alignment; the alignment explanation is the one that holds on all three
targets, since the same 64→32 shift occurred on each.)

**Consequence for future kernel changes: any rung that modifies code above the
tiny path MUST verify branch-target alignment before measurement.** This is
now a precondition in the pre-registration template — P1 in
`src/rungs/f4win64_aligned.c` — and it is checked on the LINKED binary, because
the function's absolute alignment is a link-time property (the `.text`
section's alignment, raised by the `.p2align` that `-falign-labels` emits),
not an object-time one.

## 1 MiB row — audited, not retracted

Published: +8–13% vs glibc at 1 MiB at the floor, and level to 2.7% behind at the
median. **Not retracted** — the number reproduces.

A Phase 6 audit re-derived every published row from the reference campaign's raw
window logs, now committed under `docs/data/raw/`, and found that all of them
reproduce exactly, this one included. The audit that proposed a retraction was
itself wrong: it had let glibc's `memset` samples into the `memcpy` baseline,
which inflated glibc's floor at every size and made 1 MiB look like a 54% loss.
The repo's own analyser (`scripts/analyze.sh`), which filters on the operation,
reproduces +7.95 / +10.67 / +8.28% at 1 MiB exactly.

So the row ends up more precise than a retraction would have been. The floor
says +8 to +11%, the median says level to 2.7% behind, and both sit inside the
Tier-B 25% floor — so 1 MiB is `measured`, not a claim, and the honest summary
is that the kernel is level with glibc at that size.

The mechanism is the reason for Rule 1 in
[METHODOLOGY.md §11](METHODOLOGY.md#11-reproducibility-gate): a number that
cannot be re-derived from a committed log cannot be defended, and the defence
has to be mechanical. Committing the log is what turned "we think it
reproduces" into "it reproduces, here is the command".

## 128 B — the trade we accepted (Phase 7)

The shape that wins 128 B (**+10.11% vs glibc, the first win at this size**)
costs 512 B 2.24 points of margin, because the window body that dispatches the
64-byte pair sits *before* the flat class chain: any code added to that body
displaces the chain, and the 512 B path's position in the code stream moves with
it. Phase 7.3a isolated this — the 512 B block's **code position**, not the
transition into it, moves the fast mode from 741 ns to 761 ns per 128-copy
batch. The full analysis is in [`docs/phase7_3a_mechanism.md`](phase7_3a_mechanism.md).

The no-trade alternative was measured too: **H1a** — move the window's lower
edge to 129 so n = 128 falls through to the existing (64, 128] class, which
already uses the 64-byte pair — **ties glibc at 128 B (0.00%) with zero cost
anywhere else** (512 B, 256 B, 64 B, 32 B and 1 MiB all unchanged), because
the function keeps its size and the 512 B path keeps every positional property.
It was rejected because a win (+10.11%) is worth more than a tie, and the
512 B margin loss still leaves 512 B at **+14.59% vs glibc — a certified win,
not a loss**. The promoted kernel's 512 B floor is 5.9453 ns against the
pre-promote kernel's 5.7891 ns: 20 ns per 128-copy batch, two timer ticks of
the instrument's own quantum.

A future design that reaches the 512 B path *before* the chain could win both.
That is a structural project, out of scope for Phase 7.

## 512 B — bimodal distribution note

512 B measures in two modes on this host — **fast ~741 ns, slow ~1212 ns per
128-copy batch** — and the mode split is a **host event, not a kernel property**
(Phase 7.3a Q1: the slow mode is contiguous in time and spans three different
implementations, and the 1 B/store rungs are immune to it because they are
store-throughput bound).

The consequence is that the **median at 512 B is a statistic of the run, not of
the kernel**: the shipped kernel's own 512 B median reads 5.7891 ns in the
Phase 7.2b campaign and 9.4688 ns in the Phase 7.4 one — same kernel, same
size, same host, two campaigns — while its floor reads 5.7891 ns in both. The
published 512 B number is therefore `min_of_mins`, consistent with the rest of
the table, with the median reported beside it.

Full distributions: `docs/data/phase7_p72b_acceptance_summary.csv` (the
40 000-iteration confirmation) and the per-rep values in
`docs/data/raw/phase7_p72b_40000_<target>.windows.log` and
`docs/data/raw/phase7_p74_<target>.windows.log`.

## 1 MiB — closed as parity after two attempts (Phase 8)

The 1 MiB row is a **win at the floor** (+9 to +13% against glibc) and
**parity at the median** (−1.6% to −2.3%). Two attempts were made to move the
median, both pre-registered, both with a shape control, and both rejected.

**Attempt 1 — a longer prefetch distance (4096 B instead of 128 B, in the
512 KiB–2 MiB band).** The premise was that 128 B of lookahead covers only
~1.4 ns of the copy's latency and that a longer distance would cover the DRAM
latency instead. **It cost 2–4% at the floor and 1–2% at the median, on all
three targets.** The control showed the loop-shape change was free, so the
loss was the distance: a 32× longer lookahead fetches lines that are evicted
before they are used, and the wasted bandwidth is charged to the copy.

**Attempt 2 — prefetch coverage (a second prefetch pair at +192, so both 64 B
lines of the next iteration are covered in software).** This was aimed at the
cause attempt 1 identified: the copy runs either at the loop's store-port
ceiling (32 B/cycle, 91 GB/s) or at the memory-hierarchy rate (27 B/cycle,
76 GB/s), and the gap is the loads not being covered in time. **The extra
coverage moved the median by +0.37 / +0.81 / −0.07 points — inside the noise
— and cost 0.8–1.5 points at the floor.** The coverage hypothesis is
falsified: the hardware prefetcher was already covering the second line, and
the stall is elsewhere.

**Why there is no attempt 3.** Every mechanism on the Phase 8.1 candidate list
is closed by evidence: the prefetch distance (the alignment-unroll grid, and
attempt 1); the aligned main loop and the unroll 4/6/8/12 (the alignment-unroll
grid: "only unroll 4 with the aligned loop and a gated +128 B prefetch
survived"); `PREFETCHW` on the destination ("no size at which it wins"); NT
stores (the ladder rung already writes **full 64 B lines** — four
`_mm256_stream_si256` per 128 B iteration — so its 17–22 GB/s is not a
granularity artifact, and the sweep found no size at which NT wins, a 69–372%
loss); L2/L3 residency and page-boundary crossing (not kernel-actionable: the
working set's residency is host state, and the buffers are already
`MADV_HUGEPAGE`d).

**What the row actually is.** The fast mode is a **per-(kernel, rep) lottery**:
in the attempt-1 campaign `fast`, `pf_ctl`, `pf_big` and `f4_baseline` each
won it in 3–7 of their 45 reps, at *different* reps, while `glibc` never won it
in any of its 135. The published floor is a best-of-45-lottery-draws statistic,
and it moves by about ±2 points from campaign to campaign:

| campaign | znver2 | znver3 | v3 |
|---|--:|--:|--:|
| phase7_4 (the reference) | +12.58% | +11.10% | +9.42% |
| phase8_2 | +10.57% | +9.20% | +10.81% |
| phase8_3 attempt 1 | +10.08% | +11.92% | +11.08% |
| phase8_3 attempt 2 | +9.16% | +8.50% | +10.60% |

The **value** moves; the **verdict** does not. All twelve cells are wins at the
floor (the lowest +8.50%) and all twelve medians are between −1.55% and
−2.32%, i.e. parity. A caller doing one 1 MiB copy sees the median.

The full analysis is in [`docs/phase8_1_mechanism.md`](phase8_1_mechanism.md)
and the pre-registration, criteria, campaigns and results are in
[`docs/phase8_3_preregistration.md`](phase8_3_preregistration.md). The logs
are `docs/data/raw/phase7_p83_*.windows.log` and
`docs/data/raw/phase7_p832_*.windows.log`.

## What this list is for

If you are about to try one of these, the measurement says where it ends. If you
think one of them was measured badly, the protocol is in
[METHODOLOGY.md](METHODOLOGY.md) and the frozen tables are in `docs/data/` — the
methods are reproducible even where the host is not.
