#!/usr/bin/env python3
"""Phase 10 mixed-size analysis.

    python3 scripts/phase10_analyze.py <dist>

Reads results/live/phase10_<dist>_<target>_r<rep>.windows.log (the committed
logs) and produces, per target and per repetition:

  - the aggregate ns/byte for each implementation, with the floor (min over the
    45 reps), the median and the spread - the same statistic the fixed mode
    uses (min_of_mins);
  - the total-time ratio (the sum of all per-iteration times, which is
    call-count weighted rather than byte weighted);
  - the per-size ns/byte and the delta against glibc, with the sample count and
    the number of negative residuals (the clock-jitter diagnostic);
  - the A1 verdict: the aggregate delta must clear the Tier A floor of 1.5% on
    at least 2 of the 3 targets.

Writes docs/data/phase10_<dist>_summary.csv.
"""
import csv
import os
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIVE = os.path.join(ROOT, 'results', 'live')
DATA = os.path.join(ROOT, 'docs', 'data')
TARGETS = ['znver2', 'znver3', 'v3']
REPS = [1, 2, 3]
FLOOR_A = 1.5
SHIPPED = 'fast'
BASELINE = 'glibc'


def pct(v, q):
    if not v:
        return float('nan')
    v = sorted(v)
    n = len(v)
    if n == 1:
        return v[0]
    i = q * (n - 1)
    lo = int(i)
    hi = min(lo + 1, n - 1)
    f = i - lo
    return v[lo] * (1 - f) + v[hi] * f


def load(dist, target, rep):
    path = os.path.join(LIVE, 'phase10_%s_%s_r%d.windows.log' % (dist, target, rep))
    if not os.path.exists(path):
        return None
    agg = defaultdict(list)          # impl -> per-rep aggregate ns/byte
    agg_ns = defaultdict(float)      # impl -> sum of the per-rep total ns
    agg_bytes = defaultdict(float)
    sizes = defaultdict(list)        # (impl,size) -> per-rep ns/byte
    counts = defaultdict(set)       # (impl,size) -> the per-rep counts
    negs = defaultdict(int)
    overhead = defaultdict(list)
    with open(path, errors='replace') as f:
        for line in f:
            if line.startswith('#'):
                continue        # the log's window markers
            # cheap manual parse: the row is a plain CSV with no quoting
            parts = line.rstrip('\n').split(',')
            if len(parts) < 16 or parts[0] == 'impl':
                continue        # the per-window header
            impl = parts[0]
            scope = parts[5]
            size = int(parts[6])
            count = int(parts[7])
            ns = float(parts[8])
            byt = float(parts[9])
            rate = float(parts[10])
            oh = float(parts[11])
            neg = int(parts[12])
            overhead[impl].append(oh)
            if scope == 'agg':
                agg[impl].append(rate)
                agg_ns[impl] += ns
                agg_bytes[impl] += byt
            else:
                sizes[(impl, size)].append(rate)
                counts[(impl, size)].add(count)
                negs[(impl, size)] += neg
    return dict(agg=agg, agg_ns=agg_ns, agg_bytes=agg_bytes, sizes=sizes,
                counts=counts, negs=negs, overhead=overhead)


def main():
    dist = sys.argv[1] if len(sys.argv) > 1 else 'd1'
    out = os.path.join(DATA, 'phase10_%s_summary.csv' % dist)
    rows = []
    for target in TARGETS:
        for rep in REPS:
            d = load(dist, target, rep)
            if not d:
                print('  MISSING log for %s r%d' % (target, rep))
                continue
            g = d['agg'].get(BASELINE, [])
            f = d['agg'].get(SHIPPED, [])
            if not g or not f:
                continue
            gfloor, ffloor = min(g), min(f)
            gmed, fmed = pct(g, 0.5), pct(f, 0.5)
            # total-time ratio: the sum of the per-iteration times
            tt_g = d['agg_ns'][BASELINE]
            tt_f = d['agg_ns'][SHIPPED]
            rows.append(dict(
                scope='agg', target=target, rep=rep, size=0,
                d_min_pct=100.0 * (gfloor - ffloor) / gfloor,
                d_median_pct=100.0 * (gmed - fmed) / gmed,
                d_total_time_pct=100.0 * (tt_g - tt_f) / tt_g,
                glibc_ns_per_byte=gfloor, fast_ns_per_byte=ffloor,
                spread_pct=100.0 * (max(g) - min(g)) / min(g),
                fast_spread_pct=100.0 * (max(f) - min(f)) / min(f),
                count=0, negatives=0,
                overhead_ns=pct(d['overhead'].get(SHIPPED, [0]), 0.5)))
            for (impl, size), vals in sorted(d['sizes'].items()):
                if impl != SHIPPED:
                    continue
                gv = d['sizes'].get((BASELINE, size), [])
                if not gv:
                    continue
                gfl, ffl = min(gv), min(vals)
                rows.append(dict(
                    scope='size', target=target, rep=rep, size=size,
                    d_min_pct=100.0 * (gfl - ffl) / gfl,
                    d_median_pct=100.0 * (pct(gv, 0.5) - pct(vals, 0.5))
                                      / pct(gv, 0.5),
                    d_total_time_pct=0.0,
                    glibc_ns_per_byte=gfl, fast_ns_per_byte=ffl,
                    spread_pct=100.0 * (max(gv) - min(gv)) / min(gv),
                    fast_spread_pct=100.0 * (max(vals) - min(vals)) / min(vals),
                    count=min(d['counts'].get((impl, size), [0])),
                    negatives=d['negs'].get((impl, size), 0),
                    overhead_ns=pct(d['overhead'].get(SHIPPED, [0]), 0.5)))
    if not rows:
        print('no data')
        return 1
    hdr = ['scope', 'target', 'rep', 'size', 'd_min_pct', 'd_median_pct',
           'd_total_time_pct', 'glibc_ns_per_byte', 'fast_ns_per_byte',
           'spread_pct', 'fast_spread_pct', 'count', 'negatives', 'overhead_ns']
    with open(out, 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=hdr, lineterminator='\n')
        w.writeheader()
        for r in rows:
            w.writerow({k: (('%.4f' % v) if isinstance(v, float) else v)
                         for k, v in r.items()})
    print('wrote %s (%d rows)' % (out, len(rows)))

    # ---- the A1 verdict -------------------------------------------------
    print('\n=== A1: the aggregate mixed-mode delta (floor), per target and rep ===')
    hits = defaultdict(int)
    for target in TARGETS:
        line = []
        for rep in REPS:
            sel = [r for r in rows if r['scope'] == 'agg' and r['target'] == target
                   and r['rep'] == rep]
            if not sel:
                continue
            r = sel[0]
            line.append('r%d %+6.2f%%' % (rep, r['d_min_pct']))
            if r['d_min_pct'] >= FLOOR_A:
                hits[target] += 1
        if line:
            print('  %-7s %s' % (target, '   '.join(line)))
    print('  targets clearing +%.1f%% on at least one repetition: %d of 3'
          % (FLOOR_A, sum(1 for t in TARGETS if hits[t])))
    print('  A1 (clears on >= 2 of 3 targets, in every repetition): %s'
          % ('PASS' if all(hits[t] == len(REPS) for t in TARGETS if hits[t]) and
             sum(1 for t in TARGETS if hits[t]) >= 2 else 'FAIL'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
