# Phase 2 running summary — kernel ladder on Zen2-class silicon

Machine: 4-vCPU KVM guest, CPUID reports "AMD EPYC Processor" (family 17h model 01h)
but measured throughput is Zen 2 class (2x256b load/cyc, 1x256b store/cyc,
2x256b FADD/cyc), TSC 2.7947 GHz, L1d 32K, L2 512K, L3 8M, 7.7 GiB RAM shared.
Pin: `taskset -c 2`, CPU verified with `sched_getcpu()` inside the PMU window
(never migrated in any Phase 2 row: `migrated=0` in every CSV row).
Clock: `core_ghz` snapshot per run (2.99–3.14 GHz observed); `cycles_per_byte`
comes from the PMU cycles counter only, never from TSC.

## Kernel 3 — `memcpy_sse2` (16B blocks, unrolled 4x)

Source `e082da7` was **withdrawn before any timing** and fixed in `43f01dc`: see
"defect" below. Verify PASS on znver2, znver3, v3 *after* the fix. 9 windows x
5 reps, pinned, 0 migrated reps. Report: `results/phase2_sse2_analysis.txt`;
CSVs: `results/phase2_sse2_{znver2,znver3,v3}.csv`.

### Defect: rung 3 first compiled to AVX, not SSE2

With `-mavx2` in the target flags and the file outside `SCALAR_SRCS`, clang's SLP
vectorizer merged two adjacent `_mm_loadu_si128` intrinsics into one 32-byte
`vmovups` and the rung ran on `ymm` registers — rung 4 wearing rung 3's name,
which would have made the kernel-3 vs kernel-4 comparison meaningless. Caught by
reading the generated assembly before timing. `make` now compiles that one TU
with `-mno-avx -mno-avx2 -mno-fma -fno-slp-vectorize`, and the regenerated
assembly shows **0 `ymm` references, `movups %xmm0..3`, 0 `call` instructions**.

### Incremental gain vs kernel 2 (`min_of_mins`, same windows)

| size | znver2 | znver3 | v3 | band-aware verdict |
|--:|--:|--:|--:|:--|
| 16 B | −4.4% | +0.0% | +3.9% | within-noise (all) |
| 32 B | +28.6% | **−20.1%** | +28.9% | WIN / **LOSS** / WIN |
| 64 B | +54.2% | +37.7% | +54.3% | WIN |
| 128 B | +67.6% | +54.0% | +69.3% | WIN |
| 256 B–1 KB | +75.6…+77.1% | +64.8…+71.1% | +75.2…+77.3% | WIN |
| 4–64 KB | +71.2…+73.3% | +64.8…+69.4% | +71.4…+73.5% | WIN |
| 256 KB–4 MB | +60.4…+69.1% | +53.4…+62.9% | +62.6…+68.8% | WIN |

**Measured crossover between rung 2 and rung 3: 32 B** on znver2 and v3, **64 B**
on znver3 (where 32 B is a 20.1% *loss*). Below 32 B the two rungs execute the
same byte fast path, so 16 B is within-noise as expected.

**cycles/byte from `min_of_mins`, median over sizes >=64 B:**

| target | word (k2) | sse2 (k3) | glibc |
|:--|--:|--:|--:|
| znver2 | 0.3169 | **0.0864** | 0.0729 |
| znver3 | 0.2339 | **0.0769** | 0.0702 |
| v3 | 0.4171 | **0.0848** | 0.0712 |

### Where SSE2 beats glibc: nowhere (explicit answer)

**LOSS at every size on every target** under the band-aware verdict — there is no
size where SSE2 first beats glibc. The gap is nevertheless narrow at the top:
`ours:glibc` is **1.05x at 4 MB** and 1.09–1.10x at 1 MB (znver2), 1.10–1.17x for
every size >=64 KB, and widens to 1.94x at 4 KB.

| size | znver2 | znver3 | v3 |
|--:|--:|--:|--:|
| 32 B | 1.25x | 1.50x | 1.25x |
| 1 KB | 1.82x | 1.82x | 1.83x |
| 4 KB | **1.94x** | 1.94x | 1.95x |
| 64 KB | 1.17x | 1.17x | 1.17x |
| 1 MB | 1.10x | 1.09x | 1.09x |
| 4 MB | **1.05x** | 1.06x | 1.14x |

### Codegen across the three targets

54 instructions, 18 distinct mnemonics, **208 bytes** — identical counts on all
three. znver2 == znver3 exactly; znver2/znver3 vs v3 differ by **4 moved lines
with an identical mnemonic multiset**, i.e. two `lea` instructions in a different
position, **not** an instruction-set difference. (The script printed
"instruction-set differences" here until it was fixed to compare sorted
multisets; see the defect list.)

### Rule 2 outcome, reclassified under the two-tier model

Under amendment 2 alone (11-12 of 13 sizes UNSTABLE) almost nothing was claimable.
The ratified two-tier model resolves it, and Kernel 3's gains survive:

| target | Tier A WIN | Tier B WIN | Tier B "measured, not claimed" |
|:--|--:|--:|--:|
| znver2 | 2 | 10 | 1 (16 B, -4.4%) |
| znver3 | 2 | 9 | 2 |
| v3 | 1 | 11 | 1 |

So the +54-77% gains are **claims**, not merely measurements, under the Tier B flat
25% floor; Tier A sizes use `max(1.5%, null band)`, which is tighter.

**Caveat, from the H1-H5 diagnostics (`results/h4_diagnostics.md`): those Tier B
labels are largely an artefact of the harness's 100-iteration cap.** Raising the
timed window to 4000 iterations collapses the spread at 8/16/64 KB from
43.69/48.91/n-a % to **0.75/1.48/0.68 %**. On the corrected window these sizes are
Tier A, i.e. the claims become tighter than the Tier B ruling required.


## Amendments in force (user-ratified) — see MEASUREMENT_PROTOCOL.md §3a/§3b

1. **Band-aware claim floor (wins over rule 3).** The applicable floor is
   `max(1.5%, measured null band at that target/op/size)`. Both verdicts are in
   every table and CSV; where they disagree the band verdict is the claim rule.
2. **Always 9 windows, no early stop.** After 9 windows a key whose per-window
   `min_ns` spread exceeds **10%** is marked `UNSTABLE` and carries **no claim**.
   Verified against the case cited when ratifying: word/znver2/256 KB spreads
   10.4% and is flagged.

**Consequence, and a conflict worth a ruling.** Under rule 2-as-ratified the
UNSTABLE label is *very* common, because it penalises an unstable floor even when
the measured effect is many times the floor:

| kernel | target | stable keys | UNSTABLE keys |
|:--|:--|--:|--:|
| naive | znver2 / znver3 / v3 | 18 / 16 / 12 | 21 / 23 / 27 |
| word | znver2 / znver3 / v3 | 21 / 21 / 17 | 18 / 18 / 22 |

(counts are over all rows in each CSV, i.e. 13 sizes x 3 impls = 39.)
That marks several *large* effects unclaimable: `word` at 32 B on znver2 spreads
18.4% and at 64 B 15.6%, so the ~4x win you acknowledged at 32 B carries no claim
under amendment 2 even though it clears the band cleanly under amendment 1.
Amendments 1 and 2 therefore disagree on the same measurement, and the two rules
have not been ranked against each other the way rule 3 and the band were. Both
are implemented exactly as written; this is flagged, not smoothed over.

## Kernel 2 — `memcpy_word` (8-byte chunks + byte tail)

Committed before benchmarking: `44ba1cb`. Verify PASS on znver2, znver3 and v3
(glibc reference, n=0..4096 with random offsets, 64 B canaries both sides).
`make audit`: `vector_insns=0` on all three targets. 9 windows x 5 reps, pinned,
0 migrated reps. Report: `results/phase2_word_analysis.txt`;
CSVs: `results/phase2_word_{znver2,znver3,v3}.csv`.

### Incremental gain vs kernel 1 (`min_of_mins`, same windows)

| size | znver2 | znver3 | v3 |
|--:|--:|--:|--:|
| 16 B | **−14.3%** | −8.7% | **−23.9%** |
| 32 B | +74.8% | +73.2% | +74.5% |
| 64 B | +76.1% | +77.9% | +77.5% |
| 128 B | +83.2% | +86.1% | +84.2% |
| 256 B–4 MB | +78.8…+84.5% | +78.1…+84.4% | +79.2…+84.2% |

All rows from 32 B up clear the mandated 1.5% floor **and** the measured null
band, so they are wins, not artefacts. **16 B is a genuine loss on znver2
(−14.3%) and v3 (−23.9%)**: the mandatory `<32 B` fast path costs a compare plus
a jump before the byte loop that kernel 1 does not pay. On znver3 the −8.7% sits
inside that target's 9.5% null band at 16 B and is therefore not claimable
either way.

**cycles/byte (median across sizes >=64 B, PMU cycles from the min rep):**

| target | naive (k1) | word (k2) | glibc |
|:--|--:|--:|--:|
| znver2 | 1.4940 | 0.2282 | 0.0691 |
| znver3 | 1.5105 | 0.2423 | 0.0695 |
| v3 | 1.4546 | 0.2363 | 0.0666 |

So the word rung buys **~6.3x fewer cycles/byte** than rung 1, and glibc still
needs ~3.4x fewer than us.

### vs glibc, by size class (rule 3 applied)

Loss at **every** size on **every** target — no ties, no wins. Reported as such.
Ratio `ours:glibc` on `min_of_mins`: best **1.25x** at 32 B (znver2, v3), worst
**6.5x** at 1 KB (znver3); 2.4–3.3x for every size >=64 KB.

| class | median d vs naive | median d vs glibc | vs naive (W/L/N) | vs glibc (W/L/N) |
|:--|--:|--:|:--|:--|
| tiny (<64 B) | +30.23% | −113.00% | 1/1/0 | 0/2/0 |
| small (64 B–4 KB) | +82.30% | −322.20% | 6/0/0 | 0/6/0 |
| mid (4–64 KB) | +83.67% | −328.91% | 2/0/0 | 0/2/0 |
| big (64 KB–1 MB) | +83.88% | −184.12% | 2/0/0 | 0/2/0 |
| huge (>1 MB) | +84.06% | −140.44% | 1/0/0 | 0/1/0 |

### Codegen across the three targets

**Identical on all three**: 30 instructions, 16 distinct mnemonics, **97 bytes**
each; all three diffs are empty. The hot loop is `mov (%rsi,%rcx,1),%rcx` +
`mov %rcx,(%rax,%rdi,1)` — MOV only, **zero `call` instructions**. As predicted:
with no vectorisation there is nothing for `-march` to choose between, so the
tuning target is irrelevant to this rung. Files:
`results/codegen_diff_word_znver2_vs_znver3.diff`, `..._znver2_vs_v3.diff`,
`..._znver3_vs_v3.diff`.

### Rule 2 outcome (re-evaluated under ratified amendment 2)

Under the original 0.5%-cumulative criterion 38 of 39 keys were stable and only
znver2 256 KB was UNSTABLE. Under the **ratified >10% per-window-spread** criterion
the same measurements flag `word` as UNSTABLE at 32 B, 64 B and 4 MB on znver2,
at 32/128/256/512/1024 B and 4 MB on znver3, and at 16/64/256/512/1024 B and 4 MB
on v3 (per-key spreads are in the CSVs, column `window_spread_pct`). The
criterion changed, not the data — the per-window floors of these kernels genuinely
move by 10–19% even when their cumulative `min_of_mins` is flat to 0.0000%. Notably the word rung's floors arrive much earlier
than rung 1's (usually w1–w2 vs w1–w7): a 4x longer per-call copy averages the
interference better inside each rep.

## Kernel 1 re-evaluated under the approved min_of_mins protocol (supersedes the median numbers below)

9 windows x 5 reps, pinned `taskset -c 2`, 13 sizes, 0 migrated reps. Full report:
`results/phase2_naive_analysis.txt` (median-based predecessor preserved as
`results/phase2_naive_analysis.median_based.txt`). Per-target CSVs:
`results/phase2_naive_{znver2,znver3,v3}.csv`.

**Kernel 1 vs kernel 1.** `naive` and `ref_scalar` hold the same byte loop with
byte-identical instruction streams (`results/codegen_diff_naive_*.diff`), so their
difference is pure measurement artefact. Measured in the same windows, that
artefact is **not** uniform:

| size | znver2 | znver3 | v3 |
|--:|--:|--:|--:|
| 16 B | 12.52% | 0.01% | 4.40% |
| 32 B | 1.95% | 4.96% | 2.35% |
| 64 B | 10.90% | 0.86% | 1.17% |
| 128 B | 0.00% | 1.97% | 0.00% |
| 256 B | 0.00% | 2.86% | 0.00% |
| 512 B | 0.05% | **25.33%** | 0.00% |
| 1 KB | 0.00% | **21.15%** | 0.00% |
| 4 KB | 0.05% | 0.15% | **25.15%** |
| 16 KB | 0.98% | 0.19% | 0.33% |
| 64 KB | 1.63% | 2.86% | 2.26% |
| 256 KB | 0.06% | 4.56% | 1.23% |
| 1 MB | 0.00% | 0.16% | 0.10% |
| 4 MB | 4.76% | 4.00% | 1.75% |

