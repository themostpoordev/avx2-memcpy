#!/usr/bin/env python3
"""Phase 8.2 acceptance test, applied to the re-certification campaign.

    python3 scripts/phase8_2_accept.py <tag>

Reads docs/data/raw/phase7_<tag>_<target>.windows.log and evaluates the
criteria fixed in docs/phase8_2_preregistration.md:

  A1_16   16 KiB delta vs glibc within 1.5% on >= 2 of 3 targets
  A2_16   16 KiB delta vs the previous deliverable (f4_baseline) >= +1.0%
  A1_256  256 B median delta vs glibc >= +1.5% on >= 2 of 3 targets
  A2_256  256 B min delta vs glibc unchanged (>= +8%) on all 3 targets
  GATE    16/32/64/128/512 B and 1 MiB unchanged within the null band
          relative to the Phase 7.4 reference deltas

The criteria are not in this script; they are in the pre-registration.  This
script computes and reports.
"""
import csv
import os
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW = os.path.join(ROOT, 'docs', 'data', 'raw')
REF = os.path.join(ROOT, 'docs', 'data', 'phase7_4_summary.csv')
TARGETS = ['znver2', 'znver3', 'v3']
FLOOR_A = 1.5
FLOOR_B = 25.0
SPREAD_MAX = 10.0
GATE_SIZES = [16, 32, 64, 128, 512, 1048576]


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
    ref = {}
    for r in csv.DictReader(open(REF)):
        ref[(int(r['size']), r['target'])] = r
    out = {}
    for target in TARGETS:
        loaded = load(target, tag)
        if not loaded:
            print('  MISSING campaign log for %s' % target)
            continue
        mins, per_window = loaded
        for size in sorted({s for (_i, s) in mins}):
            f = mins.get(('fast', size))
            b = mins.get(('f4_baseline', size))
            g = mins.get(('glibc', size))
            n = mins.get(('naive', size))
            rs = mins.get(('ref_scalar', size))
            if not (f and b and g and n and rs):
                continue
            fmin, fmed = f[0], pct(f, 0.5)
            gmin, gmed = g[0], pct(g, 0.5)
            band = abs(100.0 * (rs[0] - n[0]) / rs[0])
            wm = [min(v) for (i, s, w), v in per_window.items()
                  if i == 'fast' and s == size]
            wsp = 100.0 * (max(wm) - min(wm)) / min(wm) if wm else 0.0
            tier = 'A' if wsp <= SPREAD_MAX else 'B'
            floor = FLOOR_B if tier == 'B' else max(FLOOR_A, band)
            out[(size, target)] = dict(
                fmin=fmin, fmed=fmed, gmin=gmin, gmed=gmed,
                d_glibc_min=100.0 * (gmin - fmin) / gmin,
                d_glibc_med=100.0 * (gmed - fmed) / gmed,
                d_base_min=100.0 * (b[0] - fmin) / b[0],
                d_base_med=100.0 * (pct(b, 0.5) - fmed) / pct(b, 0.5),
                band=band, tier=tier, floor=floor, spread=wsp)

    print('=' * 100)
    print('PHASE 8.2 ACCEPTANCE  (campaign %s)' % tag)
    print('=' * 100)
    for size in sorted({s for (s, _t) in out}):
        print('\n--- %d B ---' % size)
        for t in TARGETS:
            r = out.get((size, t))
            if not r:
                print('  %-7s (no data)' % t)
                continue
            print('  %-7s d_glibc_min %+7.2f%%  d_glibc_med %+7.2f%%  '
                  'd_base_min %+7.2f%%  band %5.2f%%  tier %s floor %5.2f%%  '
                  'spread %5.1f%%'
                  % (t, r['d_glibc_min'], r['d_glibc_med'], r['d_base_min'],
                     r['band'], r['tier'], r['floor'], r['spread']))

    def verdict(d, floor):
        if d > floor:
            return 'WIN'
        if d < -floor:
            return 'LOSS'
        return 'measured'

    print('\n' + '=' * 100)
    print('CRITERIA')
    print('=' * 100)
    ok = True
    # A1_16 / A2_16
    within = [t for t in TARGETS if (16384, t) in out
              and abs(out[(16384, t)]['d_glibc_min']) <= FLOOR_A]
    a1_16 = len(within) >= 2
    base16 = [out[(16384, t)]['d_base_min'] for t in TARGETS if (16384, t) in out]
    a2_16 = bool(base16) and all(d >= 1.0 for d in base16)
    print('  A1_16  16 KiB within 1.5%% of glibc on >= 2 of 3 targets: %s  (%d/3: %s)'
          % ('PASS' if a1_16 else 'FAIL', len(within), ', '.join(within)))
    print('  A2_16  16 KiB vs the previous deliverable >= +1.0%%: %s  (%s)'
          % ('PASS' if a2_16 else 'FAIL',
             ', '.join('%s %+.2f%%' % (t, out[(16384, t)]['d_base_min'])
                       for t in TARGETS if (16384, t) in out)))
    ok = ok and a1_16 and a2_16
    # A1_256 / A2_256
    med256 = [t for t in TARGETS if (256, t) in out
              and out[(256, t)]['d_glibc_med'] >= FLOOR_A]
    a1_256 = len(med256) >= 2
    min256 = [(t, out[(256, t)]['d_glibc_min']) for t in TARGETS if (256, t) in out]
    a2_256 = bool(min256) and all(d >= 8.0 for _t, d in min256)
    print('  A1_256 256 B MEDIAN vs glibc >= +1.5%% on >= 2 of 3: %s  (%d/3: %s)'
          % ('PASS' if a1_256 else 'FAIL', len(med256), ', '.join(med256)))
    print('  A2_256 256 B MIN vs glibc >= +8%% on all 3: %s  (%s)'
          % ('PASS' if a2_256 else 'FAIL',
             ', '.join('%s %+.2f%%' % (t, d) for t, d in min256)))
    ok = ok and a1_256 and a2_256
    # regression gate
    print('  GATE   unchanged within the null band vs the Phase 7.4 reference:')
    for size in GATE_SIZES:
        for t in TARGETS:
            r = out.get((size, t))
            rr = ref.get((size, t))
            if not r or not rr:
                continue
            delta = r['d_glibc_min'] - float(rr['d_glibc_min_pct'])
            lim = max(FLOOR_A, r['band'])
            st = 'ok' if abs(delta) <= lim else 'MOVE'
            if st == 'MOVE':
                ok = False
            print('    %8d B %-7s new %+7.2f%%  ref %+7.2f%%  move %+6.2f%%  '
                  'band %5.2f%%  limit %5.2f%%  %s'
                  % (size, t, r['d_glibc_min'], float(rr['d_glibc_min_pct']),
                     delta, r['band'], lim, st))
    print('\n  OVERALL: %s' % ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
