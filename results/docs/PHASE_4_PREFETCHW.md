# Phase 4 — prefetchw on the destination: the experiment and the negative result

One idea: instead of glibc's read-intent prefetch of the source, prime the
**destination** with `PREFETCHW` (write intent, line requested in Exclusive so the
store does not pay a read-for-ownership). Base kernel: the deliverable
`src/memcpy/memcpy_avx2_final_v2_gcc_noxj.c`, **not modified**. Result: prefetchw
does not win anywhere. The base ships unchanged, no variant is committed.

## How the experiment was built (and why it can be trusted)

The variant body is a copy of `memcpy_avx2_tuned_impl.h` with prefetchw
injection points added and nothing else changed:

```c
#if PFW_DIST > 0
#define T_PFW_LINE(p) __builtin_prefetch((const void *)(p), 1, 3)   /* prefetchw */
#else
#define T_PFW_LINE(p) ((void)0)
#endif
```

(`__builtin_prefetch(p, 1, …)` lowers to `prefetchw` — opcode `0f 0d` — for
`-march=znver2`, checked in the object, not assumed.)

The body was never transcribed by hand. It was generated from the deliverable's
own header by string substitution, so the **control** variant (`PFW_DIST 0`) is
the V9c kernel with the prefetch knob compiled out. That control is the experiment's
instrument check, and it passed:

| check | result |
|---|---|
| control vs deliverable, znver2 / znver3 / v3 | **instruction-identical**, 243 lines each, only the symbol name differs |
| prefetchw present in variants, absent in control | 4 static `prefetchw` per variant (coverage 1), 8 for coverage 2; 0 in control; `prefetcht0` count unchanged at 4 (2 for the replace-mode variant) |
| `--verify` x3 per target (n = 0..4096, random offsets, 64 B canaries both sides) | PASS, 60 impls OK each run, rc=0 |
| `make gate` x3 | PASS every time; prefetchw raised **no** audit issue (it is a hint, not an access) |

Variants were staged into `src/memcpy/` only because the registry and the gate
read that directory; they were removed after the measurements and the tree was
verified back to the deliverable (object diff against a pre-experiment snapshot:
identical). The base kernel's source was never edited.

Measurement: 5 reps x 5 windows, `taskset -c 2`, min_of_mins, interleaved with
glibc and the base in the same windows. Logs:
`results/live/phase4_s1_*.windows.log`, `results/live/phase4_s2_*.windows.log`;
matrices next to this file.

Sizes: 16, 64, 256, 1 KiB, 4 KiB, 16 KiB, 64 KiB, 256 KiB, 1 MiB, 4 MiB. The
focus sizes are 4 KiB, 16 KiB, 64 KiB, 256 KiB, 1 MiB; the small sizes are
measured only as a regression check.

## 1. The matrix actually run (converged subset, not 45 cells)

