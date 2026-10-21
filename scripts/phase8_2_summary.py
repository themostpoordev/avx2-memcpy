#!/usr/bin/env python3
"""Phase 8.2 re-certification summary.

    python3 scripts/phase8_2_summary.py <tag>

Writes docs/data/phase8_2_summary.csv in the same schema as
docs/data/phase7_4_summary.csv, from the committed campaign log, so the two
campaigns can be compared row by row.
"""
import csv
import os
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW = os.path.join(ROOT, 'docs', 'data', 'raw')
TARGETS = ['znver2', 'znver3', 'v3']
PROMOTED = 'fast'
BASE = 'f4_baseline'
GLIBC = 'glibc'
FLOOR_A = 1.5
FLOOR_B = 25.0
SPREAD_MAX = 10.0


def pct(v, q):
    n = len(v)
    if n == 0:
        return float('nan')
    if n == 1:
        return v[0]
    i = q * (n - 1)
    lo = int(i)
    hi = min(lo + 1, n - 1)
    f = i - lo
    return v[lo] * (1 - f) + v[hi] * f


def load(target, tag):
    path = os.path.join(RAW, 'phase7_%s_%s.windows.log' % (tag, target))
    if not os.path.exists(path):
        return None
    mins = defaultdict(list)
    per_window = defaultdict(list)
    win = None
    with open(path, errors='replace') as f:
        for line in f:
            if line.startswith('# WINDOW '):
                win = int(line.split()[2])
                continue
            if not line.startswith('REPS '):
                continue
            fl = line.split()
            if fl[2] != 'memcpy':
                continue
            p = {}
            for kv in fl[3:]:
                if '=' in kv:
                    k, v = kv.split('=', 1)
                    p[k] = v
            if int(p.get('migrated', '0')):
                continue
            mins[(fl[1], int(p['n']))].append(float(p['min_ns']))
            per_window[(fl[1], int(p['n']), win)].append(float(p['min_ns']))
    for k in mins:
        mins[k].sort()
    return mins, per_window


def main():
    tag = sys.argv[1] if len(sys.argv) > 1 else 'p82'
    out = os.path.join(ROOT, 'docs', 'data', 'phase8_2_summary.csv')
    header = ['size', 'target',
              'fast_min', 'fast_median', 'fast_p25', 'fast_p75', 'fast_p99',
              'fast_spread_pct', 'window_spread_pct',
              'base_min', 'base_median', 'glibc_min', 'glibc_median',
              'd_glibc_min_pct', 'd_glibc_median_pct',
              'd_base_min_pct', 'd_base_median_pct',
              'null_band_pct', 'tier', 'tier_floor_pct',
              'v_glibc_min', 'v_glibc_median', 'v_base_min']
    rows = []
    for target in TARGETS:
        loaded = load(target, tag)
        if not loaded:
            print('  MISSING campaign log for %s' % target)
            continue
        mins, per_window = loaded
        for size in sorted({s for (_i, s) in mins}):
            f = mins.get((PROMOTED, size))
            b = mins.get((BASE, size))
            g = mins.get((GLIBC, size))
            n = mins.get(('naive', size))
            rs = mins.get(('ref_scalar', size))
            if not (f and b and g and n and rs):
                continue
            fmin, fmed = f[0], pct(f, 0.5)
            gmin, gmed = g[0], pct(g, 0.5)
            band = abs(100.0 * (rs[0] - n[0]) / rs[0])
            wm = [min(v) for (i, s, w), v in per_window.items()
                  if i == PROMOTED and s == size]
            wsp = 100.0 * (max(wm) - min(wm)) / min(wm) if wm else 0.0
            tier = 'A' if wsp <= SPREAD_MAX else 'B'
            floor = FLOOR_B if tier == 'B' else max(FLOOR_A, band)

            def claim(d, fl):
                if d > fl:
                    return 'WIN'
                if d < -fl:
                    return 'LOSS'
                return 'measured'

            dgm = 100.0 * (gmin - fmin) / gmin
            dgd = 100.0 * (gmed - fmed) / gmed
            dbm = 100.0 * (b[0] - fmin) / b[0]
            dbd = 100.0 * (pct(b, 0.5) - fmed) / pct(b, 0.5)
            rows.append([
                size, target,
                '%.4f' % fmin, '%.4f' % fmed, '%.4f' % pct(f, 0.25),
                '%.4f' % pct(f, 0.75), '%.4f' % pct(f, 0.99),
                '%.2f' % (100.0 * (pct(f, 0.99) - pct(f, 0.25)) / fmed),
                '%.2f' % wsp,
                '%.4f' % b[0], '%.4f' % pct(b, 0.5), '%.4f' % gmin, '%.4f' % gmed,
                '%+.2f' % dgm, '%+.2f' % dgd, '%+.2f' % dbm, '%+.2f' % dbd,
                '%.2f' % band, tier, '%.2f' % floor,
                claim(dgm, floor), claim(dgd, floor), claim(dbm, floor)])
    with open(out, 'w', newline='') as f:
        w = csv.writer(f, lineterminator='\n')
        w.writerow(header)
        w.writerows(rows)
    print('wrote %s (%d rows)' % (out, len(rows)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
