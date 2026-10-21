# Phase 7.3a — mechanism analysis: why the 512 B row costs 2.70%

Analysis only. No code was written and no measurement was taken in this pass; every
number below is read from an already-committed artefact, and every offset from
`objdump` of the already-built binaries.

**Verdict: TRADE STRUCTURAL.** The 128 B win and the 512 B cost are the *same
resource* — the instruction stream in front of the flat class chain. There is no
shape in the current design that wins 128 B and leaves 512 B alone. The
trade-free shape exists and has already been measured: it turns the certified
128 B loss into a **tie**, and it misses the win by exactly 0.30 ns.

---

## The facts this analysis starts from (from Phase 7.2 / 7.2b)

- 512 B regresses −2.70% against the shipped F4 with the 128 B fix; the 512 B
  path's *instructions* are byte-identical in every build.
- The no-flag build of the same source shows the identical −2.70%
  (`docs/data/raw/phase7_f4al_*.windows.log`, `docs/phase7_2b_results.md`).
- The 512 B cost is a **redistribution between two modes**: fast 741 → 761 ns
  per 128-copy batch (+20 ns), slow 1212 → 1192 ns (−20 ns).

---

## Q1. Why does 512 B land in two modes at all?

**Evidence first.** In `docs/data/raw/phase7_p72b_40000_znver2.windows.log`,
window 8 reads:

```
fast       r0=9   r1=9   r2=9   r3=9   r4=6     (ns per copy)
f4win64    r0=6   r1=9   r2=9   r3=9   r4=6
f4win64al  r0=6   r1=9   r2=9   r3=9   r4=9
glibc      r0=10  r1=7   r2=10  r3=10  r4=10
```

The slow mode is a **contiguous block in time** that spans three different
implementations, and it lands on different reps for each of them. It is not a
property of any kernel.

**The addresses never change.** `bench/bench.c:809` times
`run_case(..., g_dst, g_src, ...)` — the base 64 B-aligned buffers. The random
src/dst offsets (`rng_offset()`, `bench/bench.c:469`) are used only by the
`--verify` pass (`bench/bench.c:508,562`), never by the timed region.

**The byte-at-a-time rungs are immune.** In the same windows `ref_scalar` sits
at 168 ns/copy in both modes and `naive` at 168 ns. A 1 B/store loop is bound by
store throughput (512 stores ≈ 512 cycles ≈ 164 ns) and is therefore insensitive
to whatever the slow state is; the 32 B-move vector copies are not.

### Ranked hypotheses

| hypothesis | verdict | evidence |
|---|---|---|
| **Host interference / shared-VPS state** | **SUPPORTED** | the slow mode is time-local and spans implementations; the store-bound rungs are immune while the 32 B-move copies are not |
| Cache-line boundary crossing at 512 B | **RULED OUT** | `dst` is 64 B-aligned and 512 is a multiple of 64, so no 32 B store ever straddles a line; the addresses never change, so nothing can change |
| Branch-predictor state at the loop entry | **RULED OUT** | the 512 B path is straight-line (32 `vmovdqu`, no loop, no branch); there is no loop to mispredict, and the mode is implementation-independent |
| Store-buffer occupancy entering the loop | not needed | it cannot produce an implementation-independent, time-local event that the store-bound rungs ignore |

**Answer: the two modes are a machine-state event of about 1.6x magnitude on
this shared host, not a property of the kernel.** The 32 B-move vector copies
are sensitive to it; the 1 B/store loops are not. Note the consequence for the
whole project: the "slow mode" is ~470 ns per 128-copy batch of *host* noise, so
the 512 B fast/slow separation is 471 ns — **63% of the fast-mode value**. Any
512 B measurement on this host is dominated by which mode the window happened to
land in.

## Q2. Why do the two modes move in *opposite* directions?

**Measured precisely** (batch totals, ns, 45 reps, iters=40000):

| build | fast mode | slow mode | separation | midpoint |
|---|--:|--:|--:|--:|
| shipped F4 | 741 | 1212/1222 | 472 | ≈981 |
| knob (H1b) | 761/771 | 1192/1202 | 432 | ≈981 |

