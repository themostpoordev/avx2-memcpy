#!/bin/sh
# scripts/analyze/analyze_phase2.sh <kernel> [prev_kernel] [op]
#
#   scripts/analyze/analyze_phase2.sh naive ref_scalar
#
# Reads results/live/phase2_<kernel>_<target>.csv (harness output with --reps 3),
# joins the three targets, and emits markdown to stdout:
#
#   1. GB/s and cycles/byte by size for <prev_kernel>, <kernel> and glibc;
#   2. median GB/s per size class with the <kernel>/glibc and <kernel>/<prev>
#      ratios (the win/loss/tie evidence);
#   3. per-target min_ns for <kernel> (is the tuning target doing anything?);
#   4. count of harness variance flags (>5% spread across reps).
#
# GB/s is bytes/median_ns from CLOCK_MONOTONIC_RAW; cycles/byte is the PMU
# cycles counter. Nothing here is hand-entered.
set -eu

K="${1:?usage: analyze_phase2.sh <kernel> [prev_kernel] [op]}"
PREV="${2:-ref_scalar}"
OP="${3:-memcpy}"

SIZES="16 32 64 128 256 512 1024 4096 16384 65536 262144 1048576 4194304 16777216 67108864 268435456"

awk -F, -v OP="$OP" -v PREV="$PREV" -v K="$K" -v SIZES="$SIZES" '
function cls(n) {
    if (n < 64)       return "tiny_<64B"
    if (n <= 4096)    return "small_64B-4KB"
    if (n <= 65536)   return "mid_4KB-64KB"
    if (n <= 1048576) return "big_64KB-1MB"
    return "huge_>1MB"
}
function clsname(c) {
    if (c == "tiny_<64B")      return "tiny (<64B)"
    if (c == "small_64B-4KB")  return "small (64B-4KB)"
    if (c == "mid_4KB-64KB")   return "mid (4KB-64KB)"
    if (c == "big_64KB-1MB")   return "big (64KB-1MB)"
    return "huge (>1MB)"
}
# Median of the n values B[key SUBSEP 1..n]. The array is passed in whole:
# B[key] would be a distinct (empty) element, not the bucket - that bug made
# every aggregate read 0 on the first run of this script.
function median_key(B, key, n,   a, i, j, t) {
    if (n <= 0) return 0
    for (i = 1; i <= n; i++) a[i] = B[key SUBSEP i]
    for (i = 1; i < n; i++)
        for (j = i + 1; j <= n; j++)
            if (a[j] < a[i]) { t = a[i]; a[i] = a[j]; a[j] = t }
    return (n % 2) ? a[(n + 1) / 2] : (a[n / 2] + a[n / 2 + 1]) / 2
}
FNR == 1 { next }
{
    impl = $1; target = $2; op = $3; n = $4 + 0
    if (op != OP) next
    g[target "|" n "|" impl] = $11
    c[target "|" n "|" impl] = $13
    m[target "|" n "|" impl] = $9
    rows++
    if ($11 > 0 && $15 + 0 > 5) flagged++
    cl = cls(n)
    B[impl "|" target "|" cl, ++bn[impl "|" target "|" cl]] = $11
}
END {
    ntar = split("znver2 znver3 v3", T, " ")
    nsz  = split(SIZES, S, " ")
    nimp = split(PREV " " K " glibc", IM, " ")

    printf "rows=%d  variance_flagged=%d (%.0f%%)\n\n", rows, flagged, 100 * flagged / (rows ? rows : 1)

    # ---- 1. detail by size ------------------------------------------------
    printf "| size | impl |"
    for (i = 1; i <= ntar; i++) printf " %s GB/s | %s c/B |", T[i], T[i]
    print ""
    printf "|--:|:--|"
    for (i = 1; i <= ntar; i++) printf "--:|--:|"
    print ""
    for (si = 1; si <= nsz; si++) {
        n = S[si] + 0
        for (mi = 1; mi <= nimp; mi++) {
            impl = IM[mi]
            printf "| %d | %s |", n, impl
            for (i = 1; i <= ntar; i++) {
                k = T[i] "|" n "|" impl
                printf " %.2f | %.4f |", (k in g) ? g[k] : 0, (k in c) ? c[k] : 0
            }
            print ""
        }
    }

    # ---- 2. size classes + ratios ----------------------------------------
    split("tiny_<64B small_64B-4KB mid_4KB-64KB big_64KB-1MB huge_>1MB", CL, " ")
    printf "\n| target | class | %s GB/s | %s GB/s | glibc GB/s | %s/glibc | %s/%s |\n", PREV, K, K, K, PREV
    print "|:--|:--|--:|--:|--:|--:|--:|"
    for (i = 1; i <= ntar; i++) {
        for (ci = 1; ci <= 5; ci++) {
            cname = CL[ci]
            pvk = PREV "|" T[i] "|" cname
            kvk = K "|" T[i] "|" cname
            gvk = "glibc|" T[i] "|" cname
            pv = median_key(B, pvk, bn[pvk])
            kv = median_key(B, kvk, bn[kvk])
            gv = median_key(B, gvk, bn[gvk])
            printf "| %s | %s | %.2f | %.2f | %.2f | %.3fx | %.3fx |\n",
                   T[i], clsname(cname), pv, kv, gv, gv ? kv / gv : 0, pv ? kv / pv : 0
        }
    }

    # ---- 3. target effect (min_ns, the least noisy statistic) -------------
    printf "\n| size | kernel min_ns znver2 | znver3 | v3 | max/min across targets |\n"
    print "|--:|--:|--:|--:|--:|"
    for (si = 1; si <= nsz; si++) {
        n = S[si] + 0
        lo = 0; hi = 0
        for (i = 1; i <= ntar; i++) {
            v = m[T[i] "|" n "|" K] + 0
            if (i == 1) { lo = v; hi = v }
            if (v > 0 && v < lo) lo = v
            if (v > hi) hi = v
        }
        printf "| %d |", n
        for (i = 1; i <= ntar; i++) printf " %.1f |", m[T[i] "|" n "|" K] + 0
        printf " %.3fx |\n", lo ? hi / lo : 0
    }
}
' \
    results/live/phase2_${K}_znver2.csv \
    results/live/phase2_${K}_znver3.csv \
    results/live/phase2_${K}_v3.csv
