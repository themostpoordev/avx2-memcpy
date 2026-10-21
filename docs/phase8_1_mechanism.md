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

### Q1 — the 1 MiB distribution

Per-rep minima, 45 reps per impl per target, from
`docs/data/raw/phase7_p74_<target>.windows.log`. The full per-rep listing is
reproduced by `python3 scripts/phase8_1_dist.py`; every distinct value and its
count is in the log.

**znver2, the deliverable (`fast`), 1 MiB — two clusters:**

| cluster | reps | values (ns) |
|---|--:|---|
| fast | **12 / 45** | 23053, 23164, 23865, 23975, 24225, 24316, 24506, 24606, 24787, 25027, 25699, 25819 |
| slow | 33 / 45 | 27361 … 28884 |

**glibc, same windows, same sizes:** *no* fast cluster. Its 45 minima run
26370 → 28975 with a largest gap of **491 ns (1.9% of its minimum)**; ours has
a gap of **1662 ns (7.2%)**. znver3 and v3 are the same shape (our gap 1812 and
2775 ns; glibc's 269 and 371 ns).

**The per-rep MEDIAN is ~30,000 ns in every rep of every impl** (28283–33653) —
the fast cluster is a *tail* event inside a rep, not a state the rep sits in: in
a rep whose minimum is 23053 the median is 30236.

**The floor is a best-of-~70,000 statistic.** At 1 MiB `batch = 1`, so one
iteration is one 1 MiB copy, and the harness's 50 ms budget truncates the case:
`iters` ran 514–1639 of the requested 4000 (`bench.c:392`). So the published
`min_of_mins` is the luckiest of roughly seventy thousand copies — for glibc
too (its floor, 26370 ns, is the same kind of statistic).

### Q2 — host-driven or kernel-driven?

**Kernel-driven.** Three independent pieces of evidence:

1. **glibc never reaches the fast cluster.** Across 45 reps on each target,
   glibc's minimum never goes below 26,370 ns. Our fast cluster appears in
   12/45 (znver2) reps and in **0** glibc reps: the per-rep slow/fast flags
   agree in 2 reps, differ in 31 with only ours slow, and in **0** with only
   glibc slow (`scripts/phase8_1_dist.py`).
2. **It is not a time-local burst.** The slow reps are scattered across the
   campaign, not contiguous (unlike the 512 B host event Phase 7.3a found).
3. **It does not track other sizes' modes.** The 1 MiB fast-mode flag agrees
   with the 256 B / 512 B / 1 KiB / 4 KiB / 16 KiB / 64 KiB / 256 KiB flags in
   only 4–12 of 45 reps, at or below chance
   (`scripts/phase8_1_xsize.py`). A global "the host was in a good state"
   event would correlate.

**It also predates the promote:** `f4_baseline` — the pre-promote kernel, whose
`.text` is the shipped F4's — shows the same fast cluster (23154, 23283,
23905, 24225, 24526 ns in 9/45 reps). So this is not a Phase 7.4 artefact.

