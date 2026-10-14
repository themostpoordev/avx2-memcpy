#!/bin/sh
# scripts/codegen_diff.sh <symbol> <src-path>
#
#   scripts/codegen_diff.sh memcpy_naive src/memcpy_naive.c
#
# Extracts ONE function's instruction stream from that target's object file,
# strips addresses (so the diff shows codegen, not link layout), and diffs the
# three targets pairwise:
#
#   results/codegen_diff_<kernel>_znver2_vs_znver3.diff
#   results/codegen_diff_<kernel>_znver2_vs_v3.diff
#   results/codegen_diff_<kernel>_znver3_vs_v3.diff
#
# Also prints, per target: instruction count, mnemonic-only count (ignores
# register allocation) and byte size of the function. The mnemonic-only diff
# matters: "same instructions, different registers" is not a real codegen
# difference, and conflating the two would overstate the tuning-target effect.
set -eu

SYM="$1"
SRC="$2"
KERNEL=$(printf '%s' "$SYM" | sed 's/^memcpy_//; s/^memset_//')
# Strip whichever suffix this source has: .c kernels and .S asm rungs both map to
# a single .o next to the source. Hard-coding %.c here silently looked for
# src/memcpy_asm.S.o when the Kernel 6 asm rung was added.
OBJ_REL="${SRC%.*}.o"

mkdir -p results

for t in znver2 znver3 v3; do
    OBJ="build/$t/$OBJ_REL"
    if [ ! -f "$OBJ" ]; then
        echo "codegen_diff.sh: missing $OBJ - run 'make' first" >&2
        exit 1
    fi
    objdump -d --no-show-raw-insn "$OBJ" | awk -v sym="$SYM" '
        $0 ~ ("^[0-9a-f]+ <" sym ">:") { on = 1; next }
        on && $0 ~ /^[0-9a-f]+ </       { on = 0 }
        on {
            line = $0
            sub(/^[[:space:]]*[0-9a-f]+:[[:space:]]*/, "", line)
            if (length(line)) print line
        }
    ' > "build/$t/$KERNEL.asm"

    n_ins=$(wc -l < "build/$t/$KERNEL.asm")
    # mnemonic-only view (drop everything from the first space on)
    awk '{ sub(/ .*/, "", $0); print }' "build/$t/$KERNEL.asm" \
        > "build/$t/$KERNEL.mnem"
    n_mnem=$(sort -u "build/$t/$KERNEL.mnem" | wc -l)
    # Function byte size from the symbol table (strtonum is gawk-only, so use
    # printf, which every POSIX shell understands for hex).
    sz=$(nm -S "$OBJ" 2>/dev/null | awk -v s="$SYM" '$4 == s { print $2; exit }')
    if [ -n "${sz:-}" ]; then bytes=$(printf '%d' "0x$sz"); else bytes="?"; fi
    printf 'target=%-7s instructions=%-5s distinct_mnemonics=%-4s func_bytes=%s\n' \
        "$t" "$n_ins" "$n_mnem" "$bytes"
done

# metric <target> <ins|mnem|bytes> — recomputed per pair so the saved diff can
# describe itself.
metric() {
    case "$2" in
        ins)   wc -l < "build/$1/$KERNEL.asm" ;;
        mnem)  sort -u "build/$1/$KERNEL.mnem" | wc -l ;;
        bytes) sz=$(nm -S "build/$1/$OBJ_REL" 2>/dev/null | awk -v s="$SYM" '$4 == s { print $2; exit }')
               if [ -n "${sz:-}" ]; then printf '%d' "0x$sz"; else printf '?'; fi ;;
    esac
}

