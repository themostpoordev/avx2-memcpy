#!/bin/sh
# scripts/nt_threshold_probe.sh [target ...] — find where non-temporal stores start winning.
#
#   sh scripts/nt_threshold_probe.sh            all three targets
#
# WHAT THIS IS, AND WHAT IT IS NOT
#   This is a DIAGNOSTIC. It builds a probe binary in which the NT rung's
#   threshold is forced to 64 B - so every main-path size uses NT stores - and
#   sweeps sizes against the cached rung in the same process and the same
#   windows. Any crossover it reports would be an INFERENCE from the GB/s
#   inflection, not a measurement of the threshold: this box exposes no L3/LLC
#   events, so cache-boundary statements cannot be measured directly here (see
#   MEASUREMENT_PROTOCOL.md section 5). On this box it reports none: NT loses at
#   every size (69-371%), so the committed rung does NOT carry a probe-selected
#   threshold - it keeps a fixed experimental gate (256 KB, see
#   src/memcpy_avx2_nt_impl.h). Only the 9-window campaign on THAT binary may
#   make a claim, and that claim is the loss the Kernel 5 report states.
#
#   Probe binaries are bench/bench_<t>_ntprobe (built by `make ntprobe`); they are
#   never snapshotted and never enter a claim. Evidence lands in
#   results/phase2_avx2_nt_threshold_probe_*.log, inside the repo by policy
#   (section 9).
set -eu
cd "$(dirname "$0")/.."

SWEEP="${SWEEP:-8192,16384,32768,65536,131072,262144,524288,1048576,2097152,4194304}"
PASSES="${PASSES:-3}"
REPS="${REPS:-3}"
ITERS="${ITERS:-4000}"
BUDGET="${BUDGET:-0.05}"
PIN="${PIN:-2}"
ALIGN_SIZE="${ALIGN_SIZE:-1048576}"

command -v taskset >/dev/null 2>&1 || { echo "nt_threshold_probe: taskset missing" >&2; exit 2; }
make -s ntprobe

# "${@:-a b c}" is ONE word when quoted, so the loop ran once with a nonsense
# target; a plain default string plus unquoted expansion is what actually works.
if [ "$#" -gt 0 ]; then TARGETS="$*"; else TARGETS="znver2 znver3 v3"; fi
for t in $TARGETS; do
    LOG="results/phase2_avx2_nt_threshold_probe_$t.log"
    BIN="./bench/bench_${t}_ntprobe"
    [ -x "$BIN" ] || { echo "nt_threshold_probe: $BIN missing" >&2; exit 2; }
    : > "$LOG"
    printf '# nt_threshold_probe target=%s sweep=%s passes=%s reps=%s iters=%s budget=%s pin=cpu%s\n' \
        "$t" "$SWEEP" "$PASSES" "$REPS" "$ITERS" "$BUDGET" "$PIN" >> "$LOG"
    echo "--- $t: NT forced at >= 64 B vs the cached rung, $PASSES passes x $REPS reps"
    for p in $(seq 1 "$PASSES"); do
        printf '# pass %s\n' "$p" >> "$LOG"
        taskset -c "$PIN" "$BIN" --impl avx2,avx2_nt --sizes "$SWEEP" --reps "$REPS" \
            --dump-reps --budget "$BUDGET" --warmup 2 --iters "$ITERS" >> "$LOG" 2>&1
        printf '# pass %s done\n' "$p" >> "$LOG"
    done
    printf '# unaligned-cost sweep (src x dst offsets, step 8, size %s)\n' "$ALIGN_SIZE" >> "$LOG"
    taskset -c "$PIN" "$BIN" --align2d "$ALIGN_SIZE" 8 --impl avx2_nt \
        --budget "$BUDGET" --warmup 2 --iters 100 >> "$LOG" 2>&1

    echo
    echo "  size      cached ns     NT ns   delta   cached GB/s     NT GB/s   NT wins?"
    awk '
        /^REPS / {
            impl=$2; op=$3; n=0; mn=-1
            for (i=4;i<=NF;i++) { split($i,kv,"="); k=kv[1]; v=kv[2]+0
                if (k=="n") n=v; else if (k=="min_ns") mn=v }
            if (op!="memcpy" || n==0 || mn<=0) next
            key=impl"|"n
            # gbps is NOT in the --dump-reps line (it exists only in the CSV mode),
            # so the first version of this script printed -1.00 in that column for
            # every row. Derived here from the copy size and the minimum instead.
            if (!((key) in best) || mn<best[key]) { best[key]=mn; bestgb[key]=n/mn }
            if (n>maxn) maxn=n
        }
        END {
            firstwin=0
            for (i=1;i<=maxn;i++) if ((("avx2|" i) in best) && (("avx2_nt|" i) in best)) {
                c=best["avx2|" i]; nt=best["avx2_nt|" i]
                d=100*(c-nt)/c
                win=(d>0)?"YES":"no"
                printf "  %-9d %10.1f  %9.1f  %+6.2f%%  %12.2f  %10.2f   %s\n", i, c, nt, d, bestgb["avx2|" i], bestgb["avx2_nt|" i], win
                if (d > 0 && firstwin == 0) firstwin = i
            }
            printf "\n  first size where NT is faster: %d B\n", firstwin
        }' "$LOG"
    echo "  unaligned handling: dst offsets not multiple of 32 take the cached lead; see the"
    echo "  align2d rows at the end of $LOG (aligned src/dst off 0/32 vs off 8/16/24)"
done
