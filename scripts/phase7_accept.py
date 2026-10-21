#!/usr/bin/env python3
"""Phase 7 / 7.2 acceptance test, applied to a campaign log.

    python3 scripts/phase7_accept.py <tag> <candidate> [<candidate> ...]

where <tag> names the campaign log set:
    results/live/phase7_<tag>_<target>.windows.log      (live)
    docs/data/raw/phase7_<tag>_<target>.windows.log      (committed)

The candidate is compared against the shipped F4 (`fast`) in the SAME windows.
Two statistics are used, both over the 45 per-rep `min_ns` samples of one
(impl, size) cell, migrated reps dropped:

    min_of_mins    the minimum of the 45 samples  (analyze.sh's min_of_mins_ns)
    median_of_mins the median of the 45 samples  (computed here, because
                   analyze.sh's CSV carries no per-size median)

The CRITERIA are not in this script.  They live in the rung's source header,
committed before the measurement ran:
    src/rungs/avx2_f4win64.c        (Phase 7, H1b)
    src/rungs/f4win64_aligned.c     (Phase 7.2)

This script only applies them.
"""
import csv
import hashlib
import os
import subprocess
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGETS = ['znver2', 'znver3', 'v3']
PREV = 'fast'
GATE_SIZES = [16, 32, 64, 256, 512, 1048576]
REPORT_SIZES = [8, 16, 32, 64, 128, 256, 512, 1048576]
A1_SIZE = 128
A1_FLOOR = -2.0        # percent, relative, both min and median
A2_BACKSTOP = -5.0     # percent, no certified row may be worse than this
FLOOR_RULE3 = 1.5      # percent
EXPECT_MD5 = {'znver2': '0b2d0837e25a53b2570ef33a6606edfa',
               'znver3': '0b2d0837e25a53b2570ef33a6606edfa',
               'v3': '71c9cefef2d67698267abc118fe55e1a'}


def log_path(tag, target):
    for p in (os.path.join(ROOT, 'results', 'live',
                           'phase7_%s_%s.windows.log' % (tag, target)),
              os.path.join(ROOT, 'docs', 'data', 'raw',
                           'phase7_%s_%s.windows.log' % (tag, target))):
        if os.path.exists(p):
            return p
    return None


def samples(log):
    """(impl, size) -> sorted list of per-rep min_ns (memcpy, migrated=0)."""
    out = defaultdict(list)
    with open(log, errors='replace') as f:
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
            out[(fl[1], int(p['n']))].append(float(p['min_ns']))
    for k in out:
        out[k].sort()
    return out


def median(v):
    n = len(v)
    if n == 0:
        return float('nan')
    if n == 1:
        return v[0]
    i = 0.5 * (n - 1)
    lo = int(i)
    hi = min(lo + 1, n - 1)
    f = i - lo
    return v[lo] * (1 - f) + v[hi] * f


