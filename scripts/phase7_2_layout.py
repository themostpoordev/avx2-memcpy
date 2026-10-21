#!/usr/bin/env python3
"""Phase 7.2 layout evidence: the P1 and P2 checks, from the built artefacts.

    python3 scripts/phase7_2_layout.py

P1  every branch target reachable when n <= 64 is 64-byte aligned.  Checked
    on the LINKED binary, because the absolute alignment of the function is a
    link-time property (the .text section's alignment, raised by the
    .p2align that -falign-labels emits), not an object-time one.

P2  the 16..64 B code path is instruction-identical to its control.  The
    control for the alignment flag is src/rungs/avx2_f4win64.c (same source,
    same knob, WITHOUT -falign-labels=64).  The shipped kernel is reported
    alongside but is NOT a valid control on v3, where the TUNED_WIN64 knob
    itself changes the 16..64 B path.

Padding nops are dropped and branch targets are replaced by a placeholder, so
only the instruction stream is compared; the target offsets are then listed
side by side, where they are EXPECTED to differ.
"""
import re
import subprocess
import sys
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGETS = ['znver2', 'znver3', 'v3']
# The symbol under test. Default: the rung. Override with argv[1] to check the
# promoted deliverable (memcpy_fast) instead - the check must be run on the
# artefact that ships, not on the rung it came from.
ALIGNED = os.environ.get('P2_LAYOUT_SYMBOL', 'memcpy_f4win64_aligned')
CONTROL = 'memcpy_avx2_f4win64'     # same knob, no -falign-labels
SHIPPED = 'memcpy_fast'

PADDING_MN = ('nop', 'nopl', 'nopw', 'nopq', 'data16', 'cs', 'xchg')


def is_padding(mn, ops):
    """Padding gcc inserted for alignment: never executed."""
    m = mn.strip()
    if m.startswith('nop'):
        return True
    if m == 'data16' and 'nop' in ops:
        return True
    if m == 'xchg' and ops.strip().replace(' ', '') == '%ax,%ax':
        return True
    return False


def objdump(*args):
    return subprocess.run(['objdump', '-d', '--no-show-raw-insn'] + list(args),
                          capture_output=True, text=True).stdout


def parse_function(text, symbol):
    """-> {addr: (mnemonic, operands, target_addr_or_None)}"""
    out = {}
    started = False
    for line in text.splitlines():
        if re.match(r'^[0-9a-f]+ <%s>:' % re.escape(symbol), line):
            started = True
            continue
        if started:
            if not line.strip():
                if out:
                    break
                continue
            m = re.match(r'^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$', line)
            if not m:
                continue
            addr = int(m.group(1), 16)
            mn = m.group(2)
            ops = m.group(3).strip()
            tgt = None
            mt = re.search(r'\b([0-9a-f]+)\s+<', ops)
            if mt:
                tgt = int(mt.group(1), 16)
            out[addr] = (mn, ops, tgt)
    return out


def norm(mn, ops, tgt):
    """Normalised instruction text: padding dropped, branch target masked."""
    if is_padding(mn, ops):
        return None
    ops = re.sub(r'\b[0-9a-f]+ <[^>]*>', '<T>', ops)
    ops = re.sub(r'0x[0-9a-f]+ <[^>]*>', '<T>', ops)
    return '%s %s' % (mn, ops)


def blocks(ins):
    """Split a function's instructions into basic blocks keyed by start addr."""
    starts = set()
    for a, (mn, ops, tgt) in ins.items():
        if tgt is not None and tgt in ins:
            starts.add(tgt)
        if mn.startswith('j') and mn != 'jmp':
            pass
    # a block starts at the function entry, at any branch target, and after a
    # terminator (ret / jmp)
    addrs = sorted(ins)
    starts.add(addrs[0])
    for i, a in enumerate(addrs):
        mn = ins[a][0]
        if mn in ('ret', 'jmp') and i + 1 < len(addrs):
            starts.add(addrs[i + 1])
    b = {}
    cur = None
    for a in addrs:
        if a in starts:
            cur = a
            b[cur] = []
        if cur is not None:
            b[cur].append(a)
    return b


def reachable_n_le_64(ins, entry):
    """Block starts reachable with n <= 64.

    A conditional compare against a size bound inside [0,64] can go either
    way within that range, so BOTH successors of such a branch are reachable;
    a compare that is provably one way (e.g. `ja` after `cmp $0x40` with
    n <= 64) contributes only its fall-through.
    """
    b = blocks(ins)
    # classify each conditional branch: is its condition decided for n <= 64?
    decided = {}
    for a, (mn, ops, tgt) in ins.items():
        if mn in ('ja', 'jb', 'jbe', 'jae', 'je', 'jne', 'jg', 'jge', 'jl',
              'jle'):
            # look back for the compare that sets the flags
            dec = None
            for prev in sorted([x for x in ins if x < a], reverse=True)[:3]:
                pmn = ins[prev][0]
                if pmn == 'cmp':
                    m = re.search(r'\$0x([0-9a-f]+),%rdx', ins[prev][1])
                    if m:
                        bound = int(m.group(1), 16)
                        # n <= 64:  ja (n > B) is never taken when B >= 64
                        #          jb (n < B) is never taken when B <= 64
                        #          jbe (n <= B) is always taken when B >= 64
                        if mn in ('ja', 'jg'):
                            if bound >= 64:
                                dec = 'not-taken'
                        elif mn in ('jb', 'jl'):
                            if bound > 64:
                                dec = 'taken'
                        elif mn in ('jbe', 'jle'):
                            if bound >= 64:
                                dec = 'taken'
                    break
                if pmn in ('ret', 'jmp'):
                    break
            decided[a] = dec
    seen = set()
    stack = [entry]
    while stack:
        a = stack.pop()
        if a in seen or a not in ins:
            continue
        seen.add(a)
        mn, ops, tgt = ins[a]
        if mn.startswith('j') and mn != 'jmp' and tgt is not None:
            d = decided.get(a)
            fall = None
            nxt = sorted([x for x in ins if x > a])
            if nxt:
                fall = nxt[0]
            if d == 'not-taken':
                if fall:
                    stack.append(fall)
                continue
            if d == 'taken':
                stack.append(tgt)
                continue
            stack.append(tgt)
            if fall:
                stack.append(fall)
            continue
        if mn == 'ret':
            continue
        nxt = sorted([x for x in ins if x > a])
        if nxt:
            stack.append(nxt[0])
    return sorted(seen), b


