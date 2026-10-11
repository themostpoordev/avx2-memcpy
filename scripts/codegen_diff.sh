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
OBJ_REL="${SRC%.c}.o"

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

echo
for pair in "znver2 znver3" "znver2 v3" "znver3 v3"; do
    set -- $pair
    A="$1"
    B="$2"
    OUT="results/codegen_diff_${KERNEL}_${A}_vs_${B}.diff"
    if diff -u "build/$A/$KERNEL.asm" "build/$B/$KERNEL.asm" > "$OUT" 2>&1; then
        echo "${KERNEL}: ${A} == ${B} (identical instruction streams) -> $OUT"
    else
        changed=$(grep -c '^[+-][^+-]' "$OUT" || true)
        echo "${KERNEL}: ${A} != ${B} (${changed} changed instruction lines) -> $OUT"
    fi
    # mnemonic-only difference (ignores register allocation)
    mout="build/${A}_vs_${B}.${KERNEL}.mnem.diff"
    if diff -u "build/$A/$KERNEL.mnem" "build/$B/$KERNEL.mnem" > "$mout" 2>&1; then
        echo "    (mnemonic streams identical: differences are register/allocation only)"
    else
        echo "    mnemonic streams DIFFER: $(grep -c '^[+-][^+-]' "$mout" || true) lines (real instruction-set differences)"
    fi
done
