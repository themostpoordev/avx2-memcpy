# Phase 3, Task 3 — cleanup

Rule applied: **move, do not delete, on the first pass**. Nothing was deleted
from `results/` in this task. The only deletions are `/tmp` scratch, where the
task allows outright removal because the files are not evidence and are not in
the repo.

## Files moved: `results/live/` → `results/archive/` (16 files, 3.84 MB)

Archived because each is superseded by a committed CSV or by a later campaign,
and none is referenced by the deliverable, the protocol or any phase report.

| file | bytes | md5 |
|---|---:|---|
| phase2_avx2_shortwin_v3.csv | 4723 | ae2a7f64d770de69f31b84707aebb3a5 |
| phase2_avx2_shortwin_znver2.csv | 4878 | 0f9550fc1121cd316054b068eb8f2d60 |
| phase2_avx2_shortwin_znver3.csv | 4940 | 892165bc43817039c894352bc9f391ee |
| phase2_avx2_shortwin_v3.windows.log | 1122292 | 6c2ad309ec62930f99b2d1ce86508768 |
| phase2_avx2_shortwin_znver2.windows.log | 1127308 | 070e8d95428ae841e06771e09fd19795 |
| phase2_avx2_shortwin_znver3.windows.log | 1126139 | 9e50b40c7277da98dfcfea4cc94348f8 |
| phase2_avx2_pf2_shortwin_v3.csv | 4990 | 9429f5abc022c33439d66fa857686789 |
| phase2_avx2_pf2_shortwin_znver2.csv | 5171 | f59841154221e0694b39c1279fca7ce8 |
| phase2_avx2_pf2_shortwin_znver3.csv | 5250 | 18dcbfc8063564d43af3d4e9241c1006 |
| phase2_avx2_pf4_shortwin_v3.csv | 5021 | 6f6affa5e2a6f751d8ece11659558791 |
| phase2_avx2_pf4_shortwin_znver2.csv | 5162 | 6e03ce88b0241af134c3d69b7adc74a8 |
| phase2_avx2_pf4_shortwin_znver3.csv | 5273 | 37bd686e7a5dac1600d32520a3bfa0c5 |
| phase2_avx2_budget5s_znver2.windows.log | 379714 | b48a1366fa1ea4c328e4e28e8866757b |
| phase2_naive_v3.median_based.csv | 10501 | 812a6fbe1f3e50f460f6d2ec24f81a2b |
| phase2_naive_znver3.median_based.csv | 10819 | 8f6480b9382bf902cfdcc640cd6fb598 |
| phase2_6_p3disp_progress.log | 70267 | 6af5c666d441bf3aa42d6d28e598f269 |

Why each group:

- **`*_shortwin_*`** (9 CSVs, 3 window logs, 3.4 MB): the 64 B-512 B window
  campaigns from Phase 2.6. Superseded by the committed whole-ladder CSVs and by
  the final campaigns; the sizes they cover are certified from the full ladder.
- **`*_budget5s_*`**: a 5-second-budget screen. The frozen protocol is
  `RUN_TO_CAP=1` x 9 windows; a budget-capped run cannot produce a certified row,
  so this log cannot support a claim either way. Not evidence.
- **`*.median_based.csv`** (naive, two targets): the pre-protocol median-based
  analysis, superseded by the committed min_of_mins CSVs. The snake-pit file:
  a median-based row is exactly the artefact the protocol exists to forbid.
- **`phase2_6_p3disp_progress.log`**: the campaign that was killed when its gate
  failed (the dispatcher TU had no declared width). Its numbers were never used;
  `p3disp2` is the restarted, complete run and is what the Task 1 report cites.
  The archive keeps the killed run's progress log as the record of the restart.

## Files deleted (proof)

**None from `results/`.** Every archived file was checked for redundancy first:

- duplicate md5 **within** `results/archive/`: none (16 distinct hashes);
- md5 identical to a **committed** CSV: none;
- duplicate md5 **within** `results/live/`: none (checked across all 178 files).

With no provable duplicate, the first-pass rule forbids deleting anything from
the archive, and nothing was deleted.

`/tmp` scratch, deleted outright (not evidence, outside the repo): 28 files plus
the `pairwork/` directory, 0.93 MB total - build logs, per-campaign run outputs,
mid-range `clang -S` dumps, the EVEX probe object, pair-analyser intermediates.

## Disk

| | before | after |
|---|---:|---:|
| `results/live/` | 64 MB (194 files) | 61 MB (178 files) |
| `results/archive/` | 0 | 3.84 MB (16 files) |
| `results/` total | 66 MB | 66 MB |
| `/tmp` project scratch | 0.93 MB | 0 |
| repo working tree | 84 MB | 84 MB |

The repo does not shrink, by design: a move is not a delete, and `results/live/`
and `results/archive/` are both gitignored so neither costs history.

## Verification after the move

1. `make gate` → PASS (`audit` + `audit_width`, all three targets).
2. **Committed CSVs unchanged.** All 30 files under `results/committed/`
   compared md5-for-md5 against the same files as committed at `HEAD`: 30
   checked, 0 mismatches. `git diff HEAD -- results/committed results/docs` is
   empty.
3. **Analysis still reproduces.** `mo_analyze.sh` re-run on the Kernel 6
   campaign log produces `results/live/phase2_asm_znver2.csv`, `cmp`-identical
   to `results/committed/phase2_asm_znver2.csv`.
4. **Working tree clean** (`git status` empty): all 16 archived files and all
   deleted scratch were untracked or ignored, so cleanup touched no tracked file.

## Not touched, as instructed

`results/committed/*` (verified by md5 above), `results/docs/*`
(`MEASUREMENT_PROTOCOL.md`, the Phase 2.6 deliverable, `phase2_6_SUMMARY.md`,
the phase summaries, `analysis/`, `codegen/`), `.recon/*.c`, every codegen diff,
`src/`, `bench/`, `Makefile`. `HYBRID_FINDINGS.md` does not exist anywhere in
this working tree (`find . -name HYBRID_FINDINGS.md` returns nothing), so there
was nothing to spare - it was neither moved nor deleted by this task.