Coverage is defined per iteration, and this kernel's main loop advances 128 B per
iteration (4 x 32 B stores = 2 cache lines), not 256 B: B1 = one line per
iteration (the iteration's first), B2 = both lines, B3 = one line every other
iteration. The base kernel's unroll of 4 was deliberately not changed - otherwise
the experiment would have two variables.

5 distances x 1 coverage x 1 placement first, then coverage and placement around
the best distance (512 B), plus a replace-mode cell. 10 real cells + control.

Delta vs the base kernel at the focus sizes, `znver2 / znver3 / v3` (%). The
claim floor is 1.5 % unless that size's null band - the same byte loop compiled
in two TUs, measured in these same windows - is wider. In campaign 2 the bands
were 0.0-0.9 % at 4, 16 and 64 KiB, 0.7/5.4/2.1 % at 256 KiB and 0.1/1.7/0.0 % at
1 MiB; in campaign 1 they were 20.1 % (znver2, 4 KiB), 20.4 % (znver3, 256 KiB)
and 5.1 % (v3, 1 MiB). Where the band exceeds 1.5 % it *is* the floor, which can
only make a win harder, never easier - the null cells are reported as measured,
not as wins. Rows A1-A5 are campaign 1; the A4 B/P rows and the replace cell are
campaign 2, where the control was measured again.

| cell | 4 KiB | 16 KiB | 64 KiB | 256 KiB | 1 MiB |
|---|---|---|---|---|---|
| **control (no prefetchw)** | +0.00/+0.00/+0.00 | −0.12/+0.00/−1.07 | +0.00/−0.80/−0.72 | −0.38/+0.38/+0.38 | +0.63/−0.26/+0.93 |
| A1 128 B, 1 line, top | +0.00/+1.49/+0.00 | −3.52/−2.44/−4.63 | +0.00/+0.71/+0.00 | +0.38/+0.00/+0.02 | −0.85/+0.55/−0.60 |
| A2 256 B, 1 line, top | +0.00/+1.49/+0.00 | −2.23/−1.16/−3.44 | +0.79/+0.71/+0.00 | −0.19/+0.00/+0.00 | −0.51/+0.55/−1.62 |
| A3 384 B, 1 line, top | +0.00/+1.49/+0.00 | +0.12/+1.16/−1.07 | +0.00/+0.00/+0.79 | +0.38/+0.00/+0.00 | −0.63/+0.13/−2.61 |
| A4 512 B, 1 line, top | +1.49/+1.49/+0.00 | +0.00/+0.12/−1.07 | +0.79/+0.00/+0.08 | −0.76/+0.21/+0.00 | +0.71/+0.47/+1.10 |
| A5 1 KiB, 1 line, top | +0.00/+1.49/+0.00 | −1.06/+0.00/−2.26 | +0.79/+0.71/+0.79 | −0.38/−0.36/+0.02 | −0.34/+1.14/−1.28 |
| A4, **every line** (B2) | +0.00/+0.00/−1.51 | −5.88/−5.87/−7.13 | +0.79/−0.08/+0.08 | +0.00/+0.38/−0.19 | +0.29/−1.11/+0.59 |
| A4, **every other line** (B3) | +0.00/+1.49/+0.00 | **+3.41**/+1.29/+0.12 | +0.00/−0.80/+0.08 | −0.57/+0.38/−0.19 | +0.50/**+1.79**/+0.34 |
| A4, after loads (P2) | +1.49/+1.49/+0.00 | −1.18/−1.06/−2.38 | +0.79/−0.80/+0.08 | +0.00/+0.95/+0.19 | −2.53/−4.83/−3.82 |
| A4, one iteration ahead (P3) | +1.49/+1.49/+0.00 | −3.53/−3.40/−4.75 | +0.00/+0.00/−0.72 | +0.19/+0.76/−0.19 | +1.35/−1.50/+0.51 |
| A4, 1 line, **dst T0 removed** | +1.49/+1.49/+0.00 | +1.06/+0.12/+0.00 | +0.71/+0.00/−0.72 | −0.95/−0.95/+0.19 | +0.58/−1.07/−0.25 |

Not run (and why): coverage 2 and 3 at every other distance — B2 is the worst cell
in the matrix at 16 KiB by a wide margin, so pushing more prefetches at other
distances was not going to help; placement P2/P3 at other distances — both are
neutral-to-negative at the best distance; distances with P2/P3 — the placement
question was answered independently of distance, and the answer was no.

## 2. Did prefetchw win anywhere?

**No cell wins across the three targets.** The two cells that clear the 1.5 %
floor do so on one target each, and are level or negative elsewhere:

- A4/B3 (every other line, 512 B ahead) at 16 KiB: **+3.41 %** znver2, +1.29 %
  znver3 (inside floor = parity), +0.12 % v3 (parity). One win, two parities —
  the task's condition was a win on all three, or two of three with the third at
  parity. This is one of three.
- A4/B3 at 1 MiB: +0.50 / **+1.79** / +0.34 — one target again.
- A4/P2 (after loads) at 1 KiB: −4.16 / +4.16 / +2.81 — sign flips across targets;
  that is noise, not an effect.
- Every other cell at 4 KiB, 64 KiB, 256 KiB and 1 MiB sits inside ±1.5 % on all
  three targets, apart from two boundary cells that are one timer step out:
  A4/B2 at 4 KiB on v3 (−1.51 %) and A4/P3 at 1 MiB on znver3 (−1.50 %). Both are
  losses by a hundredth of a percent, which is the instrument, not an effect.

Against glibc (the "big win" condition — >1.5 % at 4 KiB, 256 KiB or 1 MiB):

| impl | 4 KiB | 16 KiB | 64 KiB | 256 KiB | 1 MiB |
|---|---|---|---|---|---|
| base | −1.51/−1.51/+0.00 | −1.19/−1.31/−0.12 | −0.80/+0.00/−0.08 | +0.17/−0.57/+0.56 | +10.19/+8.40/+10.43 |
| A4/B1 | +0.00/+0.00/+0.00 | −1.19/−1.19/−1.19 | +0.00/+0.00/+0.00 | −0.59/−0.36/+0.56 | +10.82/+8.83/+11.41 |
| A4/B3 | −1.51/+0.00/+0.00 | +2.26/+0.00/+0.00 | −0.80/−0.80/+0.00 | −0.40/−0.19/+0.38 | +10.63/+10.04/+10.73 |

The 4 KiB and 256 KiB rows — the two sizes where the task wanted a win over glibc
— are unchanged to two decimals. The 1 MiB advantage the deliverable already has
is not improved by prefetchw beyond noise.

Where it loses, clearly and repeatably: **short distances** (128–256 B ahead at
16 KiB: −2 % to −5 %), **every-line coverage** (16 KiB: −5.9/−5.9/−7.1 %, the
worst cells in the matrix), and **placement P2** at 1 MiB (−2.5/−4.8/−3.8 %).

## 3. Small sizes: a layout artefact, not a prefetchw regression

The 16 B and 32–512 B rows move by one or two timer steps in both directions
(16 B: −19.5 % on several variants; 64 B: **+14 %** on others). **None of that is
prefetchw**: the loop that contains prefetchw is only entered above 512 B, so at
16–512 B the executed instructions are exactly the deliverable's. The cause is
code layout — the variant functions are longer, so the tiny block lands on a
different address. Three facts prove it: the control (byte-identical body, no
prefetchw) scores +0.00 % at 16 B in the first campaign and +0.63 % in the second
- the same function measured twice, so that spread is the instrument; at 16 B the
variant's main loop is never reached, so the prefetch instructions are not
retired at all; and the same variant is −19 % at 16 B while being +14 % at 64 B,
which no prefetch can do.

Consequence for shipping: any change to this kernel that lengthens the hot
function must be judged only at sizes where it executes, and a shipped prefetchw
would have to be gated (size threshold) and re-certified from 16 B up. That cost
was not paid, because nothing won.

## 4. The mechanism: why prefetchw is redundant here

Three properties of this kernel make write-priming useless, and the matrix shows
all three:

1. **The stores already write whole lines.** Each 64 B line is written by two
   consecutive 32 B stores inside one 128 B iteration. The store buffer merges
   them into a full-line write, so the line is allocated without reading memory.
   There is no RFO read left for `PREFETCHW` to eliminate — the exact case the
   instruction exists for (partial-line writes, scatter, strided stores) does not
   occur in this loop.
2. **Zen2's L2 stream prefetcher already tracks the store stream.** A demand
   store miss trains the same tracker a load miss does, so by the time the loop
   reaches line *k+4* the line is in flight. Software prefetchw at 128–512 B adds
   an MAB/LS entry for a line the hardware already requested — visible as the
   losses at short distances and at every-line coverage, which are exactly the
   cells that add the most redundant prefetch ops.
3. **Prefetch ops compete with the stores for the same resources.** `prefetchw`
   is dispatched like a load and consumes a miss-address-buffer slot. In a loop
   that is already store-bound, a prefetch per line (B2) measurably steals
   throughput from the stores: −5.9 % to −7.1 % at 16 KiB. Every-other-line (B3)
   halves the op count and lands at parity-to-slightly-positive, which is the
   signature of a redundant hint whose only effect is queue pressure.

This matches the AMD Zen 2 PPR and the PREFETCHW literature: the instruction
pays off for store streams that cannot be merged into full lines, and for
pointer-chasing structures; for a sequential full-line copy, the write-allocate
path plus the hardware prefetcher already do the job. Our measurement is what
that prediction looks like on this box.

Also worth recording: at 4 MiB the *control* — instruction-identical to the base
— measured −11.3 %, −5.0 % and +8.8 % against the base, because the two functions
sit at different addresses. The 4 MiB region is not measurable at 5 windows with
this harness, which is why the project's standing rule is to leave it alone. No
4 MiB number in this report is used for anything.

## 5. Verdict

- **No variant wins.** 10 cells x 3 targets: the best cell is +3.41 % on one
  target, level on the others. Nothing is promoted, nothing is committed, and the
  deliverable `src/memcpy/memcpy_avx2_final_v2_gcc_noxj.c` is byte-for-byte what
  it was (verified by object diff before and after).
- **Is prefetchw a legitimate win on this box? For a sequential memcpy, no.**
  Zen2's write-allocate path plus its L2 prefetcher already prime the
  destination; a software `PREFETCHW` on the store stream is redundant, and when
  issued once per line it is a measurable loss because it competes with the
  stores it was meant to help.
- The idea is not wrong in general — it is wrong *for this access pattern*. The
  experiment that would test it properly is a partial-line or strided store
  stream, which is outside a memcpy's contract.

Reproduction: body = `memcpy_avx2_tuned_impl.h` + the `T_PFW_LINE`/`T_PFW_TOP`
macros above, knobs `PFW_DIST ∈ {128,256,384,512,1024}`, `PFW_COV ∈ {1,2,3}`,
`PFW_PL ∈ {1,2,3}`, `PFW_REPLACE ∈ {0,1}`, gcc `-O3 -march=<target>
-fno-strict-aliasing -fno-crossjumping`; campaign
`ITERS=4000 REPS=5 RUN_TO_CAP=1 mo_measure.sh` at 5 windows.
