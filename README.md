# avx2-memcpy — from-scratch memcpy/memset, measured against glibc

A performance-first memcpy/memset library: C11 kernels, clang-integrated
assembly for hot loops where justified, a purpose-built benchmark harness, and
hardware-counter evidence produced by our own `perf_event_open(2)` wrapper (the
`perf` CLI is not installed here and no packages may be installed).

Every claim in this repo must be reproducible from a command in this file.

## Status

| Phase | Content | State |
|---|---|---|
| 0 | Recon: silicon fingerprint, toolchain, PMU, huge pages, glibc baseline | done |
| 1 | Repo skeleton, 3-target Makefile, benchmark harness, own PMU wrapper | done |
| 2 | *(merged into 1)* harness validated end-to-end | done |
| 3 | Kernel ladder: naive → word → sse2 → avx2 → avx2_nt → asm → rep, memset ladder | not started |
| 4 | Hardware-counter passes per kernel, IPC / miss rates | plumbing ready (`--pmu`) |
| 5 | `results/docs/ANALYSIS.md`, SVG figures, glibc comparison, verdict | not started |

## Build

```sh
make                      # bench/bench_znver2, bench_znver3, bench_v3 (static)
make TARGET=znver2 single # just one
make memcpy               # only the memcpy rungs, all three targets
make memset               # only the memset rungs
make gate                 # audit + audit-width: mandatory before any benchmark
make campaign NAME=phase2_6_run ARGS='tag impls target'   # one measured campaign
make report NAME=phase2_k6_report                         # regenerate a report
make snapshot             # freeze current CSVs into results/committed/
make audit                # per-target vectorisation audit of src/*.o
make objdump              # build/<t>/disasm.txt for codegen diffing
make perf                 # scripts/measure/run_perf.sh (PMU validation pass)
make plot                 # plotting toolchain status
make clean
```

Tree: `src/memcpy/` and `src/memset/` hold the rungs, `src/` holds `common.h`,
`registry.c` and the generated `routing_table.h`; `scripts/{campaign,analyze,
report,verify,gen,measure}/`; `results/committed/` (frozen CSVs),
`results/docs/` (protocol, deliverable, analysis, codegen diffs),
`results/live/` (current run) and `results/archive/` (superseded).

Three codegen targets exist on purpose: the silicon is *not* Zen 3 (see below),
so "which tuning target wins" is a deliverable, not an assumption.

| target | flags |
|---|---|
| `znver2` | `-march=znver2 -mtune=znver2 -mavx2 -mfma` |
| `znver3` | `-march=znver3 -mtune=znver3 -mavx2 -mfma` |
| `v3` | `-march=x86-64-v3 -mtune=generic` |

All targets share `-std=c11 -O3 -Wall -Wextra -fno-builtin-memcpy
-fno-builtin-memset -fno-builtin-memmove` (we are reimplementing those
functions; the compiler must not fold our kernels into libc calls).

## Harness usage

```sh
taskset -c 2 ./bench/bench_znver2                       # full CSV sweep
taskset -c 2 ./bench/bench_znver2 --quick               # 6 sizes
taskset -c 2 ./bench/bench_znver2 --impl ref_scalar
taskset -c 2 ./bench/bench_znver2 --one <impl> <op> <size>   # one CSV row
taskset -c 2 ./bench/bench_znver2 --pmu <impl> <op> <size>   # all counters
taskset -c 2 ./bench/bench_znver2 --align [size]        # src/dst offset sweep
taskset -c 2 ./bench/bench_znver2 --align2d [size] [step]
taskset -c 2 ./bench/bench_znver2 --calib               # clock domains
taskset -c 2 ./bench/bench_znver2 --verify              # correctness only
taskset -c 2 ./bench/bench_znver2 --list
```

CSV goes to stdout, diagnostics to stderr. Columns:

```
impl,target,op,size_bytes,batch,iters,median_ns,min_ns,p99_ns,
gbps,gbps_wall,cycles_per_byte,tsc_ticks_per_byte,tsc_ghz,core_ghz,cycles_total
```

`gbps` is `bytes / median_ns` in units of 10^9 B/s. `cycles_per_byte` comes
from the PMU cycles counter only; `tsc_ticks_per_byte` is reported alongside so
the TSC/core skew stays visible instead of being hidden (`cycles_per_byte` is
`-1` when the PMU cannot be trusted).

## Methodology

1. **Kernels are opaque calls.** Every kernel lives in its own translation unit
   and is reached through a `volatile` function pointer, so it can never be
   inlined, folded, or deleted. No per-iteration sink is needed (which would tax
   the timed region); the destination is checksummed once, after timing.
2. **Two clock domains, never mixed.** Wall = `CLOCK_MONOTONIC_RAW`;
   core = PMU cycles. `--calib` reports both and the harness warns if the
   core/TSC ratio is implausible (a guard rail against reading the wrong PMU
   field — see "Findings" below).
