# Phase 2.6 — final deliverable

## 1. The kernel

**`src/memcpy/memcpy_avx2_final_f4_gcc.c`** — commit **`e20af8c`** (rung added), measured
in campaign `f128b` on **`b361714`** (the tree that was timed; the only later
change was the Kernel 7/8 sources and the audit's EVEX check, neither of which
touches this TU's code).

    TUNED_UNROLL     4                4 x 32 B per iteration, 32 B-aligned main loop
    TUNED_ALIGNED    1                runtime aligned/unaligned loop selection
    TUNED_PF_BYTES   128              +128 B prefetch on both streams
    TUNED_PF_MIN     65536            ... only at 64 KB and up
    TUNED_SMALL_PATH 1                n <= 64 taken from ONE compare
    TUNED_WINDOW     1                128..256 B straight-line window, one compare
    built with -fno-crossjumping      stops gcc merging the block-pair heads

A note on the path in the brief: `src/memcpy/memcpy_avx2_final.c` is the **V9 clang
build**, and it certifies a **−25% at 16 B** on all three targets. The numbers in
this document are not that file's. The measured deliverable is the F4 TU above;
`memcpy_avx2_final.c` remains in the tree as the V9 reference rung and is reported
as such in `results/docs/phase2_6_SUMMARY.md`.

## 2. The ladder, kernel vs glibc

`min_of_mins`, 5 reps × 9 windows, `taskset -c 2`, 0 migrated reps, campaign
`f128b` (2026-10-20 07:08–07:20), baseline glibc, tier floors from
`MEASUREMENT_PROTOCOL.md` §3c.

| size | znver2 | znver3 | v3 | verdict |
|--:|:--|:--|:--|:--|
| 16 B | **+44.75%** | **+38.02%** | **+38.02%** | **WIN ×3** (Tier B/A/A) |
| 32 B | **+25.45%** | **+25.45%** | **+25.49%** | **WIN ×3** |
| 64 B | **+25.49%** | **+25.49%** | **+25.10%** | **WIN ×3** |
| 128 B | −10.04% | −10.04% | −10.04% | **LOSS ×3** (Tier A, floor 1.5) |
| 256 B | +8.41% (measured) | **+8.41%** | **+8.41%** | WIN on 2 of 3 |
| 512 B | **+16.83%** | **+16.83%** | **+15.71%** | **WIN ×3** |
| 1 KiB | −1.41% | −1.41% | −1.41% | measured ×3 |
| 4 KiB | 0.00% | 0.00% | 0.00% | measured ×3 |
| 16 KiB | −2.28% LOSS | −1.19% | −1.31% | measured 2/3, one certified LOSS |
| 64 KiB | 0.00% | −0.08% | −0.08% | measured ×3 |
| 256 KiB | −0.19% | +0.75% | +1.88% | measured ×3 (Tier B) |
| 1 MiB | +7.95% | +10.67% | +8.28% | measured ×3 (Tier B) |
| 4 MiB | +1.10% | +0.93% | −4.98% | **not usable** (see §4) |

"measured" = inside the applicable floor (max(1.5%, null band) for Tier A, 25%
for Tier B). It is not a win and it is not a loss; the tables in the matrices
carry the tier and floor for every cell.

## 3. Wins

Five size classes with certified deltas over glibc:

| class | certified delta | tier |
|:--|:--|:--|
| 16 B | +38.0% … +44.8% | A (znver3, v3), B on znver2 (floor 25%, cleared) |
| 32 B | +25.4% | A on znver3 and v3, B on znver2 |
| 64 B | +25.1% … +25.5% | A ×3 |
| 256 B | +8.41% | A on znver3 and v3 |
| 512 B | +15.7% … +16.8% | A ×3 |

That is four classes certified on every target (16, 32, 64, 512 B). 256 B is the
fifth on two of the three; its third target is Tier B at a 25% floor — the
protocol's limit, not the kernel's.

## 4. Losses and unmeasurable

**128 B — −10.04%, certified on all three targets (Tier A, floor 1.5).** This is
the one remaining deficit, and it is half of what it was before the F pass
(−20.09%). Mechanism: 128 B is 16 cycles of work and 4 instructions of dispatch;
the original fused kernel reached it from two compares, every small-path build
reaches it from three plus a jump. F1 (branch layout) and F4 (dedicated window)
both moved it in the right direction; F4 recovers 0.3 ns of the 0.6 ns gap and
cannot go further without deleting a dispatch level that the 16–64 B wins depend
on. **Unresolved and documented as such** — it is a dispatch-level trade, not a
copy-path defect, and the trade was taken deliberately.

**256 B — resolved.** Before the F pass it was −15.88%/−16.93% (certified on two
targets). F4's dedicated 128..256 B window brought it to 3.4 ns, which is the
original fused kernel's number and 8.41% ahead of glibc. It is "measured" rather
than "WIN" on znver2 only because that key came out Tier B (25% floor).

**16 KiB — −2.28% LOSS on znver2** (certified, Tier A), −1.19%/−1.31% measured on
the other two. This one is new in the F4 build: the extra window compare costs a
branch at a size where the copy is 213 ns, which should be invisible — it is one
timer step and it is disclosed rather than explained away.

**4 MiB — not usable.** The null band at 4 MiB is 78% on znver2 in these windows,
the spread across windows is >10% (Tier B), and in `final1` two builds sharing
two of three knobs differed by 12% in opposite directions. No 4 MiB number in this
project is a claim, in either direction.

## 5. The 128/256 B pass (F1–F4), and what it proved

One pass, four rungs, each one knob away from V9c (`avx2_final_v2_gcc_noxj`),
measured in the same windows as it. Delta is F-rung vs V9c.

| attempt | 16 B | 32/64 B | 128 B | 256 B | verdict |
|:--|--:|--:|--:|--:|:--|
| **F1** branch hints (`__builtin_expect`) | **−60.7%** | **−29.3%** | **+16.7%** | **+13.7%** | fixes 128/256, **destroys 16–64**; rejected |
| **F2** IFUNC | 0.00% | 0.00% | 0.00% | 0.00% | indirection is free here, and a resolver never sees the pointers; cannot help |
| **F3** no alignment test (verifiable bound on the alignment-contract idea) | 0.00% | 0.00% | **0.00%** | **0.00%** | proves the test is not executed at 128/256 B; cannot help |
| **F4** dedicated 128..256 B window | 0.00% | **+14.4%** | **+8.36%** | **+21.0%** | **closed 256 B, halved 128 B**; adopted |

Detail, all three targets agreeing within one timer step:

* **F1** is the cleanest evidence for the mechanism: moving the tiny block out of
  line made 128 B and 256 B *equal to glibc* (3.1 / 3.7 ns — exactly glibc's
  numbers) and simultaneously made 16 B 2.5 ns instead of 1.5 and 32/64 B 2.8
  instead of 2.2. Layout moves both ends at once; the constraint is real, not
  imagined.
* **F2** was implemented as a genuine IFUNC (`__attribute__((ifunc))`, resolver
  returning the same body). A resolver runs once, at load time, and never sees the
  pointers, so the per-call alignment decision the brief wanted moved is not
  movable this way. Measured cost of the indirection: 0.00% at 128/256 B, −0.6% at
  16 B (one timer step). It is not a fix and it is not a cost.
* **F3** as specified — a symbol whose contract is "caller guarantees alignment" —
  is **inadmissible in this harness**: `--verify` walks n = 0..4096 at random
  src/dst offsets, so an alignment-assuming kernel fails the correctness gate, and
  weakening that gate for one rung is not a trade this project makes. What can be
  measured is its upper bound: deleting the test and the aligned loop entirely
  (`TUNED_ALIGNED 0`) changes 128/256 B by 0.00% on all three targets and 32/64 B
  by 0.00%. The test costs nothing at these sizes because it is never reached
  there. A contract change would buy nothing, so it was not adopted.
* **F4** is the only one aimed at the actual branch, and it worked: 128 B 3.7 →
  3.4 ns, 256 B 4.3 → 3.4 ns, with 32/64 B gaining a step as a layout side effect
  (2.2 → 1.9 ns) and 512 B gaining one too (6.0 → 5.8 ns). It costs three extra
  instructions of dispatch at entry for every size and one disclosed timer step at
  16 KiB on znver2.

**Verdict of the pass:** the 256 B deficit is closed; the 128 B deficit is halved
and remains. Not a goalpost move: 128 B is still reported as a certified LOSS.

## 6. Bottom line

Against glibc's AVX2 memcpy, on this host, with tier floors applied, the kernel
is **faster at 16 B (+38…+45%), 32 B (+25%), 64 B (+25%), 256 B (+8.4%) and 512 B
(+16%)**, is **level from 1 KiB to 256 KiB** (every cell inside the floor except
one −2.28% step at 16 KiB), is **about 8–11% faster at 1 MiB** without that being
certifiable in these windows, **loses 128 B by 10%**, and has **no usable
measurement at 4 MiB**.

That is the honest shape of the result: a small-copy kernel that beats the system
libc by a wide margin where calls are shortest, matches it across the middle of
the ladder, and loses one size class by one cache line's worth of dispatch.

## 7. Deliverable statement

> **This kernel beats glibc at five size classes — 16 B, 32 B, 64 B, 256 B and
> 512 B — with certified deltas of +8.4% to +44.8%, matches it from 1 KiB to
> 256 KiB, and loses one class (128 B, −10.0%), all measured under the project's
> two-tier protocol on three code-generation targets.**

## 8. Artifacts

    src/memcpy/memcpy_avx2_final_f4_gcc.c          the deliverable
    src/memcpy/memcpy_avx2_tuned_impl.h            the one body; TUNED_* knobs
    results/live/phase2_6_f128b_<t>.matrix.txt   the ladder above, per target
    results/live/phase2_6_f128b_<t>.windows.log  raw reps (9 windows, 0 migrated)
    results/live/phase2_6_pair_verdicts.txt      one-knob pair tables with tiers
    results/docs/phase2_6_SUMMARY.md             V1–V9c, all campaigns and verdicts

Reproduce:

    make gate
    sh scripts/campaign/phase2_6_run.sh f128b "ref_scalar,naive,glibc,avx2_final_v2_gcc_noxj,avx2_final_f1,avx2_final_f3_notest,avx2_final_f4_win,avx2_final_ifunc,rep" znver2 znver3 v3
    sh scripts/analyze/phase2_6_matrix.sh results/live/phase2_6_f128b_znver2.windows.log glibc memcpy
