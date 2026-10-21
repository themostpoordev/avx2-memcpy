#!/usr/bin/env python3
"""Phase 8.1: the 1 MiB distribution and the host-vs-kernel test.

Reads the committed Phase 7.4 log only. No measurement.
"""
from collections import defaultdict

LOGS = {t: 'docs/data/raw/phase7_p74_%s.windows.log' % t
        for t in ('znver2', 'znver3', 'v3')}
IMPLS = ['fast', 'f4_baseline', 'glibc', 'naive', 'ref_scalar', 'f4win64al']


def load(path):
    """(window, rep) -> {(impl,size): min_ns}"""
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


def dist(per, impl, size):
    v = sorted(d[(impl, size)] for d in per.values() if (impl, size) in d)
    c = defaultdict(int)
    for x in v:
        c[round(x, 4)] += 1
    return v, c


print('=' * 100)
print('Q1  1 MiB PER-REP DISTRIBUTION (45 reps per impl per target)')
print('=' * 100)
for t in ('znver2', 'znver3', 'v3'):
    per = load(LOGS[t])
    print('\n--- %s ---' % t)
    for impl in ('fast', 'glibc'):
        v, c = dist(per, impl, 1048576)
        print('  %-9s min %.1f  med %.1f  max %.1f  distinct %d'
              % (impl, v[0], v[len(v) // 2], v[-1], len(c)))
        print('            %s' % '  '.join('%.1fx%d' % (k, n)
                                         for k, n in sorted(c.items())))

print()
print('=' * 100)
print('Q2  IS THE MODE SPLIT HOST-DRIVEN?  (slow = a rep whose value is in the')
print('    upper cluster; marked per (window, rep) for each impl and size)')
print('=' * 100)
for t in ('znver2', 'znver3', 'v3'):
    per = load(LOGS[t])
    # the slow threshold per impl/size = the midpoint between the two clusters
    print('\n--- %s ---' % t)
    for size in (1048576, 512, 16384, 256):
        marks = []
        for impl in ('fast', 'glibc'):
            v, c = dist(per, impl, size)
            vals = sorted(c)
            # find the biggest gap in the sorted distinct values -> the split
            gaps = [(vals[i + 1] - vals[i], i) for i in range(len(vals) - 1)]
            gap, i = max(gaps)
            thr = (vals[i] + vals[i + 1]) / 2
            slow = {}
            for (w, r), d in per.items():
                if (impl, size) in d:
                    slow[(w, r)] = d[(impl, size)] > thr
            marks.append((impl, slow, thr, gap))
        # agreement: do fast and glibc mark the same reps slow?
        f = dict((k, v) for k, v in marks[0][1].items())
        g = dict((k, v) for k, v in marks[1][1].items())
        common = sorted(set(f) & set(g))
        both = sum(1 for k in common if f[k] and g[k])
        onlyf = sum(1 for k in common if f[k] and not g[k])
        onlyg = sum(1 for k in common if g[k] and not f[k])
        neither = sum(1 for k in common if not f[k] and not g[k])
        print('  %8d B  split gap %s   fast/glibc slow agreement: '
              'both %d, fast-only %d, glibc-only %d, neither %d'
              % (size,
                 ' '.join('%s=%.0f' % (m[0], m[3]) for m in marks),
                 both, onlyf, onlyg, neither))
        # contiguity of the fast kernel's slow reps
        for impl, slow, thr, gap in marks:
            seq = ''.join('S' if slow[(w, r)] else '.'
                          for w in range(1, 10) for r in range(0, 5))
            print('      %-6s %s' % (impl, seq))
