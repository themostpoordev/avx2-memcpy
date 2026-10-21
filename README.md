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

**6 certified win sizes** — 16 B, 32 B, 64 B, 128 B, 256 B and 512 B

| size | Δ_min | Δ_median | tier |
|--:|--:|--:|:--|
| 16 B | **+38..50%** | **+27..50%** | B on znver2/znver3, A on v3 |
| 32 B | **+25.1..25.5%** | **+5..25.1%** | A on znver2/v3, B on znver3 |
| 64 B | **+25.1..25.5%** | **+19..25.1%** | A x3 |
| 128 B | **+10.11%** | **+10..45%** | A x3 |
| 256 B | **+8.41%** | **+0..8.4%** | A x3 |
| 512 B | **+14.59%** | **+6..13%** | B on znver2, A on znver3/v3 |

**128 B is a new win** — it was a certified loss of −10.0% and is now ahead of
glibc at that size for the first time, +10.11% on every target. **128 B and
512 B clear their floor on two of the three targets** (on the third the row
reads `measured`); the delta itself is identical on all three.

**No certified losses.** 16 KiB used to be a certified loss (−2.3..−2.4%);
it is now a **tie** (−1.2..−0.1%), re-certified in a quiet window. The size is
memory-bound and the row follows the host — the promoted kernel is *faster*
than the kernel it replaced there (+1.16 / +0.12 / +1.05%), never slower.

**Tier-B measured, not claimed:** 1 MiB **+9..13% at the floor, but level to
2.3% behind at the median** on all three.[^record] 4 MiB is **unmeasurable**
(99–197% spread between windows). From 1 KiB to 64 KiB the kernel is level with
glibc.

**8 B is now reported too** (it is not on the 13-point ladder, but the shape
change moves that class): **+25..39%** at the floor, a win on all three.

**Nothing is retracted** — every published number reconstructs from the
committed log. A row whose two statistics disagree would be labeled conditional
instead of certified; see [docs/RESULTS.md](docs/RESULTS.md).

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

Build `src/fast.c` with gcc `-O3 -fno-crossjumping -falign-labels=64` (the
flags the certified numbers were produced with) and link it. `src/fast.c`
includes `src/fast_impl.h`, so compile from the repository root or add `-Isrc`.
**Do not drop `-falign-labels=64`**: without it the 32..64 B path loses the
64-byte alignment of its out-of-line branch target and that costs 16% at 32 B
and 64 B — two of the certified wins. Nothing else is required: no allocator,
no runtime init, no CPU dispatch.

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