**Finding — the mandated 1.5% floor is not safe at every size.** Where the band
is <=0.06% (znver2 at 128 B-1 MB) a 1.5% claim is comfortable. Where the band is
21-25% (znver3 at 512 B-1 KB, v3 at 4 KB) a 1.5% "win" would be pure artefact.
The cause is visible in the design: the identical kernels are measured at
different positions inside each rep, and at these sizes one rep is only
nanoseconds, so a burst that lands between them is charged to whichever ran
during it. `scripts/mo_analyze.sh` therefore prints **both** verdicts - the
mandated rule-3 verdict at 1.5% and a band-aware verdict at
`max(1.5%, null band)` - and never hides the difference. **Recommendation: adopt
the band-aware verdict as the claim rule, per (target, size).**

**Rule 2 outcome, kernel 1 (znver2).** 12 of 13 sizes stabilised (last-two-window
change +0.0000%); the floors were reached at windows 1-7. One size did **not**:
4 KB sat flat for six windows and then found a 20.05% deeper floor at window 9
(1617.4 -> 1293.1 ns). That size is marked `UNSTABLE` and is unusable for a
claim under rule 2 - and it is the same 4 KB key whose bimodality cost us the
pinning experiment. Consequence: the 0.5% early-stop can terminate while a
deep floor is still undiscovered, so cross-target campaigns run to the 9-window
cap (`RUN_TO_CAP=1`) and publish per-key stability instead of assuming it.

**Kernel 1 vs glibc (min_of_mins, znver2).** glibc wins every size; the ratio
`ours:glibc` runs 0.38x (16 B) to 0.03x (1 KB-4 KB), i.e. glibc is up to ~33x
faster than the byte loop at L1-resident sizes. glibc is ifunc-dispatched AVX2,
so this is the expected floor-to-ceiling gap, and it is the number the ladder
exists to close.

## Ladder progress

| kernel | commit | verified | CSV |
|---|---|---|---|
| (reference) `ref_scalar` | `80fbc4e` | vs glibc, n=0..4096 | `phase2_naive_*.csv` |
| 1 `naive` | `2de3160` | **PASS** all 3 targets | `results/phase2_naive_{znver2,znver3,v3}.csv` |
| 2 `word` | — | not started | — |

Kernel 1 note: the harness/report infrastructure and the kernel landed in the
same commit (`2de3160`) because `git add -A` staged both. History was **not**
rewritten to split it (hard constraint). From kernel 2 on, staging is per-path,
so each kernel gets its own commit before it is benchmarked.

## Summary table

`vs_glibc` and `vs_prev` are median GB/s ratios; `best_size` is where that
kernel/target reaches its highest median GB/s. `cycles_per_byte` is PMU cycles.

| kernel | target | best_size | gbps @ best | cycles/byte @ best | gbps @ 4KB | c/B @ 4KB | vs_glibc (median, by class) | vs_prev (median, by class) | notes |
|---|---|---|---|---|---|---|---|---|---|
| naive | znver2 | 32 B | 2.49 | 1.386 | 2.18 | 1.459 | 0.044x..0.39x (**loses everywhere**) | 0.94x..1.61x (≈1.0, noise) | byte loop, 5 instr/byte |
| naive | znver3 | 64 B | 2.34 | 1.470 | 1.93 | 1.470 | 0.041x..0.23x (**loses everywhere**) | 0.88x..1.02x (≈1.0, noise) | identical codegen to znver2 |
| naive | v3 | 64 B | 1.83 | 1.694 | 2.02 | 1.520 | 0.039x..0.26x (**loses everywhere**) | 0.92x..1.07x (≈1.0, noise) | identical codegen to znver2 |
| glibc | znver2 | 4 KB | **69.57** | **0.0507** | 69.57 | 0.0507 | 1.000x | — | baseline; ifunc-resolved AVX path |

Naive vs glibc by size class (median GB/s, znver2): tiny 2.36 vs 6.05 (0.39x),
small 1.87 vs 42.59 (0.044x), mid 1.78 vs 44.34 (0.040x), big 1.62 vs 36.88
(0.044x), huge 1.75 vs 8.65 (0.20x). The measured gap ranges **2.6x (16 B) to
32x (4 KB)** on `min_ns`, which is the least noisy statistic.

## Incremental gain, kernel 1 vs previous

`naive` vs `ref_scalar` (the previous rung) is a deliberate control: the two
kernels are *the same algorithm in two different translation units*, and
`objdump` confirms identical instruction streams. Measured median ratios:
znver2 0.94–1.61x, znver3 0.88–1.02x, v3 0.92–1.07x — i.e. **1.0x within
noise**, which is exactly what a working harness must show. Two consequences:

* the delta measurement is not producing fake progress signals out of thin air;
* the per-class spread of a *null* comparison (0.88x–1.61x) bounds how large a
  claimed win has to be before it means anything at tiny sizes.

Honest verdict for kernel 1: **no optimization, no gain, and it must be beaten.**
Its value is that it establishes the floor and validates the pipeline.

## Codegen across the three targets (mandatory per kernel)

`scripts/codegen_diff.sh memcpy_naive src/memcpy_naive.c`

| target | instructions | distinct mnemonics | function bytes |
|---|---|---|---|
| znver2 | 11 | 10 | 33 |
| znver3 | 11 | 10 | 33 |
| v3 | 11 | 10 | 33 |

All three pairwise diffs are **empty** (identical instruction streams):
`results/codegen_diff_naive_znver2_vs_znver3.diff`,
`..._znver2_vs_v3.diff`, `..._znver3_vs_v3.diff`.
Instruction stream: `movzbl (%rsi,%rcx,1),%edi` + `mov %dil,(%rax,%rcx,1)` +
`inc`/`cmp`/`jne` per byte, with an early `test/je` for n == 0.

Hypothesis answered for rung 1: **the tuning target is irrelevant to a scalar
byte loop.** `-march=znver3` neither helps nor hurts; there is nothing for it to
vectorise. The three targets diverge only when the source contains a loop worth
vectorising (Phase 1 negative control: 6 vs 14 vs 10 vector instructions for the
same float loop). The target question will be answered for real from kernel 3
(SSE2) onward, where the source has intrinsics the compiler cannot “improve”
away.

## Measurement floor on this host (Phase 2 noise investigation)

**Status: kernel 2 is blocked pending your decision.** The 12/12 variance-flagged
result triggered a four-step noise investigation. Raw numbers, instrumented by
`--dump-reps`, `--noise` and `scripts/noise_floor.sh`.

### Step 1 — per-rep values, two impls interleaved in ONE process (64 B, reps=5)

| rep | ref_scalar `med_ns` | ref_scalar `min_ns` | naive `med_ns` | naive `min_ns` |
|--:|--:|--:|--:|--:|
| 0 | 32.92 | 29.72 | 43.88 | 33.54 |
| 1 | 33.10 | 25.73 | 34.23 | 25.72 |
| 2 | 31.34 | 25.72 | 33.75 | 30.38 |
| 3 | 33.39 | 25.72 | 35.06 | 25.73 |
| 4 | 41.22 | 29.60 | 39.47 | 28.13 |

Medians differ 6% between two kernels that are *byte-identical*; the `min_ns`
values hit the same 25.72 ns floor in both. So the spread is **not**
cross-process noise: it is inside a single process, interleaved.

### Step 2 — noise floor: same impl, same size, 20 runs back-to-back

| size | median-of-medians spread | median-of-mins spread | null-kernel overhead |
|--:|--:|--:|--:|
| 64 B | **37.7%** | 32.1% | 1.71 ns = 4.8% of impl |
| 4 KB | **45.9%** | 20.2% | 1.25 ns = 0.06% |
| 64 KB | **30.4%** | 19.7% | below 40 ns timer floor |
| 1 MB | **25.4%** | 35.5% | below timer floor |

More samples do **not** fix it (64 B medians: 40.7% at `--iters 100`, 96.5% at
1000, 31.1% at 4000) — more samples just draw more of the interference
population. The global minimum over all runs, however, is reproducible to
**0.04%** across independent experiments (25.7217 / 25.7227 / 25.7324 / 25.7227 ns
at 64 B in four different runs).

### Step 3 — host contention

`/proc/stat` `steal` field is **0 in every sample** (cpu2 and aggregate) and PSI
shows `some avg10` 1.0–2.0%, `full` 0.00; loadavg 1.14–2.07 on 4 vCPUs with
`2/293` runnable. The guest's accounting therefore **does not expose** the
interference, and PSI does not explain a 25–45% median spread. Across three
measurements 60 s apart, medians drifted (spread 9.7% / 24.4% / 42.0%) while
global mins moved only 26.5/27.3/26.5 µs — i.e. **slow temporal drift, not
random jitter**.

### Step 4 — harness overhead, isolated with a null kernel

`--noise` measures an empty kernel through the identical call path, batch loop
and timer:

* call + loop + address setup = **1.71 ns per call** at 64 B (4.8% of the
  measured 35.8 ns) — not the source of the spread;
* timer overhead = 40 ns per sample, amortised to 0.039 ns/call at batch=1024;
* the null kernel's own medians still vary **20.1%** run-to-run while touching
  no memory at all ⇒ the jitter is imposed on the vCPU from outside, not
  produced by the memory system or the harness.

Additionally the **core clock itself drifts**: `--calib` measured 2.7468, 2.8837,
3.03, 3.11 and 3.14 GHz in different runs (PMU cycles ÷ wall seconds, so this is
not an IPC artefact). That is a 14% range and it is uncontrollable (no cpufreq
sysfs). Consequence: **ns/GB/s cannot be compared across time; the PMU
cycles/byte counter can.**

### Null-comparison band (the bar a real claim must clear)

Two provably identical kernels (`naive` vs `ref_scalar`, identical objdump
streams), interleaved, reps=9 — any "speedup" below these numbers is an artefact:

| size | global-min ratio | best-median ratio | best cycles/byte ratio |
|--:|--:|--:|--:|
| 16 B | +41.6% | +61.7% | +62.4% |
| 64 B | +0.2% | +26.4% | +11.4% |
| 128 B | +18.3% | +6.3% | +7.5% |
| 256 B | +31.8% | −1.7% | +4.1% |
| 512 B | **−4.4%** | −9.7% | −7.2% |
| 1 KB | **−3.8%** | −9.3% | −7.9% |
| 4 KB | **−2.9%** | −8.5% | −11.0% |

Two things fall out of this table. Below 512 B the band is 4–62% depending on
statistic, so **no kernel comparison at those sizes is resolvable**. From 512 B
up, the **global minimum is the tightest estimator (2.9–4.4%)** and, notably,
the sign is consistent within each size across all three statistics — an
alignment effect, not noise: identical source can differ ~10% purely from where
its loop lands.

### Live re-measurement (post-fix, same session, `bench_znver2`)

Re-run after the three harness fixes below, so every number here comes from the
current binary. Where it disagrees with the tables above, this supersedes them.

**Step 1 — prescribed command `--quick --impl naive --impl ref_scalar --reps 5`,
n=64, both impls in one process, interleaved.**

| rep | naive `med_ns` | naive `min_ns` | ref_scalar `med_ns` | ref_scalar `min_ns` |
|--:|--:|--:|--:|--:|
| 0 | 30.19 | 24.41 | 40.67 | 27.87 |
| 1 | 42.05 | 32.20 | 33.63 | 29.37 |
| 2 | 40.13 | 30.58 | 31.42 | 22.23 |
| 3 | 30.03 | 25.71 | 36.22 | 29.73 |
| 4 | 33.37 | 25.72 | 35.62 | 29.02 |

Medians 30.0–42.1 ns inside naive alone (40% within one process); the two
identical kernels sit 6.3% apart on median-of-medians and 9.8% apart on global
min. So the spread is **inside** a process: host contention or core state, not
cross-process drift.

**Step 2 — noise floor, 20 back-to-back runs, same code path.**

| size | median-of-medians spread | median-of-mins spread | null-kernel overhead |
|--:|--:|--:|--:|
| 64 B | 41.2% | 40.1% | 1.86 ns = 4.89% of impl |
| 4 KB | 36.3% | 24.1% | 1.25 ns = 0.054% |
| 64 KB | 40.4% | 11.4% | **0.000 ns** (below 40 ns timer floor) |
| 1 MB | 35.7% | 15.9% | **0.000 ns** (below timer floor) |

**Step 3 — contention, three runs 60 s apart (4 KB, 20 runs each).**

| run | wall clock | loadavg | `steal` cpu2 / agg | core_ghz | med-of-medians | spread | min-of-mins |
|--:|--:|--:|--:|--:|--:|--:|--:|
| 1 | 20:45:28 | 1.70 | 0 / 0 | 2.847 | 2235.5 ns | 43.4% | 1291.8 ns |
| 2 | 20:46:31 | 1.49 | 0 / 0 | 2.730 | 1986.9 ns | 39.0% | 1291.8 ns |
| 3 | 20:47:34 | 1.52 | 0 / 0 | 3.063 | 2503.5 ns | **3.7%** | 1745.8 ns |

