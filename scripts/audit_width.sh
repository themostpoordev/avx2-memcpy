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
#   src/perf6/*.c (the V6 -flto and V7 PGO probe TUs) are covered by the same
#   table and the same comparison. One case cannot be read from the object: a
#   slim LTO object has an empty .text and carries only GIMPLE bytecode, so its
#   width is measured in the LINKED probe binary instead - the artifact a campaign
#   actually calls. See the perf6 branch in the gate loop.
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
# override entry, and state why. Hand-written asm rungs (src/*.S) are covered by
# the same table: the gate disassembles the assembled object exactly as it does
# a compiler-produced one, so a .S declaration is checked against the binary and
# not against the source text.
declared_bits() {
    case "$1" in
        ref_scalar.c|memcpy_naive.c|memset_naive.c)                 echo 8   ;;
        memcpy_word.c|memset_word.c)                                echo 64  ;;
        memcpy_sse2.c|memset_sse2.c)                                echo 128 ;;
        memcpy_avx2.c|memcpy_avx2_pf2.c|memcpy_avx2_pf4.c|memcpy_avx2_nt.c) \
                                                                    echo 256 ;;
        # Phase 2.6 tuned family: every one of them has a 32 B (ymm) main path, so
        # the declared width is 256 even though the <32 B classes are 16/8/4/2/1 B.
        memcpy_avx2_gcc.c|memcpy_avx2_t4.c|memcpy_avx2_t8.c|memcpy_avx2_t12.c) \
                                                                    echo 256 ;;
        memcpy_avx2_t8al.c|memcpy_avx2_t8ap.c|memcpy_avx2_t8alp.c)   echo 256 ;;
        memcpy_avx2_t4pf.c)                                         echo 256 ;;
        # Phase 2.6 V9/V6/V7: every fused final builds from memcpy_avx2_tuned_impl.h
        # with TUNED_* knobs, and every one of them declares 256 (32 B AVX2
        # moves) as its widest access - the knobs change when and where the
        # moves happen, never their width. Declared explicitly per file rather
        # than by a glob so a new file here can never inherit a width nobody
        # checked: memcpy_avx2_final*.c must be added to this row by hand.
        memcpy_avx2_final.c|memcpy_avx2_final_gcc.c)                echo 256 ;;
        memcpy_avx2_final16_gcc.c)                                  echo 256 ;;
        memcpy_avx2_final_gcc_ofast.c|memcpy_avx2_final_gcc_unroll.c|memcpy_avx2_final_gcc_noplt.c) \
                                                                    echo 256 ;;
        memcpy_avx2_final_gcc_noomit.c)                             echo 256 ;;
        # V9b: same body and same 32 B moves, reached from one compare at
        # n <= 64 - the widest access is still 256 bits.
        memcpy_avx2_final_v2_gcc.c)                                 echo 256 ;;
        memcpy_avx2_final_v2_gcc_noxj.c)                            echo 256 ;;
        # F1-F4 (the 128/256 B pass): same body, same 32 B moves.
        memcpy_avx2_final_f1_gcc.c|memcpy_avx2_final_f3_gcc.c)      echo 256 ;;
        memcpy_avx2_final_f4_gcc.c|memcpy_avx2_final_ifunc_gcc.c)   echo 256 ;;
        # Phase 3 P1: the mid-range rungs share one body (256 B/iter of 32 B
        # moves) and differ only in the MID_* knobs, none of which change the
        # width - declared per file so a new mid rung cannot inherit a width
        # nobody checked.
        memcpy_avx2_mid_gcc.c|memcpy_avx2_mid_u4_gcc.c)              echo 256 ;;
        memcpy_avx2_mid_pf1_gcc.c|memcpy_avx2_mid_pfnear_gcc.c)      echo 256 ;;
        memcpy_avx2_mid_nopeel_gcc.c|memcpy_avx2_mid_ntpf_gcc.c)     echo 256 ;;
        # Phase 2.6 V6/V7 probe TUs (src/perf6/, built only by `make
        # perf6probe-<t>`): -flto and PGO variants of the fused final kernel.
        # Same body, same 32 B moves, so the same declaration, and the loop below
        # checks them in the object they were actually compiled into.
        memcpy_avx2_final_lto.c|memcpy_avx2_final_pgo.c)             echo 256 ;;
        memset_avx2.c|memset_avx2_nt.c)                             echo 256 ;;
        memset_naive.c)                                             echo 8 ;;
        memset_word.c)                                              echo 64 ;;
        memset_sse2.c)                                              echo 128 ;;
        # K7: the word loop below the gate is 64-bit; rep movsb itself is not a
        # data-movement instruction with a width the audit can see, so the
        # declaration describes the widest *instruction* in the TU.
        memcpy_rep.c)                                               echo 64 ;;
        memcpy_asm.S|memset_asm.S)                                  echo 256 ;;
        memcpy_hybrid.c)                                            echo 256 ;;
        *)                                                          echo ""  ;;
    esac
}