echo
for pair in "znver2 znver3" "znver2 v3" "znver3 v3"; do
    set -- $pair
    A="$1"
    B="$2"
    OUT="results/codegen_diff_${KERNEL}_${A}_vs_${B}.diff"
    BODY="${TMPDIR:-/tmp}/codegen_diff_$$.body"
    same=0
    if diff -u "build/$A/$KERNEL.asm" "build/$B/$KERNEL.asm" > "$BODY" 2>&1; then
        same=1
    fi
    changed=$(grep -c '^[+-][^+-]' "$BODY" || true)
    msame=0
    if [ "$same" = "0" ]; then
        sort "build/$A/$KERNEL.mnem" > "$BODY.sa"
        sort "build/$B/$KERNEL.mnem" > "$BODY.sb"
        if cmp -s "$BODY.sa" "$BODY.sb"; then msame=1; fi
        rm -f "$BODY.sa" "$BODY.sb"
    fi
    # The header matters: an empty body is the correct RESULT for identical
    # codegen, but a 0-byte file cannot be told apart from a failed or skipped
    # comparison. Every saved diff now states what was compared and the verdict.
    {
        echo "# codegen diff: kernel=$KERNEL  ${A} vs ${B}"
        echo "# compared: build/${A}/${KERNEL}.asm  vs  build/${B}/${KERNEL}.asm"
        echo "#           (function-extracted, address-stripped instruction streams from ${SRC})"
        echo "# ${A}: instructions=$(metric "$A" ins)  distinct_mnemonics=$(metric "$A" mnem)  func_bytes=$(metric "$A" bytes)"
        echo "# ${B}: instructions=$(metric "$B" ins)  distinct_mnemonics=$(metric "$B" mnem)  func_bytes=$(metric "$B" bytes)"
        if [ "$same" = "1" ]; then
            echo "# verdict: IDENTICAL instruction streams (empty body below is the result, not a failure)"
        else
            echo "# verdict: DIFFERENT — ${changed} changed instruction line(s); unified diff follows"
            # The multiset verdict is stated either way. Printing it only for the
            # ordering-only case left the other case with no multiset line at all,
            # which reads like "not compared" rather than "compared: different".
            if [ "$msame" = "1" ]; then
                echo "# mnemonic multiset: IDENTICAL - the differences are ORDERING only (moved instructions, no instruction-set change)"
            else
                echo "# mnemonic multiset: DIFFERENT - the instruction sets differ (this is not a reordering)"
            fi
        fi
    } > "$OUT"
    cat "$BODY" >> "$OUT"
    rm -f "$BODY"
    if [ "$same" = "1" ]; then
        echo "${KERNEL}: ${A} == ${B} (identical instruction streams) -> $OUT"
    else
        echo "${KERNEL}: ${A} != ${B} (${changed} changed instruction lines) -> $OUT"
    fi
    # mnemonic-only difference (ignores register allocation)
    mout="build/${A}_vs_${B}.${KERNEL}.mnem.diff"
    if diff -u "build/$A/$KERNEL.mnem" "build/$B/$KERNEL.mnem" > "$mout" 2>&1; then
        echo "    (mnemonic streams identical: differences are register/allocation only)"
    else
        # A mnemonic that shows up as BOTH - and + is a MOVED instruction, not a
        # changed one. Comparing sorted multisets separates "same instructions,
        # different order" from a genuine instruction-set difference. The first
        # version reported both as "real instruction-set differences", which
        # overstated the znver2-vs-v3 gap for the SSE2 rung: that difference is
        # ordering only (two lea instructions in a different position).
        sort "build/$A/$KERNEL.mnem" > "$mout.sa"
        sort "build/$B/$KERNEL.mnem" > "$mout.sb"
        if cmp -s "$mout.sa" "$mout.sb"; then
            echo "    mnemonic MULTISET identical: differences are ORDERING only ($(grep -c '^[+-][^+-]' "$mout" || true) moved lines, no instruction-set change)"
        else
            echo "    mnemonic streams DIFFER: $(grep -c '^[+-][^+-]' "$mout" || true) lines (real instruction-set differences)"
        fi
        rm -f "$mout.sa" "$mout.sb"
    fi
done