`steal` is exactly 0 in every sample on both cpu2 and aggregate, and PSI `some`
stays 1–2% with `full` 0.00, yet loadavg sits at 1.5–1.7 on 4 vCPUs with
`1/291` runnable while nothing of ours is running. The guest's accounting simply
does not expose the interference. Run 3 is the diagnostic find: **a window that
is internally steady (3.7%) is 26% slower than a window that is internally
chaotic (43%)** — so "quiet" and "fast" are not the same window, and neither
medians nor mins are stable across windows.

**Step 4 — harness overhead.** Call + batch loop + address setup = **1.86 ns** per
call at 64 B (4.89% of the measurement, a constant offset, not noise); 1.25 ns at
4 KB (0.054%); unresolvable at ≥64 KB. The timer costs 40 ns per sample,
amortised to 0.039 ns/call at batch=1024 and 40 ns/call at batch=1 (0.15% at
64 KB, 0.008% at 1 MB). So harness overhead is **not** the source of the spread:
the null kernel, which touches no memory, still varies 20% run-to-run.

**Step 3b — `/proc/loadavg` and `/proc/stat` before *and after* each run, with the
delta consumed *during* the window** (the explicitly requested before/after
form; rep count 20, 4 KB, unpinned):

| run | clock | window | loadavg before→after | other CPU ticks in-window agg / cpu2 (100/s) | steal | med-of-medians | spread | min-of-mins |
|--:|--:|--:|--:|--:|--:|--:|--:|--:|
| 1 | 20:55:55 | 2.92 s | 1.75→1.69 | **480 / 78** | 0 | 2571.75 ns | 20.0% | 1293.69 ns |
| 2 | 20:56:58 | 1.95 s | 1.74→1.74 | **318 / 31** | 0 | 2413.31 ns | 35.0% | 1292.44 ns |
| 3 | 20:58:00 | 2.98 s | 1.39→1.52 | **485 / 156** | 0 | 2157.19 ns | 33.6% | 1791.50 ns |

480 and 485 ticks in a ~3 s window is 4.8 s of CPU time consumed by *other*
processes on a 4-vCPU guest — about 1.6 vCPUs busy for the whole measurement while
our own loadavg contribution is ~0. So the loadavg of 1.5–1.7 is not a stale
average: foreign work is genuinely running throughout. `steal` stays **0**, which
is why the guest accounting cannot see it.

**Pinning test (performance-jō rule 2).** The harness reports
`affinity covers 4 cpu(s), running on cpu N -> use taskset -c N`, so steps 1–4
above ran *unpinned*; this closes that gap. Same command, 4 KB, 20 runs/window,
three windows back-to-back:

| config | median spread per window | min spread per window | min-of-mins |
|--|--|--|--|
| `taskset -c 2` (pinned) | 31.9% / 18.2% / 31.3% | 35.0% / 35.3% / 37.4% | 1292.44 / 1292.44 / 1291.81 ns |
| unpinned | 35.7% / 31.3% / 20.9% | 28.4% / 18.5% / 41.7% | 1293.06 / 1620.56 / 1292.44 ns |

**Pinning does not reduce the noise** — the two distributions are the same.
Migration is therefore ruled out as the cause, and the interference is imposed
on the vCPU from the host regardless of which vCPU we occupy. The CSV confirms
the rule-2 requirement is met where it can be: `taskset -c 2 ./bench_bench_znver2
--one ...` emits `cpu=2, migrated=0`, i.e. `sched_getcpu()` is read inside the
measured region.

**What *is* reproducible: the floor.** `min_of_mins_ns` landed at 1291.81–1293.06
ns in **6 of the 9 windows** above and in 3 more windows of step 3 (0.1% apart)
— while medians over the same windows moved 20–43%. The true minimum is a
stable, reproducible property of this box; the medians are contaminated.

*Caveat on `--one`/`--pmu`:* they emit a single non-replicated row (`reps=1`,
`rep_spread_pct=0.000`), so they satisfy neither the 3-rep rule nor rule 3. Only
the sweep mode aggregates interleaved reps. Use `--one` for the counter set,
never for a timing claim.

**Null-comparison band, current binary, reps=9.** Ratios of two provably
identical kernels; anything smaller than these is an artefact.

| size | global-min ratio | best-median ratio | best cycles/byte ratio |
|--:|--:|--:|--:|
| 16 B | +19.1% | +19.0% | +20.6% |
| 32 B | +5.0% | −4.4% | −0.0% |
| 64 B | +8.5% | −7.8% | −5.0% |
| 128 B | +18.4% | −16.7% | −6.3% |
| 256 B | +22.0% | +24.0% | +12.6% |
| 512 B | +0.6% | +34.4% | +12.1% |
| 1 KB | +0.3% | +28.6% | +20.9% |
| 4 KB | −20.1% | +0.2% | +0.5% |
| 16 KB | −0.9% | −0.1% | −1.4% |
| 64 KB | +1.1% | +0.1% | +3.4% |
| 256 KB | −0.0% | +7.5% | +2.4% |
| 1 MB | +0.1% | +3.2% | +1.9% |
| 4 MB | −0.4% | +7.1% | +0.3% |

Note 4 KB: the global-min ratio reads −20.1% while median and cycles/byte read
+0.2%/+0.5% — a single run's global min can itself be fooled, so a min only
counts once it has been reproduced across repeated quiet windows.

### Recommendation

1. **Primary statistic: `cycles/byte` (PMU cycles, not TSC)** — it is the only
   metric that survives the 2.73–3.06 GHz core-clock drift measured here, and it
   is the tightest of the three at large sizes.
2. **≥16 KB: claims allowed above a 3.5% floor**, taken from the best of ≥2
   repeated quiet windows (a window is quiet when its own median spread <5%).
3. **4 KB–16 KB: floor ≈ 5%, treat 4 KB as 20%** (one statistic blew up 20% there).
4. **<4 KB: no cross-kernel claims below 2x** — the band is 5–34%.
5. Medians and p99 are informational only; the harness keeps flagging >5% spreads,
   and we now also flag a run whose spread is <5% as a *usable* window.
6. All comparisons stay interleaved/paired inside one process (implemented).

### Harness defects found and fixed during this investigation

1. `bench/bench.c` interleaved-rep indexing (`rr[r*ncases+c]` vs `rr[c*reps+r]`)
   — cases read each other's samples (commit `a6f32ea`).
2. Repeating `--impl` silently kept only the last value, so the prescribed step-1
   command measured one kernel and looked like two. Repeats now append; comma
   lists still work (commit `a6f32ea`).
3. `--noise` and `--one` with a missing positional argument dereferenced NULL and
   segfaulted (rc=139) instead of printing usage. Now rc=2 + usage (commit `a6f32ea`).
4. `scripts/run_perf.sh` printed the shell's affinity mask as if it were the
   measurement's (Phase 1).

* `p99_ns` is recorded for information only and is used to claim nothing.

## Artifacts

* `results/phase2_naive_{znver2,znver3,v3}.csv` — 80 rows each (16 sizes x
  {ref_scalar, glibc, naive} x {memcpy, memset where applicable}), 21 columns.
* `results/phase2_naive_{znver2,znver3,v3}.log` — harness stderr: clock snapshot,
  variance flags, migration count.
* `results/phase2_naive_analysis.txt` — `scripts/analyze_phase2.sh` output.
* `results/codegen_diff_naive_*.diff` — pairwise codegen diffs.
* `results/phase2_perf_pass.log` — PMU validation pass (`make perf`).

# Kernel 4 — `memcpy_avx2` (32 B blocks, 4× unroll, prefetch-distance study)

## Instrument: the window, and why this campaign was run three times

Kernel 4 was measured three times, and only the third run is used. The reason is
H5 in `results/h4_diagnostics.md`: per-window spread is dominated by how long the
timed window is, and the driver's default (`ITERS=100`) produced ~0.1 ms windows.

| attempt | settings | window | outcome |
|:--|:--|:--|:--|
| 1 | `ITERS=100` (driver default) | ~0.1 ms | 40–50% spreads on most sizes: measuring interference sampling, not the kernel. **Superseded**, kept on disk as `results/phase2_avx2_*_shortwin_*` |
| 2 | `ITERS=4000 BUDGET=5 s` | 9 ms–3 s, cap-decided | correct instrument but ~8× slower than needed and the timer never fires (4000 samples finish well inside 5 s), so mid sizes ran multi-second windows. **Superseded**, kept as `results/phase2_avx2_budget5s_*`; H5's text corrected in `f8354f9` |
| 3 | `ITERS=4000 BUDGET=0.05 s` | `min(4000 samples, 50 ms)`, ≥ 9 ms everywhere | **used for every number below** |

The campaign log header now records `iters/budget/warmup/run_to_cap`, so this
cannot recur silently (`6a61b1c`). Because the corrected window removes the
artefact, most sizes are now **Tier A with 0.0–8% spread** (they were 40–50% in
attempt 1 and 11–48% in K1–K3), which is H5 confirmed at campaign scale: the
Tier B labels of K1–K3 were an instrument artefact, not a kernel property.

## 1. Kernel

`memcpy_avx2`: 32 B blocks via `_mm256_loadu_si256`/`_mm256_storeu_si256`
unrolled 4× (128 B per iteration), a 32 B loop for the remainder, an
overlapping-pair tail (a 16 B pair for a 16–31 B remainder, scalar 8/4/2/1 below
that), and a `< 64 B` path that never enters the AVX2 loop — but which, in the
compiled object, is 32 B wide at 32–63 B: see §12.6. Three
variants share one body (`src/memcpy_avx2_impl.h`) and differ only in prefetch
distance: `avx2` (+1 line), `avx2_pf2` (+2), `avx2_pf4` (+4). `avx2` is the rung;
pf2/pf4 exist to answer the distance question. Committed **before** any timing as
`6c1c589` (`phase2: memcpy_avx2`).

## 2. Verify — PASS ×3, and it caught a real bug first

The first version of the kernel failed verify at `n=33`: the 16 B overlapping
pair in the small path covers `[0,16)` and `[n-16,n)`, which is complete only for
`n ≤ 32`, so sizes 33–63 left byte 16 unwritten (`exp=7c got=ee` on all three
targets). Fixed to two 16 B pairs (`[0,32)` and `[n-32,n)`), then re-verified:
**PASS on all three targets for all eight registry entries**, and the campaign's
own logs repeat it at every window start.

## 3. Codegen diff — `results/codegen_diff_avx2_*.diff`

121 instructions, 18 mnemonics, 460 bytes, identical counts on all three targets;
`znver2 == znver3` exactly; `v3` differs by moved lines only, with the mnemonic
multiset identical (the multiset verdict inside the file, per `714e7c5`). Hot
loop: `vmovups`/`vmovdqu` 32 B ops, and the three variants differ in the
prefetch: effective addresses +64 B, +128 B, +256 B — i.e. the distance really is
the only difference, as the diff shows and the numbers can therefore be compared.

## 4. Campaign

9 windows × 5 reps = 45 reps per (impl,size), `RUN_TO_CAP=1`, `taskset -c 2`,
`cpu=2` on **5850/5850** rep lines per target and `migrated=0` on all three.
Logs: `results/phase2_avx2_campaign_{znver2,znver3,v3}.windows.log`.

## 5. Incremental gain vs kernel 3 (`sse2`), min_of_mins, same windows

| size | znver2 | znver3 | v3 | tier verdict |
|--:|--:|--:|--:|:--|
| 16 B | **+69.79%** | **+69.44%** | **+69.66%** | WIN ×3 — small-path, not width (see §11) |
| 32 B | **+20.37%** | **+20.09%** | **+20.37%** | WIN ×3 (Tier A) |
| 64 B | −54.72% | −90.27% | −54.72% | **LOSS ×3** |
| 128 B | −46.41% | −53.69% | −45.92% | **LOSS ×3** |
| 256 B | −20.57% | −31.15% | −20.57% | **LOSS ×3** |
| 512 B | +6.86% | +3.81% | +6.86% | WIN(1) / measured ×2 |
| 1024 B | +26.39% | +24.85% | +26.39% | WIN ×2 (znver3 misses by 0.15 pt) |
| 4096 B | +35.18% | +35.18% | +35.18% | WIN ×3 |
| 16 KB | +29.59% | +29.66% | +29.64% | WIN ×3 |
| 64 KB | +13.68% | +14.35% | +14.42% | WIN ×3 (Tier A) |
| 256 KB | +17.27% | +18.25% | +17.45% | WIN ×3 |
| 1 MiB | +14.20% | +13.13% | +17.22% | WIN ×2 / measured |
| 4 MiB | +14.39% | +12.56% | +18.49% | measured ×3 (Tier B, 25% floor) |

## 6. vs glibc