3. **Small sizes are batched.** `batch = clamp(65536/n, 1, 1024)` calls per
   timed sample, so the ~20 ns `clock_gettime` cost cannot dominate a 16-byte
   measurement. Both clock calls are inside the timed region and the measured
   timer overhead (median of 1001 back-to-back pairs, 40 ns here) is subtracted
   from every sample.
4. **Buffers are allocated once** (max 256 MiB, 64-byte aligned, halved on
   allocation failure) and touched before any measurement, so page faults and
   first-touch costs never land in a timed region. Opt-in `--thp` applies
   `MADV_HUGEPAGE`.
5. **Warmup** 10 iterations (time-capped at 0.5 s for huge sizes), then up to
   100 timed samples or a 1 s budget, minimum 3, reporting median / min / p99.
6. **`--verify`** checks every kernel against the byte-at-a-time reference for
   every size before it is benchmarked (`all` mode does this automatically).
7. **PMU groups are capped at 4 events** and refused when they do not run
   ~100% of the time. See below.

## Findings that shaped this harness

Recorded because each one invalidated a plausible-looking measurement:

* **The silicon is Zen2-class, not Zen3, and CPUID lies.** CPUID reports
  family 17h model 01h ("AMD EPYC Processor", Naples) under KVM, but measured
  throughput is 2x 256-bit loads/cycle, 1x 256-bit store/cycle, 2x 256-bit
  `vaddps`/cycle, and TSC = 2.7947 GHz — Naples would show ~1x 256-bit
  add/cycle and 128-bit split stores. EPYC 7402-class (Zen 2, 2.8 GHz) fits.
  Hence the `znver2` target. Full evidence: `results/docs/ANALYSIS.md` (Phase 5).
* **`perf_event_open` group reads put the times first.**
  `{nr, time_enabled, time_running, values...}` — not the reverse. Reading it
  backwards returns a *time* where a *count* is expected; here it silently
  under-reported cycles/byte by 2.8x while looking entirely plausible. Caught by
  cross-checking PMU cycles against TSC and wall clock (`.recon/pmudump.c`).
* **This vPMU exposes 4 counters and does not multiplex.** A 5-event group
  reports 0.3-1% running time and all-zero counts; 1-4 event groups run at 100%
  (`.recon/pmugsize.c`). The full counter set is therefore measured in rounds of
  <= 4 events, and each round's running% is printed.
* **`clang -O3 -march=znver2` auto-vectorises a byte loop** into a 4x32B
  `vmovups` loop. Without `SCALAR_SRCS` + `-fno-vectorize -fno-slp-vectorize
  -fno-unroll-loops` in the Makefile, the "byte-at-a-time reference" measured
  30-70 GB/s. `make audit` fails if vector instructions reappear in a
  scalar-designated file.
* **`budget_s` compared against a nanosecond delta** made every case stop after
  the 3-sample minimum. Fixed; `iters` is a CSV column so this cannot hide.

## Host constraints (Phase 0 recon, `/root/avx2-memcpy/.recon/`)

* `perf`, `hyperfine`, `gnuplot`, `nasm` are **not installed**; no packages may
  be installed. Substitutes: our own `perf_event_open` wrapper, the harness
  timers, a pure-C SVG generator (Phase 5), and the clang integrated assembler
  (`.S` files) instead of NASM — `make` rule: `clang -c foo.S -o foo.o`.
* No `cpufreq` sysfs at all: CPU governor cannot be read or set, and turbo
  cannot be disabled. Clock is therefore reported per run, not controlled.
* No huge pages configured (`HugePages_Total=0`), THP is `[madvise]`.
* Python has `numpy` but no `matplotlib`/`svgwrite`, so figures will be
  generated in C, not Python.
* `AVX512F`, `ERMS` and `FSRM` are **absent** from CPUID: `rep movsb` has no
  fast-string path here (relevant to the Phase 3 REP rung).
* Raw L3/LLC events are unavailable; the generic LLC events fail with `ENOENT`.
  Cache-boundary crossings will be inferred from inflection points in the
  GB/s-vs-size curve, and stated as inference, not measurement.

## Layout

```
Makefile                 3 targets, audit, objdump, perf, plot, clean
README.md
src/common.h             alignment macros, kernel prototypes, shared decls
src/ref_scalar.c         byte-at-a-time golden reference (vectorisation disabled)
src/registry.c           implementation table (one row per kernel)
bench/harness.h          impl_t, clock-domain primitives
bench/bench.c            benchmark harness (CSV, align, verify, calib)
bench/pmu.h bench/pmu.c  our own perf_event_open wrapper
scripts/measure/run_perf.sh      PMU validation pass
scripts/report/plot.sh          plotting toolchain status
scripts/plot.gnuplot     retained recipe (gnuplot absent)
results/                 CSVs, logs, ANALYSIS.md, SVG (Phase 5)
.recon/                  Phase 0/1 evidence probes — do not delete
```
