#!/bin/sh
# scripts/phase2_k6_campaign.sh [target ...] — Kernel 6 (memcpy_asm) campaign.
#
#   sh scripts/phase2_k6_campaign.sh znver2 znver3 v3
#
# Runs scripts/mo_measure.sh for each target with the exact parameters this
# campaign is defined by, appending one progress record per target to
# results/phase2_asm_campaign_progress.log. The per-target raw log is the
# evidence and is self-describing: mo_measure.sh writes its own header (impls,
# sizes, reps, pin, maxwin, iters, budget) and the mandatory gate line before
# window 1 (MEASUREMENT_PROTOCOL.md section 9).
#
# Parameters, frozen for this campaign and repeated here rather than living in a
# caller's shell history:
#   impls: every rung up to Kernel 6 plus glibc. avx2 is the BENCHMARK (the C
#          AVX2 rung); avx2_nt is carried so the standing report shows the whole
#          ladder in one table; naive + ref_scalar are the null band.
#   sizes: the standing 13-point ladder (16 B .. 4 MiB);
#   5 reps x 9 windows (MAXWIN=MINWIN=9, RUN_TO_CAP=1), pinned to cpu 2;
#   iters=4000, budget=0.05 s, warmup=2 - the long-window setting Kernel 4's H5
#   diagnostic showed is required for the spread to be dominated by the window
#   length rather than by the sample cap.
#
# The driver refuses to write a timing row unless `make gate` (audit +
# audit-width) passes; the gate's rc is echoed into each log by mo_measure.sh.
set -eu
cd "$(dirname "$0")/.."

IMPLS="naive,word,glibc,ref_scalar,sse2,avx2,avx2_nt,asm"
SIZES="16,32,64,128,256,512,1024,4096,16384,65536,262144,1048576,4194304"
PROG=results/phase2_asm_campaign_progress.log
TARGETS="${*:-znver2 znver3 v3}"

if [ ! -f "$PROG" ]; then
    printf 'K6 campaign start %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" > "$PROG"
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
        "results/phase2_asm_campaign_$t.windows.log" 9 9 >> "$PROG" 2>&1 || rc=$?
    printf '=== target %s end rc=%s %s\n' "$t" "$rc" "$(date '+%Y-%m-%d %H:%M:%S')" >> "$PROG"
done
printf 'K6 campaign done %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" >> "$PROG"
