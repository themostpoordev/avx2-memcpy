#!/usr/bin/env python3
"""Regenerate docs/data/f128b_reference_summary.csv from the committed logs.

    python3 scripts/f128b_summary.py

Reads docs/data/raw/f128b_<target>.windows.log (the committed reference
campaign) and writes the summary CSV that every headline row in
docs/RESULTS.md is quoted from. No number in the CSV is typed in by hand:
it is computed here from the log, and the log is committed.

The kernel under test is the shipped F4, which the campaign recorded under
the name `avx2_final_f4_win` (`avx2_final` plus F4's TUNED_WINDOW).
The baseline is `glibc`, measured in the same process and the same windows.

Sample definition (must match docs/METHODOLOGY.md):
  - one sample per timed rep: the rep's `min_ns` (fastest single batch)
  - reps with migrated=1 are dropped (none are, in this campaign: n=45)
  - min_of_mins    = min of the 45 samples
  - median_of_mins = median of the 45 samples
  - spread%        = (p99 - p25) / median * 100
  - tier           = A if the per-window min spread <= 10%, else B
  - floor          = Tier A: max(1.5%, null band); Tier B: 25%
  - null band      = |naive vs ref_scalar| at the same size (the measured
                     noise floor of the instrument, same byte loop)
"""
import csv
import os
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW = os.path.join(ROOT, 'docs', 'data', 'raw')
OUT = os.path.join(ROOT, 'docs', 'data', 'f128b_reference_summary.csv')

KERNEL = 'avx2_final_f4_win'
OP = 'memcpy'
TARGETS = ['znver2', 'znver3', 'v3']
SIZES = [16, 32, 64, 128, 256, 512, 1024, 4096, 16384, 65536, 262144,
         1048576, 4194304]
FLOOR_A = 1.5
FLOOR_B = 25.0
SPREAD_MAX = 10.0


def parse(path):
    """(impl, size) -> [(window, min_ns, migrated)]

    The campaign measures one op (memcpy), but the harness also emits
    companion `memset` rows for glibc and ref_scalar. Those rows are
    interleaved with the memcpy rows in the same log, so the op field
    must be filtered on: glibc's memset is strictly faster than its
    memcpy (no read stream), and letting those samples into the
    baseline makes glibc look up to 2x faster than it is.
    """
    rows = defaultdict(list)
    win = None
    with open(path) as f:
        for line in f:
            if line.startswith('# WINDOW '):
                win = int(line.split()[2])
                continue
            if not line.startswith('REPS '):
                continue
            fields = line.split()
            impl = fields[1]
            op = fields[2]
            if op != OP:
                continue
            kv = {}
            for item in fields[3:]:
                if '=' in item:
                    k, v = item.split('=', 1)
                    kv[k] = v
            n = int(kv.get('n', '-1'))
            mn = float(kv.get('min_ns', '-1'))
            mig = int(kv.get('migrated', '0'))
            if mn <= 0 or n < 0:
                continue
            rows[(impl, n)].append((win, mn, mig))
    return rows


def pct(sorted_vals, q):
    """Linear-interpolation percentile over the sorted sample."""
    n = len(sorted_vals)
    if n == 0:
        return float('nan')
    if n == 1:
        return sorted_vals[0]
    idx = q * (n - 1)
    lo = int(idx)
    hi = min(lo + 1, n - 1)
    frac = idx - lo
    return sorted_vals[lo] * (1 - frac) + sorted_vals[hi] * frac


def samples(rows, impl, size):
    return sorted(mn for (_w, mn, mig) in rows.get((impl, size), [])
                  if mig == 0)


def verdict(delta, floor):
    if delta > floor:
        return 'WIN'
    if delta < -floor:
        return 'LOSS'
    return 'tie'


def main():
    out_rows = []
    for target in TARGETS:
        log = os.path.join(RAW, 'f128b_%s.windows.log' % target)
        rows = parse(log)
        for size in SIZES:
            ks = samples(rows, KERNEL, size)
            gs = samples(rows, 'glibc', size)
            if not ks or not gs:
                continue
            # tier: spread of the per-window minima
            per_window = defaultdict(list)
            for w, mn, mig in rows.get((KERNEL, size), []):
                if mig == 0:
                    per_window[w].append(mn)
            wmin = [min(v) for v in per_window.values()]
            wspread = 100.0 * (max(wmin) - min(wmin)) / min(wmin)
            # null band: naive vs ref_scalar, same byte loop, same windows
            nv = samples(rows, 'naive', size)
            rv = samples(rows, 'ref_scalar', size)
            band = abs(100.0 * (rv[0] - nv[0]) / rv[0]) if nv and rv else -1.0
            tier = 'A' if wspread <= SPREAD_MAX else 'B'
            floor = FLOOR_B if tier == 'B' else max(FLOOR_A, band)
            kmin, gmin = ks[0], gs[0]
            kmed, gmed = pct(ks, 0.5), pct(gs, 0.5)
            d_min = 100.0 * (gmin - kmin) / gmin
            d_med = 100.0 * (gmed - kmed) / gmed
            spread = 100.0 * (pct(ks, 0.99) - pct(ks, 0.25)) / kmed
            out_rows.append([
                size, target,
                '%.4f' % kmin, '%.4f' % kmed,
                '%.4f' % pct(ks, 0.25), '%.4f' % pct(ks, 0.75),
                '%.4f' % pct(ks, 0.99),
                '%.4f' % gmin, '%.4f' % gmed,
                '%+.2f' % d_min, '%+.2f' % d_med,
                '%.2f' % spread, tier, '%.2f' % floor,
                verdict(d_min, floor), verdict(d_med, floor),
            ])

    header = ['size', 'target', 'k_min', 'k_med', 'k_p25', 'k_p75', 'k_p99',
              'g_min', 'g_med', 'delta_min_pct', 'delta_med_pct', 'spread_pct',
              'tier', 'floor', 'verdict_min', 'verdict_med']
    with open(OUT, 'w', newline='') as f:
        w = csv.writer(f, lineterminator='\n')
        w.writerow(header)
        w.writerows(out_rows)
    print('wrote %s (%d rows)' % (OUT, len(out_rows)))


if __name__ == '__main__':
    main()
