#!/bin/sh
# scripts/measure/quiet_window.sh [target] [impl] [op] [size] [windows] [gap_seconds]
#
# Repeated-window measurement for the Phase 2 noise protocol.
#
# This host's medians vary 25-45% run-to-run, but the variation is SYSTEMATIC
# WITHIN a window and random ACROSS windows (results/docs/phase2_SUMMARY.md, step 3):
# one window can be internally steady to 3.7% while another is 43% chaotic, and
# the steady window can be the slower one. A single run therefore cannot be
# trusted, and "take the min" over one run can be fooled (a 4 KB global-min read
# -20.1% against a byte-identical kernel in one measured run).
#
# Protocol implemented here:
#   - measure the same impl/size n times back-to-back per window;
#   - record the window's OWN spread; QUIET when the median spread is < 5%;
#   - only QUIET windows contribute to the cross-window best;
#   - report min_ns AND PMU cycles/byte, because the core clock drifts
#     2.73-3.06 GHz on this box and cycles/byte does not share that confound.
#
# Example (per kernel, per size, from kernel 2 onwards):
#   ./scripts/quiet_window.sh znver2 memcpy_word memcpy 65536 3 60
set -eu

TARGET="${1:-znver2}"
IMPL="${2:-naive}"
OP="${3:-memcpy}"
SIZE="${4:-65536}"
WINDOWS="${5:-3}"
GAP="${6:-60}"
RUNS="${RUNS:-20}"
QUIET="${QUIET:-5.0}"     # per-window median spread below which a window is "quiet"

BIN="./bench/bench_${TARGET}"
[ -x "$BIN" ] || { echo "quiet_window: no binary $BIN (run make first)" >&2; exit 2; }

SUM="${TMPDIR:-/tmp}/quiet_window_sum_$$.txt"
OUT="${TMPDIR:-/tmp}/quiet_window_out_$$.txt"
trap 'rm -f "$SUM" "$OUT"' EXIT INT TERM
: > "$SUM"

# field helper: value of <key> on the line starting with <prefix>
field() {   # field <prefix> <key> <file>
    awk -F'[= ]' -v pre="$1" -v key="$2" '
        index($0, pre) == 1 { for (i = 1; i <= NF; i++) if ($i == key) { print $(i + 1); exit } }
    ' "$3"
}

printf 'quiet_window: target=%s impl=%s op=%s size=%s windows=%s gap=%ss runs/window=%s quiet_threshold=%s%%\n' \
    "$TARGET" "$IMPL" "$OP" "$SIZE" "$WINDOWS" "$GAP" "$RUNS" "$QUIET"
printf '%-7s %-9s %10s %8s %12s %10s %9s %9s %s\n' \
    window clock med_ns spread min_ns best_cpb core_ghz null_ns verdict

i=1
while [ "$i" -le "$WINDOWS" ]; do
    if ! "$BIN" --noise "$IMPL" "$OP" "$SIZE" "$RUNS" > "$OUT" 2>/dev/null; then
        echo "quiet_window: window $i measurement failed" >&2
        exit 1
    fi

    med=$(field  median_of_medians_ns median_of_medians_ns "$OUT")
    spr=$(field  median_of_medians_ns spread_pct            "$OUT")
    mn=$(field   median_of_mins_ns    min_of_mins_ns        "$OUT")
    nul=$(field  null_kernel_median_ns null_kernel_median_ns "$OUT")
    : "${med:=0}"; : "${spr:=999}"; : "${mn:=0}"; : "${nul:=0}"
    cpb=$(awk -F, '/^[0-9]+,/ { c = $5 + 0; if (c > 0 && (b == 0 || c < b)) b = c } END { printf "%.4f", b + 0 }' "$OUT")
    chz=$("$BIN" --calib 2>/dev/null | tr ' ' '\n' | sed -n 's/^core_ghz=//p' | head -1)
    : "${chz:=0}"

    verdict=$(awk -v s="$spr" -v q="$QUIET" 'BEGIN { print (s < q) ? "QUIET" : "contended" }')
    printf '%-7s %-9s %10.2f %7.2f%% %12.2f %10.4f %9s %9s %s\n' \
        "$i" "$(date +%T)" "$med" "$spr" "$mn" "$cpb" "$chz" "$nul" "$verdict"
    # machine-readable accumulation: win med spread min best_cpb null quiet(0/1)
    awk -v w="$i" -v med="$med" -v spr="$spr" -v mn="$mn" -v cp="$cpb" -v nu="$nul" -v q="$QUIET" \
        'BEGIN { printf "%d %.4f %.4f %.4f %.4f %.4f %d\n", w, med, spr, mn, cp, nu, (spr < q) }' >> "$SUM"

    [ "$i" -lt "$WINDOWS" ] && sleep "$GAP"
    i=$((i + 1))
done

awk '
    { q[$1] = $8
      if ($8 == 1) {
          nq++
          if (bmin == 0 || $4 < bmin) bmin = $4
          if ($5 > 0 && (bcpb == 0 || $5 < bcpb)) bcpb = $5
          if (bmed == 0 || $2 < bmed) bmed = $2
          if (qlo == 0 || $4 < qlo) qlo = $4
          if ($4 > qhi) qhi = $4
      }
      if (worst == 0 || $3 > worst) worst = $3
      medsum += $2
    }
    END {
        printf "\nwindows=%d quiet=%d worst_window_spread=%.2f%%\n", NR, nq, worst
        if (nq == 0) {
            print "RESULT: NO QUIET WINDOW — box too contended for a trustworthy number."
            print "        do not compare kernels from these samples; retry later or use >2x effects."
        } else {
            printf "RESULT: best over %d quiet window(s): min_ns=%.2f  best_cycles_per_byte=%.4f  best_median_ns=%.2f\n", \
                   nq, bmin, bcpb, bmed
            printf "        min_ns across quiet windows: %.2f .. %.2f  (%.2f%% apart -> report as the floor)\n", \
                   qlo, qhi, (qlo > 0 ? 100 * (qhi - qlo) / qlo : 0)
        }
    }
' "$SUM"

rm -f "$SUM" "$OUT"
trap - EXIT INT TERM