| size | znver2 | znver3 | v3 |
|--:|--:|--:|--:|
| 16 B | **+12.92%** | +0.39% | **+12.53%** |
| 64 B–16 KB | −113% … −13% | −138% … −12% | −113% … −14% |
| 64 KB | −0.80% | −0.08% | +0.00% |
| 256 KB | +1.13% | +0.76% | +1.13% |
| 1 MiB | **+4.82%** | +5.66% | **+8.47%** |
| 4 MiB | +7.63% | +7.86% | +9.48% |

The ladder claims wins over glibc at 16 B and at 1 MiB; 64 KB–4 MiB is
"measured" (all Tier B); 4 KB–16 KB is a claimed **LOSS**. glibc remains the
fastest implementation in the box for 64 B–16 KB (see §9).

## 7. Tier status (ratified §3c model)

Tier A (spread ≤ 10%) at 9–13 of 13 sizes on each target with floors of
max(1.5%, band) — as tight as 1.50% — and Tier B (flat 25%) only at 512 B,
1024 B, 16 KB, 1 MiB, 4 MiB on some targets. Every claim above is against its own
row's floor; the analyzer prints both the rule3/band verdicts and the tier
verdict, and nothing below the applicable floor is claimed.

## 8. Prefetch distance — answer to the standing question

Best of the three per size, same windows (full table in
`results/phase2_avx2_analysis.txt`):

* **pf2 wins the 64 B–1 KB band on every target**, by +5.28…+13.09%; on znver2
  and v3 those margins clear Tier A floors (1.5%) and are **claims**, on znver3
  256 B/1024 B are Tier B and stay *measured*.
* **pf4 wins 1 MiB on znver2** (+4.48%, Tier A → claim) and 4 MiB on znver2/v3
  (+3.51%/+6.58%, both Tier B → measured); on znver3 pf1 (the rung) is best at
  4 MiB, where prefetching actually hurts pf2 by 6.9%.
* No variant wins consistently at ≥ 256 KB, and prefetch is not monotone in
  distance: +2 lines is the useful setting, +4 is not better except at 1–4 MiB.
* The rung therefore stays `avx2` (pf1): switching it on this evidence would trade
  a claimed 256 KB/4 MiB position for a claimed mid-band gain, and no single
  variant wins both. pf2 is the recommendation for a Kernel 5 variant.

## 9. Crossovers and who is fastest per size

* **word → sse2: 32 B.** Re-measured under the corrected window (`sse2` is a
  prev of this campaign), sse2 vs word is +9.10% at 32 B (WIN), +35…+71% for
  64 B…4 MiB (WIN, mostly Tier A), and 0.00%/−8.34% at 16 B — i.e. below 32 B the
  scalar word rung is at least as good. This is the H5 re-measurement of Kernel 3,
  appended to `results/phase2_sse2_analysis.txt`; the frozen short-window K3 record
  is untouched.
* **sse2 → avx2: 512 B–1 KB.** avx2 claimably beats sse2 from 512 B (v3 Tier A
  +6.86%; 1 KB +26.4% on znver2/v3) and at every size ≥ 4 KB, but **loses at
  64–256 B** (§11) and wins at 16/32 B through its small path. The monotone
  ladder does not exist: the fastest rung is size-dependent.
* **Fastest entry per size in this campaign:** `avx2`/`avx2_pf*` at 16–32 B,
  **glibc at 64 B–16 KB by 10–37%**, then `avx2_pf4` at 64 KB (tied with glibc),
  `avx2` at 256 KB, `avx2_pf4` at 1–4 MiB.

## 10. H1–H4 (+H5) diagnostics

Answered before any Kernel 4 timing and recorded in `results/h4_diagnostics.md`:
H1 (alignment branches) **refuted** — the kernel has no alignment branch and the
harness measures 64 B-aligned buffers at offset 0; H2 (fast-path crossover)
**not supported** — the size sitting exactly on the switch is the most stable of
eleven; H3 (L1 contention) **not supported as stated** — spread tracks window
length, not cache tier; H4 (codegen after the AVX→SSE2 fix) **verified** — 7 VEX
instructions / 4 ymm references before, 0/0 after; H5 (discovered while answering
H1–H3) **confirmed** — 43.69% → 0.75% at 8 KB by lengthening the window, which is
what attempt 3 of this campaign implements.

## 11. Surprises

1. **The wider rung is slower at 64–256 B**, by up to −90% (znver3, 64 B), a
   claimed LOSS on all three targets. Mechanism: the 4×32 B unrolled loop needs
   n ≥ 128 B, so 64–127 B falls through to the 32 B remainder loop, while the
   SSE2 rung's 64 B block completes in exactly one unrolled iteration. The AVX2
   rung is not "SSE2 but wider" in that band; it is structurally worse.
2. **The 16 B gain of +69% is not a width effect.** It comes from the small-path
   change (rung 3 copies `< 32 B` byte-wise, rung 4 uses 16 B pairs). It is real
   and claimable, but it must not be attributed to 32 B blocks. The same cannot be
   said for the 32 B row: in the compiled object the 32–63 B branch is **32 B
   wide** (§12.6), so that +20% does involve the wider access.
3. **glibc is 10–37% faster than every rung at 64 B–16 KB.** The ladder's lead
   starts at 64 KB.
4. **Identical code, ~9% apart at 4 MiB**: znver2 and znver3 have byte-identical
   AVX2 instruction streams (codegen diff) yet 108063 ns vs 117452 ns. Same
   machine, same clock: this is host state (DRAM placement / thermal), and it is
   the reason 4 MiB claims stay Tier B.
5. **Prefetch distance is not monotone and not portable**: +2 lines helps up to
   13% in the mid band, +4 helps at 1–4 MiB on two targets and hurts 6.9% at
   4 MiB on the third.

## 12. Defects found and fixed this cycle

1. Campaign attempt 1 ran with the window H5 had already shown to be an artefact
   generator (`ITERS=100`); caught before any number was used, superseded, kept as
   `*_shortwin_*`.
2. Attempt 2 used `BUDGET=5 s` from my own H5 text, where the 4096-sample cap
   proves the timer can never fire (~8× slower for no information); superseded,
   kept as `*_budget5s_*`, and H5's text corrected (`f8354f9`).
3. The driver's log header did not record iters/budget/warmup — the exact reason
   defects 1–2 were possible; fixed with the settings recorded (`6a61b1c`).
4. `make snapshot`'s behaviour exposed that **all nine frozen K1–K3 CSVs had
   silently diverged from their live sources**: the two-tier regeneration had
   rewritten the live CSVs (gaining the tier columns) without re-freezing, so the
   record and the working copies disagreed. Resolved by treating the frozen file
   as authoritative: the live paths were restored from `results/committed/` and
   are byte-identical again. The tier-annotated Kernel 3 view lives in
   `results/phase2_sse2_analysis.txt` (text, committed), not in a rewritten CSV.
   This is the guard doing its job: it refused the divergence instead of hiding it.
