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
