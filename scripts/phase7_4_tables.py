#!/usr/bin/env python3
"""Emit the markdown table rows for docs/RESULTS.md from the committed CSV.

    python3 scripts/phase7_4_tables.py

Everything printed here is read from docs/data/phase7_4_summary.csv, which is
itself regenerated from the committed campaign logs. No number in the published
tables is typed by hand.
"""
import csv
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV = os.path.join(ROOT, 'docs', 'data', 'phase7_4_summary.csv')
LADDER = [16, 32, 64, 128, 256, 512, 1024, 4096, 16384, 65536, 262144,
          1048576, 4194304]
LABEL = {16: '16 B', 32: '32 B', 64: '64 B', 128: '128 B', 256: '256 B',
         512: '512 B', 1024: '1 KiB', 4096: '4 KiB', 16384: '16 KiB',
         65536: '64 KiB', 262144: '256 KiB', 1048576: '1 MiB',
         4194304: '4 MiB', 8: '8 B'}
TARGETS = ['znver2', 'znver3', 'v3']


def main():
    rows = list(csv.DictReader(open(CSV)))
    by = {(int(r['size']), r['target']): r for r in rows}

    def triple(size, col, bold=True):
        out = []
        for t in TARGETS:
            r = by.get((size, t))
            if not r:
                out.append('-')
                continue
            v = '%+.2f' % float(r[col])
            if bold and v.startswith('+'):
                v = '**%s**' % v
            out.append(v)
        return ' / '.join(out)

    print('### headline wins table (floor vs glibc)')
    print('| size | Δ_min (znver2 / znver3 / v3) | Δ_med (znver2 / znver3 / v3) '
          '| spread% | tier | verdict |')
    print('|--:|--:|--:|--:|:--:|:--|')
    for size in (16, 32, 64, 128, 256, 512):
        r = by.get((size, 'znver2'))
        vs = []
        for t in TARGETS:
            v = by[(size, t)]['v_glibc_min']
            vs.append({'WIN': 'WIN', 'measured': 'measured',
                       'LOSS': 'LOSS'}[v])
        verdict = ('WIN x3' if all(v == 'WIN' for v in vs) else
                   'WIN %d/3' % sum(1 for v in vs if v == 'WIN'))
        tiers = '/'.join(by[(size, t)]['tier'] for t in TARGETS)
        print('| %s | %s | %s | %s | %s | %s |'
              % (LABEL[size], triple(size, 'd_glibc_min_pct'),
                 triple(size, 'd_glibc_median_pct'),
                 '/'.join('%.1f' % float(by[(size, t)]['fast_spread_pct'])
                          for t in TARGETS),
                 tiers, verdict))

    print()
    print('### full ladder table')
    print('| size | Δ_min (znver2 / znver3 / v3) | Δ_med (znver2 / znver3 / v3) '
          '| spread% | tier | verdict |')
    print('|--:|--:|--:|--:|:--:|:--|')
    for size in LADDER:
        vs = [by[(size, t)]['v_glibc_min'] for t in TARGETS]
        if all(v == 'WIN' for v in vs):
            verdict = 'WIN x3'
        elif all(v == 'measured' for v in vs):
            verdict = 'measured x3'
        elif all(v == 'LOSS' for v in vs):
            verdict = '**LOSS x3**'
        else:
            verdict = 'WIN %d/3' % sum(1 for v in vs if v == 'WIN')
        if 'LOSS' in vs and 'WIN' not in vs and 'measured' in vs:
            verdict = 'measured 2/3, one certified LOSS (%s)' % \
                TARGETS[vs.index('LOSS')]
        tiers = '/'.join(by[(size, t)]['tier'] for t in TARGETS)
        print('| %s | %s | %s | %s | %s | %s |'
              % (LABEL[size], triple(size, 'd_glibc_min_pct'),
                 triple(size, 'd_glibc_median_pct'),
                 '/'.join('%.1f' % float(by[(size, t)]['fast_spread_pct'])
                          for t in TARGETS),
                 tiers, verdict))

    print()
    print('### extra reported row (8 B, not on the ladder)')
    print('| size | Δ_min (znver2 / znver3 / v3) | Δ_med (znver2 / znver3 / v3) '
          '| spread% | tier | verdict |')
    print('|--:|--:|--:|--:|:--:|:--|')
    print('| 8 B | %s | %s | %s | %s | WIN x3 |'
          % (triple(8, 'd_glibc_min_pct'), triple(8, 'd_glibc_median_pct'),
             '/'.join('%.1f' % float(by[(8, t)]['fast_spread_pct'])
                      for t in TARGETS),
             '/'.join(by[(8, t)]['tier'] for t in TARGETS)))


if __name__ == '__main__':
    main()
