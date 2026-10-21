# Phase 3, Task 1 — the 1 KiB .. 64 KiB push

Scope: close the "level" band at 1 KiB .. 64 KiB without touching 128 B, the
hybrid/asm/NT rungs, or the certified deliverable. Five attack vectors (P1-P5),
each measured in the frozen protocol: 13-point ladder, 5 reps x 9 windows,
`RUN_TO_CAP=1`, `taskset -c 2`, `make gate` inside every campaign. Raw logs are
`results/live/phase2_6_p3{mid,mid2,asm,disp2}_<target>.windows.log`; the matrices
next to this file (`results/docs/analysis/phase3_p3*.matrix.txt`) are produced from those logs
by `scripts/analyze/phase2_6_matrix.sh`, which shares its floor/tier definitions with
`scripts/analyze/mo_analyze.sh`. Nothing below is hand-entered.

Kernels: `src/memcpy/memcpy_avx2_mid_impl.h` (one parameterised body) and ten one-knob
wrappers, all registered as ladder rows. Reference for every delta is glibc; the
deliverable `avx2_final_v2_gcc_noxj` is carried in every campaign so the mid
numbers can be read against the kernel we already ship.

## Answer

The mid band is **not** uniformly level: one size inside it — 16 KiB — is a
certified win, and the rest of the band is genuinely at parity.

| size | deliverable | `avx2_mid_pm` | tier | verdict |
|------|------------:|--------------:|:----:|---------|
| 1 KiB  | -1.41 % | -1.41 % | A | measured (inside floor) |
| 4 KiB  |  0.00 % |  0.00 % | A | measured (inside floor) |
| 16 KiB |  0.00 / -1.19 / -1.19 % | **+7.13 / +5.95 / +7.13 %** | A A A | **WIN** |
| 64 KiB |  0.00 / +0.08 / -0.80 % | -0.80 / +0.08 / 0.00 % | A | measured |
| 256 KiB| +0.21 / +0.75 / +0.59 % | +0.76 / +0.56 / +0.57 % | A | measured |

(columns are znver2 / znver3 / v3; min_of_mins, delta vs glibc)

`avx2_mid_pm` at 16 KiB: 193.0 ns against glibc's 210.2 ns on znver2, 195.5 vs
210.2 on znver3, 195.2 vs 210.2 on v3 — a 2.2 % band, a 13-15 ns margin, Tier A
on all three targets. That is the acceptance condition of the task (>1.5 %,
Tier A, any size in range) met, on the strongest silicon we have.

## It does not ship, and that is the point of the last measurement

The win is real; a shippable *composition* is not. Two facts, both measured:

1. **The small path is not portable.** Any new translation unit that carries the
   deliverable's sub-1 KiB path loses it: `avx2_mid_pm` is 3.1 ns at 32 B and
   64 B where the deliverable is 2.2 ns (-24.75 % on both), 4.3 vs 3.7 at 128 B.
   Same source, same knobs, different function body and different layout.
2. **A runtime size dispatch costs about 0.3 ns per call,** which is roughly the
   whole margin at 16-512 B. Measured with `avx2_mid_disp`, one symbol that
   tail-calls the deliverable below 4 KiB and `avx2_mid_pm` above:

| size | deliverable | `avx2_mid_disp` | what the dispatch costs |
|------|------------:|----------------:|-------------------------|
| 16 B  | +38.02 % | +25.10 % (Tier B) | 1.5 -> 1.9 ns |
| 32 B  | +12.92 % | +0.35 % measured  | 2.2 -> 2.5 ns |
| 64 B  | +12.92 % |  0.00 % measured  | 2.2 -> 2.5 ns |
| 128 B | -20.09 % | -30.13 %          | 3.7 -> 4.0 ns |
| 256 B | -16.93 % | -25.34 %          | 4.3 -> 4.7 ns |
| 512 B | +13.47 % | +11.22 % WIN      | 6.0 -> 6.2 ns |
| 1 MiB | +10.85 % |  +5.19 %          | - |

So the dispatcher is worse at eleven of thirteen sizes and better at exactly one:
16 KiB, where the mid loop's 13 ns margin is two orders of magnitude larger than
the call it costs. Paying three certified small-size wins (32 B, 64 B, 256 B) to
buy one mid-size win is a net loss, and "keep both" is not available: the
deliverable's small-path performance is a property of its own function layout,
and every attempt to carry that path into a second, larger function has lost
20-40 % of it.

