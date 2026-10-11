#!/bin/sh
# scripts/noise_floor.sh [reps] [size ...]         (TARGET env var selects binary)
#
# Measures the NULL-COMPARISON BAND: `naive` and `ref_scalar` are two different
# translation units containing the same byte loop, whose objdump instruction
# streams are byte-identical (see results/codegen_diff_naive_*.diff). The ratio
# of their timings is therefore a pure measurement artefact, and it sets the bar
# any real kernel comparison has to clear.
#
# Reports, per size:
#   - the paired min_ns ratio for every rep, measured interleaved in one process;
#   - the GLOBAL MIN ratio (min over all samples of all reps);
#   - the best-median ratio.
#
# If a "speedup" is smaller than the band this prints, it is not a speedup.
set -eu

TARGET="${TARGET:-znver2}"
REPS="${1:-9}"
[ $# -gt 0 ] && shift
SIZES="${*:-64 1024 65536 1048576 268435456}"

./bench/bench_${TARGET} --impl naive,ref_scalar --reps "$REPS" --dump-reps \
    --budget 0.05 --iters 100 > /dev/null 2> /tmp/noise_floor.log

echo "noise_floor: target=$TARGET reps=$REPS core_ghz=$(./bench/bench_${TARGET} --calib 2>/dev/null | sed 's/.*core_ghz=\([0-9.]*\).*/\1/')"
awk -v SIZES="$SIZES" '
    $1 == "REPS" {
        impl = $2; op = $3; n = 0; rep = 0; mn = 0; md = 0; cp = 0
        for (i = 4; i <= NF; i++) {
            split($i, kv, "="); k = kv[1]; v = kv[2] + 0
            if (k == "n") n = v
            else if (k == "rep") rep = v
            else if (k == "min_ns") mn = v
            else if (k == "med_ns") md = v
            else if (k == "cpb") cp = v
        }
        if (op != "memcpy" || n == 0) next
        PM[n "|" rep "|" impl] = mn
        PD[n "|" rep "|" impl] = md
        if (cp > 0) {
            CP[n "|" rep "|" impl] = cp
            if ((n "|" impl) in gc) { if (cp < gc[n "|" impl]) gc[n "|" impl] = cp }
            else gc[n "|" impl] = cp
        }
        KR[n "|" rep] = 1
        if (!(n in seen)) { seen[n] = 1; order[++cnt] = n }
        if ((n "|" impl) in gm) { if (mn < gm[n "|" impl]) gm[n "|" impl] = mn }
        else gm[n "|" impl] = mn
        if ((n "|" impl) in gd) { if (md < gd[n "|" impl]) gd[n "|" impl] = md }
        else gd[n "|" impl] = md
    }
    END {
        for (oi = 1; oi <= cnt; oi++) {
            n = order[oi]
            printf "\n--- size=%d ---\n", n
            worst = 0; sum = 0; np = 0
            for (k in KR) {
                split(k, p, "|")
                if (p[1] + 0 != n) continue
                a = PM[n "|" p[2] "|naive"]
                b = PM[n "|" p[2] "|ref_scalar"]
                if (a > 0 && b > 0) {
                    r = a / b
                    printf "  rep%-3s paired min ratio naive/ref = %.4f   (%.3f vs %.3f ns)\n", p[2], r, a, b
                    sum += r; np++
                    d = (r > 1) ? r - 1 : 1 - r
                    if (d > worst) worst = d
                }
            }
            a = gm[n "|naive"]; b = gm[n "|ref_scalar"]
            printf "  GLOBAL MIN   naive=%.4f ref=%.4f  ratio=%.4f  (%.2f%% apart)\n", a, b, a / b, 100 * (a / b - 1)
            a = gd[n "|naive"]; b = gd[n "|ref_scalar"]
            printf "  BEST MEDIAN  naive=%.2f ref=%.2f  ratio=%.4f  (%.2f%% apart)\n", a, b, a / b, 100 * (a / b - 1)
            a = gc[n "|naive"]; b = gc[n "|ref_scalar"]
            if (a > 0 && b > 0)
                printf "  BEST cycles/byte naive=%.4f ref=%.4f  ratio=%.4f  (%.2f%% apart)\n", a, b, a / b, 100 * (a / b - 1)
            printf "  NULL-COMPARISON BAND for this size: worst paired rep deviation %.2f%%\n", 100 * worst
        }
    }
' /tmp/noise_floor.log
