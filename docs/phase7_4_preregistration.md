# Phase 7.4 — pre-registration: promote the Phase 7.2 candidate (128 B win)

Committed **before** any code change. The promote, the re-certification campaign
and the headline rewrite all follow the criteria below, and the criteria were
fixed before the promote measurement existed.

## Decision being implemented

Path B. The Phase 7.2 candidate `src/rungs/f4win64_aligned.c` becomes
`src/fast.c`. It wins 128 B by **+10.11% against glibc** — the first win at
that size — and costs 512 B **2.24 points of margin** (512 B stays a certified
win at +14.59%).

Phase 7.3a (`docs/phase7_3a_mechanism.md`) proved the trade is **structural**:
the window body that dispatches the H = 64 pair sits *before* the flat class
chain, and any code added to that body displaces the chain, which moves the
512 B path's position in the code stream and moves its fast mode from 741 ns to
761 ns per 128-copy batch. The no-trade alternative (H1a: move the window's lower
edge to 129 so n = 128 falls through to the existing H = 64 class) was measured
and ties glibc at 128 B with zero cost elsewhere; it was rejected because a win
is worth more than a tie and 512 B remains a certified win.

**This is the final answer for 128 B. No third shape is attempted.**

## Acceptance for promote

**A1.** 128 B delta against the **shipped F4** (the one being replaced) is
**≥ +10%**, at both the floor and the median, on all three targets — matching
what Phase 7.2 measured.

**A2.** 512 B delta against the shipped F4 is **≥ −2.70%**, at both the floor
and the median, on all three targets. A worse trade than the one already
measured is not accepted.

**A3.** 16 B, 32 B, 64 B, 256 B and 1 MiB stay **within the null band**
against the shipped F4, with the null band (naive vs ref_scalar) measured in
the same run at the same size.

**A4.** The new F4 `.text` md5 is published.

## Stop conditions

**S1.** If any of A1–A3 fails at the promote measurement → **REJECT the
promote**, keep the shipped F4, and the tree stays as it is.

**S2.** If 512 B falls below −2.70% against the shipped F4 → **REJECT**. A
larger trade than the measured one is not accepted.

**S3.** If any certified row regresses outside the null band → **REJECT**.

## How the comparison is made

The shipped F4 that A1–A3 are measured against must be present **in the same
windows** as the promoted kernel, so the comparison is relative and host drift
cancels. It is therefore kept as a rung, `src/rungs/f4_baseline.c`, which is
the current `src/fast.c` verbatim with only the symbol renamed. Its `.text` md5
must equal the shipped F4's current `.text` md5
(`0b2d0837e25a53b2570ef33a6606edfa` on znver2 and znver3,
`71c9cefef2d67698267abc118fe55e1a` on v3) — otherwise it is not the same
kernel and the comparison is void.

The campaign is the standard protocol: 9 windows × 5 reps, `taskset -c 2`,
iters 4000, all 13 sizes, all three targets, null band in the same run, zero
migrated reps. Logs are committed under `docs/data/raw/phase7_4_<target>.windows.log`
and the summary under `docs/data/phase7_4_summary.csv`. The RESULTS.md tables
are **regenerated from that CSV**, never typed from memory.

## Promote equivalence check

The promoted `src/fast.c` must produce a `.text` byte-identical to the Phase 7.2
rung's `.text` (`0eb00867a4688a618d0e4329195c188d` on znver2 and znver3,
`368680ed8dac9be06d0c1d382b5dfc6c` on v3). The only permitted differences are
the symbol name and the file's location; the knob set and the compiler flags
(`-fno-crossjumping -falign-labels=64`) must be carried over. If the `.text`
differs, the promote changed the binary and the measurement does not transfer.

## Addendum, declared before the campaign (after the code review)

Added after a code review of the promote, and **before** the campaign is run.

**The alignment flag is part of the kernel.** `src/fast.c` is built by the gcc
rule with `GCC_EXTRA_fast.c`; the Phase 7.2 rung was built with
`-fno-crossjumping -falign-labels=64`. The deliverable must therefore carry
`-falign-labels=64` too, or the shipped kernel is the *unaligned* H1b shape —
the one Phase 7 rejected with −16% at 32 B and 64 B. The symbol name and the
file path provably do not affect the generated `.text`, so the `.text` md5
check above is a complete equivalence test.

**A2 tie-break, pre-declared.** The 512 B floor is the 741 ns mode (5.7891 ns
per copy) against the 761 ns mode (5.9453 ns) — exactly 20 ns per 128-copy
batch, i.e. exactly the −2.70% measured in Phase 7.2. The instrument's quantum
at this size is 1 ns per 128-copy batch (the wall clock is integer
nanoseconds; the reported value is `(batch_total − 40 ns) / 128`), which is
0.17% of the floor. So:

- the 512 B floor delta **≥ −2.70%** → A2 passes;
- the 512 B floor delta in **(−2.87%, −2.70%)** — within one quantum of the
  measured trade — is decided by the **40 000-iteration confirmation** used in
  Phase 7.2b, at 512 B only;
- the 512 B floor delta **< −2.87%** → **S2 rejects**. A trade larger than the
  measured one, by more than the instrument's own resolution, is not accepted.

This is declared before the numbers exist and is not widened afterwards.

**A3's floor is the protocol's own.** "Within the null band" means
`|delta| ≤ max(1.5%, null_band_pct)`, which is the applicable floor defined in
[METHODOLOGY.md §3](METHODOLOGY.md#3-the-claim-model-floor-first-then-tier)
("the applicable floor is max(1.5%, band)"). It is not a loosening: a band
computed as 0.00% is two byte-identical kernels landing on the same timer
value, not a zero noise floor.

**The 8 B row is measured too.** The knob moves the `n <= 15` class, and the
Phase 7.2 campaign measured 8 B; the 13-point ladder does not include it. 8 B
is therefore added as an extra reported (non-gate) row so the new campaign is
comparable with the Phase 7.2 one.

**The candidate stays in the campaign as an in-run equivalence check.** The
promoted `memcpy_fast` is byte-identical to `memcpy_avx2_f4win64_aligned`, so
the same run must show `fast ≈ f4win64al` (within noise) and
`fast > f4_baseline` at 128 B. If `fast` and `f4win64al` disagree, the `.text`
equivalence did not hold in the linked binary and the promote is void.

**The control rung's construction.** `src/rungs/f4_baseline.c` is the current
`src/fast.c` verbatim with only `TUNED_FN` changed, built by the **gcc** rule
with `-fno-crossjumping` and **without** `-falign-labels=64`. Its `.text` md5
must equal the shipped F4's current one, or it is not the same kernel and the
comparison is void. It is registered immediately after `fast` so the two compared
kernels are adjacent in the harness's rep-major loop, which minimises the
positional asymmetry between them.

**What the promote invalidates and must be rewritten from the new CSV, not from
memory:** the certified 128 B row (`docs/RESULTS.md`), the numbers repeated in
`include/memcpy_fast.h`, and `src/fast.c`'s own header comment.
