#!/usr/bin/env python3
"""Regenerate docs/data/phase7_f4win_acceptance_summary.csv.

    python3 scripts/phase7_accept.py

Runs the repo's own analyser (scripts/analyze.sh) on the committed Phase 7
campaign logs and collects the rows the acceptance test in
src/rungs/avx2_f4win64.c is written against.  The tier model, the null band
and the verdicts all come from analyze.sh, so this script only collects and
formats: it never decides anything.

The criteria are NOT in this file.  They are in the rung's source header,
which was committed before the measurement ran.
"""
import csv
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'docs', 'data', 'phase7_f4win_acceptance_summary.csv')
TARGETS = ['znver2', 'znver3', 'v3']
CANDIDATES = ['f4win64', 'f4win129']
PREV = 'fast'          # the shipped F4: every delta is against it
GATE_SIZES = [16, 32, 64, 256, 512, 1048576]
A1_SIZE = 128
A1_FLOOR = -2.0        # percent; relative, same windows, same process


def analyse(target, cand):
    """Run analyze.sh on the committed log; return {(impl,size): row}."""
    log = os.path.join(ROOT, 'docs', 'data', 'raw',
                       'phase7_f4win_%s.windows.log' % target)
    if not os.path.exists(log):
        return None
    r = subprocess.run(['sh', 'scripts/analyze.sh', log, cand, PREV, target,
                        'memcpy'],
                       cwd=ROOT, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
    if r.returncode != 0:
        return None
    gen = os.path.join(ROOT, 'results', 'live',
                       'phase2_%s_%s.csv' % (cand, target))
    if not os.path.exists(gen):
        return None
    cells = {}
    with open(gen) as f:
        for row in csv.DictReader(f):
            cells[(row['impl'], int(row['size_bytes']))] = row
    return cells


def main():
    header = ['target', 'candidate', 'size', 'gate_size', 'k_min', 'prev_min',
              'glibc_min', 'delta_vs_f4_pct', 'delta_vs_glibc_pct',
              'null_band_pct', 'tier', 'tier_floor_pct', 'verdict_vs_f4',
              'verdict_vs_glibc', 'a1_verdict', 'a2_verdict']
    out = []
    for target in TARGETS:
        for cand in CANDIDATES:
            cells = analyse(target, cand)
            if cells is None:
                print('  no analysis for %s/%s' % (target, cand))
                continue
            for size in sorted({s for (_i, s) in cells}):
                k = cells.get((cand, size))
                p = cells.get((PREV, size))
                g = cells.get(('glibc', size))
                if not k or not p:
                    continue
                is_gate = size in GATE_SIZES
                a1 = ''
                a2 = ''
                if size == A1_SIZE:
                    a1 = 'PASS' if float(k['delta_vs_prev_pct']) >= A1_FLOOR \
                        else 'FAIL'
                if is_gate:
                    a2 = 'REGRESSION' if k['tier_verdict_vs_prev'] == 'LOSS' \
                        else 'ok'
                out.append([
                    target, cand, size, 'yes' if is_gate else 'no',
                    k['min_of_mins_ns'], p['min_of_mins_ns'],
                    g['min_of_mins_ns'] if g else '',
                    k['delta_vs_prev_pct'], k['delta_vs_glibc_pct'],
                    k['null_band_pct'], k['tier'], k['tier_floor_pct'],
                    k['tier_verdict_vs_prev'], k['tier_verdict_vs_glibc'],
                    a1, a2])
    with open(OUT, 'w', newline='') as f:
        w = csv.writer(f, lineterminator='\n')
        w.writerow(header)
        w.writerows(out)
    print('wrote %s (%d rows)' % (OUT, len(out)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
