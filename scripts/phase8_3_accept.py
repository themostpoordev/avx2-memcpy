#!/usr/bin/env python3
"""Phase 8.3 acceptance test. Written BEFORE the campaign, per the code review.

    python3 scripts/phase8_3_accept.py <tag>

Evaluates the criteria fixed in docs/phase8_3_preregistration.md:

  P1  every certified row (8/16/32/64/128/256/512 B, 16 KiB) unchanged within
      the null band versus the Phase 7.4 deliverable, in the same windows
  P2  the 1 MiB floor is preserved: not worse than the Phase 7.4 reference
      (+12.58 / +11.10 / +9.42%) on any target
  P3  the 1 MiB median moves to >= +1.5% (the Tier A floor, or the null band
      where that is larger) against glibc on >= 2 of 3 targets

It also decomposes the 1 MiB effect:
  pf_ctl vs fast  = the price of the loop SHAPE (the runtime-distance
                    re-scheduling GCC does when the distance is not constant)
  pf_big vs pf_ctl = the price of the DISTANCE (4096 B vs 128 B)

The criteria are not in this script; they are in the pre-registration.
"""
import csv
import os
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW = os.path.join(ROOT, 'docs', 'data', 'raw')
TARGETS = ['znver2', 'znver3', 'v3']
FLOOR_A = 1.5
SPREAD_MAX = 10.0
# P2's reference: the Phase 7.4 1 MiB floor against glibc, fixed here
P2_REF = {'znver2': 12.58, 'znver3': 11.10, 'v3': 9.42}
P1_SIZES = [8, 16, 32, 64, 128, 256, 512, 16384]
SHIPPED = 'fast'
# the candidate and its control, overridable on the command line:
#   attempt 1 -> pf_big / pf_ctl      (prefetch DISTANCE)
#   attempt 2 -> pf_cov2 / pf_ctl2    (prefetch COVERAGE)
CAND = os.environ.get('P83_CAND', 'pf_cov2')
CTL = os.environ.get('P83_CTL', 'pf_ctl2')


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
    tag = sys.argv[1] if len(sys.argv) > 1 else 'p83'
    out = {}
    for target in TARGETS:
        loaded = load(target, tag)
        if not loaded:
            print('  MISSING campaign log for %s' % target)
            continue
        mins, per_window = loaded
        for size in sorted({s for (_i, s) in mins}):
            g = mins.get(('glibc', size))
            n = mins.get(('naive', size))
            rs = mins.get(('ref_scalar', size))
            if not (g and n and rs):
                continue
            gmin, gmed = g[0], pct(g, 0.5)
            band = abs(100.0 * (rs[0] - n[0]) / rs[0])
            row = dict(band=band, gmin=gmin, gmed=gmed)
            for impl in (SHIPPED, CAND, CTL):
                v = mins.get((impl, size))
                if not v:
                    continue
                wm = [min(x) for (i, s, w), x in per_window.items()
                      if i == impl and s == size]
                wsp = 100.0 * (max(wm) - min(wm)) / min(wm) if wm else 0.0
                row[impl] = dict(
                    min=v[0], med=pct(v, 0.5), spread=wsp,
                    d_min=100.0 * (gmin - v[0]) / gmin,
                    d_med=100.0 * (gmed - pct(v, 0.5)) / gmed)
            out[(size, target)] = row

    print('=' * 104)
    print('PHASE 8.3 ACCEPTANCE  (campaign %s)' % tag)
    print('=' * 104)
    print('\n1 MiB, the row under attack:')
    for t in TARGETS:
        r = out.get((1048576, t))
        if not r:
            continue
        print('  %-7s band %5.2f%%' % (t, r['band']))
        for impl in (SHIPPED, CTL, CAND):
            if impl not in r:
                continue
            d = r[impl]
            print('      %-8s d_min %+7.2f%%  d_med %+7.2f%%  (floor %.0f ns, '
                  'median %.0f ns, spread %.1f%%)'
                  % (impl, d['d_min'], d['d_med'], d['min'], d['med'],
                     d['spread']))
        if CTL in r and CAND in r and SHIPPED in r:
            print('      decomposition: shape (pf_ctl vs fast) %+.2f%% floor / '
                  '%+.2f%% median   |   distance (pf_big vs pf_ctl) %+.2f%% '
                  'floor / %+.2f%% median'
                  % (r[CTL]['d_min'] - r[SHIPPED]['d_min'],
                     r[CTL]['d_med'] - r[SHIPPED]['d_med'],
                     r[CAND]['d_min'] - r[CTL]['d_min'],
                     r[CAND]['d_med'] - r[CTL]['d_med']))

    print('\n' + '=' * 104)
    print('CRITERIA')
    print('=' * 104)
    ok = True
    print('  P1  the certified rows, unchanged within the null band vs the '
          'shipped kernel (same windows):')
    for size in P1_SIZES:
        for t in TARGETS:
            r = out.get((size, t))
            if not r or CAND not in r or SHIPPED not in r:
                continue
            move = r[CAND]['d_min'] - r[SHIPPED]['d_min']
            lim = max(FLOOR_A, r['band'])
            st = 'ok' if abs(move) <= lim else 'MOVE'
            if st == 'MOVE':
                ok = False
            print('    %8d B %-7s cand %+7.2f%%  shipped %+7.2f%%  move '
                  '%+6.2f%%  band %5.2f%%  limit %5.2f%%  %s'
                  % (size, t, r[CAND]['d_min'], r[SHIPPED]['d_min'], move,
                     r['band'], lim, st))

    print('  P2  the 1 MiB floor preserved (not worse than the Phase 7.4 '
          'reference on any target):')
    p2 = True
    for t in TARGETS:
        r = out.get((1048576, t))
        if not r or CAND not in r:
            continue
        got = r[CAND]['d_min']
        good = got >= P2_REF[t] - 1e-9
        if not good:
            p2 = False
            ok = False
        print('    %-7s cand %+7.2f%%  ref %+7.2f%%  %s'
              % (t, got, P2_REF[t], 'ok' if good else 'REGRESSED'))
    ok = ok and p2

    print('  P3  the 1 MiB median moved to >= +1.5%% (Tier A floor) on >= 2 of 3:')
    hits = []
    for t in TARGETS:
        r = out.get((1048576, t))
        if not r or CAND not in r:
            continue
        lim = max(FLOOR_A, r['band'])
        good = r[CAND]['d_med'] >= lim
        if good:
            hits.append(t)
        print('    %-7s cand median %+7.2f%%  limit %5.2f%%  %s'
              % (t, r[CAND]['d_med'], lim, 'ok' if good else 'no'))
    p3 = len(hits) >= 2
    ok = ok and p3
    print('    -> %d of 3' % len(hits))

    print('\n  OVERALL: %s' % ('PASS' if ok else 'FAIL'))
    if not p1_ok(out):
        pass
    return 0 if ok else 1


def p1_ok(out):
    return True


if __name__ == '__main__':
    sys.exit(main())
