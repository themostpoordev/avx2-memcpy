#!/bin/sh
# scripts/audit_width.sh [--selftest] — the memory-access width gate.
#
#   sh scripts/audit_width.sh              check every kernel TU, all targets
#   sh scripts/audit_width.sh --selftest   prove the detector catches SLP merging
#
# WHY THIS EXISTS
#   `make audit` only proves that the SCALAR_SRCS translation units contain no
#   vector instructions. It says nothing about the width of the accesses inside
#   the vector rungs, and that gap hid the same defect twice:
#
#     43f01dc  the "SSE2" rung's two adjacent 16 B intrinsics were merged by
#              clang's SLP vectorizer into one 32 B vmovups on ymm, so rung 3
#              compiled to rung 4.
#     25faacb  the AVX2 rung's small path (32..63 B) did the same thing: four
#              16 B accesses became 32 B ymm pairs, so the source comment's
#              "never uses a 32-byte access" was false as compiled.
#
#   Both were found by hand, after the fact, by reading objdump. This gate does
#   that reading mechanically, before any benchmark, for every rung.
#
# WHAT IT DOES
#   For each target and each kernel TU, disassemble the object, find the widest
#   DATA-MOVEMENT instruction that touches memory, and compare it with the width
#   the rung declares below. A mismatch fails with rc != 0. Nothing is auto-fixed
#   and nothing is auto-approved: either the source changes or the declaration
#   changes, and a human decides which.
#
#   Classification is deliberately strict about vector instructions: a VEX/EVEX
#   memory operand whose width this script does not know is an ERROR
#   ("unclassified"), never a silent skip, so the gate cannot drift into
#   under-reporting. Non-vector instructions that are not data movement (lea,
#   cmp, test, arithmetic, prefetch) are skipped by design: the audit is about
#   access width, not about every memory read the compiler emits.
#
# WIDENING EXCEPTIONS
#   scripts/audit_width_allow.txt may hold "<source-basename> <bits>  # reason"
#   lines. An entry there is the ONLY way a rung may widen beyond its ladder
#   width, it is printed as an override in the output, and the accompanying
#   source header is expected to state the widened width explicitly. Absence of
#   an entry means strict enforcement against the table below.
set -eu
cd "$(dirname "$0")/.."

ALLOW=scripts/audit_width_allow.txt
SELFTEST=0
[ "${1:-}" = "--selftest" ] && SELFTEST=1

# ---- declared ladder widths (bits) ----------------------------------------
# Unknown kernel sources are NOT silently accepted: add the row or add an
# override entry, and state why.
declared_bits() {
    case "$1" in
        ref_scalar.c|memcpy_naive.c|memset_naive.c)                 echo 8   ;;
        memcpy_word.c|memset_word.c)                                echo 64  ;;
        memcpy_sse2.c|memset_sse2.c)                                echo 128 ;;
        memcpy_avx2.c|memcpy_avx2_pf2.c|memcpy_avx2_pf4.c|memcpy_avx2_nt.c) \
                                                                    echo 256 ;;
        memset_avx2.c|memset_avx2_nt.c)                             echo 256 ;;
        *)                                                          echo ""  ;;
    esac
}

