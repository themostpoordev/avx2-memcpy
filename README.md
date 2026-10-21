# avx2-memcpy

A from-scratch memcpy library for Zen 2, built measurement-first: on the
reference host it beats glibc at **7 sizes** (8, 16, 32, 64, 128, 256, 512 B),
ties at **6** (1 KiB, 4 KiB, 16 KiB, 64 KiB, 256 KiB, 1 MiB), has **1
unmeasurable** size (4 MiB) and **no certified losses** — every size mapped to
exactly one category in the
[final result summary](docs/RESULTS.md#final-result-summary). Every published
number is reconstructable from a committed window log.

Two copy kernels, seven reference rungs, one benchmark harness, and a measurement
protocol that decides what may be called a win. Everything here is built from
scratch: no libc wrappers around the functions being replaced, no copied glibc
code, and every claim traces to a committed window log in
[`docs/data/raw/`](docs/data/raw/) and the CSV regenerated from it.

Every row below is backed by a raw window log committed under
[`docs/data/raw/`](docs/data/raw/), and the summary CSV is regenerated from that
log. A row whose two statistics disagree is labelled conditional rather than
resolved in favour of the more favourable one; the full tables are in
[docs/RESULTS.md](docs/RESULTS.md).

## Quick start

```c
#include "memcpy_fast.h"          /* include/ */

void *p = memcpy_fast(dst, src, n);   /* same contract as memcpy */
```

Build `src/fast.c` with gcc `-O3 -fno-crossjumping -falign-labels=64` (the
flags the certified numbers were produced with) and link it. `src/fast.c`
includes `src/fast_impl.h`, so compile from the repository root or add `-Isrc`.
**Do not drop `-falign-labels=64`**: without it the 32..64 B path loses the
64-byte alignment of its out-of-line branch target and that costs 16% at 32 B
and 64 B — two of the certified wins. Nothing else is required: no allocator,
no runtime init, no CPU dispatch.

The contract is identical to `memcpy` for non-overlapping `src` and `dst`, at
any alignment and any `n` including 0. Every build is checked byte-for-byte
against an independent reference with 64 B canaries on both sides before it is
timed (`make gate`).

## Opt-in: `memcpy_mid`

```c
#include "memcpy_mid.h"

/* dst, src must not overlap. In range: the tuned mid-band kernel.
 * Out of range: forwarded to memcpy_fast by one predicted branch. */
void *p = memcpy_mid(dst, src, n);
```

`memcpy_mid` is **not** part of the general memcpy path. It exists because one
band of sizes (1 KiB–64 KiB) rewards a different schedule, and a caller who
knows their workload sits in that band can opt in: 16 KiB is **+6..7%**
certified on all three targets.[^record] General callers should use
`memcpy_fast`. Link `src/mid.c` in addition to `src/fast.c`.

[^record]: **Record-derived.** Certified at measurement time on a quieter host
    window; reproduction today depends on host load. See
    [METHODOLOGY.md §9](https://github.com/themostpoordev/avx2-memcpy/blob/main/docs/METHODOLOGY.md#9-host-variance-the-floor-moves).
    The committed window logs in `docs/data/raw/`, and the CSV regenerated from
    them, are the authoritative record.

## The harness

```sh
make                      # bench/bench_znver2, bench_znver3, bench_v3 (static)
make gate                 # audits + width gate: mandatory before any benchmark
make experiments          # the documented-loss dispatcher (not built by default)
taskset -c 2 ./bench/bench_znver2 --list
taskset -c 2 ./bench/bench_znver2 --impl glibc,fast --sizes 16,64,512 --reps 5
taskset -c 2 ./bench/bench_znver2 --verify
```

Three targets exist because "which tuning target wins" is a measured variable:
`znver2` (the measured silicon), `znver3` (the originally assumed one) and `v3`
(`-march=x86-64-v3`, generic). CSV output, diagnostics on stderr; the harness
records median, min, p99, PMU cycles and per-rep CPU/migration status, and only
`min_of_mins` is ever used in a claim.

## Experiments

[`experiments/README.md`](experiments/README.md) is the documented-loss
dispatcher, shipped so it can be verified. It holds the rungs and the schedules
that lost, with the measurement that retired each one.

**This is a memcpy library.** `memcpy_fast` and `memcpy_mid` are the two
shipped kernels, and there is no shipped `memset` — the `memset_*` rungs in
`src/rungs/` exist only as ladder references for the copy path's methodology,
and the ladder's `memset` columns are not part of the certified table.

## Documentation

| document | what it is |
|---|---|
| [docs/RESULTS.md](docs/RESULTS.md) | every certified number, the ladder, the caveats |
| [docs/METHODOLOGY.md](docs/METHODOLOGY.md) | `min_of_mins`, the two-tier claim model, the width gate, evidence policy, reproducibility gate |
| [docs/NEGATIVE_RESULTS.md](docs/NEGATIVE_RESULTS.md) | what was tried and lost, with the mechanism for each |
| [experiments/README.md](experiments/README.md) | the documented-loss dispatcher, shipped so it can be verified |

## The baseline and the toolchain

**The baseline is glibc, as glibc itself resolves it on this host.** The
harness calls `memcpy` through the PLT, so glibc's ifunc resolver picks the
variant for the CPU: on the reference host (glibc 2.44+r50, AMD EPYC Zen 2) it
selects `__memmove_avx_unaligned_erms`, whose copy loop is 32-byte `vmovdqu`
moves on `ymm` registers. The EVEX/AVX-512 variant is not selected — this
silicon has no AVX-512 — and the `rep movsb` path is not used, because it has
neither ERMS nor FSRM. At 8–64 B, where the copy is two or three nanoseconds,
that decision *is* the comparison: a 16-byte SSE2 baseline would lose to us by
more than the 25% we report.

**The toolchain is part of the result.** The certified numbers were produced
with **gcc 16.2.1** and `-O3 -fno-crossjumping -falign-labels=64`. The last
flag is load-bearing: without it the 32–64 B path loses the 64-byte alignment
of its out-of-line branch target and that costs **16% at 32 B and 64 B** — two
of the seven wins. The alignment sensitivity is measured and written up in
[`docs/NEGATIVE_RESULTS.md`](docs/NEGATIVE_RESULTS.md); the honest reading is
that the 32–64 B win is a property of *this code at this alignment*, not a
portable property of the algorithm, and a different compiler may lay the
function out differently. The `asm` rung in the ladder is the hand-written
alternative, and it is in the tree so the comparison can be re-made.

## Reproduce

```sh
make clean && make -j$(nproc)
make gate
taskset -c 2 ./bench/bench_znver2 --impl fast --verify
taskset -c 2 ./bench/bench_znver2 --impl glibc,fast --sizes 16,32,64,512,1048576 \
    --reps 5 --iters 4000
```

Numbers depend on the host, the governor and the neighbour load, so a fresh
run on a busy host will not reproduce them exactly. The **committed CSVs in
[`docs/data/`](docs/data/) are the frozen record**: the reference campaign's raw
window logs are committed under `docs/data/raw/`, and

```sh
python3 scripts/phase7_4_summary.py       # rebuilds docs/data/phase7_4_summary.csv
python3 scripts/phase8_2_summary.py p82   # rebuilds docs/data/phase8_2_summary.csv
sh scripts/analyze.sh docs/data/raw/phase7_p74_znver2.windows.log fast f4_baseline znver2 memcpy
python3 scripts/check_published.py        # re-checks every published delta against the CSVs
```

regenerates and re-checks them with the repo's own analyser. The phase-2
ladder rows are the older campaign, whose record is the committed CSVs in
`docs/data/`; the raw window logs behind six of the seven rungs are committed
too, under `docs/data/raw/ladder/`, and the three remaining CSVs
(`phase2_avx2_pf2`, `phase2_avx2_pf4`, `phase2_sse2_longwin`) are record-only —
they are not published in any table. The candidate kernels that lost stayed on
the machine that ran the campaign, by design. The public tree is the curated
subset: the two shipped kernels, the seven-rung ladder, and one experiment that
lost.

## What this is NOT

* **Not a drop-in replacement for glibc in general use.** glibc is level with us
  from 1 KiB to 64 KiB — at 16 KiB `memcpy_fast` is a tie and `memcpy_mid` is
  6–7% ahead — and glibc is ported, tested and tuned for far more silicon
  than this.
* **Not portable.** The kernels require AVX2 (the ladder includes an SSE2 rung,
  and the reference is scalar, but the shipped kernel is AVX2). The certified
  numbers come from one Zen 2 host; a different core may not reproduce them.
* **Not a claim about your workload.** Benchmark first: these are per-size
  floors on one machine, not a general speedup, and the losses are as real as the
  wins. The floors are also the *best case* for a size-specialised kernel: each
  size is measured on its own, in a tight loop, on a warm cache, single
  threaded, with the branch predictor trained on that one size. A workload that
  mixes sizes pays a mispredict and a cache miss per call. We have not measured
  that case, and it is where the margin would shrink first.

## What the project proves

That a small, hand-written AVX2 `memcpy` can beat glibc at seven sizes on one
host, and — more usefully — that the *size* of that claim is knowable. Every
number here comes from a committed window log, every candidate was measured
against the kernel it would replace in the same windows, and the two-tier
claim model (a floor and a median, with a null band between them) is what
separates the seven real wins from the three sizes that are a tie, a lottery
or unmeasurable. The failures are documented with the same rigour as the wins,
and nothing was retracted along the way.

## License

MIT — see [LICENSE](LICENSE).
