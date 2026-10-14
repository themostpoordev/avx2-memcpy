#!/bin/sh
# scripts/phase2_hybrid_campaign.sh [target ...] — Phase 2.5 hybrid campaign.
#
#   sh scripts/phase2_hybrid_campaign.sh znver2 znver3 v3
#
# Runs scripts/mo_measure.sh for each target with the parameters this campaign is
# defined by and appends one progress record per target to
# results/phase2_hybrid_campaign_progress.log. The per-target raw log is the
# evidence and carries its own header (impls, sizes, reps, pin, maxwin, iters,
# budget, gate line) - see MEASUREMENT_PROTOCOL.md section 9.
#
# WHY EVERY RUNG IS IN THE LIST
#   The claim this campaign has to settle is "the hybrid beats the best single
#   rung", and the only way to answer it is to measure the hybrid and all six
#   rungs in the SAME windows. The generator read a previous campaign for the
#   routing table; this campaign re-measures everything, so the comparison is
#   within one window set even where the table was built from another.
#
# SIZES
#   the 13 standing ladder points, plus 100, 1000, 20000 and 300000. Those four
#   fall BETWEEN measured class boundaries, so they are what tests the Phase 2.5
#   "a size between two measured points uses the lower point's winner" rule with
#   real timing instead of a printout: the hybrid must land on the lower class's
#   rung, and the per-size comparison shows whether doing so was right.
#
#   iters=4000, budget=0.05 s, warmup=2 - the long-window setting Kernel 4's H5
#   diagnostic showed is required; 5 reps x 9 windows (MAXWIN=MINWIN=9,
#   RUN_TO_CAP=1), pinned to cpu 2, min_of_mins primary.
#
# The driver refuses to write a timing row unless `make gate` passes; the gate's
# rc is echoed into each log by mo_measure.sh.
set -eu
cd "$(dirname "$0")/.."

IMPLS="naive,word,glibc,ref_scalar,sse2,avx2,avx2_nt,asm,hybrid"
SIZES="16,32,64,100,128,256,512,1000,1024,4096,16384,20000,65536,262144,300000,1048576,4194304"
PROG=results/phase2_hybrid_campaign_progress.log
TARGETS="${*:-znver2 znver3 v3}"

if [ ! -f "$PROG" ]; then
    printf 'Phase 2.5 hybrid campaign start %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" > "$PROG"
else
    printf '\n=== campaign re-invocation %s (targets: %s)\n' \
        "$(date '+%Y-%m-%d %H:%M:%S')" "$TARGETS" >> "$PROG"
    printf '    (named targets are re-run whole from window 1; a previous log for a\n' >> "$PROG"
    printf '     named target is replaced by a complete run, complete or not)\n' >> "$PROG"
fi

for t in $TARGETS; do
    printf '=== target %s start %s\n' "$t" "$(date '+%Y-%m-%d %H:%M:%S')" >> "$PROG"
    rc=0
    ITERS=4000 RUN_TO_CAP=1 sh scripts/mo_measure.sh "$t" "$IMPLS" memcpy "$SIZES" \
        "results/phase2_hybrid_campaign_$t.windows.log" 9 9 >> "$PROG" 2>&1 || rc=$?
    printf '=== target %s end rc=%s %s\n' "$t" "$rc" "$(date '+%Y-%m-%d %H:%M:%S')" >> "$PROG"
done
printf 'Phase 2.5 hybrid campaign done %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" >> "$PROG"