# ---- dispatcher TUs (Phase 2.5) -------------------------------------------
# A dispatcher is a rung that contains NO data movement of its own: it classifies
# a size, indexes a generated table and tail-calls another rung. Comparing its
# "widest access" with a declared width would be meaningless both ways — it has no
# copying access to find, and its declared width is not a property of its own code
# but the maximum width of the rungs it routes to. So the gate checks the three
# things that ARE properties of the object:
#   (a) no vector data movement of its own (its widest access must be <= 64 bits,
#       i.e. pointer loads only);
#   (b) every undefined symbol in the object must be a rung entry point that this
#       same gate width-checks — so a dispatcher cannot smuggle in a wide access by
#       calling something the ladder does not gate;
#   (c) the declared width must EQUAL the maximum declared width of the rungs the
#       object actually references (256 = max(8,64,128,256)), not merely be larger.
# Nothing here is auto-approved and nothing is skipped quietly: every dispatcher
# prints its own line with the routed set and the widths involved.
DISPATCHER_SRCS="memcpy_hybrid.c"

# rung symbol -> declared width of the TU that defines it
rung_symbol_bits() {
    case "$1" in
        memcpy_naive)              echo 8   ;;
        memcpy_word)               echo 64  ;;
        memcpy_sse2)               echo 128 ;;
        memcpy_avx2)               echo 256 ;;
        memcpy_avx2_nt)            echo 256 ;;
        memcpy_asm)                echo 256 ;;
        *)                         echo ""  ;;
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