def analyse(tag, cand, target):
    log = log_path(tag, target)
    if log is None:
        return None, None
    r = subprocess.run(['sh', 'scripts/analyze.sh', log, cand, PREV, target,
                        'memcpy'], cwd=ROOT, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
    if r.returncode != 0:
        return None, None
    gen = os.path.join(ROOT, 'results', 'live',
                       'phase2_%s_%s.csv' % (cand, target))
    if not os.path.exists(gen):
        return None, None
    cells = {}
    with open(gen) as f:
        for row in csv.DictReader(f):
            cells[(row['impl'], int(row['size_bytes']))] = row
    return cells, samples(log)


def f4_md5(target):
    o = os.path.join(ROOT, 'build', target, 'src', 'fast.o')
    out = '/tmp/p7_accept_fast_%s.text' % target
    if not os.path.exists(o):
        return None
    subprocess.run(['objcopy', '-O', 'binary', '--only-section=.text', o, out],
                   check=True)
    with open(out, 'rb') as f:
        return hashlib.md5(f.read()).hexdigest()


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    tag = sys.argv[1]
    cands = sys.argv[2:]
    overall = True
    csv_rows = []
    for cand in cands:
        print('=' * 104)
        print('ACCEPTANCE TEST  candidate=%s  campaign=%s' % (cand, tag))
        print('=' * 104)
        for target in TARGETS:
            cells, samp = analyse(tag, cand, target)
            if cells is None:
                print('  %-7s no analysis' % target)
                overall = False
                continue
            print('\n  --- %s ---' % target)
            # ---- A1: 128 B, min AND median ----
            k = cells.get((cand, A1_SIZE))
            p = cells.get((PREV, A1_SIZE))
            g = cells.get(('glibc', A1_SIZE))
            d_min = float(k['delta_vs_prev_pct'])
            km, pm = median(samp[(cand, A1_SIZE)]), median(samp[(PREV, A1_SIZE)])
            d_med = 100.0 * (pm - km) / pm
            a1 = d_min >= A1_FLOOR and d_med >= A1_FLOOR
            csv_rows.append([target, cand, A1_SIZE, 'A1',
                             '%.4f' % float(p['min_of_mins_ns']),
                             '%.4f' % float(k['min_of_mins_ns']),
                             '%.4f' % pm, '%.4f' % km,
                             '%+.2f' % d_min, '%+.2f' % d_med,
                             '%+.2f' % float(k['delta_vs_glibc_pct']),
                             '%.2f' % float(k['null_band_pct']), k['tier'],
                             '%.2f' % float(k['tier_floor_pct']),
                             k['tier_verdict_vs_prev'],
                             'PASS' if a1 else 'FAIL', ''])
            print('  A1  %d B vs F4   d_min %+7.2f%%   d_med %+7.2f%%'
                  '   (floor >= %.2f%%)   %s'
                  % (A1_SIZE, d_min, d_med, A1_FLOOR, 'PASS' if a1 else 'FAIL'))
            gmins = float(g['min_of_mins_ns']) if g else float('nan')
            print('      mins: F4 %s ns, %s %s ns | medians: F4 %.4f ns, '
                  '%s %.4f ns | vs glibc: %.4f ns -> %s'
                  % (p['min_of_mins_ns'], cand, k['min_of_mins_ns'], pm, cand,
                     km, gmins, k['tier_verdict_vs_glibc']))
            if not a1:
                overall = False
            # ---- A2: regression gate ----
            print('  A2  regression gate (band rule: |delta| <= max(%.1f%%, band))'
                  % FLOOR_RULE3)
            a2_fail = []
            for size in GATE_SIZES:
                r = cells.get((cand, size))
                if r is None:
                    continue
                d = float(r['delta_vs_prev_pct'])
                band = float(r['null_band_pct'])
                eff = max(FLOOR_RULE3, band)
                tier_v = r['tier_verdict_vs_prev']
                ok_band = abs(d) <= eff
                ok_tier = tier_v != 'LOSS'
                ok_back = d >= A2_BACKSTOP
                bad = not (ok_band and ok_tier and ok_back)
                if bad:
                    a2_fail.append(size)
                csv_rows.append([target, cand, size, 'A2',
                                 '%.4f' % float(cells[(PREV, size)]['min_of_mins_ns']),
                                 '%.4f' % float(r['min_of_mins_ns']),
                                 '%.4f' % median(samp[(PREV, size)]),
                                 '%.4f' % median(samp[(cand, size)]),
                                 '%+.2f' % d, '',
                                 '%+.2f' % float(r['delta_vs_glibc_pct']),
                                 '%.2f' % band, r['tier'],
                                 '%.2f' % eff, tier_v,
                                 'ok' if not bad else 'FAIL',
                                 'REGRESSION' if bad else 'ok'])
                print('      %9d B  d %+8.2f%%  band %6.2f%%  floor %6.2f%%  '
                      'tier %s %-9s  band:%s tier:%s backstop:%s  %s'
                      % (size, d, band, eff, r['tier'], tier_v,
                         'ok' if ok_band else 'FAIL',
                         'ok' if ok_tier else 'FAIL',
                         'ok' if ok_back else 'FAIL',
                         'REGRESSION' if bad else 'ok'))
            if a2_fail:
                overall = False
                print('  A2  FAIL at: %s'
                      % ', '.join('%d B' % s for s in a2_fail))
            else:
                print('  A2  PASS - no regression against F4')
            # ---- non-gate report rows ----
            print('  report rows (not gates):')
            for size in REPORT_SIZES:
                if size in GATE_SIZES or size == A1_SIZE:
                    continue
                r = cells.get((cand, size))
                if r is None:
                    continue
                print('      %9d B  d %+8.2f%%  band %6.2f%%  tier %s'
                      % (size, float(r['delta_vs_prev_pct']),
                         float(r['null_band_pct']), r['tier_verdict_vs_prev']))
            # ---- A3: F4 md5 ----
            m = f4_md5(target)
            ok3 = (m == EXPECT_MD5[target])
            overall = overall and ok3
            print('  A3  F4 .text md5 %s  %s' % (m, 'PASS' if ok3 else 'FAIL'))
        print()
    print('=' * 104)
    print('OVERALL: %s' % ('PASS' if overall else 'FAIL'))
    print('=' * 104)
    out = os.path.join(ROOT, 'docs', 'data',
                       'phase7_%s_acceptance_summary.csv' % tag)
    hdr = ['target', 'candidate', 'size', 'criterion', 'f4_min', 'cand_min',
           'f4_median', 'cand_median', 'delta_vs_f4_min_pct',
           'delta_vs_f4_median_pct', 'delta_vs_glibc_pct', 'null_band_pct',
           'tier', 'tier_floor_pct', 'verdict_vs_f4', 'criterion_verdict',
           'a2_verdict']
    with open(out, 'w', newline='') as f:
        w = csv.writer(f, lineterminator='\n')
        w.writerow(hdr)
        w.writerows(csv_rows)
    print('wrote %s (%d rows)' % (out, len(csv_rows)))
    return 0 if overall else 1


if __name__ == '__main__':
    sys.exit(main())