The two modes' separation shrinks by 40 ns about an unchanged midpoint. A pure
"the code moved" story predicts both modes shift the *same* way; this does not.

**What I can and cannot say.** I can say with certainty that the shift is
*positional*: the two builds that keep the flat class chain exactly where the
shipped F4 has it (same size before the chain, same 512 B path offsets, same
boundary straddles) both measure 741 ns, and the two builds that displace the
chain both measure 761 ns **despite landing at different positions**
(mod 64 = 29 and 13; 32 B straddles 4 and 3; 64 B straddles 1 and 1). The
effect is therefore robust to the exact new position, which rules out a
fine-grained alignment coincidence and points at something coarse about the
displaced code stream.

**What I cannot say.** I have not identified the microarchitectural cause of the
mode split itself, nor of the opposite sign. Three candidate metrics separate
the two groups identically (the knob's presence, the 64 B phase, and the
boundary-straddle count), so with four builds they cannot be told apart. I am
not going to name a mechanism I have not isolated.

## Q3. Is it the 512 B path, or the transition into it?

**Testable without measuring — the offsets.** From `objdump -d bench/bench_znver2`:

| build | entry | 512 B block | dist. from entry | start mod 64 | 32 B straddles | 64 B straddles |
|---|---|---|--:|--:|--:|--:|
| **shipped F4** (`memcpy_fast`) | 0x403c00 | 0x403f10 | 784 | 16 | 6 | 3 |
| **H1a** (`memcpy_avx2_f4win129`) | 0x404d00 | 0x405010 | 784 | 16 | 6 | 3 |
| **H1b** (`memcpy_avx2_f4win64`) | 0x405240 | 0x40541d | 477 | 29 | 4 | 1 |
| **aligned** (`memcpy_f4win64_aligned`) | 0x405a40 | 0x405c4d | 525 | 13 | 3 | 1 |

**It is a property of the 512 B path's position in the code stream, not of the
transition into it.** The transition is identical in all four builds: the same
`cmp $0x200` / `cmp $0x100` dispatch, the same chain, the same
`T_BLOCKPAIR(8)` instructions. What differs is where that path sits.

**The two builds that measure 741 ns are the two whose functions have the same
size in front of the chain.** `build/znver2/src/fast.o` and
`build/znver2/src/rungs/avx2_f4win129.o` have **identical `.text` length
(1301 bytes)**; the diff between them is 184 bytes of *register renumbering plus
one immediate* (`cmp $0x80,%r8` → `lea -0x81(%rdx),%rcx; cmp $0x7f,%rcx`), with
the instruction sequence and every subsequent offset preserved. H1b and the
aligned build have different sizes in front of the chain, so the 512 B path
moves.

**Corroboration:** the 256 B path lives *inside* the window body, and the window
body moved too (mod 64: 0 → 26) in both knob builds — yet 256 B measured
**+0.00%** against F4 in the Phase 7.2 campaign. So the displacement is not a
generic "code after the window got worse" effect; it is specific to the 512 B
path.

## Q4. Is there a shape with the H = 64 pair at n = 128 AND the 512 B path's alignment unchanged?

**Yes — and it is already built and measured. It does not win, and that is the
finding.**

### The trade-free shape

**H1a** (`src/rungs/avx2_f4win129.c`): the window's lower edge moves from 128 to
129, and n = 128 is served by the flat class chain's existing `(64, 128]` class —
which already uses `T_BLOCKPAIR(2)`, the H = 64 pair, and tiles n = 128
*exactly* (4 loads + 4 stores, no redundancy). The change is one immediate in
the window's test, so the function keeps its size and **the 512 B path stays at
distance 784 from the entry, phase 16, straddles 6/3** — every positional
property identical to the shipped F4.

Measured (Phase 7.2 campaign, `docs/data/raw/phase7_f4al_*.windows.log`):