**The one caveat:** *whether* a given rep gets the fast mode is host state (the
2 MiB working set's residency), but *only our kernel can get it at all*. The
kernel difference is the software prefetch; the host difference decides whether
the gift is collected in a given rep.

### Q3 — what in the kernel at 1 MiB could produce two modes?

Ranked **by evidence**:

1. **The software prefetch.** It is the only structural difference between our
   1 MiB path and glibc's: our main loop issues `prefetcht0` on both streams
   (`build/znver2/src/fast.o`, +0x505 / +0x50f), gated by `TUNED_PF_MIN = 65536`
   so it is active at 1 MiB, at `TUNED_PF_BYTES = 128` — 128 B ahead, one
   iteration. The fast cluster is exclusive to the three kernels that have it
   (`fast`, `f4_baseline`, `f4win64al`) and absent from all three that do not
   (`glibc`, `ref_scalar`, `naive`). Ranked first on that evidence alone.
2. **Cache/TLB residency — a bimodal per-copy time needs a state change.**
   The working set at 1 MiB is 2 MiB (1 MiB read + 1 MiB written), far past the
   512 KB L2 and inside the shared 32 MB L3. The measured bandwidth degrades
   exactly as the working set outgrows the cache: ~100 GB/s at 64 KiB, ~90 at
   256 KiB, ~70 at 1 MiB, ~60 at 4 MiB. The fast mode's 91 GB/s is the
   L3-resident case and the slow mode's 76 GB/s the rest. **This hypothesis
   cannot be tested on this host** — it exposes no L3/LLC events, which
   [NEGATIVE_RESULTS.md](NEGATIVE_RESULTS.md) already records for the NT-store
   investigation. It is ranked second because it is the only explanation for
   *bimodality* as opposed to a shifted mean, and it is untestable here.
3. **The aligned main loop and unroll.** `TUNED_ALIGNED = 1` (the harness's
   buffers are 64 B aligned, so the `vmovdqa` path is always taken) and unroll
   4. Not separable from (1) with the committed data: the pre-promote F4 shares
   both.
4. **Prefetch distance as the limiter.** 128 B of lookahead covers ~1.4 ns at
   the fast mode's 91 GB/s, against a DRAM latency of tens of ns. That is
   short — but the data does **not** show the distance to be the limiter,
   because the fast cluster already exists *with* a 128 B distance. It is a
   hypothesis for attempt 1, not a finding.
5. **NT store path selection — RULED OUT by inspection.** The shipped kernel
   has no NT store path at all; NT stores are a separate ladder rung
   (`src/rungs/avx2_nt.c`) and they lost by 69–372%
   ([NEGATIVE_RESULTS.md](NEGATIVE_RESULTS.md)).

**What evidence would distinguish the survivors:** L3 hit/miss counters on the
1 MiB stream (this host has none) would separate (1)/(4) from (2) directly —
if the fast cluster is L3-residency, it would appear in glibc too, and it does
not; if it is the prefetch's doing, a variant with a different distance would
move it.

### Q4 — a construction that eliminates the slow mode without changing the fast mode?

**Yes, one candidate, stated as a hypothesis the attempt-1 measurement tests:**
a **size-gated longer prefetch distance** for the 1 MiB band.

The published symptom is a *median* problem, not a floor problem: the floor is
already a win (+12.58% / +11.10% / +9.42%) and the fast cluster already
exists. A longer prefetch distance would not touch the floor — the fast mode's
data is already in cache — but it would let the *typical* copy reach the same
91 GB/s, which is what moves the median.

**It must be size-gated, not a global `TUNED_PF_BYTES` change.** The prefetch
is live for every n ≥ 64 KiB, so a global change would also move the 64 KiB and
256 KiB rows — which the Phase 8.3 P1 gate does **not** cover (it covers
8/16/32/64/128/256/512 B and 16 KiB) — and would touch 4 MiB, which is out of
scope. A new size-gated knob avoids both.

**This is a hypothesis, not a proven construction.** What the committed data
does *not* establish: that the 128 B distance is what holds the typical copy
at 76 GB/s. The fast cluster exists with the current distance, and the
L3-residency confound cannot be tested here. If attempt 1 fails, there is
**no second mechanism available** — NT stores are already measured and lost,
and the prefetch distance is the only lever the 1 MiB path has. So 1 MiB has
effectively **one** attempt, and a failure closes it as parity.

## VERDICT: **VARIANT VIABLE**

Q2 identifies a kernel cause (the fast cluster is exclusive to the
prefetching kernels, predates the promote, and does not correlate across
sizes), and Q4 states a construction — a size-gated longer prefetch distance
for the 1 MiB band — that targets the median without touching the floor.

**Phase 8.3 is therefore a candidate**, to be run after Phase 8.2, under its
own pre-registration (P1 certified rows unchanged, P2 the 1 MiB floor
preserved at ≥ +10%, P3 the 1 MiB median moved to ≥ +1.5%).

Two things the candidate inherits, stated here so they are not discovered
later:

* the L3-residency hypothesis **cannot be tested on this host** (no L3/LLC
  events), so a failed attempt will not tell us *why* — only that the
  distance was not the limiter;
* there is **one** attempt, not two: the only other lever (NT stores) was
  measured and lost by 69–372%.

## Consequences for the row, if the attempt fails

The 1 MiB row stays as published: a **win at the floor** (+12.58 / +11.10 /
+9.42%) and **parity at the median** (−1.96 / −2.02 / −2.29%). The floor is
a best-of-~70,000-copies statistic and the median is what a caller doing one
1 MiB copy actually sees. Both are stated, and neither is a claim beyond the
model's own terms.
