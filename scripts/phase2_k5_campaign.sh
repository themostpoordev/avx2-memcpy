#!/bin/sh
# scripts/phase2_k5_campaign.sh [target ...] — Kernel 5 (avx2_nt) measurement campaign.
#
#   sh scripts/phase2_k5_campaign.sh znver2 znver3 v3
#
# Runs scripts/mo_measure.sh for each target with the exact parameters this
# campaign is defined by, appending one progress record per target to
# results/phase2_avx2_nt_campaign_progress.log. The per-target raw log is the
# evidence; it carries its own header (impls, sizes, reps, pin, maxwin, iters,
# budget, gate line) so it is self-describing - see MEASUREMENT_PROTOCOL.md §9.
#
# Parameters were frozen when the campaign was defined; they are repeated here
# rather than in a caller's shell history so the campaign can be re-run:
#   impls: every rung up to Kernel 5 plus both prefetch variants and glibc;
#   sizes: the standing 13-point ladder (16 B .. 4 MiB);
#   5 reps x 9 windows (MAXWIN=MINWIN=9, RUN_TO_CAP=1), pinned to cpu 2;
#   iters=4000, budget=0.05 s, warmup=2 - the long-window setting Kernel 4's
#   H5 diagnostic showed is required for the spread to be dominated by the
#   window length rather than by the sample cap.
#
# The driver refuses to write a timing row unless `make gate` (audit +
# audit-width) passes; the gate's rc is echoed into each log by mo_measure.sh.
set -eu
cd "$(dirname "$0")/.."

IMPLS="naive,word,glibc,ref_scalar,sse2,avx2,avx2_pf2,avx2_pf4,avx2_nt"
SIZES="16,32,64,128,256,512,1024,4096,16384,65536,262144,1048576,4194304"
PROG=results/phase2_avx2_nt_campaign_progress.log
TARGETS="${*:-znver2 znver3 v3}"

if [ ! -f "$PROG" ]; then
    printf 'K5 campaign start %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" > "$PROG"
else
    printf '\n=== campaign re-invocation %s (targets: %s)\n' \
        "$(date '+%Y-%m-%d %H:%M:%S')" "$TARGETS" >> "$PROG"
    printf '    (named targets are re-run whole from window 1; a previous log for a\n'
    printf '     named target is replaced by a complete run, complete or not)\n' >> "$PROG"
fi

for t in $TARGETS; do
    printf '=== target %s start %s\n' "$t" "$(date '+%Y-%m-%d %H:%M:%S')" >> "$PROG"
    rc=0
    ITERS=4000 RUN_TO_CAP=1 sh scripts/mo_measure.sh "$t" "$IMPLS" memcpy "$SIZES" \
        "results/phase2_avx2_nt_campaign_$t.windows.log" 9 9 >> "$PROG" 2>&1 || rc=$?
    printf '=== target %s end rc=%s %s\n' "$t" "$rc" "$(date '+%Y-%m-%d %H:%M:%S')" >> "$PROG"
done
printf 'K5 campaign done %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" >> "$PROG"
