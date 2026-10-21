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
