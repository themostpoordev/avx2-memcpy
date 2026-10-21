# Phase 3, Task 2 — project structure and build speed

Two goals: the tree navigable in under 60 seconds, and a full rebuild measurably
faster. The first is met. The second is not, and the measurement below says why
rather than dressing the number up.

## Tree, before

```
src/            51 kernels + 4 impl headers + common.h + registry.c
                + routing_table.h, all in one directory
scripts/        26 files: campaigns, analysers, reports, audits, generators
results/        94 tracked files (5 docs, 21 codegen diffs, 47 matrices and
                analysis texts, 30 frozen CSVs interleaved with everything
                else) plus 192 ignored logs and CSVs, 65 MB in one directory
```

Navigating `src/` meant reading 51 near-identical filenames to find the shipped
kernel; `results/` was a single directory where a frozen record, a codegen diff
and last night's log looked the same.

## Tree, after

```
src/
  memcpy/        45 files: every memcpy_*.c/.S/.h rung, incl. the deliverable
  memset/         6 files: the memset ladder
  perf6/          2 files: -flto and PGO probe TUs (built only by make perf6probe)
  common.h        unchanged
  registry.c      unchanged
  routing_table.h generated, shared by memcpy and tools/route_check.c
bench/            harness (unchanged)
scripts/
  campaign/       5  phase2_6_run, phase2_6_screen, k5/k6/hybrid campaigns
  analyze/        3  mo_analyze, phase2_6_matrix, analyze_phase2
  report/         7  k4/k5/k6/hybrid reports, pairs, plot
  verify/         5  audit_width (+allow list), codegen_diff, asm-vs-c, routecheck
  gen/            1  gen_routing_table
  measure/        5  mo_measure, noise_floor, quiet_window, nt_threshold_probe, run_perf
results/
  committed/     30 frozen CSVs, byte-identical to before (unchanged in this task)
  docs/          77 tracked: protocol, deliverable, phase reports, analysis/*.txt,
                 codegen/*.diff
  live/          current run outputs (gitignored)
  archive/       superseded campaigns (gitignored; populated in Task 3)
```

Moves are `git mv`, so history follows the files (no copy/delete pairs).

## Makefile

Top-level targets are now exactly: `all`, `gate`, `bench`, `snapshot`, `clean`
(plus `TARGET=.. single` for one binary). Scoped: `memcpy`, `memset`,
`campaign NAME=`, `report NAME=`. `make -j` compiles independent TUs in
parallel with no added ordering constraints; the three binaries link in
parallel as independent goals.

The wildcard sets had to learn the new directories —
`src/memcpy/*.c`, `src/memcpy/*.S`, `src/memcpy/*_gcc*.c`, `src/memset/*.c` —
and `INC` gained `-Isrc/memcpy` so the impl headers still resolve from `src/`
and `src/perf6/`. The per-source gcc rules key on `$(notdir ...)`, so the
`GCC_EXTRA_<file>` flag table kept working unchanged.

## Path references fixed

| where | what broke | fix |
|---|---|---|
| Makefile | `$(wildcard src/*.c)` no longer saw the rungs | five wildcard sets moved to the new dirs |
| Makefile | `SCALAR_SRCS`, `SSE2_SRCS` (must stay scalar / must stay 128-bit) | paths moved; `audit` matches on the source path it derives from the object name, which now includes the subdirectory - verified by the gate |
| Makefile | perf6 recipes wrote `results/*.log` | `results/live/` |
| Makefile | `snapshot` read `results/phase2_*.csv` | `results/live/phase2_*.csv` |
| scripts (16 files) | `cd "$(dirname "$0")/.."` is one level short | `/../..` |
| scripts (16 files) | cross-script calls `sh scripts/mo_measure.sh` etc. | `scripts/measure/`, `scripts/analyze/`, `scripts/verify/` |
| scripts | outputs went to `results/phase2_*.{log,csv}` | `results/live/`; analysis and routing texts to `results/docs/analysis/`; codegen diffs to `results/docs/codegen/` |
| audit_width.sh | walked `src/*.c src/*.S` | walks `src/memcpy/* src/memset/* src/perf6/*`; width declarations still keyed on basename |
| docs (6 .md), README | paths in prose | rewritten to the new layout (verified: no `results/<file>` reference remains outside committed/live/docs) |

Historical note: the `.txt` and `.matrix.txt` evidence files were moved but
**not edited**. Their header lines still name the log path as it was when the
tool printed them, which is provenance, not a stale reference.

## Verification

1. `make gate` → `audit` PASS and `audit_width` PASS on all three targets, every
   rung still declaring and compiling its width (PASS after the move, and again
   after the Makefile rewrite).
2. **CSV reproduction, byte-identical.** `mo_analyze.sh` re-run on the Kernel 6
   campaign log from the new paths:
   `results/live/phase2_asm_znver2.csv` `cmp`-identical to
   `results/committed/phase2_asm_znver2.csv`.
3. **Campaign end-to-end through the new dispatcher.**
   `make campaign NAME=phase2_6_run ARGS='restruct glibc,avx2_final_v2_gcc_noxj znver2'`
   completed rc=0 in 57 s and wrote `results/live/phase2_6_restruct_znver2.windows.log`.
   Its matrix reproduces the certified deliverable rows: 16 B 1.6 ns (+37.62 %),
   32 B 2.2 ns (+12.92 %), 64 B 2.2 ns (+12.92 %), 512 B 6.0 ns (+13.47 %),
   1 MiB 23714 ns (+10.44 %). Nothing in the restructure moved the code.

## Build time: measured, not achieved

`make clean && make -j4`, wall clock, 4 cores, same box, same session:

| build | run 1 | run 2 | run 3 | min |
|---|---:|---:|---:|---:|
| before restructure | 27.81 s | 27.89 s | - | 27.81 s |
| after restructure | 25.71 s | 26.33 s | 26.63 s | 25.71 s |
| after, `-pipe` removed (control) | 24.48 s | 26.25 s | - | 24.48 s |

**The full rebuild is not measurably faster, and this report will not claim it
is.** The spread inside one configuration (24.5-28.2 s across all clean builds
taken today) is larger than the difference between configurations. A first pass
added `-pipe` and appeared to win 5 %; the control above shows the win was
noise, so `-pipe` was removed again and the flag set is exactly what the
certified numbers were measured with.

Why there is nothing to win here: the restructure moves files, it does not
remove work. The build is 147 translation units plus three static links, it is
CPU-bound, and `-j4` already saturates the four cores. The only ways to make a
full rebuild faster are to compile fewer rungs (the rungs are the evidence, so
no) or to add a compiler cache (an install, which the standing rules forbid).

What *is* measurably faster is the scoped rebuild the restructure enables:

| command | time |
|---|---:|
| `make -j4 memset` (6 rungs x 3 targets) | 1.7 s |
| `make -j4 memcpy` (41 rungs x 3 targets) | 22.6 s |
| `make -j4` (47 rungs + harness x 3 targets) | 26.4 s |

A memset change now rebuilds in 6 % of the full-build time instead of 100 %.

## Bottom line

Navigation: `src/` went from 51 flat kernel files to two named families plus
three shared files; `scripts/` from 26 flat files to six purpose-named
directories; `results/` from one 65 MB pile to a frozen record, a documentation
tree, a live area and an archive. Build time: unchanged, measured, and reported
as unchanged.