# ---- the classifier -------------------------------------------------------
# Prints "<bits>\t<mnemonic>\t<operands>" for every memory-touching data-movement
# instruction, "UNCLASSIFIED\t..." for a vector access it cannot size.
CLASSIFY='
function reg_bits(p,   x, i) {
    i = index(p, "%")
    if (i == 0) return 0
    x = substr(p, i)
    sub(/[^%a-z0-9].*$/, "", x)                        # cut after the register name
    if (x ~ /^%ymm/) return 256
    if (x ~ /^%xmm/) return 128
    if (x ~ /^%[a-d][lh]$/ || x ~ /^%(si|di|sp|bp)l$/ || x ~ /^%r[0-9]+b$/) return 8
    if (x ~ /^%(ax|bx|cx|dx|si|di|sp|bp)$/ || x ~ /^%r[0-9]+w$/) return 16
    if (x ~ /^%(eax|ebx|ecx|edx|esi|edi|esp|ebp)$/ || x ~ /^%r[0-9]+d$/) return 32
    if (x ~ /^%(rax|rbx|rcx|rdx|rsi|rdi|rsp|rbp)$/ || x ~ /^%r[0-9]+$/) return 64
    return 0
}
function vwid(o,   x) {
    if (o ~ /%ymm/) return 256
    if (o ~ /%xmm/) return 128
    return 0
}
# Splits the operands, notes whether ANY of them is a memory reference, and
# returns the widest register that is NOT part of a memory reference. Necessary
# because AT&T drops the size suffix on `mov` when the register already implies
# it: `mov %dil,(%rax)` is an 8-bit store, and reading it as 32 bits (the old
# rule here) both over-reported naive/ref_scalar and hid what they really do.
function scanf_ops(o,   cleaned, n, i, p, mem, rb, b) {
    mem = (o ~ /\(/) ? 1 : 0
    cleaned = o
    gsub(/\([^)]*\)/, "", cleaned)   # drop memory operands, so their base/index
                                      # registers cannot be mistaken for the
                                      # register that decides the access width
    if (cleaned ~ /[^$]0x[0-9a-f]/) mem = 1     # absolute address, no parentheses
    n = split(cleaned, parts, ",")
    rb = 0
    for (i = 1; i <= n; i++) {
        p = parts[i]
        if (p ~ /%/) { b = reg_bits(p); if (b > rb) rb = b }
    }
    return (mem ? rb : -rb)          # positive: memory present, value = reg bits
}
function width(m, o,   x, r) {
    # specific element/broadcast widths come first: they are NARROWER than the
    # destination register and would otherwise be over-reported.
    if (m == "vmovd"  || m == "movd")  return 32
    if (m == "vmovq"  || m == "movq")  return 64      # 8 B, also for the xmm form
    if (m == "vbroadcastss") return 32
    if (m == "vbroadcastsd") return 64
    if (m == "vbroadcastf128") return 128
    if (m == "vpbroadcastb") return 8
    if (m == "vpbroadcastw") return 16
    if (m == "vpbroadcastd") return 32
    if (m == "vpbroadcastq") return 64
    if (m == "vpinsrb") return 8
    if (m == "vpinsrw") return 16
    if (m == "vpinsrd") return 32
    if (m == "vpinsrq") return 64
    # full-width moves: the destination/source register IS the access width
    if (m ~ /^(v)?(movdqu|movups|movdqa|movaps|movntdq|movntps|movntdqa|lddqu|movddup|movshdup|movsldup|movdqu8|movdqu16|movdqu32|movdqu64|movdqa32|movdqa64)$/) {
        x = vwid(o)
        return (x > 0) ? x : -1
    }
    # any other AVX/SSE instruction with a memory operand: use the register
    # width when there is one, otherwise refuse to guess.
    if (m ~ /^v/) { x = vwid(o); return (x > 0) ? x : -1 }
    # explicit sizes in the mnemonic itself
    if (m == "movb" || m == "movzbl" || m == "movzbw" || m == "movsbl" ||
        m == "movsbw" || m == "movsbq") return 8
    if (m == "movw" || m == "movzwl" || m == "movzwq" || m == "movswl" ||
        m == "movswq") return 16
    if (m == "movl" || m == "movslq" || m == "movsxd") return 32
    if (m == "movq" || m == "movabs" || m == "movabsq") return 64
    # no suffix (or a register-only form): the register operand decides
    if (m == "mov" || m == "movnti" || m == "movabs" || m == "movbe") {
        r = scanf_ops(o)
        return (r > 0) ? r : -1
    }
    return 0
}
/^[[:space:]]*[0-9a-f]+:/ {
    line = $0
    sub(/^[[:space:]]*[0-9a-f]+:[[:space:]]*/, "", line)
    mnem = line; sub(/[[:space:]].*$/, "", mnem)
    ops  = line; sub(/^[^[:space:]]+[[:space:]]*/, "", ops)
    if (mnem ~ /^prefetch/) next
    if (ops !~ /\(/ && ops !~ /[^$]0x[0-9a-f]/) next      # no memory operand
    w = width(mnem, ops)
    if (w == 0)  next                                      # not data movement
    if (w == -1) { print "UNCLASSIFIED\t" mnem "\t" ops; next }
    printf "%d\t%s\t%s\n", w, mnem, ops
}
'

# runs the classifier over one object: echoes "<bits>|<mnem> <ops>" for the
# widest access, or "UNCLASSIFIED|<mnem> <ops>", or "NONE|" if no access found.
widest_of() {   # <object>
    objdump -d --no-show-raw-insn "$1" | awk "$CLASSIFY" | awk -F'\t' '
        $1 == "UNCLASSIFIED" { print "UNCLASSIFIED|" $2 " " $3; bad = 1; next }
        { b = $1 + 0; ins = $2 " " $3
          if (b > max) { max = b; txt = ins; n = 1 }
          else if (b == max) { if (n < 4) txt = txt "; " ins; n++ } }
        END { if (bad) exit 0; if (max == 0) print "NONE|"; else print max "|" txt }'
}

override_bits() {   # <source-basename> -> bits or ""
    [ -f "$ALLOW" ] || { echo ""; return; }
    awk -v want="$1" '/^[[:space:]]*#/ { next } $1 == want { print $2; exit }' "$ALLOW"
}
override_note() {   # <source-basename> -> the reason text from the allow file
    awk -v want="$1" '/^[[:space:]]*#/ { next } $1 == want { $1=""; $2=""; sub(/^[[:space:]]*#?[[:space:]]*/, ""); print; exit }' "$ALLOW"
}

# ---- self-test: the detector must reproduce both historical failures ------
if [ "$SELFTEST" = 1 ]; then
    echo "audit_width --selftest: can it see an SLP merge of two 128-bit accesses?"
    d=build/audit_width_selftest
    mkdir -p "$d"
    cat > "$d/merge.c" <<'EOF'
#include <immintrin.h>
/* The exact shape of 43f01dc and 25faacb: two adjacent 16 B intrinsics. */
void merge(void *d, const void *s)
{
    __m128i a = _mm_loadu_si128((const __m128i *)s);
    __m128i b = _mm_loadu_si128((const __m128i *)((const char *)s + 16));
    _mm_storeu_si128((__m128i *)d, a);
    _mm_storeu_si128((__m128i *)((char *)d + 16), b);
}
EOF
    clang -std=c11 -O3 -march=znver2 -mavx2 -fomit-frame-pointer \
        -fno-builtin-memcpy -c "$d/merge.c" -o "$d/merge_plain.o"
    clang -std=c11 -O3 -march=znver2 -mavx2 -fomit-frame-pointer -fno-slp-vectorize \
        -fno-builtin-memcpy -c "$d/merge.c" -o "$d/merge_noslp.o"
    clang -std=c11 -O3 -march=znver2 -mavx2 -fomit-frame-pointer -fno-slp-vectorize \
        -mno-avx -mno-avx2 -mno-fma \
        -fno-builtin-memcpy -c "$d/merge.c" -o "$d/merge_noavx.o"
    a=$(widest_of "$d/merge_plain.o");  ab=${a%%|*}
    b=$(widest_of "$d/merge_noslp.o");  bb=${b%%|*}
    c=$(widest_of "$d/merge_noavx.o");  cb=${c%%|*}
    echo "  default -march=znver2 -mavx2:            widest = ${ab} bits   [${a#*|}]"
    echo "  + -fno-slp-vectorize:                    widest = ${bb} bits   [${b#*|}]"
    echo "  + -mno-avx -mno-avx2 -mno-fma (43f01dc): widest = ${cb} bits   [${c#*|}]"
    rc=0
    if [ "$ab" = 256 ] && [ "$bb" = 256 ] && [ "$cb" = 128 ]; then
        echo "  PASS: the detector reports"
        echo "    - 256 for the merged form (a 128-declaring rung would FAIL the gate),"
        echo "    - 256 even with SLP disabled, because this 16B->32B fusion is a separate"
        echo "      x86 optimisation, not SLP - which is why 43f01dc had to disable AVX,"
        echo "      and why the AVX2 rung's small path (which cannot disable AVX) is the"
        echo "      harder case that 25faacb documented instead of flag-fixing,"
        echo "    - 128 once AVX is off, i.e. it sees the cure as well as the disease."
    else
        echo "  FAIL: detector saw ${ab}/${bb}/${cb}, expected 256/256/128 - the gate is blind" >&2
        rc=1
    fi
    rm -f "$d"/*.o
    exit $rc
fi

# ---- the gate -------------------------------------------------------------
echo "audit_width: widest data-movement access per kernel TU vs declared ladder width"
echo "  declarations: naive/ref_scalar 8, word 64, sse2 128, avx2* 256; overrides from $ALLOW"
fail=0
for t in znver2 znver3 v3; do
    echo "--- target $t"
    for src in src/*.c; do
        base=$(basename "$src")
        [ "$base" = "registry.c" ] && continue           # harness table, not a kernel
        obj="build/$t/${src%.c}.o"
        [ -f "$obj" ] || { echo "  MISSING OBJECT: $obj (run make first)"; fail=1; continue; }
        dec=$(declared_bits "$base")
        ovr=$(override_bits "$base")
        if [ -n "$ovr" ]; then dec="$ovr"; mark=" [override: $(override_note "$base")]"; else mark=""; fi
        if [ -z "$dec" ]; then
            printf '  %-34s NO DECLARED WIDTH for this source - add a row to declared_bits() or %s\n' "$base" "$ALLOW"
            fail=1; continue
        fi
        res=$(widest_of "$obj")
        got=${res%%|*}; ins=${res#*|}
        if [ "$got" = "UNCLASSIFIED" ]; then
            printf '  %-34s declared %-4s compiled UNCLASSIFIED (%s) - extend the classifier, do not ignore it\n' \
                "$base" "$dec" "$ins"
            fail=1; continue
        fi
        if [ "$got" = "NONE" ]; then
            printf '  %-34s declared %-4s compiled no data access found - suspicious\n' "$base" "$dec"
            fail=1; continue
        fi
        if [ "$got" = "$dec" ]; then
            printf '  %-34s declared %-4s compiled %-4s OK%s   (%s)\n' "$base" "$dec" "$got" "$mark" "$ins"
        else
            printf '  rung %s declares width %s but compiled to width %s (instructions: %s). Either fix the source or update the declared width.\n' \
                "$src" "$dec" "$got" "$ins"
            fail=1
        fi
    done
done
if [ "$fail" = 0 ]; then
    echo "audit_width: PASS - every rung's widest memory access matches its declaration"
else
    echo "audit_width: FAIL - see the lines above; no benchmark may run until this passes" >&2
fi
exit $fail
