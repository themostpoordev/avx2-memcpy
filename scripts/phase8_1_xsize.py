#!/usr/bin/env python3
"""Phase 8.1 Q2/Q3: is the 1 MiB fast mode a property of the 1 MiB path, or a
global 'the host was in a good state' event?  Cross-size correlation of the
per-rep minima, on the committed log only.
"""
from collections import defaultdict

LOGS = {t: 'docs/data/raw/phase7_p74_%s.windows.log' % t
        for t in ('znver2', 'znver3', 'v3')}
SIZES = [256, 512, 1024, 4096, 16384, 65536, 262144, 1048576]


def load(path):
    per = defaultdict(dict)
    win = None
    for line in open(path, errors='replace'):
        if line.startswith('# WINDOW '):
            win = int(line.split()[2])
            continue
        if not line.startswith('REPS '):
            continue
        fl = line.split()
        if fl[2] != 'memcpy':
            continue
        p = dict(kv.split('=', 1) for kv in fl[3:] if '=' in kv)
        if int(p.get('migrated', '0')):
            continue
        per[(win, int(p['rep']))][(fl[1], int(p['n']))] = float(p['min_ns'])
    return per


def bimodal_flag(vals):
    """True if the sample splits into two clusters (largest gap > 8% of the span)."""
    v = sorted(vals)
    if len(v) < 4:
        return False
    gaps = [(v[i + 1] - v[i], i) for i in range(len(v) - 1)]
    gap, i = max(gaps)
    return gap > 0.08 * (v[-1] - v[0])


for t in ('znver2',):
    per = load(LOGS[t])
    print('=' * 96)
    print('%s: per-rep minima of the DELIVERABLE, normalised to each size\'s own'
          % t)
    print('fast mode = the rep\'s minimum is in the lower cluster.')
    print('=' * 96)
    flags = {}
    for size in SIZES:
        vals = [d[('fast', size)] for d in per.values() if ('fast', size) in d]
        if not vals:
            continue
        v = sorted(vals)
        gaps = [(v[i + 1] - v[i], i) for i in range(len(v) - 1)]
        gap, i = max(gaps)
        thr = (v[i] + v[i + 1]) / 2
        f = {k: (d[('fast', size)] < thr) for k, d in per.items()
             if ('fast', size) in d}
        flags[size] = f
        nlow = sum(1 for x in f.values() if x)
        print('  %8d B  min %.1f  max %.1f  cluster gap %.1f ns (%.1f%%)  '
              'fast-mode reps %2d/45  slow-mode reps %2d/45'
              % (size, v[0], v[-1], gap, 100 * gap / v[0], nlow, 45 - nlow))

    print()
    print('  cross-size agreement of the fast-mode flag (45 reps):')
    print('  %10s' % '', ' '.join('%9d' % s for s in SIZES if s in flags))
    for s1 in SIZES:
        if s1 not in flags:
            continue
        line = []
        for s2 in SIZES:
            if s2 not in flags:
                continue
            common = set(flags[s1]) & set(flags[s2])
            both = sum(1 for k in common if flags[s1][k] and flags[s2][k])
            line.append('%2d/%2d' % (both, len(common)))
        print('  %8d B' % s1, ' '.join('%9s' % x for x in line))

    print()
    print('  and the same for glibc (its own fast-mode flag at each size):')
    for size in SIZES:
        vals = [d[('glibc', size)] for d in per.values() if ('glibc', size) in d]
        if not vals:
            continue
        v = sorted(vals)
        gaps = [(v[i + 1] - v[i], i) for i in range(len(v) - 1)]
        gap, i = max(gaps)
        print('  %8d B  min %.1f  max %.1f  biggest gap %.1f ns (%.1f%% of min)'
              % (size, v[0], v[-1], gap, 100 * gap / v[0]))
