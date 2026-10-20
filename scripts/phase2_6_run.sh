#!/bin/sh
# scripts/phase2_6_run.sh <tag> <impls> [target ...] — run one Phase 2.6 campaign.
#
#   sh scripts/phase2_6_run.sh v9 "glibc,asm,avx2_final,..." znver2 znver3 v3
#
# Everything the frozen protocol requires, in one place so every campaign of the
# performance push is measured the same way: 13-point ladder, 5 reps x 9 windows
# (RUN_TO_CAP=1, so a campaign never stops early on a quiet window), taskset -c 2,
# iters 4000, and the mandatory `make gate` inside mo_measure.sh - a campaign whose
# tree fails either audit writes no timing row at all.
#
# Logs:  results/phase2_6_<tag>_<target>.windows.log
#        results/phase2_6_<tag>_progress.log  (start/end times, one line per target)
#
# The tag is also what mo_analyze.sh is run against, so a tag must be unique per
# campaign: two campaigns writing the same tag would let a reader mix windows
# from two different binaries, which is the one thing this project does not do.
set -eu
cd "$(dirname "$0")/.."

TAG="${1:?usage: phase2_6_run.sh <tag> <impls> [target ...]}"
IMPLS="${2:?impl list required}"
shift 2
TARGETS="${*:-znver2 znver3 v3}"

SIZES="16,32,64,128,256,512,1024,4096,16384,65536,262144,1048576,4194304"
STAMP="$(date '+%Y-%m-%d %H:%M:%S')"
PROG="results/phase2_6_${TAG}_progress.log"

# PROBE=1 measures bench/<t>_perf6 instead of bench/<t>, per target. That is the
# binary holding the V6 -flto and V7 PGO rungs, which are link-time properties
# and so cannot live in the normal binaries. The tag must still be unique: a
# campaign that mixed windows from two different binaries would not be a
# measurement of anything.

{
    printf 'Phase 2.6 campaign tag=%s start %s\n' "$TAG" "$STAMP"
    printf 'impls=%s\ntargets=%s\n' "$IMPLS" "$TARGETS"
} > "$PROG"

for t in $TARGETS; do
    printf '=== target %s start %s\n' "$t" "$(date '+%Y-%m-%d %H:%M:%S')" >> "$PROG"
    rc=0
    if [ "${PROBE:-0}" = "1" ]; then bin="./bench/bench_${t}_perf6"; else bin=""; fi
    # OP lets the same driver measure the memset ladder (Kernel 8) with the same
    # sizes, windows, pin and gate. Default stays memcpy so every earlier tag is
    # reproducible byte for byte.
    BIN="$bin" ITERS=4000 RUN_TO_CAP=1 sh scripts/mo_measure.sh "$t" "$IMPLS" "${OP:-memcpy}" "$SIZES" \
        "results/phase2_6_${TAG}_${t}.windows.log" 9 9 >> "$PROG" 2>&1 || rc=$?
    printf '=== target %s end rc=%s %s\n' "$t" "$rc" "$(date '+%Y-%m-%d %H:%M:%S')" >> "$PROG"
done
printf 'Phase 2.6 campaign tag=%s done %s\n' "$TAG" "$(date '+%Y-%m-%d %H:%M:%S')" >> "$PROG"
tail -3 "$PROG"
