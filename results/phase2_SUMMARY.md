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
