# avx2-memcpy — from-scratch memcpy/memset for Zen 2, measured against glibc

Two copy kernels, seven reference rungs, one benchmark harness, and a measurement
protocol that decides what may be called a win. Everything here is built from
scratch: no libc wrappers around the functions being replaced, no copied glibc
code, and every claim traces to a committed window log in
[`docs/data/raw/`](docs/data/raw/) and the CSV regenerated from it.

## Results at a glance

Versus glibc, same process, pinned core, 9 windows x 5 reps, from the
**committed** reference campaign in [`docs/data/raw/`](docs/data/raw/). Two
statistics per row: **min** (the floor) and **median** (the typical). A row is
called a win only if it clears its floor under both.

**4 certified wins** (every target clears its floor under both statistics)

| size | Δ_min | Δ_median | tier |
|--:|--:|--:|:--|
| 16 B | **+38..45%** | **+37..50%** | B on znver2, A on znver3/v3 |
| 32 B | **+25.4%** | **+25.1%** | B on znver2, A on znver3/v3 |
| 64 B | **+25.1..25.5%** | **+25.1%** | A x3 |
| 512 B | **+15.7..16.8%** | **+15.7..16.8%** | A x3 |

**1 certified loss on all three targets:** 128 B **−10.0%** at the floor and at
the median alike — the price of the one-compare tiny path, disclosed rather than
tuned away.

**Tier-B measured, not claimed:** 256 B **+8.4%** on two of three targets (the
third lands inside its floor) and 1 MiB **+8..11% at the floor, but level to
2.7% behind at the median** on all three.[^record]
4 MiB is **unmeasurable** (21–30% spread between builds sharing two of three
knobs). From 1 KiB to 64 KiB the kernel is level with glibc.

**Nothing is retracted, and no published row is statistic-dependent** — every
number reconstructs from the committed log. A row whose two statistics disagree
would be labeled conditional instead of certified; see
[docs/RESULTS.md](docs/RESULTS.md).

**Opt-in `memcpy_mid`, 1 KiB <= n <= 64 KiB:** 16 KiB **+6..7%** certified on all
three targets.[^record] Outside that size it is level — which is why it is opt-in.

[^record]: **Record-derived.** Certified at measurement time on a quieter host
    window; reproduction today depends on host load. See
    [docs/METHODOLOGY.md §host-variance](docs/METHODOLOGY.md#9-host-variance-the-floor-moves).
    The committed window logs in `docs/data/raw/`, and the CSV regenerated from
    them, are the authoritative record.

Full tables, per-target columns, tier labels and the ladder:
[docs/RESULTS.md](docs/RESULTS.md).

## Quick start

```c
#include "memcpy_fast.h"          /* include/ */

void *p = memcpy_fast(dst, src, n);   /* same contract as memcpy */
```

Build `src/fast.c` with gcc `-O3 -fno-crossjumping` (the flags the certified
numbers were produced with) and link it. `src/fast.c` includes
`src/fast_impl.h`, so compile from the repository root or add `-Isrc`. Nothing
else is required: no allocator, no runtime init, no CPU dispatch.

## Opt-in: `memcpy_mid`

```c
#include "memcpy_mid.h"

/* dst, src must not overlap. In range: the tuned mid-band kernel.
 * Out of range: forwarded to memcpy_fast by one predicted branch. */
void *p = memcpy_mid(dst, src, n);
```

`memcpy_mid` is **not** part of the general memcpy path. It exists because one
band of sizes (1 KiB–64 KiB) rewards a different schedule, and a caller who knows
their workload sits in that band can opt in. General callers should use
`memcpy_fast`. Link `src/mid.c` in addition to `src/fast.c`.

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

The `snapshot` target from the VPS experiment is not included; the frozen CSVs in
`docs/data/` are the record.

## Documentation

| document | what it is |
|--|--|
| [docs/RESULTS.md](docs/RESULTS.md) | every certified number, the ladder, the caveats |
| [docs/METHODOLOGY.md](docs/METHODOLOGY.md) | `min_of_mins`, the two-tier claim model, the width gate, evidence policy, reproducibility gate |
| [docs/NEGATIVE_RESULTS.md](docs/NEGATIVE_RESULTS.md) | what was tried and lost, with the mechanism for each |
| [experiments/README.md](experiments/README.md) | the documented-loss dispatcher, shipped so it can be verified |

## Reproduce

```sh
make clean && make -j$(nproc)
make gate
taskset -c 2 ./bench/bench_znver2 --impl fast --verify
taskset -c 2 ./bench/bench_znver2 --impl glibc,fast --sizes 16,32,64,512,1048576 \
    --reps 5 --iters 4000
```

Numbers depend on the host, the governor and the neighbour load. The shipped
kernel's published rows **are** regenerable from this tree: the reference
campaign's raw window logs are committed under `docs/data/raw/`, and

```sh
python3 scripts/f128b_summary.py     # rebuilds docs/data/f128b_reference_summary.csv
sh scripts/analyze.sh docs/data/raw/f128b_znver2.windows.log \
   avx2_final_f4_win avx2_final znver2 memcpy
```

reproduces them with the repo's own analyser. The phase-2 ladder rows are the
older campaign, whose record is the committed CSVs in `docs/data/`; the raw logs
behind those and most candidate kernels stayed on the machine that ran the
campaign, by design. The public tree is the curated subset: the two shipped
kernels, the seven-rung ladder, and one experiment that lost.

## License

MIT — see [LICENSE](LICENSE).

## What this is NOT

* **Not a drop-in replacement for glibc in general use.** glibc is faster at
  128 B, level with us through most of 1 KiB–64 KiB, and it is ported, tested and
  tuned for far more silicon than this.
* **Not portable.** The kernels require AVX2 (the ladder includes an SSE2 rung,
  and the reference is scalar, but the shipped kernel is AVX2). The certified
  numbers come from one Zen 2 host; a different core may not reproduce them.
* **Not a claim about your workload.** Benchmark first: these are per-size
  floors on one machine, not a general speedup, and the losses are as real as the
  wins.
