#!/bin/sh
# scripts/phase2_6_screen.sh [target ...] — Phase 2.6 screening campaign.
#
#   sh scripts/phase2_6_screen.sh znver2 znver3 v3
#
# One campaign that answers V1 (gcc vs clang on identical source), V3 (aligned
# store path), V4 (unroll 4/8/12) and V5 (prefetch: none / fixed +128 / per class)
# in the SAME windows, because a difference between two rungs measured in
# different windows is not a difference.
#
# The rung set is deliberately small and every member is one knob away from its
# neighbour: avx2 (clang, kernel 4) vs avx2_gcc (V1, same source, gcc);
# avx2_t4/t8/t12 (V4 unroll, no aligned path, no prefetch); t8al adds the V3
# aligned path; t8ap adds a fixed +128 B prefetch; t8alp makes that prefetch
# per-class. glibc is measured in the same windows as the baseline.
#
# Parameters are the frozen ones from MEASUREMENT_PROTOCOL.md: 13-point ladder,
# 5 reps x 9 windows, taskset -c 2, iters=4000, budget=0.05, warmup=2,
# RUN_TO_CAP=1, min_of_mins primary. scripts/mo_measure.sh refuses to write a
# timing row unless `make gate` passes, and echoes the gate's rc into the log.
set -eu
cd "$(dirname "$0")/.."

IMPLS="glibc,avx2,avx2_gcc,avx2_t4,avx2_t8,avx2_t12,avx2_t8al,avx2_t8ap,avx2_t8alp"
SIZES="16,32,64,128,256,512,1024,4096,16384,65536,262144,1048576,4194304"
PROG=results/phase2_6_campaign_progress.log
TARGETS="${*:-znver2 znver3 v3}"

if [ ! -f "$PROG" ]; then
    printf 'Phase 2.6 screening campaign start %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" > "$PROG"
else
    printf '\n=== campaign re-invocation %s (targets: %s)\n' \
        "$(date '+%Y-%m-%d %H:%M:%S')" "$TARGETS" >> "$PROG"
fi

for t in $TARGETS; do
    printf '=== target %s start %s\n' "$t" "$(date '+%Y-%m-%d %H:%M:%S')" >> "$PROG"
    rc=0
    ITERS=4000 RUN_TO_CAP=1 sh scripts/mo_measure.sh "$t" "$IMPLS" memcpy "$SIZES" \
        "results/phase2_6_screen_$t.windows.log" 9 9 >> "$PROG" 2>&1 || rc=$?
    printf '=== target %s end rc=%s %s\n' "$t" "$rc" "$(date '+%Y-%m-%d %H:%M:%S')" >> "$PROG"
done
printf 'Phase 2.6 screening campaign done %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" >> "$PROG"
