#!/bin/sh
# scripts/codegen_diff_asm_vs_c.sh — Kernel 6 report item 8.
#
#   sh scripts/codegen_diff_asm_vs_c.sh
#
# Per target, extracts the instruction stream of the hand-written rung
# (`memcpy_asm`, assembled from src/memcpy_asm.S) and of the C AVX2 rung the
# compiler produced for Kernel 4 (`memcpy_avx2`, from src/memcpy_avx2.c),
# strips addresses, and writes:
#
#   results/codegen_diff_asm_vs_avx2_c_<target>.diff
#
# This is a different comparison from scripts/codegen_diff.sh: that one diffs
# the THREE TARGETS of one kernel (is -march a factor?), while this one diffs
# TWO KERNELS within ONE target (did the hand-written version beat the
# compiler's?). Both are addressed-stripped for the same reason: the diff must
# show codegen, not link layout.
#
# Each file states both instruction counts, both distinct-mnemonic counts, both
# function byte sizes, an explicit verdict, and an explicit mnemonic-multiset
# verdict, then the unified diff. The header is not decoration: an empty diff
# body is the *correct* result for two identical instruction streams, and a
# 0-byte file cannot be told apart from a skipped comparison.
set -eu
cd "$(dirname "$0")/.."
mkdir -p results

# extract <object> <symbol> <outfile> — address-stripped instruction stream of
# exactly one function. The `>:` anchor is required: without it `<memcpy_avx2>`
# would also match `<memcpy_avx2_pf2>`.
extract() {
    objdump -d --no-show-raw-insn "$1" | awk -v sym="$2" '
        $0 ~ ("^[0-9a-f]+ <" sym ">:") { on = 1; next }
        on && $0 ~ /^[0-9a-f]+ </       { on = 0 }
        on {
            line = $0
            sub(/^[[:space:]]*[0-9a-f]+:[[:space:]]*/, "", line)
            if (length(line)) print line
        }
    ' > "$3"
}

# func_bytes <object> <symbol>
func_bytes() {
    sz=$(nm -S "$1" 2>/dev/null | awk -v s="$2" '$4 == s { print $2; exit }')
    if [ -n "${sz:-}" ]; then printf '%d' "0x$sz"; else printf '?'; fi
}

for t in znver2 znver3 v3; do
    asm_obj="build/$t/src/memcpy_asm.o"
    c_obj="build/$t/src/memcpy_avx2.o"
    if [ ! -f "$asm_obj" ] || [ ! -f "$c_obj" ]; then
        echo "codegen_diff_asm_vs_c.sh: missing object for $t - run 'make' first" >&2
        exit 1
    fi

    A="build/$t/asm_vs_c.memcpy_asm.asm"
    C="build/$t/asm_vs_c.memcpy_avx2.asm"
    extract "$asm_obj" memcpy_asm  "$A"
    extract "$c_obj"   memcpy_avx2 "$C"

    n_asm=$(wc -l < "$A")
    n_c=$(wc -l < "$C")
    m_asm=$(awk '{ sub(/ .*/, "", $0); print }' "$A" | sort -u | wc -l)
    m_c=$(awk '{ sub(/ .*/, "", $0); print }' "$C" | sort -u | wc -l)
    b_asm=$(func_bytes "$asm_obj" memcpy_asm)
    b_c=$(func_bytes "$c_obj" memcpy_avx2)

    BODY="build/$t/asm_vs_c.body"
    same=0
    if diff -u "$A" "$C" > "$BODY" 2>&1; then same=1; fi
    changed=$(grep -c '^[+-][^+-]' "$BODY" || true)

    msame=0
    if [ "$same" = "0" ]; then
        awk '{ sub(/ .*/, "", $0); print }' "$A" | sort > "$BODY.sa"
        awk '{ sub(/ .*/, "", $0); print }' "$C" | sort > "$BODY.sb"
        if cmp -s "$BODY.sa" "$BODY.sb"; then msame=1; fi
        rm -f "$BODY.sa" "$BODY.sb"
    fi

    OUT="results/codegen_diff_asm_vs_avx2_c_$t.diff"
    {
        echo "# codegen diff: hand-written asm vs the C compiler, target=$t"
        echo "# compared: memcpy_asm (src/memcpy_asm.S) vs memcpy_avx2 (src/memcpy_avx2.c)"
        echo "#           both from build/$t/, function-extracted, addresses stripped"
        echo "# memcpy_asm  (hand-written asm): instructions=$n_asm  distinct_mnemonics=$m_asm  func_bytes=$b_asm"
        echo "# memcpy_avx2 (clang -O3 -march=$t): instructions=$n_c  distinct_mnemonics=$m_c  func_bytes=$b_c"
        if [ "$same" = "1" ]; then
            echo "# verdict: IDENTICAL instruction streams (empty body below is the result, not a failure)"
        else
            echo "# verdict: DIFFERENT - $changed changed instruction line(s); unified diff (- asm, + C) follows"
            if [ "$msame" = "1" ]; then
                echo "# mnemonic multiset: IDENTICAL - the differences are ORDERING/allocation only, no instruction-set change"
            else
                echo "# mnemonic multiset: DIFFERENT - the two kernels use different instruction sets (not a reordering)"
            fi
        fi
    } > "$OUT"
    cat "$BODY" >> "$OUT"
    rm -f "$BODY"

    printf '%-7s asm %s ins / %s B   vs   C-avx2 %s ins / %s B   -> %s\n' \
        "$t" "$n_asm" "$b_asm" "$n_c" "$b_c" "$OUT"
done