def region_16_64(ins):
    """The 16..64 B path: the entry block (through its first `ret`) plus the
    32..64 B block that the entry's `cmp $0x1f` branch targets.  The n <= 15
    class and every n > 64 path are deliberately EXCLUDED: they are different
    size classes, and the n <= 15 class is reported separately because the
    alignment flag moves it too."""
    addrs = sorted(ins)
    region = []
    for a in addrs:
        region.append(a)
        if ins[a][0] == 'ret':
            break
    tgt = None
    for a in addrs:
        mn, ops, _t = ins[a]
        if mn == 'cmp' and '$0x1f' in ops:
            for b in addrs:
                if b > a and ins[b][0].startswith('j') and ins[b][0] != 'jmp':
                    tgt = ins[b][2]
                    break
            break
    if tgt is not None and tgt in ins:
        started = False
        for a in addrs:
            if a == tgt:
                started = True
            if not started:
                continue
            region.append(a)
            if ins[a][0] == 'ret':
                break
    return region


def region_stream(ins, b, starts):
    """Normalised instruction text of the given blocks, in address order."""
    out = []
    for s in starts:
        for a in b.get(s, []):
            mn, ops, tgt = ins[a]
            t = norm(mn, ops, tgt)
            if t is not None:
                out.append(t)
    return out


def stream_16_64(ins):
    return [x for x in (norm(ins[a][0], ins[a][1], ins[a][2])
                        for a in region_16_64(ins)) if x is not None]


def main():
    print('=' * 96)
    print('P1  ALIGNMENT PRECONDITION  (linked binary, absolute addresses)')
    print('=' * 96)
    p1_ok = True
    for t in TARGETS:
        binary = os.path.join(ROOT, 'bench', 'bench_%s' % t)
        text = objdump(binary)
        ins = parse_function(text, ALIGNED)
        if not ins:
            print('  %-7s FAIL: %s not found in %s' % (t, ALIGNED, binary))
            p1_ok = False
            continue
        entry = min(ins)
        reach, b = reachable_n_le_64(ins, entry)
        tgts = sorted({ins[a][2] for a in reach
                       if ins[a][2] is not None})
        bad = [x for x in tgts if x % 64 != 0]
        p1_ok = p1_ok and not bad
        print('  %-7s entry=0x%x  reachable blocks at n<=64: %s'
              % (t, entry, ' '.join('0x%x' % x for x in reach)))
        print('          branch targets: %s'
              % ' '.join('0x%x' % x for x in tgts))
        print('          all 64-byte aligned: %s   %s'
              % ('YES' if not bad else 'NO -> %s' % ' '.join('0x%x' % x for x in bad),
                 'PASS' if not bad else 'FAIL'))
    print()
    print('=' * 96)
    print('P2  INSTRUCTION IDENTITY  (16..64 B region)')
    print('=' * 96)
    p2_ok = True
    for t in TARGETS:
        binary = os.path.join(ROOT, 'bench', 'bench_%s' % t)
        text = objdump(binary)
        streams = {}
        targets = {}
        for sym in (ALIGNED, CONTROL, SHIPPED):
            ins = parse_function(text, sym)
            if not ins:
                print('  %-7s FAIL: %s not found' % (t, sym))
                p2_ok = False
                streams[sym] = None
                continue
            entry = min(ins)
            reach, b = reachable_n_le_64(ins, entry)
            streams[sym] = stream_16_64(ins)
            targets[sym] = {a: ins[a][2] for a in reach}
        a_ = streams.get(ALIGNED)
        c_ = streams.get(CONTROL)
        s_ = streams.get(SHIPPED)
        if a_ is None or c_ is None:
            p2_ok = False
            continue
        same = (a_ == c_)
        p2_ok = p2_ok and same
        print('  %-7s vs CONTROL (%s): %s   (%d instructions)'
              % (t, CONTROL, 'IDENTICAL' if same else 'DIFFERS', len(a_)))
        if not same:
            import difflib
            for l in difflib.unified_diff(c_, a_, 'control', 'aligned',
                                          lineterm='', n=1):
                print('        %s' % l)
        if s_ is not None:
            same_s = (a_ == s_)
            print('          vs SHIPPED (%s): %s   (control vs shipped: %s)'
                  % (SHIPPED, 'IDENTICAL' if same_s else 'DIFFERS',
                     'IDENTICAL' if c_ == s_ else 'DIFFERS'))
    print()
    print('=' * 96)
    print('P1 %s   P2 %s' % ('PASS' if p1_ok else 'FAIL',
                             'PASS' if p2_ok else 'FAIL'))
    print('=' * 96)
    return 0 if (p1_ok and p2_ok) else 1


if __name__ == '__main__':
    sys.exit(main())
