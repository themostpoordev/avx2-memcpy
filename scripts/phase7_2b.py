#!/usr/bin/env python3
"""Phase 7.2b analysis: is the 512 B regression real or an artifact?

    python3 scripts/phase7_2b.py

Reads the committed campaign logs
    docs/data/raw/phase7_p72b_<iters>_<target>.windows.log
and reports, for every implementation, the floor statistics of its 45 per-rep
`min_ns` samples, at iters = 40000 and iters = 4000, and the deltas against
the shipped F4.

The RULES are not here.  They are in docs/phase7_2b_preregistration.md,
committed before any measurement was taken.  This script reports the numbers
and names the rule that the numbers select; it does not decide anything.
"""
import os
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW = os.path.join(ROOT, 'docs', 'data', 'raw')
TARGETS = ['znver2', 'znver3', 'v3']
ITERS = [40000, 4000]
IMPLS = ['fast', 'f4win64al', 'f4win64', 'glibc']
BASE = 'fast'
SIZE = 512
R1_FLOOR = 1.5          # percent, |delta| below this -> "moved toward 0"


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


def load(target, iters):
    path = os.path.join(RAW, 'phase7_p72b_%d_%s.windows.log' % (iters, target))
    out = defaultdict(list)
    if not os.path.exists(path):
        return None
    with open(path, errors='replace') as f:
        for line in f:
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
            if int(p['n']) != SIZE:
                continue
            out[fl[1]].append(float(p['min_ns']))
    for k in out:
        out[k].sort()
    return out


def stats(v):
    return dict(min=v[0], med=pct(v, 0.5), p25=pct(v, 0.25), p75=pct(v, 0.75),
                p99=pct(v, 0.99), spread=100.0 * (pct(v, 0.99) - pct(v, 0.25)) / pct(v, 0.5),
                n=len(v))


def main():
    print('=' * 118)
    print('PHASE 7.2b  -  512 B, 9 windows x 5 reps, taskset -c 2, zero migrated reps')
    print('=' * 118)
    summary = {}
    for iters in ITERS:
        for target in TARGETS:
            s = load(target, iters)
            if s is None:
                print('  MISSING log for %s iters=%d' % (target, iters))
                continue
            print('\n### %s   iters=%d   (per-rep min_ns over 45 reps; '
                  'band = |naive - ref_scalar| at the same size and iters)'
                  % (target, iters))
            st = {k: stats(v) for k, v in s.items()}
            band = abs(100.0 * (st['ref_scalar']['min'] - st['naive']['min'])
                       / st['ref_scalar']['min'])
            b = st[BASE]
            hdr = ('%-12s %9s %9s %9s %9s %9s %8s | %9s %9s | %9s %9s'
                   % ('impl', 'min', 'median', 'p25', 'p75', 'p99', 'spread%',
                      'd_min%', 'd_med%', 'd_vs_glb%', 'verdict'))
            print('  ' + hdr)
            print('  ' + '-' * (len(hdr) + 2))
            for impl in IMPLS:
                if impl not in st:
                    continue
                d = st[impl]
                dm = 100.0 * (b['min'] - d['min']) / b['min']
                dmed = 100.0 * (b['med'] - d['med']) / b['med']
                dg = 100.0 * (st['glibc']['min'] - d['min']) / st['glibc']['min']
                verdict = ('toward 0' if abs(dm) < R1_FLOOR else
                           ('REGRESS' if dm < 0 else 'ahead'))
                print('  %-12s %9.4f %9.4f %9.4f %9.4f %9.4f %8.2f | %+9.2f %+9.2f '
                      '| %+9.2f  %s'
                      % (impl, d['min'], d['med'], d['p25'], d['p75'], d['p99'],
                         d['spread'], dm, dmed, dg, verdict))
            print('  null band (naive vs ref_scalar, same windows, same iters): '
                  '%.2f%%   [naive %.4f ns, ref_scalar %.4f ns]'
                  % (band, st['naive']['min'], st['ref_scalar']['min']))
            summary[(target, iters)] = (st, band)
    print('\n' + '=' * 118)
    print('RULE SELECTION (rules: docs/phase7_2b_preregistration.md)')
    print('=' * 118)
    toward0 = 0
    persists = 0
    for target in TARGETS:
        for iters in ITERS:
            s = summary.get((target, iters))
            if not s:
                continue
            st, band = s
            b = st[BASE]
            for impl in ('f4win64al', 'f4win64'):
                if impl not in st:
                    continue
                dm = 100.0 * (b['min'] - st[impl]['min']) / b['min']
                print('  %-7s iters=%-6d %-11s delta_min %+7.2f%%  %s'
                      % (target, iters, impl, dm,
                         'moved toward 0 (< %.1f%%)' % R1_FLOOR
                         if abs(dm) < R1_FLOOR else 'PERSISTS'))
                if iters == 40000:
                    if abs(dm) < R1_FLOOR:
                        toward0 += 1
                    else:
                        persists += 1
    print('\n  at iters=40000: %d impl/target cells moved toward 0, %d persist'
          % (toward0, persists))
    return 0


if __name__ == '__main__':
    sys.exit(main())
