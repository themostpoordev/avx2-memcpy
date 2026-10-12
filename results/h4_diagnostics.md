# H1-H4 diagnostics: why was the SSE2 rung UNSTABLE at 11-12 of 13 sizes?

All runs pinned `taskset -c 2`, 9 windows, `migrated=0` throughout. Requested
before Kernel 4 because, if instability were a property of vectorised kernels,
every AVX2 claim would be Tier B. It is not.

## H1 - alignment branch mispredictions: REFUTED

* The kernel contains **no alignment branch at all** - it uses
  `_mm_loadu_si128` / `_mm_storeu_si128` unconditionally, so there is no
  alignment test to mispredict. Whole-TU count of `0xf` mask tests: **0**.
* The harness measures at **offset 0 with `posix_memalign(..., CACHELINE=64, ...)`
  buffers**, i.e. every measured copy was already fully 64-byte aligned.
* Nothing about alignment changes between windows, so it cannot produce a
  window-to-window spread.

## H2 - fast-path crossover inside the range: NOT SUPPORTED

Switch is at `n < 32` (asm: `cmp $0x20,%rdx`). Spread by size (znver2, 9 windows):

| size | path | spread |
|--:|:--|--:|
| 16 | byte | 19.69% |
| 24 | byte | 23.83% |
| 31 | byte | 20.30% |
| **32** | **sse (the switch)** | **2.50%** |
| 33 | sse | 22.30% |
| 48 | sse | 12.57% |
| 64 | sse | 30.20% |

The size sitting exactly on the crossover is the **most stable of the eleven**.
The instability is not localised at the switch.

## H3 - L1 contention sensitivity: NOT SUPPORTED as stated

8 KB (L1) 43.69% vs 1 MB (L2/L3) 4.91% superficially fits, but H5 below shows the
difference tracks window length, not cache tier. Within L1 alone the spread spans
2.50% (32 B) to 48.91% (16 KB), and 4 KB - also L1 - is 11.78%.

## H4 - codegen after the AVX->SSE2 fix: VERIFIED

Same source compiled twice, whole translation unit (not just the kernel symbol):

| build | insns | VEX-prefixed | ymm refs | 16-byte ops | calls |
|:--|--:|--:|--:|--:|--:|
| pre-fix (target flags) | 51 | 7 | 4 | 6 | 0 |
| post-fix (current make) | 54 | **0** | **0** | 10 (`movups`) | 0 |

The fix covers the entire TU. No unexpected differences beyond the intended
widening of the hot loop's instruction form back to 128-bit.

## H5 - discovered while answering H1-H3: the timed window is too short

**CONFIRMED.** Same size, same windows, same pinning, only the iteration count
changed:

| size | spread @ iters=100 | spread @ iters=4000 |
|--:|--:|--:|
| 8 KB | 43.69% | **0.75%** |
| 16 KB | 48.91% | **1.48%** |
| 64 KB | n/a | **0.68%** |

Root cause: `run_case()` stops at `iters_max` (default 100) regardless of elapsed
time, so a fast kernel's per-rep window can be ~0.1 ms while a slow kernel's is
~7 ms. A short window samples few interference events, so its per-window minimum
moves more. This also explains why the *slower* rungs looked more stable at the
same size - their windows are longer, not their memory access better.

**Consequences.**
1. The instability is an **instrument setting, not a kernel property**, so
   Kernel 4 does not inherit it merely by being vectorised.
2. Kernel 4 is therefore measured with the window driven by the time budget
   rather than the iteration cap (`ITERS=4000`, `BUDGET=0.05 s`): each case
   samples `min(4000, what 50 ms buys)` - 50 ms at every size where 4000 samples
   take longer than that, and 9-40 ms at the smallest sizes - against ~0.1 ms in
   K1-K3. An earlier draft of this line said `BUDGET=5 s`; that figure was never
   used and could not have bound, because 4000 samples finish well inside 5 s at
   every size in this ladder, so the sample cap would have decided every window.
   The campaign log header now records the settings, so this cannot recur.
3. This makes claims **stricter, not looser**: with the corrected window most
   sizes become Tier A, whose floor is `max(1.5%, null band)` - much tighter than
   Tier B's flat 25%.
4. The Tier B labels on Kernel 3 (and the amendment-1-vs-2 conflict that prompted
   the two-tier model) are mostly artefacts of the 100-iteration cap. Kernel 3
   should be re-measured on the corrected window; Kernel 4's campaign does this
   for free, because `sse2` is its `prev`.

---

**Footnote (added with the audit upgrade, commit `phase2: audit-width gate`):**
H5 log lost; its `iters`/`budget` are unrecoverable. The surviving evidence is
`results/phase2_sse2long_znver2.csv` (and the `sse2diag`/`sse2diag2` CSVs next to
it) plus this file. The instrument settings that would have made the log
self-describing were added to `mo_measure.sh` afterwards — see commit `6a61b1c`,
"fix measurement log header omits the window settings" — and the no-evidence-
outside-the-repo rule that would have prevented the loss is now section 9 of
`results/MEASUREMENT_PROTOCOL.md`.