| size | H1a vs F4 | vs glibc |
|---|--:|--:|
| 128 B | **+9.13%** | **+0.00%** — a tie, not a win |
| 512 B | **+0.00%** | +14.59% |
| 32 B / 64 B / 256 B | +0.00% / −0.53% / +0.00% | unchanged |
| 1 MiB | −0.05% | unchanged |

**No trade at all.** The certified 128 B *loss* becomes a *tie*.

### Why it cannot also win

The 0.30 ns that separates H1a's tie from the knob's win is exactly the cost
of reaching the H = 64 pair through the chain instead of through the window:
**four extra compares** (`n <= 512`, `n > 256`, `n > 128`, `n > 64` in the chain,
after the window's test has already failed). At 3.13 GHz that is ~1 cycle of
dependent, perfectly-predicted compares — and it lands 128 B at 3.10 ns against
glibc's 3.11 ns.

And a shape that *does* dispatch to the H = 64 pair in one compare must put a
branch **in the window body**, which by construction precedes the flat class
chain. That branch grows the code in front of the chain, which displaces the
512 B path, which costs 2.70%. There is no third option: the window is the fast
dispatch precisely because it is *before* the chain, and anything added to it
moves the chain.

### Verdict on Q4

**The trade is structural in the current design.** The 128 B win and the 512 B
cost are the same resource — the instruction stream in front of the flat class
chain:

- **Keep the chain** (the 512 B path exactly where the shipped F4 has it) and
  the H = 64 pair is only reachable through the chain's dispatch: 4 extra
  compares, **128 B lands at parity with glibc**, a tie, not a win.
- **Win 128 B** with a one-compare dispatch to the H = 64 pair and the branch
  has to live in the window body: the chain moves, the 512 B path moves with it,
  and **512 B costs 2.70%** — 20 ns per 128-copy batch, 0.156 ns per copy,
  about half a core cycle per copy.

The measured cost of the 4 extra compares (0.30 ns) and the measured cost of
the chain's displacement (0.156 ns/copy, 2.70%) are separate quantities; the
first is the price of *not* winning, the second is the price of winning. There
is no shape that avoids both.

---

## What this does to the plan

Per the brief, a TRADE STRUCTURAL verdict stops the phase: **no variant is
attempted, and the attempt budget is untouched.**

Two things are worth putting in front of the decision-maker, because they were
not visible before this pass:

1. **The trade-free shape is real, cheap and already measured.** H1a turns the
   certified 128 B loss into a tie at **zero cost anywhere else** — 512 B, 256 B,
   64 B, 32 B and 1 MiB are all unchanged. It misses the win by 0.30 ns, and it
   was ruled out earlier on the grounds that "V9c's history says it is fragile"
   (four extra compares) and by Phase 7.3b's A1 threshold of +10% against F4
   (H1a delivers +9.13%). If a tie at 128 B with no trade is worth more than a
   win at 128 B with a 2.70% cost at 512 B, H1a is the answer and it needs no
   further experiment. If not, the trade stands and the decision is a
   prioritisation call, not an engineering one.

2. **The 512 B measurement on this host is weak, independently of any of
   this.** The fast/slow mode separation at 512 B is 471 ns per 128-copy batch —
   63% of the fast-mode value — and the slow mode is a host event, not a
   property of the kernel. The −2.70% is 20 ns, i.e. **4% of the mode
   separation** and 2.7% of the fast-mode value. A 512 B row measured on this
   host is worth less than a 512 B row measured on a quiet one, and the honest
   statement of the 512 B result is the *distribution*, not a single number. The
   distribution is in `docs/data/phase7_p72b_acceptance_summary.csv` and
   `docs/phase7_2b_results.md`.

## If a future attempt is wanted

The one construction that is *not* ruled out by this analysis is a **shape
change, not a knob change**: one that lets the n = 128 case be dispatched in one
compare **without adding code in front of the flat class chain** — e.g. by
restructuring so the 512 B path is reached *before* the chain, or by giving the
window its own copy of the 512 B class. Both change the 512 B path's position
too, so they would need their own pre-registration with P2 written as "the 512 B
path's positional metrics are unchanged", and they would very likely cost the
256 B win. That is a design project, not a variant, and it is not attempted
here.