5. Aggregate references to preparatory work outside the measured campaign
   (`f8cce6a`'s message) remain disclosed rather than rewritten; history is never
   rewritten in this project.
6. **The AVX2 source comment claimed a width the compiler did not produce.** Found
   in the final completion check, by disassembling the built object — which no
   earlier check did. At 32–63 B the four 16 B accesses of the small path are
   merged by clang's SLP vectorizer into 32 B `ymm` accesses: 14 ymm references in
   `build/<target>/src/memcpy_avx2.o`, four of them in that branch (16–31 B stays
   genuinely 16 B). This is the same merge that made the "SSE2" rung compile to
   AVX at `43f01dc`. Correctness is unaffected — each merged access covers
   `[0,32)` or `[n-32,n)`, both inside `[0,n)` for n ≥ 32, so the 64 B canaries in
   `bench.c` cannot be touched and verify passes at every n — and **no measurement
   changes**: the comment-only fix leaves all 9 kernel objects byte-identical
   (`cmp`, 9/9), so this campaign's frozen numbers still describe this binary. The
   false statement was retired from the source header; forcing 128-bit there is a
   Kernel 5 candidate, because it would invalidate this campaign's snapshots.

## 13. Artifacts and limitations

* `results/phase2_avx2_{znver2,znver3,v3}.csv` (rung), `..._pf2_*`, `..._pf4_*`,
  each snapshotted into `results/committed/` (18 frozen files total, 0 refused).
* `results/phase2_avx2_analysis.txt` — full analyzer output for the rung on all
  three targets, the prefetch comparison, the crossovers and the per-size fastest
  entry, generated by `scripts/phase2_k4_report.sh`, which is in the repo and
  idempotent: re-running it rewrites this file from the raw logs and does not
  duplicate the Kernel 3 appendix. Every number in it is printed by awk from a
  log or a CSV, none is typed in.
* `results/phase2_sse2_longwin_{znver2,znver3,v3}.csv` — the long-window Kernel 3
  re-measurement, copied into `results/committed/` by hand (the snapshot target's
  `SNAPSHOT_IMPLS` list covers the ladder's rungs, not this extra view).
* `results/phase2_sse2_analysis.txt` — now carries both the flat-25% Tier B
  reclassification and the long-window re-measurement of Kernel 3.
* Superseded evidence stays on disk and is gitignored by the project's policy
  (`results/*.csv`, `results/*.log`): `*_shortwin_*`, `*_budget5s_*`,
  `*_longwin_*`, and the diag logs. Nothing was deleted to make a result cleaner.
* Limitations: 4 MiB claims remain Tier B because of host-state variance (§11.4);
  p99 is recorded but used for nothing; no rung-selection code was written, so the
  crossovers in §9 are findings, not an implementation.


# Kernel 5 — `memcpy_avx2_nt` (32 B non-temporal stores above an experimental gate)

Rung: Kernel 4's AVX2 body, with 32 B stores using `_mm256_stream_si256` above a
threshold, one `_mm_sfence()` before return, a cached lead of at most 31 B when
the destination is not 32 B aligned, source prefetch inherited from Kernel 4's
winning distance (+2 cache lines, §8 above), and rung 4's cached loop below the
threshold. Committed as `94d8d20` before any timing row existed; the threshold
probe is `0bc6b03`.

## 1. The mandatory gate (audit-width) — K5 item 1

`make audit` rc=0 (no vector instruction in the scalar TUs) and `make audit-width`
rc=0 at generation time. audit-width compares every rung's widest memory access
with its declared ladder width, per target: all 8 kernel TUs × 3 targets report
`declared W  compiled W  OK`, including `memcpy_avx2_nt.c  declared 256
compiled 256  OK` on all three targets (NT stores are 32 B accesses, compiled to
`vmovntps`). Each campaign log carries the gate's own verdict before window 1:
`# gate: make audit PASSED and make audit-width PASSED (rc=0) before window 1`,
and `scripts/mo_measure.sh` refuses to write a timing row without it — so the
campaign logs cannot exist unless the gate passed first.

## 2. Verify first, then codegen

* `verify: avx2_nt OK` ×9 windows ×3 targets (the harness re-verifies before
  every window: 81 verify lines per log) — glibc reference, n=0..4096 random
  offsets, 64 B canaries both sides, plus the size list (covers the NT path, the
  unaligned lead and the cached fallback).
* `results/codegen_diff_avx2_nt_*.diff`, each carrying an explicit verdict: all
  three pairs are genuinely DIFFERENT instruction sets — znver2↔znver3 186
  changed lines, znver2↔v3 148, znver3↔v3 228, mnemonic multiset DIFFERENT in all
  three (106/78/108 mnemonic lines). Unlike Kernel 4 (where znver2 and znver3
  were byte-identical), this rung compiles per target, so cross-target rows are
  not the same binary and are reported per target only.
* NT census (verify cannot see this): `vmovntps=5  sfence=2` in every target's
  `memcpy_avx2_nt.o`.

## 3. Campaign instrument

5 reps × 9 windows, `taskset -c 2`, 6435 rep lines per target, **0 migrated**,
all `cpu=2`; `iters=4000`, `budget=0.05`, `warmup=2`, `RUN_TO_CAP=1` (recorded in
each log's header, which is also the gate line). Disclosure: znver2 and znver3 ran
2026-10-12 19:15–19:23; v3's first attempt was cut off by a host session
suspension mid-window-8 (no end line, process gone) and v3 was re-run whole the
next day — recorded in the progress log, and every comparison below is within one
log's own windows. Driver: `scripts/phase2_k5_campaign.sh` (in the repo).

## 4. Standing report — min_of_mins per size, tiers and verdicts (items 2 and 4)

Below the 256 KB gate the rung runs the same source loop as rung 4, so its
sub-256 KB deltas (up to ~10%, several clearing the Tier A floor as WIN) are
compilation-layout and instrument effects between two builds of the same
algorithm — flagged, not claimed. The rows that compare two algorithms are
256 KB, 1 MiB and 4 MiB, and there the verdict is LOSS on every target.

znver2 (min_of_mins ns; verdicts: vs `avx2` / vs glibc):

| size | avx2 | avx2_nt | glibc | tier | nt vs avx2 | nt vs glibc |
|--:|--:|--:|--:|:--|:--|:--|
| 16 | 2.2 | 2.2 | 2.5 | A | measured | WIN |
| 32 | 2.5 | 2.5 | 2.5 | A | measured | measured |
| 64 | 5.3 | 5.3 | 2.5 | A | measured | LOSS |
| 128 | 5.9 | 5.6 | 3.1 | A | WIN (same loop) | LOSS |
| 256 | 7.1 | 6.8 | 3.7 | A | WIN (same loop) | LOSS |
| 512 | 9.5 | 8.6 | 7.0 | A | WIN (same loop) | LOSS |
| 1024 | 14.9 | 14.5 | 11.1 | A | WIN (same loop) | LOSS |
| 4096 | 51.9 | 51.3 | 41.3 | A | measured | LOSS |
| 16384 | 240.5 | 237.8 | 210.2 | A | measured | LOSS |
| 65536 | 1262 | 1253 | 1262 | A | measured | measured |
| 262144 | 5250 | 12394 | 5270 | A | **LOSS −136.1%** | LOSS |
| 1048576 | 24406 | 50746 | 25508 | A | **LOSS −107.9%** | LOSS |
| 4194304 | 110157 | 208393 | 117813 | A | **LOSS −89.2%** | LOSS |

znver3:

| size | avx2 | avx2_nt | glibc | tier | nt vs avx2 | nt vs glibc |
|--:|--:|--:|--:|:--|:--|:--|
| 16 | 2.5 | 2.2 | 2.5 | A | WIN | WIN |
| 32 | 2.5 | 2.5 | 2.5 | A | measured | measured |
| 64 | 5.9 | 5.6 | 2.5 | A | WIN (same loop) | LOSS |
| 128 | 6.2 | 6.6 | 3.1 | A | LOSS (same loop) | LOSS |
| 256 | 7.7 | 7.7 | 3.7 | A | measured | LOSS |
| 512 | 9.9 | 9.5 | 7.0 | A | WIN (same loop) | LOSS |
| 1024 | 15.2 | 15.6 | 11.1 | A | LOSS (same loop) | LOSS |
| 4096 | 51.9 | 52.6 | 41.3 | A | measured | LOSS |
| 16384 | 237.8 | 232.8 | 208.0 | A | WIN (same loop) | LOSS |
| 65536 | 1262 | 1252 | 1252 | A | measured | measured |
| 262144 | 5240 | 12544 | 5280 | A | **LOSS −139.4%** | LOSS |
| 1048576 | 24446 | 50976 | 25488 | B (spread > 10%) | **LOSS −108.5%** | LOSS |
| 4194304 | 111240 | 208885 | 114186 | A | **LOSS −87.8%** | LOSS |

v3:

| size | avx2 | avx2_nt | glibc | tier | nt vs avx2 | nt vs glibc |
|--:|--:|--:|--:|:--|:--|:--|
| 16 | 2.2 | 2.2 | 2.5 | A | measured | WIN |
| 32 | 2.5 | 2.5 | 2.5 | B | measured | measured |
| 64 | 5.3 | 5.3 | 2.5 | B | measured | LOSS |
| 128 | 5.9 | 5.6 | 3.1 | B | measured | LOSS |
| 256 | 7.1 | 7.7 | 3.7 | B | measured | LOSS |
| 512 | 9.5 | 9.5 | 7.0 | B | measured | LOSS |
| 1024 | 14.9 | 15.0 | 11.1 | B | measured | LOSS |
| 4096 | 51.9 | 51.9 | 41.3 | A | measured | LOSS |
| 16384 | 238.0 | 235.2 | 210.2 | B | measured | measured |
| 65536 | 1262 | 1262 | 1262 | A | measured | measured |
| 262144 | 5270 | 12483 | 5650 | A | **LOSS −136.9%** | LOSS |
| 1048576 | 24015 | 51628 | 26670 | A | **LOSS −115.0%** | LOSS |
| 4194304 | 112211 | 208403 | 113303 | A | **LOSS −85.7%** | LOSS |

The per-key 9-window series (w1–w9, first floor window and last-two change) is in
`results/phase2_avx2_nt_analysis.txt` §3, published by the analyzer, not averaged
in here.

## 5. The NT threshold — method, result, caveat (item 5)

Method (`0bc6b03`): `scripts/nt_threshold_probe.sh` builds a probe binary with
the rung's threshold forced to 64 B — NT on every main-path size — and sweeps
8 KB–4 MiB, 3 passes × 3 reps, same process, pinned to cpu 2.

Result: **no size at which NT wins, on any target.** NT throughput is flat,
17–22 GB/s from 512 KB to 4 MiB, while the cached rung scales normally
(34–81 GB/s); the loss is 69–372%. At 4 MiB, where the sfence is negligible
against a ~100 µs copy, NT is still ~1.9× slower — this is not the fence and not
a small-size artefact. **This is an inference, not a cache-boundary
measurement**: this box exposes no L3/LLC events (protocol §5), so *why* NT is
slow here (a bandwidth-capped streaming write path) is labelled inference while
the loss is the measurement.

Consequence: there is no discovered crossover to encode, so the committed rung
keeps **256 KB as an experimental gate** — NT at or above it, rung 4's cached loop
below — which is what lets the campaign measure the NT effect at all. The rung's
measured verdict is the LOSS above; it is not a promotion candidate.

## 6. Explicit crossover: `avx2` → `avx2_nt` (item 6)

The only switch point is the experimental gate at 256 KB, and it is a step down:
5250 → 12394 ns (znver2), 5240 → 12544 ns (znver3), 5270 → 12483 ns (v3) — a
2.36–2.39× slowdown at the switch, shrinking to ~1.9× at 4 MiB. Above the gate the
cached `avx2` rung is faster at every size on every target (and rung 4 beats glibc
at 1 MiB and 4 MiB on znver2: 24406 vs 25508, 110157 vs 117813 ns). On this box,
in 8 KB–4 MiB, **NT has no winning side** — a fact about this host, not about NT
stores in general.

## 7. Unaligned handling cost (32 B alignment requirement)

Measured with the harness `--align2d` sweep at 1 MiB (aligned offsets vs the
rest, where a cached lead of ≤ 31 B runs first): minimum +0.34%/+1.24%/−1.57%
and mean +2.12%/+2.76%/+2.46% (znver3/znver2/v3). Bounded and small; v3's
negative minimum is inside noise.

## 8. Surprises (item 7)

1. NT loses everywhere on this box — the textbook expectation (NT wins for
   large, non-reused destinations) fails here, with a flat ~20 GB/s signature.
2. Same-source-loop rows differ by up to ~10% between two rungs and can clear
   the Tier A floor as WIN: compilation layout, not a kernel effect. The null
   band measures harness noise, not layout, so these are flagged, not claimed.
3. This rung compiles to genuinely different instruction sets per target
   (197/231/257 instructions), unlike Kernel 4.
4. v3's last window was still moving 13.31% (worst key `naive|64`) at MAXWIN;
   the analyzer publishes that key as unusable rather than averaging it in.
5. Tooling defects found this cycle, all fixed, none of them changing a number:
   `codegen_diff.sh` stated a mnemonic-multiset verdict only for ordering-only
   differences, so the "instruction sets differ" case had no multiset line at
   all; `SNAPSHOT_IMPLS` did not contain `avx2_nt`, so the new rung's CSVs would
   have been warned about and silently not frozen; and the source header, the
   Makefile and the probe script described 256 KB as "the threshold the probe
   selected", which is false — the probe selected nothing, because no winning
   size exists. The comment-only source fix leaves all 30 build objects
   byte-identical, so the frozen numbers still describe this binary.

## 9. Artifacts and limitations

* `results/committed/phase2_avx2_nt_{znver2,znver3,v3}.csv` — frozen (21
  snapshots total, 0 refused; each cmp-identical to the working CSV).
* `results/phase2_avx2_nt_analysis.txt` — gate proof, instrument, analyzer output
  per target, NT census, probe tables and alignment cost; regenerated by
  `scripts/phase2_k5_report.sh` (in the repo; every number printed by awk from a
  log or CSV, or by the gate itself).
* `results/codegen_diff_avx2_nt_*.diff` — three verdict-carrying diffs.
* `scripts/phase2_k5_campaign.sh` — the campaign driver, in the repo (earlier
  campaigns' wrappers were inline shell; protocol §9 requires evidence and scripts
  to live inside the repo).
* Raw campaign and probe logs are gitignored by project policy but
  self-describing: the campaign header records impls/sizes/reps/pin/windows/
  iters/budget and the gate line.
* Limitations: the cause of the NT slowdown is not measurable here and is
  reported as inference; v3 was measured one day after znver2/znver3 (disclosed
  in §3), and this rung's cross-target codegen differs anyway, so cross-target
  rows are per-target findings, not a hardware comparison; znver3's 1 MiB is
  Tier B; p99 recorded, used for nothing; no selection code was written — this is
  a finding and a rejected candidate.

# Kernel 6 — `memcpy_asm` (hand-written AVX2 assembly, `src/memcpy_asm.S`)

The raw-est rung: the same algorithm as Kernel 4 (four 32 B `ymm` lanes per
128 B, source prefetch at +2 cache lines, overlapping-pair tail), written by
hand in GAS/AT&T syntax and assembled by clang's integrated assembler. No nasm.
Committed as `914266f` ("phase2: memcpy_asm") before any timing row existed.
Two things were added to it that the compiler's version does not have: a 32 B
*aligned* store path (`vmovdqa`, entered when `dst % 32 == 0`, so no store can
straddle a 64 B line) and a GPR-only < 64 B path (required by the rung's
definition, expected to cost something at small sizes).

## 1. The mandatory gate (report item 1)

`make audit` rc=0 (no vector instruction in the scalar TUs) and
`make audit-width` rc=0, both re-run at report-generation time and both printed
in full into `results/phase2_asm_analysis.txt` §1. audit-width compares every
rung's widest memory access with its declared width, per target: all 9 kernel
TUs × 3 targets report `declared W  compiled W  OK`, including
`memcpy_asm.S  declared 256  compiled 256  OK` — and that is not taken from the
file's header comment: the gate disassembles the object and reads the operand
widths off the instructions. `scripts/audit_width.sh` had to learn to see `.S`
sources for this (the decl table and the source-derivation in `make audit` both
assumed `%.c`), and `scripts/codegen_diff.sh` had the same assumption in its
object-path derivation (`src/memcpy_asm.S.o`); both are fixed. The campaign's own
logs carry the gate line before window 1, and `scripts/mo_measure.sh` refuses to
write a timing row without it.

## 2. Verify first — PASS ×3, then what the objects actually contain (item 4)

* `verify: asm OK` ×9 windows ×3 targets, from the same check every other rung
  passes: byte-for-byte against glibc for n = 0..4096 at random src/dst offsets,
  64 B canaries both sides of the destination, plus the standing size list.
* Assembled content (verify cannot see any of this), identical on all three
  targets: 155 instructions, 1032 B object, `vmovdqa=5`, `vmovdqu(ymm)=19`,
  `vmovdqu(xmm)=4`, `prefetcht0=4`, `vzeroupper=1`. Widest access is 32 B
  everywhere, matching the declaration the gate checks.
* **Object identity:** `build/{znver2,znver3,v3}/src/memcpy_asm.o` are one and
  the same file by md5 (`327dd835…`, 1 distinct hash). A hand-written stream
  cannot be `-march`-sensitive, and it is not. The C rungs are: Kernel 5's NT
  rung compiled to three different instruction sets per target, and even Kernel
  4's `memcpy_avx2.o` is byte-identical for znver2/znver3 (`d99162ba…`) but
  different for v3 (`626fa5ec…`) — that fact matters in §6 and §8 below.

## 3. Codegen diff — the hand-written stream against the compiler's (item 8)

`results/codegen_diff_asm_vs_avx2_c_{znver2,znver3,v3}.diff` (regenerable with
`scripts/codegen_diff_asm_vs_c.sh`; this is a *two-kernels-one-target*
comparison, not the three-targets-one-kernel one `scripts/codegen_diff.sh`
does). Every file is non-empty, states both instruction counts and both
mnemonic-multiset verdicts, and its body is the address-stripped unified diff.
All three pairs are `verdict: DIFFERENT` and `mnemonic multiset: DIFFERENT` —
these are different instruction sets, not a reordering. Target-vs-target for
this rung is the opposite: `codegen_diff_asm_znver2_vs_znver3.diff` and friends
are all `IDENTICAL instruction streams` with an explicitly empty body.

| | instructions | distinct mnemonics | function bytes |
|:--|--:|--:|--:|
| `memcpy_asm` (hand-written) | 155 | 23 | 617 |
| `memcpy_avx2` (clang -O3 -march=znver2) | 121 | 18 | 460 |

What the compiler did that the asm did not: one loop with 22 `vmovups` (it never
uses an aligned store), a *vector* small path (`< 64 B` uses `xmm`/`ymm`
overlapping pairs), and a prefetch at +64 B rather than +128 B. What the asm
has and the compiler does not: the second (aligned-store) loop, the scalar-only
small path, and exactly one `vzeroupper`/return site — clang has 7, one per
return path. The hand-written version is *larger* than the compiler's (617 B vs
460 B) because it pays for both store paths; the aligned path is not free.

## 4. Campaign instrument

`scripts/phase2_k6_campaign.sh`: 5 reps × 9 windows, `taskset -c 2`, 5850 rep
lines per target, **0 migrated**, all `cpu=2`; `iters=4000`, `budget=0.05`,
`warmup=2`, `RUN_TO_CAP=1`, min_of_mins primary, recorded in each log's header
along with the gate line. Claims use the ratified two-tier model: Tier A floor
= max(1.5%, null band), Tier B floor 25%. Disclosure: znver2's first attempt was
aborted after 3 windows by a launcher problem (a `nohup &` inside a synchronous
tool call did not survive it) and znver2 was re-run whole from window 1; the
progress log records both invocations. All three targets then ran back-to-back
in one session (18:38–18:51), so unlike Kernel 5 there is no day boundary inside
this campaign.

## 5. Standing report — 9-point min_of_mins per size (items 2, 3 and 4)

min_of_mins in ns; `asm vs avx2` is rule 3 (1.5% floor) and the tier verdict is
the authoritative one this project ratifies. Tier A/B per size comes from the
asm row's per-key window spread (Tier B = spread > 10%). The glibc column is the
analyzer's own `tier_verdict_vs_glibc` with its `delta_vs_glibc_pct` beside it,
read straight from the frozen CSV — nothing in these three tables is typed in.

znver2:

| size | avx2 | asm | glibc | tier | asm vs avx2 (rule 3) | tier verdict | asm vs glibc (d) |
|--:|--:|--:|--:|:--|:--|:--|:--|
| 16 | 2.2 | 2.2 | 2.5 | B | within-noise | measured | measured (+12.5%) |
| 32 | 2.5 | 2.8 | 2.5 | B | **LOSS −12.6%** | measured | measured (−12.6%) |
| 64 | 5.3 | 5.6 | 2.5 | B | LOSS −5.9% | measured | **LOSS (−125.5%)** |
| 128 | 5.9 | 5.6 | 3.1 | B | WIN +5.6% | measured | **LOSS (−79.9%)** |
| 256 | 7.1 | 6.8 | 3.7 | B | WIN +4.4% | measured | **LOSS (−83.3%)** |
| 512 | 9.5 | 9.2 | 7.0 | B | WIN +3.3% | measured | **LOSS (−32.7%)** |
| 1024 | 14.9 | 13.3 | 11.1 | B | WIN +10.5% | measured | measured (−19.7%) |
| 4096 | 51.9 | 43.2 | 41.3 | B | WIN +16.9% | measured | measured (−4.5%) |
| 16384 | 240.2 | 225.2 | 212.8 | B | WIN +6.2% | measured | measured (−5.9%) |
| 65536 | 1262 | 1262 | 1252 | A | within-noise | measured | measured (−0.8%) |
| 262144 | 5290 | 5270 | 5370 | B | within-noise | measured | measured (+1.9%) |
| 1048576 | 24567 | 24066 | 26550 | B | WIN +2.0% | measured | measured (+9.4%) |
| 4194304 | 123122 | 126078 | 127490 | B | LOSS −2.4% | measured | measured (+1.1%) |

znver3:

| size | avx2 | asm | glibc | tier | asm vs avx2 (rule 3) | tier verdict | asm vs glibc (d) |
|--:|--:|--:|--:|:--|:--|:--|:--|
| 16 | 2.5 | 2.5 | 2.5 | A | within-noise | measured | measured (+0.0%) |
| 32 | 2.5 | 2.8 | 2.5 | A | **LOSS −12.6%** | **LOSS** (floor 5.0%) | **LOSS (−12.6%)** |
| 64 | 5.9 | 5.3 | 2.5 | A | WIN +10.5% | **WIN** (floor 1.5%) | **LOSS (−113.0%)** |
| 128 | 6.2 | 5.4 | 3.1 | A | WIN +12.6% | measured | **LOSS (−74.8%)** |
| 256 | 7.7 | 6.5 | 3.7 | B | WIN +16.1% | measured | **LOSS (−74.9%)** |
| 512 | 9.9 | 8.9 | 7.0 | B | WIN +9.5% | measured | **LOSS (−28.2%)** |
| 1024 | 15.2 | 13.0 | 11.1 | B | WIN +14.4% | measured | measured (−16.9%) |
| 4096 | 51.9 | 42.6 | 41.3 | B | WIN +18.1% | measured | measured (−3.0%) |
| 16384 | 238.0 | 225.2 | 207.8 | B | WIN +5.4% | measured | measured (−8.4%) |
| 65536 | 1262 | 1262 | 1252 | A | within-noise | measured | measured (−0.8%) |
| 262144 | 5250 | 5280 | 5280 | B | within-noise | measured | measured (+0.0%) |
| 1048576 | 24526 | 23665 | 25798 | B | WIN +3.5% | measured | measured (+8.3%) |
| 4194304 | 120287 | 116500 | 119946 | B | WIN +3.2% | measured | measured (+2.9%) |

v3:

| size | avx2 | asm | glibc | tier | asm vs avx2 (rule 3) | tier verdict | asm vs glibc (d) |
|--:|--:|--:|--:|:--|:--|:--|:--|
| 16 | 2.2 | 2.2 | 2.5 | B | within-noise | measured | measured (+12.5%) |
| 32 | 2.5 | 2.8 | 2.5 | B | **LOSS −12.6%** | measured | measured (−12.6%) |
| 64 | 5.3 | 5.6 | 2.5 | B | LOSS −5.9% | measured | **LOSS (−125.5%)** |
| 128 | 5.9 | 5.6 | 3.1 | B | WIN +5.3% | measured | **LOSS (−80.5%)** |
| 256 | 7.1 | 6.8 | 3.7 | B | WIN +3.8% | measured | **LOSS (−84.3%)** |
| 512 | 9.5 | 9.3 | 7.0 | B | WIN +2.4% | measured | **LOSS (−33.9%)** |
| 1024 | 14.9 | 13.3 | 11.1 | B | WIN +10.5% | measured | measured (−19.7%) |
| 4096 | 51.9 | 43.2 | 41.3 | B | WIN +16.9% | measured | measured (−4.5%) |
| 16384 | 238.0 | 225.5 | 212.8 | B | WIN +5.3% | measured | measured (−6.0%) |
| 65536 | 1262 | 1262 | 1262 | A | within-noise | measured | measured (+0.0%) |
| 262144 | 5290 | 5260 | 5310 | B | within-noise | measured | measured (+0.9%) |
| 1048576 | 23765 | 23955 | 26420 | B | within-noise (band LOSS) | measured | measured (+9.3%) |
| 4194304 | 108364 | 119495 | 121229 | B | **LOSS −10.3%** | measured | measured (+1.4%) |

Size-class medians (analyzer's own roll-up; W/L/N counts use the 1.5% floor):

| class | znver2 vs avx2 | znver2 vs glibc | znver3 vs avx2 | znver3 vs glibc | v3 vs avx2 | v3 vs glibc |
|:--|--:|--:|--:|--:|--:|--:|
| tiny (<64 B) | −6.3% (0/1/1) | −0.0% | −6.5% (0/1/1) | −6.3% | −6.3% (0/1/1) | −0.0% |
| small (64 B–4 KB) | +5.0% (5/1) | −56.3% (0/6) | +13.5% (6/0) | −51.5% (0/6) | +4.6% (5/1) | −57.2% (0/6) |
| mid (4 KB–64 KB) | +3.1% (1/0/1) | −3.3% | +2.7% (1/0/1) | −4.6% | +2.6% (1/0/1) | −3.0% |
| big (64 KB–1 MB) | +1.2% (1/0/1) | +5.6% | +1.5% (1/0/1) | +4.1% | −0.1% (0/0/2) | +5.1% |
| huge (>1 MB) | −2.4% (0/1/0) | +1.1% | +3.2% (1/0/0) | +2.9% | −10.3% (0/1/0) | +1.4% |

Tier A note: only **two rows in the whole campaign get a tier verdict against the
previous rung at all** — znver3 32 B (LOSS) and znver3 64 B (WIN); against glibc
the tier verdict is LOSS at 64 B–512 B and `measured` everywhere else. Every
remaining vs-`avx2` row prints `measured`, i.e. the delta is inside that size's
floor and is not a claim. Nothing in Tier B comes close to the 25% floor: the
largest rule-3 win anywhere is +18.1%.

## 6. Where asm beat the compiler, and where it lost (items 5 and 6)

**Did asm beat the compiler? Yes at 128 B–16 KB by rule 3 (+2.4% to +18.1%,
WIN at 18 of 18 size×target cells in that band — 6 sizes × 3 targets), no at
< 64 B, and it is a wash from 256 KB up. None of those wins is promotable
(Tier B, 25% floor).**

* **32 B — LOSS −12.57%, identical on all three targets, and reproducible.**
  This one has a named mechanism, not a guess: the rung is *required* to keep a
  scalar-only < 64 B path, so at n = 32 it runs 8 × 8 B loads and 8 × 8 B stores,
  while clang's C version copies the same 32 B with two `vmovups` (ymm) — four
  memory operations against sixteen. The alternative (a 32 B `ymm` pair below
  64 B) exists in the C rung and is not available to this one by definition.
  This is the honest cost of the mandated design, and it is the rung's most
  solid result: −12.6% at 32 B, −5.9% at 64 B on two of three targets.
* **64 B — sign flip on byte-identical code.** For znver2 and znver3 the C
  `memcpy_avx2.o` is the *same file* (md5 `d99162ba…`) and the asm `.o` is the
  same file on all three targets, yet the same pair measured −5.88% (LOSS) on
  znver2 and +10.54% (WIN) on znver3 four minutes apart. The 64 B deltas
  therefore do not reproduce; they are inside this instrument's run-to-run
  spread at that size, which is consistent with the rule-2 evidence in §5
  (`UNSTABLE`, per-window spreads of 13–55% over 64 B–16 KB). I am not claiming
  the 64 B WIN.
* **128 B–16 KB — consistent direction, unreproducible magnitude.** The sign is
  the same on all three targets (asm faster at every size in the band), but the
  magnitude swings: 128 B +5.3/+5.6/+12.6%, 256 B +3.8/+4.4/+16.1%, 1 KB
  +10.5/+10.5/+14.4%, 4 KB +16.9/+16.9/+18.1%. The same-target C baselines also
  moved by up to 11% between the znver2 and znver3 campaigns despite identical
  objects (avx2 at 64 B: 5.3 vs 5.9). The mechanism I *can* point at for the asm
  side is real and visible in the diff — the aligned `vmovdqa` store path is
  active on every harness call (buffers are 64 B aligned), so no store can split
  a 64 B line, and the loads are grouped ahead of the stores; at 4 KB the whole
  working set is L1-resident, which is why that is where the gap is widest. The
  magnitude of the effect is not established by this instrument.
* **> 1 MB — the one place the asm loses big, and I cannot attribute it.**
  znver3 +3.2%, znver2 −2.4%, v3 −10.3% (Tier floor 25%, so all three are
  `measured` at the tier level, but −10.3% clears rule 3). The obvious
  hypothesis is prefetch distance — the asm prefetches +128 B while clang
  prefetched +64 B (Kernel 4's own study picked +2 lines over +1 and +4, and
  this rung inherited that answer rather than re-testing it) plus the aligned
  store path's read-for-ownership behaviour on a streaming copy. **This
  campaign does not isolate that variable, and I am not reporting the hypothesis
  as a finding.** It is reported as an unexplained per-target divergence at
  4 MiB.
* **vs glibc: the rung loses outright in the small band.** The tier verdict
  (the authoritative one) is LOSS at 64 B–512 B on every target, and the raw
  ratio is a deficit at *every* size from 16 B to 16 KB: glibc 2.5 ns against
  asm's 5.3–5.6 ns at 64 B (a 2.1–2.2× gap), 3.7 against 6.5–6.8 at 256 B, and
  41.3 against 42.6–43.2 at 4 KB (3.0–4.5%). glibc's ifunc-selected path owns
  that band outright; this rung is not in that fight. From 256 KB up the tables
  turn (asm ahead by 9.4% at 1 MiB on znver2, 8.3% on znver3, 9.3% on v3), but
  those rows are Tier B and every one of them is `measured`, not a claim.

## 7. Surprises (item 7)

1. **The hand-written object is byte-identical across all three targets** (one
   md5), while the C rungs next to it are not. Kernel 5's rung compiled to three
   different instruction sets; Kernel 4's compiled identically on znver2/znver3
   and differently on v3. This is the first rung in the ladder where "same
   binary, different numbers" is guaranteed by construction rather than
   established by a diff — which is exactly what made the 64 B sign flip (§6)
   legible instead of explainable away.
2. **The compiler's code is smaller than the hand-written code** (121 vs 155
   instructions, 460 vs 617 B) and still loses in the 128 B–16 KB band. More
   instructions is the price of the aligned-store split; the reward is that the
   aligned store cannot split a cache line.
3. **asm loses at 32 B by exactly the same −12.57% on all three targets**, i.e.
   the rung's one clear loss is also its most reproducible result. Consistency
   cut the other way from the wins, and that is worth recording as it stands.
4. **clang emits 7 `vzeroupper`s (one per return) and the asm emits 1**, which
   should favour the asm on the < 64 B path — and does not, because on that path
   the asm never enters a vector register at all and so has nothing to clean up.
   The vector-state discipline was not the bottleneck at the sizes where it
   could have shown.

## 8. Is this rung a promotion candidate over C AVX2? (item 8)

**No.** The rule-3 wins live in sizes the ratified model classes as Tier B
(25% floor) and none clears it: the best is +18.1%. The only tier-confirmed
verdicts against the previous rung in the whole campaign are znver3 32 B LOSS
and znver3 64 B WIN, and the 64 B one flips sign on byte-identical objects
between two of the three campaigns, so it cannot carry a promotion. Against
glibc this rung loses in the 64 B–512 B band and never beats it below 16 KB. And
at 4 MiB it loses on two of three targets with a cause this campaign does not
isolate.

The honest summary of the asm experiment is: **hand-writing the loop did beat
the compiler inside the L1/L2 band on all three targets at the rule-3 floor, and
it lost to the compiler at 32 B; but neither result survives the project's own
claim model, and no threshold or tuning was applied after the fact to change
that.** No selection code was written; rung 6 stays in the registry as a
measured rung, alongside the AVX2 C rung it did not displace.

## 9. Artifacts and limitations

* `results/committed/phase2_asm_{znver2,znver3,v3}.csv` — frozen (24 snapshots
  total, 0 refused; each cmp-identical to the working CSV).
* `results/phase2_asm_analysis.txt` — gate proof, instrument, per-target
  analyzer output, assembled-rung census and object identity, and the codegen-diff
  headers; regenerated by `scripts/phase2_k6_report.sh` (every number printed by
  awk from a log, a CSV or an object file, or by the gate itself).
* `results/codegen_diff_asm_vs_avx2_c_{znver2,znver3,v3}.diff` — the asm-vs-C
  comparison (item 8), non-empty, multiset verdict on every one;
  `results/codegen_diff_asm_<a>_vs_<b>.diff` — the target-vs-target check that
  returns IDENTICAL.
* `scripts/phase2_k6_campaign.sh` (the campaign driver) and
  `scripts/codegen_diff_asm_vs_c.sh` (the two-kernels-one-target diff), both in
  the repo; raw campaign logs are gitignored by project policy but
  self-describing (impls, sizes, reps, pin, windows, iters, budget, gate line).
* Limitations: the 64 B and 128 B–16 KB deltas are magnitude-unstable within
  this instrument and only their direction is reported; the 4 MiB divergence has
  no measured cause; the per-target C baselines are not the same binary for v3,
  so cross-target rows are per-target findings, not a hardware comparison; p99
  recorded, used for nothing; below-64 B behaviour is defined by the rung's
  scalar-path requirement and is priced, not tuned.

# Phase 2.5 — hybrid dispatcher (`memcpy_hybrid`), the detour before Kernel 7

The premise this phase exists to test: **glibc does not win because one of its
kernels beats ours — it wins because it dispatches among several per size.**
After Kernel 6 the project had six rungs and per-size `min_of_mins` for all of
them on all three targets, and had never tried that strategy. So Phase 2.5 is one
kernel that routes each call to the best rung for its size, with the routing
table *generated from frozen measurements* rather than picked by hand. It is a
detour, not a replacement: Kernels 7 and 8 still follow.

## 0. What was built, and the one place the request was not met

`src/memcpy_hybrid.c` (the dispatcher), `src/routing_table.h` (generated, "DO
NOT EDIT"), `scripts/gen_routing_table.sh` (generates it from the Kernel 6
campaign), `tools/route_check.c` + `scripts/hybrid_route_check.sh` (verification
against the *built object*), plus a `routecheck` target and a dispatcher rule in
`scripts/audit_width.sh`. `memcpy_hybrid` is declared in `src/common.h` and
registered in `src/registry.c` like every other rung.

The dispatcher routes on **size only**. The request's rationale mentioned
"(and where applicable, its alignment)": no rung in the ladder is
alignment-specialised, the campaign measures one offset configuration, and an
alignment axis would need its own campaign to produce a table — so it is not in
the table, and that is a stated limitation (§9), not an oversight. The standard
rungs are alignment-agnostic by contract (unaligned vector loads, scalar small
paths), and the canary/verify suite is what checks that.

## 1. The gate passed (item 1)

`make -s gate` **rc=0**: `make audit` rc=0 (the scalar TUs still hold zero vector
instructions) and `make audit-width` rc=0 with `audit_width: PASS`. The new rule
is reported as its own kind of row, once per target:

```
memcpy_hybrid.c   DISPATCHER declared 256 = max width of the 4 gated rung(s) it references;
                  own widest access 64 (mov (%rcx,%rax,8),%rax)
```

(3 gated rungs on v3 — its table references `avx2`, `sse2`, `asm`.) The declared
256 is required *because* the rung forwards to rungs that move 256-bit vectors;
the dispatcher's own widest access is 64-bit, i.e. it moves no data, and the rule
runs the reference walk to prove it references only gated rungs. `make gate`
itself is `audit + audit-width`; `make routecheck` is the separate target run
below, and each campaign log carries the gate line before window 1:
`# gate: make audit PASSED and make audit-width PASSED (rc=0) before window 1`.

## 2. Instrument (item 2)

`scripts/phase2_hybrid_campaign.sh`: 9 impls (`naive word glibc ref_scalar sse2
avx2 avx2_nt asm hybrid`), **17 sizes** — the standing 13-point ladder plus four
in-between sizes (100 B, 1000, 20000, 300000) so the classes have witnesses that
are *not* their own boundaries — 5 reps × 9 windows, `taskset -c 2`, `iters=4000`,
`budget=0.05`, `warmup=2`, `RUN_TO_CAP=1`, min_of_mins primary. Per target: 8415
rep lines, **0 migrated**, every line `cpu=2`. All three targets ran back-to-back
in one session (18:32:35–18:49:34), so there is no day boundary inside this
campaign. As in Kernel 6, the extra rep lines for `glibc`/`ref_scalar` are their
memset cases; the analyzer filters `op=memcpy`, so no comparison uses them.

Tier coverage is the first thing to read about this phase: **Tier A only 5 of 17
sizes on znver2, 5 on znver3, 8 on v3.** Most of this campaign's cells are Tier B
(per-window spread > 10%, 25% floor) and therefore carry `measured`, not a claim —
including nearly every tiny-size cell, where the copy is a handful of
instructions and the per-window spread at 32 B reaches 59.93%.

## 3. The routing table (item 3)

Generated, per target, from the **Kernel 6 campaign log** — the only dataset that
measured all six ladder rungs plus glibc in one window set. Sizes between two
measured points take the lower point's winner (conservative); n < 16 takes the
16 B winner; n > 4 MiB takes the 4 MiB winner. The requested class list went to
256 MiB; the truth is that **only 16 B–4 MiB were ever measured by a Phase 2
`min_of_mins` campaign** — the only rows at 16/64/256 MiB anywhere in `results/`
are Phase 1 smoke runs and one legacy `*.median_based.csv` (which `make snapshot`
does not even track), neither of them a same-window comparison. So the table stops
at 4 MiB and states the clamp, instead of inventing a winner for sizes no rung has
been measured at.

| size | znver2 | znver3 | v3 |
|--:|:--|:--|:--|
| 16 | avx2 | avx2_nt | avx2 |
| 32 | avx2 | avx2 | avx2 |
| 64 | sse2 | sse2 | sse2 |
| 128 | sse2 | sse2 | sse2 |
| 256 | sse2 | sse2 | sse2 |
| 512 | avx2_nt | asm | asm |
| 1024 | asm | asm | asm |
| 4096 | asm | asm | asm |
| 16384 | asm | asm | asm |
| 65536 | avx2 | avx2 | avx2 |
| 262144 | asm | avx2 | asm |
| 1048576 | asm | asm | avx2 |
| 4194304 | avx2 | asm | avx2 |

Routed rungs: `naive word sse2 avx2 avx2_nt asm`. **Never routed to** (excluded,
and the provenance prints what they would have won so the cost of the exclusion
is visible): `ref_scalar` (the golden reference, not a rung), `glibc` (the
baseline being chased), `avx2_pf2`/`avx2_pf4` (prefetch variants of a rung that
already lost, and never measured together with `asm` — routing to them would need
the cross-campaign mixing the generator refuses).

The generator cross-checks a second source (the union of frozen committed CSVs)
but does not decide with it, and prints every disagreement: **5 of 39 cells**. The
reason is in the file — glibc's 4 MiB row across the nine frozen CSVs spans
116990–131739 ns on one unchanged binary. Two further honesty features:
`scripts/gen_routing_table.sh` validates its own extractor against the three
rungs a frozen same-campaign CSV shares with it (max |frozen − extracted| =
0.0500 ns, i.e. the CSV's own rounding), and it labels every class **decided** or
**coin-flip** (winner margin ≤ null band). Coin-flips: **4 of 13 classes on
znver2, 6 on znver3, 4 on v3** — one of them is the 16 B class the dispatcher
pays the most for (§5). The lookup is `routing_small[n >> 4]` below 4096 (one
load, generated from the same 13 bounds) and a bounded walk above it; both are
re-derived exhaustively from the built object by `tools/route_check.c`, so the
mapping in the evidence is the binary's, not the source's.

## 4. Standing report — 9-point `min_of_mins` per size (items 2 and 4)

ns; `routed` is the class winner the generated table sends that size to, `tier`
is the hybrid row's tier, `hybrid vs asm` is rule 3 (1.5% floor) against the
strongest single rung, and the last two columns are the analyzer's own tier
verdicts (`tier_verdict_vs_prev`, `tier_verdict_vs_glibc`) with its raw glibc
delta beside them — read from the frozen CSVs, nothing typed in.

znver2:

| size | routed | asm | hybrid | glibc | tier | hybrid vs asm (rule 3) | tier verdict vs asm | tier verdict vs glibc (d) |
|--:|:--|--:|--:|--:|:--|:--|:--|:--|
| 16 | avx2 | 2.2 | 2.5 | 2.8 | B | LOSS | measured | measured (+11.17%) |
| 32 | avx2 | 2.8 | 2.8 | 2.5 | B | within-noise | measured | measured (−12.57%) |
| 64 | sse2 | 5.9 | 3.4 | 2.5 | B | **WIN** | **WIN** | **LOSS (−37.67%)** |
| 100 | sse2 | 5.9 | 5.3 | 3.1 | A | **WIN** | **WIN** | **LOSS (−70.14%)** |
| 128 | sse2 | 5.9 | 4.0 | 3.1 | A | **WIN** | **WIN** | **LOSS (−30.13%)** |
| 256 | sse2 | 7.1 | 5.9 | 3.7 | B | WIN | measured | **LOSS (−58.99%)** |
| 512 | avx2_nt | 9.5 | 9.2 | 7.0 | B | WIN | measured | **LOSS (−32.66%)** |
| 1000 | avx2_nt | 12.9 | 14.0 | 11.1 | B | LOSS | measured | **LOSS (−26.49%)** |
| 1024 | asm | 13.6 | 13.6 | 11.1 | B | within-noise | measured | measured (−22.50%) |
| 4096 | asm | 43.2 | 43.2 | 41.3 | B | within-noise | measured | measured (−4.54%) |
| 16384 | asm | 222.8 | 225.2 | 208.0 | B | within-noise | measured | measured (−8.29%) |
| 20000 | asm | 364.0 | 370.7 | 360.7 | A | LOSS | **LOSS** | **LOSS (−2.77%)** |
| 65536 | avx2 | 1262.0 | 1262.0 | 1252.0 | A | within-noise | measured | measured (−0.80%) |
| 262144 | asm | 5320.0 | 5270.0 | 5330.0 | A | within-noise | measured | measured (+1.13%) |
| 300000 | asm | 6052.0 | 6041.0 | 6122.0 | B | within-noise | measured | measured (+1.32%) |
| 1048576 | asm | 23004.0 | 23164.0 | 25539.0 | B | within-noise | measured | measured (+9.30%) |
| 4194304 | avx2 | 121039.0 | 120047.0 | 118203.0 | B | within-noise | measured | measured (−1.56%) |

znver3:

| size | routed | asm | hybrid | glibc | tier | hybrid vs asm (rule 3) | tier verdict vs asm | tier verdict vs glibc (d) |
|--:|:--|--:|--:|--:|:--|:--|:--|:--|
| 16 | avx2_nt | 2.5 | 2.8 | 2.5 | A | LOSS | **LOSS** | **LOSS (−12.57%)** |
| 32 | avx2 | 2.8 | 3.0 | 2.5 | B | LOSS | measured | measured (−18.84%) |
| 64 | sse2 | 5.0 | 3.7 | 2.5 | A | **WIN** | **WIN** | **LOSS (−50.24%)** |
| 100 | sse2 | 4.8 | 4.7 | 3.1 | B | WIN | measured | **LOSS (−50.02%)** |
| 128 | sse2 | 5.6 | 4.7 | 3.1 | B | WIN | measured | **LOSS (−50.28%)** |
| 256 | sse2 | 6.8 | 6.5 | 3.7 | B | WIN | measured | **LOSS (−75.92%)** |
| 512 | asm | 8.9 | 9.5 | 7.0 | B | LOSS | measured | **LOSS (−37.15%)** |
| 1000 | asm | 11.7 | 12.0 | 11.1 | B | LOSS | measured | measured (−8.32%) |
| 1024 | asm | 13.0 | 13.3 | 11.1 | B | LOSS | measured | measured (−19.69%) |
| 4096 | asm | 43.2 | 43.2 | 41.3 | B | within-noise | measured | measured (−4.54%) |
| 16384 | asm | 223.0 | 225.2 | 207.8 | B | within-noise | measured | measured (−8.42%) |
| 20000 | asm | 367.3 | 370.7 | 360.7 | A | within-noise | measured | **LOSS (−2.77%)** |
| 65536 | avx2 | 1253.0 | 1262.0 | 1262.0 | A | within-noise | measured | measured (+0.00%) |
| 262144 | avx2 | 5270.0 | 5260.0 | 5290.0 | B | within-noise | measured | measured (+0.57%) |
| 300000 | avx2 | 6332.0 | 6081.0 | 6483.0 | A | **WIN** | **WIN** | **WIN (+6.20%)** |
| 1048576 | asm | 23645.0 | 23635.0 | 25709.0 | B | within-noise | measured | measured (+8.07%) |
| 4194304 | asm | 121980.0 | 121099.0 | 121870.0 | B | within-noise | measured | measured (+0.63%) |

v3:

| size | routed | asm | hybrid | glibc | tier | hybrid vs asm (rule 3) | tier verdict vs asm | tier verdict vs glibc (d) |
|--:|:--|--:|--:|--:|:--|:--|:--|:--|
| 16 | avx2 | 2.2 | 2.5 | 2.8 | A | LOSS | **LOSS** | **WIN (+10.86%)** |
| 32 | avx2 | 2.8 | 2.8 | 2.5 | A | within-noise | measured | **LOSS (−12.57%)** |
| 64 | sse2 | 5.9 | 3.4 | 2.5 | A | **WIN** | **WIN** | **LOSS (−37.67%)** |
| 100 | sse2 | 5.9 | 5.0 | 3.1 | A | **WIN** | **WIN** | **LOSS (−60.35%)** |
| 128 | sse2 | 5.9 | 4.4 | 3.1 | A | **WIN** | **WIN** | **LOSS (−40.24%)** |
| 256 | sse2 | 7.1 | 5.9 | 3.7 | B | WIN | measured | **LOSS (−58.99%)** |
| 512 | asm | 9.5 | 9.9 | 7.0 | B | LOSS | measured | **LOSS (−41.64%)** |
| 1000 | asm | 12.9 | 13.2 | 11.1 | B | LOSS | measured | measured (−19.42%) |
| 1024 | asm | 13.6 | 13.6 | 11.1 | A | within-noise | measured | **LOSS (−22.50%)** |
| 4096 | asm | 43.2 | 43.2 | 41.3 | B | within-noise | measured | measured (−4.54%) |
| 16384 | asm | 222.8 | 225.5 | 210.2 | B | within-noise | measured | measured (−7.25%) |
| 20000 | asm | 367.3 | 367.3 | 360.7 | A | within-noise | measured | **LOSS (−1.85%)** |
| 65536 | avx2 | 1262.0 | 1262.0 | 1252.0 | A | within-noise | measured | measured (−0.80%) |
| 262144 | asm | 5240.0 | 5240.0 | 5250.0 | B | within-noise | measured | measured (+0.19%) |
| 300000 | asm | 6071.0 | 6101.0 | 6292.0 | B | within-noise | measured | measured (+3.04%) |
| 1048576 | avx2 | 23004.0 | 23334.0 | 26289.0 | B | within-noise | measured | measured (+11.24%) |
| 4194304 | avx2 | 110478.0 | 108946.0 | 122671.0 | B | within-noise | measured | measured (+11.19%) |

The tables are also in `results/phase2_hybrid_analysis.txt` §5b with the raw and
band verdicts; §4 there is the analyzer's per-size and per-class output.

## 5. Where the hybrid beats glibc, loses, and cannot be resolved (item 5)

Counted over the 51 cells (17 sizes × 3 targets), using the sanctioned tier
verdicts — **2 wins to glibc, 22 losses, 27 unresolvable**:

* **Beats glibc (tier-confirmed):** znver3 **300 KB +6.20%** (Tier A, floor
  1.5%), v3 **16 B +10.86%** (Tier A, floor 9.5% — the band is doing the work
  there). Two cells out of fifty-one.
* **Loses to glibc (tier-confirmed):** the whole **64 B–20 KB band**, on all
  three targets — znver2 64 B/100/128/256/512/1000/20000, znver3
  16/64/100/128/256/512/20000, v3 32/64/100/128/256/512/1024/20000. The
  magnitudes are not marginal: 64 B–512 B cells are −30% to −75.92%, 1024 B on
  v3 is −22.50%, 16 B/32 B on znver3/v3 are −12.57%, and only the three 20000 B
  cells sit inside 3% (−2.77%, −2.77%, −1.85%). This is not new information
  from this phase; what is new is that **optimal dispatch among our own rungs does
  not remove it**.
* **Unresolvable:** 27 cells are Tier B `measured`, including every 1 MiB+ cell.
  The raw deltas there favour us (1 MiB: +9.30/+8.07/+11.24%; 4 MiB v3 +11.19%)
  but Tier B's 25% floor is not cleared, so they are not claims. Under the older
  band model the picture is the same shape: 2/2/3 wins, 3/3/3 within-noise,
  12/12/11 losses.

## 6. Does the hybrid beat the best single rung? (item 6)

**Against the strongest single rung (`asm`, the most-won rung by the provenance's
own count): yes, exactly where that rung is worst, and it is the first win in
this project that holds on all three targets.** Tier verdicts vs `asm`: **WIN
3/2/3, LOSS 1/1/1, measured 13/14/13.** The wins: **64 B on all three targets
(+42.18%, +25.04%, +42.18%)**, 100 B on znver2/v3 (+10.57%, +15.72%), 128 B on
znver2/v3 (+31.47%, +26.15%), and znver3 300 KB (+3.96%). The three losses are
single cells: znver2 20000 B (−1.83%), znver3 16 B (−12.57%), v3 16 B (−14.32%).

**Against the per-size best rung — the floor a dispatcher is trying to reach —
no, and this is the honest price of the design.** The hybrid is **+6.4% to
+28.7% above the per-size floor at 16 B–512 B** (one class lookup and an indirect
jump, worth 0.3 ns at 16–128 B and 0.6 ns by 512 B, on a copy that is only
2.2–9.5 ns there), and at most ±2.77% from 1 KB up. It never reaches the floor at
the sizes where the call overhead is the copy; a dispatcher that did would need
the lookup for free.

The hypothesis the detour was built around — "no single kernel wins everywhere" —
is **confirmed**, from the same windows (analysis §6): `asm` has the best median
penalty against the per-size floor (+0.24/+0.19/+0.00%) but its worst case is
**+60.17% to +90.27%** at 64 B; `sse2`'s worst is **+243.96% to +272.66%** at
16 B; `avx2`'s worst +70.21% to +90.27% at 64 B; `avx2_nt`'s +137.17% to
+139.20%. The hybrid's median penalty is +1.83/+2.63/+1.85% and its worst
+14.32/+28.69/+14.32% — a **3–5× better worst case than any single rung, for
about a 2% median price**.

## 7. Verdict: is the hybrid a promotion candidate? (item 7)

**As a glibc-beater, no** — and the detour answered its own question in the
negative in a way worth writing down: dispatch was glibc's strategy, we now do it
with a table generated from our own measurements, and glibc still owns the 64 B
–20 KB band on all three targets by tier verdict, with two wins to show for it in
51 cells. "glibc is fast because it dispatches" is therefore **not sufficient** to
explain the gap; the small-size band is a kernel-quality gap, not a routing gap.

**As the project's best single entry point, yes** — with the 16 B class priced,
not tuned. It is the only composition whose worst case against the per-size
floor is under 30%, it turns the 64 B hole into a tier-confirmed win over the
strongest single rung, and its table is regenerable data. It should not be
presented as beating glibc; it should be presented as the ceiling of the ladder
we have built, which is a different and fully supported claim. Kernels 7 and 8
remain the right next steps — they complete the ladder's evidence, and this phase
shows they are not on their own a path to the glibc gap.

## 8. What surprised me (item 8)

1. **The premise failed where it was strongest.** The 64 B–20 KB band is exactly
   where per-size dispatch should help, and it is exactly where the answer did
   not move: routing 64 B to our own 3.4 ns `sse2` (instead of `asm`'s 5.9 ns)
   wins big against our rungs and still loses to glibc's 2.5 ns.
2. **The 64 B cell flipped from "coin-flip" to decisive once a consumer
   measured it.** Kernel 6 called 64 B a coin-flip and Kernel 4 measured the
   sign of that cell flipping on byte-identical objects. The dispatcher routes it
   to `sse2` and gets a tier-confirmed WIN against `asm` on all three targets.
   The lesson is about the instrument, not the rung: the earlier flip was the
   comparison, and a dispatcher is a much better-controlled way to ask the same
   question.
3. **The lookup is bigger than the copy at 16 B.** 2.5 ns hybrid against 2.2 ns
   for the rung it calls; +14.32% on znver2/v3 and +28.69% on znver3, and on two
   targets a tier-confirmed **LOSS** against `asm`. Worse, the provenance calls
   that class a **coin-flip** (winner margin 0.00% vs the null band on znver2 and
   v3) — so one class of the table is routing on noise, at the one size where the
   error is largest relative to the copy.
4. **Most of this phase cannot be claimed at all.** 12 of 17 cells are Tier B on
   znver2/znver3 — the instrument's per-window spread at small sizes (59.93% at
   32 B) is larger than the differences being measured. That is why §5's answer is
   "2 wins, 22 losses, 27 unresolvable" rather than a size-by-size story.
5. **The table is only as stable as one window set.** 5 of 39 cells disagreed
   between the same-window log and the frozen CSVs, and the routing differs per
   target at 65536 B and 4 MiB. Regeneration is deterministic, but a different
   campaign would move some of these cells — which is why the table ships with
   its provenance file, margins and coin-flip flags rather than as a bare array.

## 9. Artifacts and limitations

* `src/memcpy_hybrid.c`, `src/routing_table.h` (generated), `src/common.h`,
  `src/registry.c`; `scripts/gen_routing_table.sh` →
  `results/phase2_hybrid_routing.txt` (sources, extractor validation, both
  sources' tables, the 5 disagreements, the emitted table, the hypothesis count).
* `tools/route_check.c` + `scripts/hybrid_route_check.sh`: check 1 exhaustive
  class mapping (8,388,614 sizes + SIZE_MAX/SIZE_MAX−1, **0 mismatches**), check 2
  the table as compiled, check 3 **420 copies** at every class boundary, boundary
  ±1, the in-between sizes and 7 offsets, against `memcpy_ref` with 64 B canaries
  both sides — **0 bad**, on all three targets. `--verify` PASS ×3 through the
  harness's own CLI: `hybrid OK (glibc reference, n=0..4096 random offsets,
  canary 64B both sides, plus size list)`.
* `scripts/phase2_hybrid_campaign.sh` (driver), `scripts/phase2_hybrid_report.sh`
  → `results/phase2_hybrid_analysis.txt` (551 lines: gate, instrument, table,
  analyzer output, the hypothesis test, the per-size sanctioned verdicts, the
  per-rung penalty table and the two-tier summary; every number printed from a
  log, a CSV, an object or the gate itself). Raw logs stay gitignored per project
  policy but are self-describing. `results/committed/phase2_hybrid_<t>.csv`:
  frozen snapshots, cmp-identical to the working CSVs.
* Commit order: `ee05fc7` "phase2.5: hybrid dispatcher" at 18:32:23, campaign
  started 18:32:35 — commit before benchmark, with the dispatcher (not the
  results) in it.
* Limitation that goes to the top of the list for anyone reading the verdict:
  **27 of 51 cells are unresolvable in this instrument** (Tier B). Also: the
  table is size-only (no alignment axis, §0); one class per target is routed on a
  coin-flip cell; sizes above 4 MiB are clamped to the 4 MiB winner because no
  Phase 2 campaign measured them (§3); the routed set excludes the prefetch
  variants because no single
  campaign measured them with `asm`; and the ~0.3 ns lookup cost is measured as a
  percentage at tiny sizes, where percentages are not a stable instrument — the
  direction (dispatcher slower below 512 B) is what is claimed, not the magnitude.
