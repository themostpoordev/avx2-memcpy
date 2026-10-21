#!/usr/bin/env python3
"""Check every published delta in docs/RESULTS.md against the committed CSV.

    python3 scripts/check_published.py

The CSV is docs/data/phase7_4_summary.csv (the committed reference campaign).
The markdown is parsed, not trusted: each table row's three per-target deltas
are compared with the CSV's, for both statistics, and every mismatch is
reported. 0 mismatches means the published table IS the CSV.
"""
import csv
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV = os.path.join(ROOT, 'docs', 'data', 'phase7_4_summary.csv')
DOC = os.path.join(ROOT, 'docs', 'RESULTS.md')
TARGETS = ['znver2', 'znver3', 'v3']
LABEL = {'8 B': 8, '16 B': 16, '32 B': 32, '64 B': 64, '128 B': 128,
         '256 B': 256, '512 B': 512, '1 KiB': 1024, '4 KiB': 4096,
         '16 KiB': 16384, '64 KiB': 65536, '256 KiB': 262144,
         '1 MiB': 1048576, '4 MiB': 4194304}


def main():
    rows = list(csv.DictReader(open(CSV)))
    by = {(int(r['size']), r['target']): r for r in rows}

    bad = 0
    checked = 0
    for line in open(DOC):
        m = re.match(r'\|\s*([0-9]+ (?:B|KiB|MiB))\s*\|(.*)\|\s*$', line)
        if not m or m.group(1) not in LABEL:
            continue
        size = LABEL[m.group(1)]
        cells = [c.strip() for c in m.group(2).split('|')]
        deltas = []
        for c in cells:
            vals = re.findall(r'[+−-]?\d+\.\d+', c.replace('−', '-'))
            if len(vals) == 3:
                deltas.append([float(v.replace('−', '-')) for v in vals])
        if len(deltas) < 2:
            continue
        for idx, col in ((0, 'd_glibc_min_pct'), (1, 'd_glibc_median_pct')):
            for ti, t in enumerate(TARGETS):
                r = by.get((size, t))
                if r is None:
                    continue
                exp = float(r[col])
                got = deltas[idx][ti]
                checked += 1
                if abs(exp - got) > 0.011:
                    print('MISMATCH %-8s %-6s %-18s doc=%+.2f csv=%+.2f'
                          % (m.group(1), t, col, got, exp))
                    bad += 1
    print('checked %d published deltas against %s: %d mismatches'
          % (checked, os.path.basename(CSV), bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
