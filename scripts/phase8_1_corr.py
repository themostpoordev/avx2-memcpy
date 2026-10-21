#!/usr/bin/env python3
"""Phase 8.1 Q2/Q3: is the 1 MiB split a property of the KERNEL or of the run?

At 1 MiB the harness's 50 ms budget truncates the timed case, so `iters` is
effectively a second, independent measurement of the per-copy speed in that
rep. If min_ns tracks iters, the split is the run's speed; if it does not,
the split is in the copy itself.
"""
from collections import defaultdict

LOGS = {t: 'docs/data/raw/phase7_p74_%s.windows.log' % t
        for t in ('znver2', 'znver3', 'v3')}


def load(path):
    rows = []
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
        rows.append(dict(w=win, rep=int(p['rep']), impl=fl[1],
                         n=int(p['n']), min_ns=float(p['min_ns']),
                         med_ns=float(p['med_ns']),
                         iters=int(p.get('iters', 0))))
    return rows


for t in ('znver2',):
    rows = load(LOGS[t])
    print('=' * 100)
    print('1 MiB, %s: does min_ns track iters (= run speed)?' % t)
    print('=' * 100)
    for impl in ('fast', 'f4_baseline', 'f4win64al', 'glibc', 'naive',
                 'ref_scalar'):
        rs = [r for r in rows if r['impl'] == impl and r['n'] == 1048576]
        if not rs:
            continue
        print('\n--- %s ---' % impl)
        print('  %-4s %-5s %10s %10s %8s' % ('win', 'rep', 'min_ns', 'med_ns',
                                              'iters'))
        for r in sorted(rs, key=lambda x: (x['w'], x['rep'])):
            print('  %-4d %-5d %10.1f %10.1f %8d'
                  % (r['w'], r['rep'], r['min_ns'], r['med_ns'], r['iters']))

print()
print('=' * 100)
print('ALL IMPLS at 1 MiB: min_ns and iters, side by side per (window, rep)')
print('=' * 100)
rows = load(LOGS['znver2'])
impls = ['ref_scalar', 'glibc', 'fast', 'f4_baseline', 'f4win64al', 'naive']
by = {(r['impl'], r['w'], r['rep']): r for r in rows if r['n'] == 1048576}
print('%-5s %-4s | %s' % ('win', 'rep',
                         ' | '.join('%-22s' % i for i in impls)))
for w in range(1, 10):
    for rep in range(0, 5):
        cells = []
        for i in impls:
            r = by.get((i, w, rep))
            cells.append('%-22s' % ('%8.0f/%5d' % (r['min_ns'], r['iters'])
                                     if r else '-'))
        print('%-5d %-4d | %s' % (w, rep, ' | '.join(cells)))
