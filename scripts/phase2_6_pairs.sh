#!/bin/sh
# scripts/phase2_6_pairs.sh <matrix-file> <impl-A> <impl-B>
#
#   sh scripts/phase2_6_pairs.sh results/phase2_6_screen3_znver2.matrix.txt avx2 avx2_gcc
#
# Prints, for every size present in the matrix, the min_of_mins of A and of B and
# the percentage by which B is faster than A (positive = B faster, i.e. the knob
# B adds/changes helps). Sizes are only those actually measured: an earlier
# one-liner iterated 16,32,64,... and silently printed +0.0% for 2048 and the
# other non-ladder sizes, which is exactly the kind of invented cell this project
# does not publish. Reads only the matrix file (which itself reads only the raw
# window log), so nothing here can disagree with the underlying log.
#
# Usage note: A and B must differ in exactly one knob for the number to mean
# anything; the calling report states which knob that is for each pair.
set -eu

M="${1:?usage: phase2_6_pairs.sh <matrix> <A> <B>}"
A="${2:?}"
B="${3:?}"

awk -v A="$A" -v B="$B" '
/^[0-9]/ { k = $1 "|" $2; v[k] = $3 + 0
           if (!(($1) in s)) { s[$1] = 1; n++; S[n] = $1 + 0 } }
END {
    for (i = 1; i < n; i++)
        for (j = i + 1; j <= n; j++)
            if (S[j] < S[i]) { t = S[i]; S[i] = S[j]; S[j] = t }
    printf "# %s vs %s (positive = %s faster)\n", B, A, B
    printf "# %-9s %12s %12s %10s\n", "size", A "_min_ns", B "_min_ns", "delta_pct"
    for (i = 1; i <= n; i++) {
        sz = S[i]
        a = v[sz "|" A]; b = v[sz "|" B]
        if (!(a > 0) || !(b > 0)) continue
        printf "  %-9d %12.1f %12.1f %+9.2f%%\n", sz, a, b, 100 * (a - b) / a
    }
}
' "$M"
