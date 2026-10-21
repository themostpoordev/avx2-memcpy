# Phase 8.1 — 1 MiB: mechanism analysis (pre-registered)

**PRE-REGISTRATION.** Written and committed **before** any Phase 8.1 analysis
was performed, and specifically before looking at the 1 MiB per-rep
distributions. The question, the questions and the verdict rule below are
fixed; the answers are filled in afterwards, in a later commit, and the verdict
rule is not restated after seeing them.

## Why this sub-phase exists

Phase 7 closed at 6 certified win sizes (8/16/32/64/128/256/512 B), 1
host-limited loss (16 KiB), 1 conditional (256 B at the median) and 1 parity
(1 MiB at the median). Phase 8 attacks the three rows that are not certified
wins.

1 MiB is first because **it is the only sub-phase that does not depend on host
state**: it reads the committed Phase 7.4 log and the disassembly of the
current deliverable. Its output — viable or structural — decides whether
Phase 8.3 exists at all. If the answer is structural, 2–3 days are saved.

## The row under investigation

From the committed reference campaign
(`docs/data/raw/phase7_p74_<target>.windows.log`, summarised in
`docs/data/phase7_4_summary.csv`):

| size | Δ_min (znver2 / znver3 / v3) | Δ_med (znver2 / znver3 / v3) | spread% | tier |
|---|--:|--:|--:|:--:|
| 1 MiB | +12.58 / +11.10 / +9.42% | −1.96 / −2.02 / −2.29% | 11.3 / 15.9 / 6.1% | A / B / B |

The row is bimodal: the floor is a win, the median is parity. Two readings are
possible and they lead to opposite decisions:

* if the slow mode is a **host** event, the row is a certified win waiting to
  be recognised, and no kernel change is warranted;
* if the slow mode is a **kernel** property, a variant might eliminate it and
  turn the median into a win too.

## Questions

**Q1. Distribution at 1 MiB.** Report the full per-rep distribution for the
deliverable and for glibc at 1 MiB on all three targets: every distinct batch
value and its count, not just min and median. Identify the fast mode and the
slow mode in ns, and the percentage of reps in each.

**Q2. Is the mode split host-driven or kernel-driven?** Check:

* whether glibc's own 1 MiB samples **in the same windows** show the same mode
  split at the same moments;
* whether the slow reps are **contiguous in time** (a host event) or
  interleaved (a kernel property);
* whether **other sizes in the same windows** show the slow mode at the same
  moments.

If the mode split correlates with glibc's and with other sizes' slow windows,
it is host. If it is unique to our 1 MiB path, it is kernel.

**Q3. If kernel-driven: what in the kernel at 1 MiB could produce two
modes?** Rank the candidates **by evidence, not by plausibility**:

* prefetch distance hit or miss — check `TUNED_PF_MIN` and `TUNED_PF_BYTES`
  against 1 MiB, and whether the prefetch stream covers the full loop;
* NT store path selection — does the kernel switch to NT above a size
  threshold near 1 MiB;
* L2/L3 residency — 1 MiB is exactly L3 size on this host; does the mode
  correlate with which cache level the source or destination falls into;
* page boundary crossing — 1 MiB is a huge-page multiple; is the destination
  huge-page-backed or 4 KiB-backed in the harness.

State what evidence would distinguish the surviving candidates.

**Q4. Is a variant possible that eliminates the slow mode without touching the
fast mode?** State the specific construction, or state "structural" with the
reason.

## VERDICT RULE (fixed before the analysis)

* **"VARIANT VIABLE"** — Q2 identifies a **kernel** cause AND Q4 states a
  construction that eliminates the slow mode **without changing the fast
  mode**.
* **"STRUCTURAL"** — the slow mode is **host-driven** (Q2), OR Q4 finds no
  construction that does not regress the fast mode.
* **"UNKNOWN"** — Q2 cannot distinguish host from kernel with the committed
  data. This is a valid verdict; a mechanism that has not been isolated will
  not be guessed.

**Consequence of the verdict.** STRUCTURAL or UNKNOWN → 1 MiB is closed as
parity and Phase 8.3 is skipped entirely. VARIANT VIABLE → Phase 8.3 becomes
a candidate, to be run after Phase 8.2.

## Stop conditions for Phase 8 as a whole

Two attempts maximum per size, counting any Phase 7 attempt. No re-rolling
a campaign for a kinder window, no threshold adjustment after seeing results,
no fourth 128 B shape, no touching 4 MiB.

## Answers

*(filled in after the analysis; committed separately)*