**Adopted: nothing.** `src/memcpy/memcpy_avx2_final_v2_gcc_noxj.c` and its certified
rows are untouched by this task. `avx2_mid_pm` and the nine other mid rows stay
in the tree as evidence rungs, registered and reproducible, not as the shipped
kernel. The 1 KiB-64 KiB band remains at parity in the shipped kernel; 16 KiB is
a certified win that this project can demonstrate but cannot currently ship.

## Per-vector results

### P1 — dedicated mid kernel (unroll 8, 256 B/iteration, 3 prefetch streams)

Wins at 16 KiB, level elsewhere, and one loss the first version shipped with: the
head peel (aligning `dst` before the vector loop) was paid at 1 KiB, where there
is no loop to align — 1 KiB came out at -4.22 % on all three targets. Gating the
peel at 4 KiB (`avx2_mid_pm`) turned that into -1.41 %, inside the floor. That
gate is the only fix found by the whole task, and it costs nothing anywhere else.

The unroll-4 control (`avx2_mid_u4`, 128 B/iteration) settles the mechanism: it
loses 13-15 % at 16 KiB and 31.77 % at 4 KiB against the unroll-8 kernel. The win
is iteration count, not prefetch.

### P2 — multi-stream prefetch

Three streams at the wide distances (+256/+512/+1024 B) beat everything else at
16 KiB: `pm` +7.21/+7.02/+8.20 % against `pf1` (two streams) +3.57/+4.76/+5.95 %
and `pfnear` (the brief's literal +128/+256 B) +4.76/+4.76/+4.76 %, with `pfnear`
also losing 8.44 % at 1 KiB. Reverse the ordering at 256 KiB, where the
single-stream `pf1` is the best row (+1.72/+0.76/+2.26 %) and `pm` is +0.76/+0.56/
+0.57 % — the streams start competing for queue entries once the source is out of
L2. Both are inside the floor; no stream count is certified above 16 KiB.

### P3 — non-temporal prefetch on the source, 256 KiB and up

Catastrophic and unanimous: -56.34 %, -60.91 %, -59.07 % at 256 KiB, plus -9.84 %
and -8.44 % leaks at 1 KiB. `prefetchnta` on a source that is about to be read
linearly, into a destination that is written normally, demotes the lines the
store-forwarding path needs. Rejected; the rung stays in the tree precisely so
this is a measurement rather than a remembered opinion.

### P4 — explicit cache-line split handling for 64-512 B

Not built as a separate kernel, because the measurement said there was nothing to
build: the mid kernel never enters its vector loop below 1 KiB, and the
deliverable's block-pair path already carries 64 B, 128 B, 256 B and 512 B
(and `avx2_mid_pm`, which keeps that path verbatim, still wins +11.22 % at 512 B).
The one split-shaped variable with a measurable price was the head peel, worth
+2.81 pp at 1 KiB and 0.00 pp at 4 KiB, 16 KiB and 64 KiB. An explicit split-copy
for one- and two-cache-line sizes would have had to beat +13.47 % at 512 B to
matter; nothing in the campaign came near that, and 128 B is out of scope by
instruction.

### P5 — hand-scheduled asm twin of the winning loop

`src/memcpy/memcpy_avx2_mid_asm.S`, same peel, same 256 B iteration, same three
prefetches, hand-scheduled: -6.06 %, -5.82 %, -7.21 % against the C kernel at
16 KiB. It is 30 ns slower at 16 KiB and level everywhere else. The C version is
kept; this reproduces the Phase 2.6 asm result (compiler >= hand schedule on this
silicon) at a second kernel, which is what the task asked for.

## Housekeeping

- `src/memcpy/memcpy_avx2_mid_asm.S` is registered as `avx2_mid_asm` and width-gated
  like every other asm rung: declared 256, compiled 256 OK on all three targets.
- Commits: P1 kernel `d56e78f`, P5 asm twin `3602042`, mid + dispatcher wiring
  `bae20e4`, width-gate fix for the dispatcher TU `92461dc`. Campaign matrices
  and this report are committed together.
- `make gate` PASS with every rung registered; the dispatcher's gate failure
  during its first campaign (a TU with no audited width declaration) was fixed
  and the campaign restarted from zero rather than patched mid-run.

## Bottom line

The task's question was whether 1 KiB-64 KiB could be pushed. It can, at 16 KiB,
by 6-7 % over glibc on all three targets, with a Tier A verdict — and the shipped
kernel cannot carry the win without giving back three certified small-size wins
worth 13 % each. So the honest result is: one certified mid-band win, demonstrated
and documented, not shipped. The deliverable stays as certified in
`results/docs/PHASE_2_6_DELIVERABLE.md`, and the mid band stays at parity.
