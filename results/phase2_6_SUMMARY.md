# Phase 2.6 — the performance push: V1–V9c

Machine: 4-vCPU KVM guest, CPUID "AMD EPYC Processor" (family 17h model 01h),
measured Zen 2 class (2×256 b load/cyc, 1×256 b store/cyc), TSC 2.79473 GHz,
L1d 32 K, L2 512 K, L3 8 M. Pin `taskset -c 2`, CPU confirmed with
`sched_getcpu()` inside the PMU window, 5 reps × 9 windows, `RUN_TO_CAP=1`,
iters 4000, 13-point ladder (16 B … 4 MiB). Protocol: `results/MEASUREMENT_PROTOCOL.md`
(§3c two-tier claim model). `make gate` (audit + audit-width) ran inside
`mo_measure.sh` for every campaign; a tree that fails either audit writes no
timing row at all, and none of the four campaigns below did.

This document reports the nine questions the push was opened with. Each one was
answered by a rung that differs from its neighbour in exactly one knob, measured
in the same windows as that neighbour, so the difference is the knob.

    V1  gcc or clang for the deliverable
    V2  what glibc actually does, read off its own disassembly
    V3  a 32 B-aligned main loop (is the alignment test worth a branch)
    V4  unroll depth (4 / 8 / 12)
    V5  prefetch distance, and whether to prefetch at all
    V6  compiler flags on the adopted compiler (-Ofast, -funroll-loops,
        -fno-plt, -fno-omit-frame-pointer, -flto)
    V7  PGO (profile-generate over the ladder, then profile-use)
    V8  the 32..63 B small shape: four 16 B moves or two 32 B moves
    V9  fuse the winners into one kernel and measure it against glibc
    V9b the tiny-class entry chain (opened by V7's result)
    V9c V9b without gcc's cross-jumped block-pair heads

## 0. Campaigns, and why there are four

| tag | impls | what it answers | log |
|:--|:--|:--|:--|
| `screen3` | 12 (K4 body + 7 knob variants + gcc build) | V1, V3, V4, V5 | `results/phase2_6_screen3_<t>.windows.log` |
| `final1` | 12 (fused kernel, its gcc build, V6 flags, PGO, -flto) | V6, V7, V8, V9 | `results/phase2_6_final1_<t>.windows.log` |
| `v9b` | 5 (glibc, fused gcc build, V9b) | the tiny-class fix | `results/phase2_6_v9b_<t>.windows.log` |
| `v9c` | 5 (glibc, V9b, V9c) | the 256 B layout cost | `results/phase2_6_v9c_<t>.windows.log` |

Per-rung tables: `results/phase2_6_<tag>_<target>.matrix.txt`
(`scripts/phase2_6_matrix.sh`, baseline glibc).
Pair tables with tier verdicts: `results/phase2_6_pair_verdicts.txt`
(`scripts/mo_analyze.sh` run per pair — the same tier/band logic every ladder
rung is judged by, so a knob delta cannot be graded more kindly than a rung).
Campaign-B commentary: `results/phase2_6_final1_analysis.txt`.

## 1. The instrument, restated because it decides every verdict below

Two facts control what may be claimed.

**The null band.** `naive` and `ref_scalar` are two translation units holding the
same byte loop with byte-identical instruction streams. Their difference at a
size *is* the measurement artefact at that size, measured in these same windows.
In these campaigns it reaches 4.35% at 16 B, 9.51% at 64 B, 20.22% at 4 KB and
78% at 4 MiB.

**Two tiers (§3c).** A key whose per-window spread is ≤ 10% is Tier A and may be
claimed at max(1.5%, band). A key above that spread is Tier B and may only be
claimed at a flat 25%. At 128 B…1 MiB almost every key in these campaigns is
Tier B, which is why the tables are full of "measured": it means *inside the
floor*, i.e. not a win, not a loss, and not available as a tie-breaker. Nothing
in this report converts a "measured" row into a claim in prose; where a number
is quoted as evidence it is quoted with its tier and floor.

Consequence, stated plainly: on this host, with this protocol, the only results
that can be certified at 1 KB…1 MiB are deltas above 25%. Most of the honest
findings of this push are below that.

## 2. V1 — compiler

`avx2_gcc` is `memcpy_avx2.c`'s body built by gcc 16.2.1 instead of clang, same
`-march/-mtune/-mavx2/-mfma`, same `-fno-builtin` (neither compiler may call
libc's memcpy for us).

Certified (campaign `screen3`, `avx2_gcc` vs `avx2`, min_of_mins):

| size | znver2 | znver3 | v3 | verdict |
|--:|--:|--:|--:|:--|
| 16 B | +0.44% | +12.00% (measured, band 19.1%) | −13.64% | no/contradictory |
| 32 B | **+24.00%** | **+24.00%** | **+28.00%** | WIN ×3 |
| 64 B | **+16.98%** | **+25.42%** | **+16.98%** | WIN ×3 |
| 128 B–4 KB | +8.45%…+19.27% | +15.15%…+19.35% | +8.45%…+19.49% | Tier B, not claims |
| 1 MiB | −0.33% | +2.57% | −7.82% | Tier B, not claims |
| 4 MiB | −1.71% | −0.80% | +2.31% | Tier B, not claims |

**Verdict: gcc is the compiler for the deliverable.** Six certified wins, no
certified loss except 16 B on one target, and the direction is consistent across
all three targets at 32/64 B. That is the only build-level decision in this push
that survived measurement.

One correction to the round-1 reading, because it matters for V9: on the *fused*
kernel (campaign `final1`, `avx2_final_gcc` vs `avx2_final`) gcc and clang are
indistinguishable — 0.00% at 16/32/64/256/4096 B, −5.02% at 128 B and −10.95% at
4 MiB, every row Tier B. The compiler advantage lives in the *small path of the
K4 body*, not in the fused kernel's loop, and V9b's result below explains why:
the fused kernel's tiny class was spending its time in the entry chain, where
both compilers are equally bad.

## 3. V2 — what glibc does (`__memcpy_avx_unaligned_erms`)

Read from `results/glibc_memcpy_avx_unaligned.disasm.txt` (no measurement, so no
claim — this is the design record for the fusion):

* **Main loop**: 4× unaligned 32 B loads per iteration (`vmovdqu` at +0x0/+0x20/
  +0x40/+0x60, 128 B = two cache lines), stored with **aligned** `vmovdqa`,
  because glibc aligns the *destination* once and then keeps it aligned. The
  overlapping head/tail pair is at 0x176520 and the back-to-back variant at
  0x1765a0 — it copies the first 128 B, walks forward aligned, then re-copies
  the last 128 B from `-0x20(%rdx)`.
* **Non-temporal tier**: above `__x86_shared_non_temporal_threshold` it switches
  to `vmovntdq`, 2×128 B per iteration (0x1000 apart), with four `prefetcht0`
  per 128 B block at +0x80/+0xc0/+0x100/+0x140 and four more at +0x1080…+0x1140.
  That is a 4 KB-ahead prefetch stream and it is *only* in the NT loop.
* **ERMS tier**: below `__x86_rep_movsb_stop_threshold` it aligns to 64 B and
  runs `rep movsb`.
* **Shared**: nothing narrower than 32 B for the bulk, and `vzeroupper` before
  every return.

What this means for our ladder: the shape we converged on (32 B accesses, 4×
unroll, overlapping head/tail pair, prefetch) is the same shape glibc uses. glibc
beats us below 128 B not by copying differently but by *getting to the copy
sooner* — it has no size-class chain at all, just two thresholds. That
observation is what V9b later turned into 1.5 ns at 16 B.

## 4. V3 — 32 B-aligned main loop

`avx2_t8al` = `avx2_t8` plus one test of the low bits of both pointers, taking an
aligned loop when it passes and the unaligned loop otherwise (there is no
alignment *assumption*; there is a real runtime check).

Certified: **LOSS** at 16 B (−20.06%, znver2) and 128 B (−5.02%, znver2).
Everything else in 32 B…4 MiB is Tier B ("measured"), including the only large
row that looks like a gain (+10.80% at 4 KB, znver2).

**Verdict: V3 is not adopted on evidence.** Its only certified effects are
negative, and the one apparent large gain is in a Tier-B key where the floor is
25%. It survives in the fused kernel because the fused configuration is what
campaigns `final1`/`v9b`/`v9c` measured, and silently changing a knob now would
invalidate them — but it is carried as a *liability*, not as a win, and it is the
first thing to remove if the 16–128 B range ever becomes the goal (V9b's data
suggests the aligned branch is pure overhead there, since those sizes never reach
the loop).

## 5. V4 — unroll depth

`avx2_t4` / `avx2_t8` / `avx2_t12` differ only in `TUNED_UNROLL`.

| pair | certified | measured elsewhere |
|:--|:--|:--|
| t8 vs t4 | **WIN +16.71% at 16 B** (znver2); **LOSS −13.82% at 4 KB** (znver2) | everything else |
| t12 vs t8 | **LOSS −9.68% at 16 B**, **LOSS −11.71% at 1 KB** (znver2) | everything else |

The 16 B "WIN" for t8 is not a loop effect: 16 B never enters the loop, so it is
code layout. The 4 KB loss is a real loop effect and it is the only one.

**Verdict: unroll 4.** No certified gain for 8 or 12 anywhere, one certified loss
for 8 at 4 KB, two for 12 in the small and mid classes. This is a negative
result, and it is reported as one: doubling the unroll did not buy anything that
this protocol can see.

## 6. V5 — prefetch

Three rungs, one knob each: `avx2_t4pf` (flat +128 B on the unaligned loop),
`avx2_t8ap` (prefetch added to the aligned loop), `avx2_t8alp` (both), against
`avx2_t4` / `avx2_t8al` / `avx2_t8ap`.

Certified: **WIN +16.22% at 16 B** (t4pf vs t4, znver2 — again a size that never
enters the loop, so layout); **LOSS −9.68% at 32 B** and **−9.68% at 64 B**
(t8ap vs t8al, znver2); **WIN ×3 at 16/32/64 B** for t8alp vs t8ap (znver2) and
**LOSS −8.82% at 64 B** on znver3. Everything from 128 B to 4 MiB is Tier B.

**Verdict: prefetch distance is not resolvable on this host, and prefetch is
neutral-to-harmful where it can be resolved.** The fused kernel therefore keeps
the shape that was measured best on the one class where prefetch could matter at
all — flat +128 B, and *gated* to n ≥ 64 KB (`TUNED_PF_MIN`), because the first
V5 attempt (one distance per size class) lost at 4 MiB to a flat +128 B, and the
screening data put 4 KB without prefetch at 41.9 ns against 47.6 ns with it. The
gate is carried as a *guard*, not as a certified win: in `final1` the gated
64 KB–1 MiB rows are all "measured".

## 7. V6 — compiler flags (campaign `final1`)

Each rung is `avx2_final_gcc` plus exactly one flag, in the same binary as its
twin (`bench/<t>_perf6` holds the -flto and PGO objects beside the unflagged
ones), so the pair delta is the flag.

| flag | pair delta (worst case over 3 targets) | verdict |
|:--|:--|:--|
| `-Ofast` | 0.00% at 16 B…16 KB; ±0.8% at 64 KB; ±4% at 4 MiB (Tier B) | **rejected** — compiles the same bytes, licenses FP reassociation |
| `-funroll-loops` | −19.35% at 16 B, −10.71% at 32 B, −5.12% at 1 MiB | **rejected** — actively hurts |
| `-fno-plt` | 0.00% at 16 B…512 B; +3.07% at 1 MiB, +15.15% at 4 MiB (Tier B) | **rejected** — a control rung for a kernel that makes no calls |
| `-fno-omit-frame-pointer` | 0.00% at 16 B…512 B; −3.55% at 4 MiB | **rejected** — costs a register, buys nothing |
| `-flto` | 0.00% mostly; −9.68% at 16 B, −6.25% at 128 B; sign flips at 32 B across targets; one certified WIN (512 B, znver2, +15.71%) | **rejected** — a single win on one target does not justify a link-time build requirement |

**No compiler flag earned its place.** The deliverable keeps the plain gcc flag
set. `-Ofast` producing byte-identical behaviour is worth recording: on this
kernel, the "faster compiler flags" family is empty.

## 8. V7 — PGO, and what it accidentally proved

`avx2_final_pgo` is the same source compiled with `-fprofile-generate`, run over
the size ladder, then rebuilt with `-fprofile-use`. Its twin sits in the same
binary.

Certified wins: **16 B +25.10% / +25.20% / +25.20%** and **32 B +25.10% /
+25.20% / +25.49%** against glibc; as a pair against the unflagged build it is
16 B 3.1 → 1.9 ns, 32 B 3.1 → 1.9 ns, 128 B 3.3 → 2.8 ns. Everything from 256 B
to 4 MiB is unchanged.

PGO changes nothing about the loop, and the loop is what it would optimise if the
kernel were loop-bound at these sizes. What it did was move the *cold size
classes out of the hot path* — which turned a 25% deficit at 16–32 B into a 25%
lead. That is a statement about the **entry chain** (six compares before the tiny
case is reached: 512, 256, 128, 64, 32, 16), not about code generation.

**Verdict: PGO is not adopted** (it needs a per-target profile build), but it was
decisive as a diagnostic, and it directly caused V9b.

## 9. V8 — 32..63 B as four 16 B moves

`avx2_final16_gcc` vs `avx2_final_gcc`: one knob (`TUNED_SMALL_AVX`).

Certified: **+9.39% at 16 B on v3** and **−11.20% at 32 B on v3**; 64 B +10.04%
on znver2/znver3 but Tier B there, so not a claim. On znver2/znver3 16 B is
−10.0% and 32 B 0.00%.

**Verdict: no answer.** The two targets disagree and the one target that
certifies anything certifies both a win and a loss at adjacent sizes. The fused
kernel keeps the gcc build's 16 B shape (four `T_BLOCKPAIR`-style 16 B moves at
32..63 B) because that is what the `final1` campaign measured, and the honest
summary is that this knob is invisible at this measurement resolution.

## 10. V9 — the fused kernel against glibc (campaign `final1`)

Knobs: gcc build (V1), aligned loop (V3), unroll 4 (V4), gated +128 B prefetch
(V5), flat overlap classes with no tail branch (V8).

Certified in `final1`: **512 B +16.83% (znver2) and +16.83% (v3)**, and the tiny
class — **16 B −25.14% on znver2 and znver3, −38.21% on v3**, all certified, plus
32/64 B losses.

Measured but below the 25% Tier-B floor, and therefore not claims: +8.4% at
256 B, +15.7…17.0% at 512 B on the remaining pairs, +7.9…10.8% at 1 MiB. The two
later campaigns re-measured the same binary in fresh windows and the 1 MiB
advantage did certify there (v9b: +10.33% Tier A on znver2, +12.02% Tier A on v3),
which is the strongest statement this push can make about the big sizes: **the
fused kernel is about 10% ahead of glibc at 1 MiB, and level with it from 1 KB to
64 KB.**

4 MiB is unusable in `final1` (spread 25%, and the clang and gcc builds of the
same knobs differ by 10 points in opposite directions — that is the window, not
the code). The same campaign's `-flto` build measured 119 676 ns, the plain build
119 044 ns and the clang build 107 292 ns on znver2: a 12% spread between builds
that share two of three knobs. Nothing at 4 MiB in this push is a claim.

Census for the whole campaign: 468 rung × size × target rows, **10 certified
wins, 139 certified losses, 280 "measured"**. Of the 139 losses, 132 are
`ref_scalar` and `naive` (every size); the rest are the fused rungs at 16/32/
64 B and exactly one outside the tiny class — `avx2_final_gcc_ofast` at 128 B,
−2.53% against a 1.5% Tier-A floor. That composition is the instrument, not the
kernel.

## 11. V9b — the tiny class, fixed from one compare

`avx2_final_v2_gcc` = `avx2_final_gcc` plus `TUNED_SMALL_PATH 1`: n ≤ 64 is taken
from a single compare before the flat-class chain, and copied with the K4 shape
(four 16 B moves at 32..63 B, two at 16..31 B, overlap pairs below).

Pair verdicts against `avx2_final_gcc`, same windows (campaign `v9b`):

| size | znver2 | znver3 | v3 | verdict |
|--:|--:|--:|--:|:--|
| 16 B | **+50.47%** (3.1→1.5) | **+50.47%** | **+54.98%** (3.4→1.5) | WIN ×3 |
| 32 B | **+20.40%** (3.1→2.5) | **+20.40%** | **+11.51%** | WIN ×3 |
| 64 B | **+20.40%** (3.1→2.5) | **+20.40%** | **+20.37%** | WIN ×3 |
| 128 B | −4.78% | −4.78% | −7.35% | LOSS ×3 |
| 256 B | −18.48% | −18.48% | −26.52% | LOSS (certified on v3) |
| 512 B…64 KB | 0.00% at every size | 0.00% | 0.00% | control intact |

Against glibc, `avx2_final_v2_gcc` reads **16 B +38.02% (Tier A, spread 0.63,
WIN ×3)**, 32/64 B +0.39% (level), 512 B +15.71% (WIN), 1 MiB +10.33% (WIN,
znver2) / +12.02% (WIN, v3). So the kernel's worst certified deficit — the 25%
loss at 16–64 B — is gone, replaced by a certified 38% lead at 16 B and parity at
32/64 B. V7's diagnosis held on all three targets, and 1.5 ns beats PGO's 1.9 ns
without a profile in the build.

The price, and its mechanism: 128 B and 256 B each move about one timer step
(3.3→3.4, 3.4→4.0). Both are consequences of *inserting a dispatch level* into a
function whose 64–512 B classes were straight-line before: at 128–256 B the copy
itself is only 8–16 cycles, so one extra taken branch is 5–10% of the work.

## 12. V9c — undoing the cross-jumped heads

The `v9b` object showed gcc cross-jumping the head of `T_BLOCKPAIR(2)` (64..128 B)
with the head of `T_BLOCKPAIR(4)` (256..512 B), so n = 256 ran two 32 B moves, a
compare, two more moves and a `jmp` (`memcpy_avx2_final_v2_gcc+0x1c0 → +0x2f0 →
+0x27b`). `avx2_final_v2_gcc_noxj` is the same source built with
`-fno-crossjumping`; its object has the straight-line block pair back
(`+0x210: cmp $0x100, jbe`, then 4 head + 4 tail moves, no mid-path jump).

Pair verdicts against V9b (campaign `v9c`, znver2; znver3/v3 agree within one
step):

| size | V9b | V9c | delta | verdict |
|--:|--:|--:|--:|:--|
| 16 B | 1.5 | 1.5 | 0.00% | measured |
| 32 B | 2.5 | **2.2** | **+12.58%** | WIN |
| 64 B | 2.5 | **2.2** | **+12.58%** | WIN |
| 128 B | 3.4 | 3.7 | −9.13% | LOSS |
| 256 B | 4.0 | 4.3 | −6.78% | LOSS |
| 512 B | 5.9 | 6.0 | −2.66% | LOSS (one timer step) |
| 1 KB…1 MiB | equal | equal | 0.00% | measured |

Against glibc, V9c certifies **16 B +38.02% / +37.62% / +38.02%, 32 B +12.92% on
all three, 64 B +12.92%, 512 B +13.47% / +13.47% / +14.59%**, and **1 MiB
+11.21% / +9.95% / +9.99%** (Tier A in `v9c`'s windows), while 128 B is −20.09%
(certified on all three) and 256 B is −15.88%/−16.93% (certified on znver2 and
v3; Tier B, i.e. not a claim, on znver3).

So the cross-jumping hypothesis was **wrong for 256 B** — the straight-line pair
is there and 256 B is still 4.3 ns — and right for 32/64 B, which gained a timer
step. The 128–256 B cost is the dispatch level, not the merge.

## 13. Bottom line

What the deliverable is
------------------------
`avx2_final_v2_gcc_noxj` (V9c): the fused kernel — gcc, 32 B-aligned main loop,
unroll 4, +128 B prefetch gated at 64 KB, flat overlap classes, no tail branch,
one-compare tiny path — built with `-fno-crossjumping`.

What it certifiably does against glibc, on all three targets:

| 16 B | 32 B | 64 B | 128 B | 256 B | 512 B | 1 KiB | 4 KiB | 16 KiB | 64 KiB | 1 MiB | 4 MiB |
|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|
| **+38%** | **+13%** | **+13%** | −20% | −16%¹ | **+13%** | 0% | −1.5%² | 0% | 0% | **+10%** | not usable |

¹ certified on znver2 and v3, Tier B (not a claim) on znver3.
² −1.51% on znver3 only (Tier A, floor 1.5, so certified); 0.00% on znver2 and
v3. Every other size from 1 KiB to 64 KiB is inside ±1.5%, i.e. measured.

Bold = certified (tier floor exceeded). "0%" is "measured": level, and that
level-ness is itself the result — glibc's 4×32 B loop is the same loop.
"not usable" is the protocol's verdict on 4 MiB in these windows.

What it certifiably loses: 128 B by 0.6 ns (two timer steps) and 256 B by 0.6 ns
(one step), plus one 1.51% step at 4 KiB on znver3. The first two are
dispatch-level costs of the tiny-class fix, both are reproducible across `v9b`
and `v9c`, and both are disclosed rather than tuned away. If 128–256 B matters
more than 16 B, the alternative is `avx2_final_v2_gcc` (V9b: 128 B −10%, 256 B
−8.5%, 32/64 B only level).

What this push established, in one sentence each:

* gcc over clang at 32/64 B — certified, and the only build change adopted.
* No compiler flag helps; `-Ofast` is a no-op, `-funroll-loops` hurts.
* PGO's 25% at 16–32 B was the entry chain, not the compiler — proven by V9b
  reproducing it statically, 1.5 ns vs 1.9 ns.
* Unroll 8/12 and the aligned loop are not justified by any certified result.
* Prefetch cannot be resolved on this host; it is kept gated, as a guard.
* The fused kernel is ~10% ahead at 1 MiB and level (±1.5%) from 1 KiB to
  64 KiB; its only certified deficits are 128 B and 256 B, both self-inflicted by
  the fix that removed the 16–64 B deficits, and one 1.51% step at 4 KiB on
  znver3.

Open items, in the order they would be worth someone's time:

1. **A size-class dispatch that costs one compare for every class** (e.g.
   `if (n > 512)` for the loop, then `switch (n >> 6)`), instead of a chain plus
   an early return. That is the one design that could keep V9c's 16/32/64 B wins
   *and* restore 128–256 B; V9b/V9c are both attempts to have it with a chain.
2. **4 MiB is unmeasurable here** (spread 25%, 12% spread between builds sharing
   two knobs). Any statement about 4 MiB needs a different instrument, not a
   different kernel.
3. **The aligned path (V3)** should be deleted and re-measured; the only certified
   evidence it ever produced was negative, and at 16–128 B it can only add cost.

## 14. Artifacts

    src/memcpy_avx2_tuned_impl.h         the one body; every V-rung is a knob set
    src/memcpy_avx2_final.c              V9, clang
    src/memcpy_avx2_final_gcc.c          V9, gcc
    src/memcpy_avx2_final16_gcc.c        V8 (32..63 B as 16 B moves)
    src/memcpy_avx2_final_gcc_{ofast,unroll,noplt}.c      V6 flag rungs
    src/memcpy_avx2_final_gcc_noomit.c   V6 (-fno-omit-frame-pointer)
    src/perf6/memcpy_avx2_final_{lto,pgo}.c               V6 -flto, V7 PGO
    src/memcpy_avx2_final_v2_gcc.c       V9b (TUNED_SMALL_PATH 1)
    src/memcpy_avx2_final_v2_gcc_noxj.c  V9c (+ -fno-crossjumping)
    src/memcpy_avx2_t{4,8,12}.c          V4 unroll sweep
    src/memcpy_avx2_t8{a,al,ap,alp}.c    V3/V5 alignment x prefetch grid
    src/memcpy_avx2_gcc.c                V1 (K4 body, gcc)
    scripts/phase2_6_run.sh              one campaign, protocol in one place
    scripts/phase2_6_matrix.sh           per-rung table with tiers and floors
    scripts/phase2_6_pairs.sh            one-knob pair deltas
    results/phase2_6_<tag>_<t>.{windows.log,matrix.txt}
    results/phase2_6_final1_analysis.txt, results/phase2_6_pair_verdicts.txt

Reproduce any campaign with:

    sh scripts/phase2_6_run.sh <tag> "<impl,impl,...>" znver2 znver3 v3
    sh scripts/phase2_6_matrix.sh results/phase2_6_<tag>_znver2.windows.log glibc memcpy

Every rung above passes `--verify` on all three targets (glibc reference, n =
0..4096 at random offsets, 64 B canaries both sides, plus the ladder sizes) and
`make gate` passes on every tree that produced a timing row. The two-flag rung
(V9c) also passes: a flag that changes the answer would not have been timed.

## 15. Lessons worth keeping

* **A 25% loss at 16 B was not a copy problem.** Three separate instruments
  (K4's 2.2 ns, PGO's 1.9 ns, V9b's 1.5 ns) said the same thing before the code
  said it. The disassembly of the entry (`cmp $0x40, %rdx / ja`) is the receipt.
* **The band is the boss.** 280 of 468 rows in the final campaign are
  "measured". Reporting them as small wins would have been easy and wrong.
* **A one-knob rung can still move two sizes.** V9b's 128 B loss is not in the
  knob's description; it appeared because the function got six bytes longer.
  Every layout-sensitive claim here comes with its disassembly.
* **"No effect" is a result.** V4 and V6 are written up as negative findings
  rather than dropped, because the next person to try unroll 8 or `-Ofast` on
  this kernel deserves to know it was measured.
