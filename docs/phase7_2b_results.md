# Phase 7.2b — results: is the 512 B regression real or an artifact?

Rules: [`docs/phase7_2b_preregistration.md`](phase7_2b_preregistration.md),
committed `a90b236` before any measurement. Campaign logs:
`docs/data/raw/phase7_p72b_{40000,4000}_{znver2,znver3,v3}.windows.log`
(512 B only, 9 windows × 5 reps, `taskset -c 2`, zero migrated reps, all
implementations inside one process per window).

## Verdict: **R2 — the regression is REAL, and it comes from code shape, not from alignment placement.**

---

## T3 — timer resolution

Read directly from the committed Phase 7.2 log
(`docs/data/raw/phase7_f4al_znver2.windows.log`):

```
harness: target=znver2 clock: tsc=2.79477 GHz core=3.12990 GHz ... timer_overhead=40.00 ns
```

(this campaign's own `tsc_ghz` column reads 2.79481 — the same clock.)

| quantity | value |
|---|---|
| 1 TSC tick | 1 / 2.79477 GHz = **0.357811 ns** |
| F4 `min_ns` @512 B | **5.7891 ns** (batch total 741 ns) |
| candidate `min_ns` @512 B | **5.9453 ns** (batch total 761 ns) |
| delta | **0.1562 ns** |
| delta in TSC ticks | **0.4365 → below 1 tick** |
| delta in the instrument's quantum on the *reported* value | **19.99 → far above 1 quantum** |

**The tick is not this instrument's resolution for the reported number.** The
harness times a batch with `CLOCK_MONOTONIC_RAW` (integer nanoseconds) and
reports `(batch_total − overhead) / batch` (`bench/bench.c:388`). At 512 B the
batch is 128 copies (`pick_batch`: 65536/n), so the **reported** per-copy value
has a quantum of **1/128 = 0.0078125 ns**, not one TSC tick. The evidence is
in the data: every reported value is an exact multiple of 1/128 ns, i.e. every
batch total is an integer number of nanoseconds (741, 761, 751, 771, 1192, …).

The observed difference is **20 quanta** — 20 ns of wall time in a 741 ns batch.
**R4 does not fire**: the difference is not at or below the instrument's
granularity; the one-tick test applies to a single timer reading, and this is
a batch average. T1 confirms it empirically below.

## T2 — isolate alignment from code shape

**Build check.** `src/rungs/f4win64_aligned.c` compiled with the Makefile's
gcc rule **minus** `-falign-labels=64` is **byte-identical** to the H1b control
object:

```
H1b  (avx2_f4win64.o)          .text md5 8365c013c3fa2069e08c27e81a40d90e   1244 bytes
noflag (f4win64_aligned.c)     .text md5 8365c013c3fa2069e08c27e81a40d90e   1244 bytes
```

So the no-flag build **is** the H1b control (`f4win64` in the registry); no
third artefact was needed.

**16–64 B instruction diff, no-flag vs H1b** — `scripts/phase7_2_layout.py`,
16–64 B region (entry block through its first `ret`, plus the 32–64 B block),
padding nops dropped, branch targets masked:

```
znver2  vs CONTROL: IDENTICAL (21 instructions)   diff empty
znver3  vs CONTROL: IDENTICAL (21 instructions)   diff empty
v3      vs CONTROL: IDENTICAL (21 instructions)   diff empty
```

**512 B measurement.** The no-flag build shows the **identical** −2.70% as the
aligned candidate on znver2 and znver3 (table in T1). The regression is
therefore caused by the **knob's code shape** — the code the knob adds to the
window body moves the flat class chain that contains the 512 B path — and
**not** by the alignment flag. **R3 does not fire**: there is no alignment
strategy that avoids this, because the alignment flag is not the cause.

## T1 — higher-resolution measurement at 512 B

All values are the floor statistics of the 45 per-rep `min_ns` samples.
`spread%` = (p99 − p25) / median. Null band = |naive − ref_scalar| in the
same windows at the same iters.

### iters = 40000 (10× the standard)

| target | impl | min | median | p25 | p75 | p99 | spread% | Δ_min% | Δ_med% | Δ vs glibc% |
|---|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|
| znver2 | fast | 5.7891 | 5.7891 | 5.7891 | 9.4688 | 9.5469 | 64.91 | — | — | +16.83 |
| | f4win64al | 5.9453 | 5.9453 | 5.9453 | 6.0234 | 9.3906 | 57.95 | **−2.70** | **−2.70** | +14.59 |
| | f4win64 (noflag) | 5.9453 | 5.9453 | 5.9453 | 6.0234 | 9.3906 | 58.69 | **−2.70** | **−2.70** | +14.59 |
| | glibc | 6.9609 | 6.9609 | 6.9609 | 6.9609 | 10.0156 | 43.88 | −20.24 | −20.24 | — |
| | *null band* | | | | | | | **0.00%** | | |
| znver3 | fast | 5.7891 | 5.8672 | 5.8672 | 9.4688 | 9.5469 | 62.72 | — | — | +16.83 |
| | f4win64al | 5.9453 | 5.9453 | 5.9453 | 6.0234 | 9.3906 | 57.95 | **−2.70** | −1.33 | +14.59 |
| | f4win64 (noflag) | 5.9453 | 5.9453 | 5.9453 | 6.0234 | 9.3906 | 57.95 | **−2.70** | −1.33 | +14.59 |
| | glibc | 6.9609 | 6.9609 | 6.9609 | 6.9609 | 10.0156 | 43.88 | −20.24 | −18.64 | — |
| | *null band* | | | | | | | **0.00%** | | |
| v3 | fast | 5.8672 | 5.8672 | 5.8672 | 5.8672 | 9.5906 | 63.46 | — | — | +15.71 |
| | f4win64al | 5.9453 | 5.9453 | 5.9453 | 6.0234 | 9.3950 | 58.02 | **−1.33** | −1.33 | +14.59 |
| | f4win64 (noflag) | 6.0234 | 6.0234 | 6.0234 | 9.3125 | 9.3906 | 55.90 | −2.66 | −2.66 | +13.47 |
| | glibc | 6.9609 | 6.9609 | 6.9609 | 6.9609 | 10.0156 | 43.88 | −18.64 | −18.64 | — |
| | *null band* | | | | | | | **0.00%** | | |

### iters = 4000 (the standard, same windows)

| target | impl | min | median | p25 | p75 | p99 | spread% | Δ_min% | Δ_med% |
|---|---|--:|--:|--:|--:|--:|--:|--:|--:|
| znver2 | fast | 5.7891 | 9.5469 | 5.7891 | 9.5469 | 9.6250 | 40.18 | — | — |
| | f4win64al | 5.9453 | 9.3906 | 6.0234 | 9.3906 | 9.4688 | 36.69 | **−2.70** | +1.64 |
| | f4win64 (noflag) | 5.9453 | 9.3906 | 6.0234 | 9.3906 | 9.4688 | 36.69 | **−2.70** | +1.64 |
| | glibc | 6.9609 | 10.0156 | 6.9609 | 10.0156 | 10.1719 | 32.06 | −20.24 | −4.91 |
| znver3 | fast | 5.7891 | 9.5469 | 5.8672 | 9.5469 | 9.5513 | 38.59 | — | — |
| | f4win64al | 5.9453 | 9.3906 | 5.9531 | 9.3906 | 9.5160 | 37.94 | **−2.70** | +1.64 |
| | f4win64 (noflag) | 5.9453 | 9.3906 | 5.9453 | 9.3906 | 9.6781 | 39.75 | **−2.70** | +1.64 |
| | glibc | 6.9609 | 10.0156 | 6.9609 | 10.0156 | 10.1419 | 31.76 | −20.24 | −4.91 |
| v3 | fast | 5.8672 | 9.4688 | 5.8672 | 9.5469 | 9.7125 | 40.61 | — | — |
| | f4win64al | 5.9453 | 9.3906 | 6.0234 | 9.3906 | 9.5469 | 37.52 | **−1.33** | +0.83 |
| | f4win64 (noflag) | 6.0234 | 9.3906 | 6.0234 | 9.3906 | 9.5160 | 37.19 | −2.66 | +0.83 |
| | glibc | 6.9609 | 10.0156 | 6.9609 | 10.0156 | 10.1719 | 32.06 | −18.64 | −5.77 |

### The full 512 B distributions (batch totals in ns; all 45 reps, none dropped)

```
znver2, iters=40000
  fast        32x741   7x1212  6x1222
  f4win64al   27x761   7x771   5x1192 1x1193 5x1202
  f4win64     29x761 1x762 2x771 3x1192 4x1193 5x1202 1x1212
  glibc       34x891 1x911 1x951 1x1272 1x1273 7x1282
znver3, iters=40000
  fast         8x741 1x742 24x751 3x1212 2x1213 7x1222
  f4win64al   28x761 1x762 5x771 1x1192 1x1193 9x1202
  f4win64     29x761 1x762 9x771 6x1202
  glibc       34x891 2x951 9x1282
v3, iters=40000
  fast        36x751 3x1212 1x1213 4x1222 1x1232
  f4win64al   29x761 8x771 1x781 6x1202 1x1203
  f4win64     32x771 4x1192 1x1193 8x1202
  glibc       39x891 6x1282
```

**The fast modes are disjoint.** F4's fast mode is 741 ns; both knob builds'
fast mode is 761 ns. In 45 reps × 40 000 samples — 1.8 million batches per
implementation — the candidate never once reached F4's best. That is a real,
stable difference in the code, not an estimator artifact: ten times the samples
leaves Δ_min at exactly −2.70%.

**And the shift is two-sided.** The candidate's *slow* mode is ~20 ns **faster**
than F4's (1192–1202 vs 1212–1222 on znver2). So this is not "the 512 B path
is slower"; it is a 20 ns redistribution between the two modes the 512 B copy
lands in, caused by the knob's code growth. The mechanism of *which* mode a copy
lands in is not identified here and is not claimed.

## Rule selection

| rule | fires? | why |
|---|:--:|---|
| R1 (delta shrinks < 1.5% at higher iters on ≥2 of 3) | **no** | at iters=40000 the candidate's Δ_min is −2.70% on znver2 and znver3 (−1.33% on v3): 5 of 6 impl/target cells persist |
| R2 (persists AND the no-flag build shows ≈−2.70%) | **YES** | it persists, and the no-flag build shows the **identical** −2.70% on znver2 and znver3 |
| R3 (persists AND the no-flag build shows 0.00%) | **no** | the no-flag build shows −2.70%, so this is not alignment-flag placement |
| R4 (delta at or below the instrument's per-tick granularity) | **no** | the delta is 20 quanta of the reported value (1/128 ns); the TSC tick is the granularity of the batch total, not of the reported average |

**Verdict: R2 — the 512 B regression is real, and it is caused by the code
shape the knob adds, not by the alignment strategy.** The alignment strategy
works; there is simply nothing left for it to fix at 512 B.

## The trade, for the strategic decision (nothing is promoted)

| | shipped F4 | candidate | change |
|---|--:|--:|--:|
| 128 B vs F4 | — | +18.31% (min and median, all three targets) | a certified **loss** becomes a **win** |
| 128 B vs glibc | −10.04% | **+10.11%** | the first time 128 B is ahead of glibc in this project |
| 512 B vs F4 | — | −2.70% | two timer ticks, 20 quanta, stable at 10× resolution |
| 512 B vs glibc | +16.83% | +14.59% | still a certified win, 2.2 points smaller |
| 16/32/64/256 B, 1 MiB | — | +0.00% / +0.00% / +0.00% / +0.00% / −0.38% | unchanged |

## Deviations from the brief, declared

1. **`bench/bench.c` was changed**: `MAX_SAMPLES` 4096 → 65536. The harness
   silently capped `--iters` at 4096, so the specified 40000-iteration run was
   impossible without it. The **standard protocol is unaffected** — `--iters
   4000` still yields 4000 samples, and every frozen campaign reproduces
   bit-for-bit. No kernel was touched; the shipped F4 `.text` md5 is unchanged.
2. **The two iters settings are separate invocations, not one process.** The
   harness takes one `--iters` per run, so they are run back to back inside the
   same window and share that window's host state. All implementations are
   inside one process per window, as required.
3. **R4's TSC-tick test was applied and reported, but the "below resolution"
   classification was not adopted**, because the instrument's resolution on the
   reported value is 1/128 ns (the TSC tick is the granularity of the batch
   total, not of the batch average) and the difference is 20 of those quanta.
   T1 then settled it empirically.
4. **`f4win64_h1b_noflag` was not built as a new file**: the no-flag build is
   byte-identical to the existing H1b control, which is proven above.
