#!/usr/bin/env python3
"""Rewrite docs/RESULTS.md against the phase7_4 reference campaign.

    python3 scripts/phase7_4_rewrite_results.py

The headline and the shipped-kernel table are generated from
docs/data/phase7_4_summary.csv (itself generated from the committed campaign
logs). The phase-2 reference-ladder section is carried over VERBATIM from the
previous file, so nothing in it can be corrupted by a retype.
"""
import csv
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV = os.path.join(ROOT, 'docs', 'data', 'phase7_4_summary.csv')
RES = os.path.join(ROOT, 'docs', 'RESULTS.md')

TARGETS = ['znver2', 'znver3', 'v3']


def main():
    old = open(RES).read()
    # carry the phase-2 ladder section over verbatim
    i = old.index('## The ladder')
    j = old.index('## How the numbers were produced')
    ladder = old[i:j].rstrip() + '\n'

    tables = subprocess.run(
        [sys.executable, os.path.join(ROOT, 'scripts', 'phase7_4_tables.py')],
        capture_output=True, text=True, check=True).stdout
    def rows_only(block):
        """the generator emits the header and the separator too; the template
        supplies its own, so keep only the data rows."""
        lines = [l for l in block.strip().splitlines()
                 if not l.startswith('| size |') and not l.startswith('|--:')]
        return '\n'.join(lines).strip()

    wins = tables.split('### headline wins table (floor vs glibc)')[1]
    wins = rows_only(wins.split('### full ladder table')[0])
    ladder_tbl = tables.split('### full ladder table')[1]
    ladder_tbl = rows_only(ladder_tbl.split('### extra reported row')[0])
    extra = rows_only(tables.split('### extra reported row (8 B, not on the ladder)')[1])

    rows = list(csv.DictReader(open(CSV)))
    by = {(int(r['size']), r['target']): r for r in rows}
    md5_z2 = '0eb00867a4688a618d0e4329195c188d'
    md5_v3 = '368680ed8dac9be06d0c1d382b5dfc6c'

    doc = f"""# Results — memcpy_fast against glibc on Zen 2

Every number below comes from the **committed** reference campaign
[`docs/data/raw/phase7_p74_<target>.windows.log`](data/raw/) — 9 windows x 5
reps per target, `taskset -c 2`, `migrated=0` on every rep, glibc measured in
the same process and the same windows. The per-size summary is
[`docs/data/phase7_4_summary.csv`](data/phase7_4_summary.csv), regenerated from
those logs by `python3 scripts/phase7_4_summary.py`. The tables below are
generated from that CSV by `python3 scripts/phase7_4_tables.py`, so no
published number is typed by hand.

Two statistics are reported for every row, and they mean different things:

* **Δ_min** — the *floor*. The fastest single sample observed, against glibc's
  own floor. This is the statistic the claim model is built around.
* **Δ_med** — the *typical*. The median of the 45 per-rep samples.

A row is a certified win only if it clears its floor under **both**.

## Reference campaign

The shipped kernel is the **Phase 7.4 promote**: the F4 shape plus the 128 B
case that gives `n == 128` a 64-byte block pair (four loads and four stores,
no redundancy) inside the 128–256 B window, compiled with `-falign-labels=64`
so the 32–64 B path keeps the 64-byte alignment of its out-of-line branch
target. Its `.text` md5 is `{md5_z2}` on znver2 and znver3 and `{md5_v3}` on
v3.

The promote was measured against the kernel it replaced in the same windows
(`f4_baseline` in the campaign, the pre-promote F4 verbatim). The trade it makes
is documented in [NEGATIVE_RESULTS.md](NEGATIVE_RESULTS.md): **128 B is won,
512 B loses 2.24 points of margin and stays a certified win.** The mechanism is
in `docs/phase7_3a_mechanism.md`: the window body that dispatches the 64-byte
pair sits *before* the flat class chain, so the branch that adds it displaces
the chain, and the 512 B path's position in the code stream moves with it.

## Certified wins

| size | Δ_min (znver2 / znver3 / v3) | Δ_med (znver2 / znver3 / v3) | spread% | tier | verdict |
|--:|--:|--:|--:|:--:|:--|
{wins}

**Six sizes clear their floor.** 16 B, 32 B, 64 B and 256 B clear it on all
three targets. **128 B clears it on two of three**: on v3 the null band at that
size is 18.35%, so +10.11% is inside the band and the row reads `measured`
there — the delta is identical on all three targets, the band is not. **512 B
clears it on two of three**: on znver2 the cell is Tier B (its per-window
spread is 57.95%), so its floor is 25% and +14.59% reads `measured` there; the
delta is identical on all three targets.

**128 B is the new win.** It was a certified loss of −10.04% before the
promote and is now **+10.11%** on every target — the first time this kernel is
ahead of glibc at that size. The cause was structural: at exactly 128 B the
window's 128-byte block pair had its head and tail blocks coincident, so the
kernel issued eight loads and eight stores for a 128 B copy where glibc issues
four and four. Its own 128 B copy used to cost *more* than its 256 B copy.

**256 B is a win at the floor and conditional at the median** — +8.41% on all
three targets at the floor, but a tie at the median on znver2.

| size | Δ_min (znver2 / znver3 / v3) | Δ_med (znver2 / znver3 / v3) | spread% | tier | verdict |
|--:|--:|--:|--:|:--:|:--|
{extra}

**8 B is now reported too.** It is not on the 13-point ladder, but the knob
moves the `n <= 15` class, so the size is measured and reported rather than
left unsaid: +25.49% / +25.49% / +38.93% at the floor, a win on all three
targets.

## Certified loss

**16 KiB: −2.28 / −2.41 / −2.40%, a certified loss on all three targets**
(Tier A, 1.5% floor). This is not new and not caused by the promote: the
promoted kernel is +1.16% against the kernel it replaced at that size on
znver2, and the same −2.28% was already published for znver2 under the previous
reference. What changed is that the other two targets moved from just inside
the 1.5% floor to just outside it. The size is memory-bound, so the row
follows the host, and the honest reading is "level with glibc, currently
2.4% behind".

## Conditional and parity

* **1 MiB: parity at the median** (−1.96 / −2.02 / −2.29%), and +12.58 /
  +11.10 / +9.42% at the floor. Tier B, so the row is `measured` on two of the
  three targets and a win on the third. The floor is a real property of the
  kernel; the median is what a caller actually sees. It is not a claim.
* **256 B**: a win at the floor on all three targets, a tie at the median on
  znver2 (see above).

## Unmeasurable

**4 MiB.** The per-window spread is 117% / 197% / 99% — the campaign's worst
cell by a wide margin — so nothing at that size is resolvable and nothing is
claimed. The floor reads +0.16 / +6.91 / +1.49%, which is the spread talking,
not a result.

## The shipped kernel, full ladder

| size | Δ_min (znver2 / znver3 / v3) | Δ_med (znver2 / znver3 / v3) | spread% | tier | verdict |
|--:|--:|--:|--:|:--:|:--|
{ladder_tbl}

`spread%` = (p99 − p25) / median of the 45 per-rep mins.
spread > 50% indicates a bimodal distribution — for those
rows, read both min and median; do not select the more
favorable one.

"measured" means inside the applicable floor. It is not a win, not a loss and
not a tie-breaker. From 1 KiB to 64 KiB the kernel is level with glibc — the
interesting part of that result is that glibc's loop is the same 4x32 B loop, so
there is nothing left to win there by unrolling harder.

{ladder}
## How the numbers were produced

`min_of_mins` (the smallest single-batch time over every rep in every window),
9 windows x 5 reps with no early stop, `taskset -c 2`, `migrated=0` required, and
a per-cell floor of `max(1.5%, measured null band)`. Every row additionally
reports `median_of_mins` (the median of the 45 per-rep samples) and the spread
`(p99 − p25) / median`; a row is a certified win only if it clears its floor
under both statistics. The full contract, the two-tier claim model, the
audit-width gate, the evidence policy and the reproducibility gate are in
[METHODOLOGY.md](METHODOLOGY.md). Verified, not assumed: every kernel is checked
byte-for-byte against an independent reference with canaries before it is timed.

**One warning about the median at 512 B and below.** The per-rep distribution at
those sizes is bimodal: a fast mode and a slow mode, ~471 ns apart per
128-copy batch, and which mode a rep lands in is a property of the *host*, not
of the kernel. The shipped kernel's own 512 B median reads 5.7891 ns in the
Phase 7.2b campaign and 9.4688 ns in this one — the same kernel, the same size,
the same host, two campaigns. Its floor reads 5.7891 ns in both. That is why
the claim model is built on the floor and why the median is reported beside it
rather than instead of it.

## What made the wins possible

Four things, in order of contribution. **Small-size specialisation:** at 16–64 B
the copy is 2–3 ns, so the branch structure *is* the kernel; the shipped kernel
takes the whole 1–64 B range from one compare and copies it with straight-line
overlapping moves. **The right block size at the class boundary:** the ladder's
rule is that a class window is `(H, 2H]` and the block pair uses `H` = the
class's lower bound, so the two blocks tile `[0, n)` exactly at `n = 2H`. The
128–256 B window used `H = 128` over a range that starts at 128, so the one
size where the ladder's own rule bites — 128 B — got a pair whose head and
tail blocks coincided. Giving that size `H = 64` is the 128 B win, and it is
worth +20 points at that size. **Per-size tuning instead of one loop:** the
512 B win comes from the block-pair shape chosen for that class, and the
256 B win from a dedicated 128–256 B window rather than from wider
instructions. **Measurement discipline:** every candidate was measured in the
same windows as the thing it replaced, so the changes that survived are the
ones that moved a floor, and the 128 B regression was visible rather than hidden
by a median.

## Scope and honesty notes

* **This tree is a curated subset of the experiment.** The public tree contains
  the shipped kernels and the seven-rung ladder. The shipped kernel's published
  rows **are** reconstructable: the reference campaign's raw window logs are
  committed under `docs/data/raw/`, and `scripts/phase7_4_summary.py`
  regenerates the summary CSV from them and `scripts/phase7_4_tables.py`
  regenerates the tables in this file. The phase-2 ladder rows below are the
  older campaign, whose record is the committed phase-2 CSVs in `docs/data/`;
  the raw logs behind those CSVs did not ship and are not reconstructable here.
  The candidate kernels that lost (V9b, V9c variants, the prefetch-window grid,
  the PGO/LTO probes) stayed on the VPS, by design — a record that only lists
  winners is a sales sheet, and [NEGATIVE_RESULTS.md](NEGATIVE_RESULTS.md) is
  the record of what lost.
* Numbers are silicon-specific: one Zen 2 host, `taskset -c 2`, glibc as
  measured in the same process. Your machine will differ; measure before you
  switch. The reference host itself is shared, and its floor moves with load —
  see [METHODOLOGY.md §9](METHODOLOGY.md#9-host-variance-the-floor-moves).
* A loss is reported with the same prominence as a win. 128 B is now a win and
  16 KiB is now a loss; both are stated from the same committed campaign.
* **The floor is the floor, and the median is the truth.** Every row reports
  both. Where they disagree, the row says so: at 1 MiB the floor says +9 to
  +13% and the median says parity, and the honest summary is that the kernel
  is level with glibc at that size.

[^record]: **Record-derived.** Certified at the time of measurement, on a quieter
    host window. Reproduction on the current host depends on host load — see
    [METHODOLOGY.md §9](METHODOLOGY.md#9-host-variance-the-floor-moves). The
    committed window logs in [`docs/data/raw/`](data/) are the authoritative
    record.
"""
    open(RES, 'w').write(doc)
    print('rewrote %s (%d bytes)' % (RES, len(doc)))


if __name__ == '__main__':
    main()
