#!/usr/bin/env python3
"""Phase 7.4 re-certification summary.

    python3 scripts/phase7_4_summary.py

Reads the committed re-certification campaign
    docs/data/raw/phase7_p74_<target>.windows.log
and writes
    docs/data/phase7_4_summary.csv

with, for every (size, target): the floor statistics of the promoted kernel
(`fast`), the pre-promote control (`f4_baseline`) and `glibc`, the deltas of
the promoted kernel against both, the null band measured in the same run, the
tier, and the verdicts.

The acceptance CRITERIA are not here. They are in
docs/phase7_4_preregistration.md, committed before the campaign ran. This
script computes and reports; the verdict is stated in the commit message and
in docs/RESULTS.md.
"""
import csv
import os
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW = os.path.join(ROOT, 'docs', 'data', 'raw')
OUT = os.path.join(ROOT, 'docs', 'data', 'phase7_4_summary.csv')
TARGETS = ['znver2', 'znver3', 'v3']
PROMOTED = 'fast'
BASE = 'f4_baseline'
GLIBC = 'glibc'
FLOOR_A = 1.5
FLOOR_B = 25.0
SPREAD_MAX = 10.0


def load(target):
    path = os.path.join(RAW, 'phase7_p74_%s.windows.log' % target)
    if not os.path.exists(path):
        return None
    mins = defaultdict(list)
    per_window = defaultdict(list)
    with open(path, errors='replace') as f:
        win = None
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


def claim(d, floor):
    if d > floor:
        return 'WIN'
    if d < -floor:
        return 'LOSS'
    return 'measured'


def main():
    header = ['size', 'target',
              'fast_min', 'fast_median', 'fast_p25', 'fast_p75', 'fast_p99',
              'fast_spread_pct',
              'base_min', 'base_median', 'glibc_min', 'glibc_median',
              'd_base_min_pct', 'd_base_median_pct',
              'd_glibc_min_pct', 'd_glibc_median_pct',
              'null_band_pct', 'tier', 'tier_floor_pct',
              'v_base_min', 'v_base_median', 'v_glibc_min', 'v_glibc_median']
    rows = []
    for target in TARGETS:
        loaded = load(target)
        if not loaded:
            print('  MISSING campaign log for %s' % target)
            continue
        mins, per_window = loaded
        sizes = sorted({s for (_i, s) in mins})
        for size in sizes:
            f = mins.get((PROMOTED, size))
            b = mins.get((BASE, size))
            g = mins.get((GLIBC, size))
            if not f or not b or not g:
                continue
            fmin, fmed = f[0], pct(f, 0.5)
            bmin, bmed = b[0], pct(b, 0.5)
            gmin, gmed = g[0], pct(g, 0.5)
            # the cell's tier: the per-window min spread of the promoted kernel
            wm = [min(v) for (i, s, w), v in per_window.items()
                  if i == PROMOTED and s == size]
            wsp = 100.0 * (max(wm) - min(wm)) / min(wm) if wm else 0.0
            tier = 'A' if wsp <= SPREAD_MAX else 'B'
            band = abs(100.0 * (mins[('ref_scalar', size)][0]
                                - mins[('naive', size)][0])
                        / mins[('ref_scalar', size)][0])
            floor = FLOOR_B if tier == 'B' else max(FLOOR_A, band)
            dbm = 100.0 * (bmin - fmin) / bmin
            dbmd = 100.0 * (bmed - fmed) / bmed
            dgm = 100.0 * (gmin - fmin) / gmin
            dgmd = 100.0 * (gmed - fmed) / gmed
            rows.append([
                size, target,
                '%.4f' % fmin, '%.4f' % fmed, '%.4f' % pct(f, 0.25),
                '%.4f' % pct(f, 0.75), '%.4f' % pct(f, 0.99),
                '%.2f' % (100.0 * (pct(f, 0.99) - pct(f, 0.25)) / fmed),
                '%.4f' % bmin, '%.4f' % bmed, '%.4f' % gmin, '%.4f' % gmed,
                '%+.2f' % dbm, '%+.2f' % dbmd, '%+.2f' % dgm, '%+.2f' % dgmd,
                '%.2f' % band, tier, '%.2f' % floor,
                claim(dbm, floor), claim(dbmd, floor),
                claim(dgm, floor), claim(dgmd, floor)])
    with open(OUT, 'w', newline='') as f:
        w = csv.writer(f, lineterminator='\n')
        w.writerow(header)
        w.writerows(rows)
    print('wrote %s (%d rows)' % (OUT, len(rows)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