# Same classifier, but over ONE symbol inside a LINKED binary. Needed for slim
# LTO objects (-flto without -ffat-lto-objects): their .text is empty and the
# function exists only as GIMPLE bytecode, which the linker compiles with the LTO
# plugin. The object therefore cannot answer "what width is benchmarked" - the
# linked function can. See the perf6 branch in the gate loop.
widest_of_sym() {   # <binary> <symbol>
    objdump -d --no-show-raw-insn --disassemble="$2" "$1" | awk "$CLASSIFY" | awk -F'\t' '
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
echo "  declarations: naive/ref_scalar 8, word 64, sse2 128, avx2* 256, asm 256,"
echo "                hybrid 256 (dispatcher: must equal the max width of the rungs it routes to);"
echo "                overrides from $ALLOW"
fail=0
for t in znver2 znver3 v3; do
    echo "--- target $t"
    for src in src/*.c src/*.S src/perf6/*.c; do
        base=$(basename "$src")
        [ "$base" = "registry.c" ] && continue           # harness table, not a kernel
        case "$src" in
        src/perf6/*) obj="build/$t/src/perf6/${base%.c}.o" ;;   # V6/V7 probe TUs
        *)           obj="build/$t/${src%.*}.o" ;;
        esac
        if [ ! -f "$obj" ]; then
            case "$src" in
            src/perf6/*)
                # A probe TU exists only after `make perf6probe-<t>`, which is also
                # the only way one is ever benchmarked. Absent means "nothing in
                # the ladder can measure it either", so this is reported, not
                # failed; present means it is checked like any other rung. The
                # line is printed on every gate run so the omission stays visible.
                printf '  %-34s not built (probe TU: `make perf6probe-%s` builds and gates it)\n' "$base" "$t"
                ;;
            *)
                echo "  MISSING OBJECT: $obj (run make first)"; fail=1 ;;
            esac
            continue
        fi
        dec=$(declared_bits "$base")
        ovr=$(override_bits "$base")
        if [ -n "$ovr" ]; then dec="$ovr"; mark=" [override: $(override_note "$base")]"; else mark=""; fi
        if [ -z "$dec" ]; then
            printf '  %-34s NO DECLARED WIDTH for this source - add a row to declared_bits() or %s\n' "$base" "$ALLOW"
            fail=1; continue
        fi
        # ---- slim-LTO probe TU: read the width in the LINKED binary ----------
        # A -flto object compiled without -ffat-lto-objects carries NO machine
        # code: .text is empty and the function exists only as GIMPLE bytecode
        # for the linker's LTO plugin to compile. Reading that object says nothing
        # about the code a campaign calls, and the first version of this gate
        # FAILED such a TU with "compiled no data access found" - the gate being
        # wrong, not the rung. So for these TUs the width is taken from the
        # function as it exists in the linked probe binary. Nothing is skipped and
        # nothing is auto-approved: a missing binary or a missing symbol is a
        # failure, not a pass.
        case "$src" in
        src/perf6/*)
            if [ "$(objdump -h "$obj" | awk '$2 == ".text" { print $3 }')" = "00000000" ]; then
                sym=${base%.c}
                probe="bench/bench_${t}_perf6"
                if [ ! -x "$probe" ]; then
                    printf '  %-34s slim LTO object, but %s is missing: build it with `make perf6probe-%s`\n' \
                        "$base" "$probe" "$t"
                    fail=1; continue
                fi
                if ! nm "$probe" 2>/dev/null | grep -qE "[[:space:]]$sym$"; then
                    printf '  %-34s symbol %s is not in %s - cannot check the width of what is benchmarked\n' \
                        "$base" "$sym" "$probe"
                    fail=1; continue
                fi
                res=$(widest_of_sym "$probe" "$sym"); got=${res%%|*}; ins=${res#*|}
                if [ "$got" = "$dec" ]; then
                    printf '  %-34s declared %-4s compiled %-4s OK%s   (in the linked binary: %s) [slim LTO: no code of its own]\n' \
                        "$base" "$dec" "$got" "$mark" "$ins"
                else
                    printf '  %s declares width %s but the linked function %s compiles to width %s (%s). Either fix the source or update the declared width.\n' \
                        "$src" "$dec" "$sym" "$got" "$ins"
                    fail=1
                fi
                continue
            fi
            ;;
        esac
        # ---- dispatcher rule (a)(b)(c), see the block above ----------------
        case " $DISPATCHER_SRCS " in
        *" $base "*)
            res=$(widest_of "$obj"); got=${res%%|*}; ins=${res#*|}
            undef=$(nm -u "$obj" 2>/dev/null | awk '{ print $NF }' | sort -u | tr '\n' ' ')
            maxb=0; badtarget=""
            for sym in $undef; do
                sb=$(rung_symbol_bits "$sym")
                if [ -z "$sb" ]; then badtarget="$badtarget $sym"; continue; fi
                [ "$sb" -gt "$maxb" ] && maxb="$sb"
            done
            if [ "$got" != "NONE" ] && [ "$got" != "UNCLASSIFIED" ] && [ "$got" -gt 64 ]; then
                printf '  %-34s DISPATCHER declares %s but its own widest access is %s bits (%s) - a dispatcher must not copy\n' \
                    "$base" "$dec" "$got" "$ins"
                fail=1; continue
            fi
            if [ -n "$badtarget" ]; then
                printf '  %-34s DISPATCHER references symbol(s) that are not width-gated rungs:%s\n' "$base" "$badtarget"
                fail=1; continue
            fi
            if [ "$maxb" != "$dec" ]; then
                printf '  %-34s DISPATCHER declares %s but routes to rungs whose maximum declared width is %s - the declaration must equal the widest thing it can reach\n' \
                    "$base" "$dec" "$maxb"
                fail=1; continue
            fi
            printf '  %-34s DISPATCHER declared %-4s = max width of the %s gated rung(s) it references%s; own widest access %s (%s)\n' \
                "$base" "$dec" "$(printf '%s' "$undef" | wc -w | tr -d ' ')" "$mark" "$got" "$ins"
            continue
            ;;
        esac
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
