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

## What this list is for

If you are about to try one of these, the measurement says where it ends. If you
think one of them was measured badly, the protocol is in
[METHODOLOGY.md](METHODOLOGY.md) and the frozen tables are in `docs/data/` — the
methods are reproducible even where the host is not.
