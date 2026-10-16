#!/bin/sh
# scripts/mo_measure.sh <target> <impl-list> <op> <sizes> <logfile> [maxwin] [minwin]
#
#   scripts/mo_measure.sh znver2 naive,ref_scalar,glibc memcpy 16,64,4096 \
#                         results/phase2_naive_znver2.windows.log
#
# Measurement driver for the approved protocol (results/MEASUREMENT_PROTOCOL.md):
#
#   - every window is PINNED to cpu 2 with taskset;
#   - 5 reps per window (REPS env overrides), interleaved across impls inside
#     one process by the harness;
#   - at least MINWIN windows, then keep adding windows until min_of_mins is
#     stable to <= STABLE_PCT across the last two windows, capped at MAXWIN.
#
# The log holds the raw evidence: one "# WINDOW n <epoch>" marker per window,
# the harness's CSV rows on stdout, and the per-rep dump lines (with cpu= and
# migrated=) on stderr. Nothing is summarised here; scripts/mo_analyze.sh reads
# this file and applies the claim floor.
set -eu

TARGET="${1:?usage: mo_measure.sh <target> <impls> <op> <sizes> <log> [maxwin] [minwin]}"
IMPLS="${2:?impl list required}"
OP="${3:-memcpy}"
SIZES="${4:?size list required}"
LOG="${5:?log path required}"
MAXWIN="${6:-9}"
MINWIN="${7:-3}"

REPS="${REPS:-5}"
STABLE_PCT="${STABLE_PCT:-0.5}"
# RUN_TO_CAP=1 disables the early stable stop and always runs MAXWIN windows.
#
# Reason (measured): the 0.5% stop can terminate while a deeper floor is still
# undiscovered. v3 stopped at window 5 on a 0.04% change, and znver2's naive
# 4 KB key sat flat for six windows and then improved 20% at window 9. Because
# min_of_mins only ever decreases with more windows, campaigns that stop at
# different window counts are NOT comparable with each other, so cross-target
# work runs to the cap and per-key stability is published instead.
RUN_TO_CAP="${RUN_TO_CAP:-0}"
BUDGET="${BUDGET:-0.05}"
WARMUP="${WARMUP:-2}"
ITERS="${ITERS:-100}"
PIN="${PIN:-2}"

# BIN may be overridden: the V6/V7 probe rungs (-flto, PGO) live in
# bench/<t>_perf6 because they are link-time properties, and a campaign that
# measures them has to run THAT binary. Everything else about the measurement is
# unchanged - in particular the rung being compared against is still measured in
# the same process and the same windows.
BIN="${BIN:-./bench/bench_${TARGET}}"
[ -x "$BIN" ] || { echo "mo_measure: $BIN missing (run make first)" >&2; exit 2; }
command -v taskset >/dev/null 2>&1 || {
    echo "mo_measure: taskset not found; pinning is mandatory, refusing to measure" >&2
    exit 2
}

# MANDATORY GATE. `make audit` (no vector instructions in the scalar TUs) and
# `make audit-width` (every rung's widest memory access equals its declared width)
# must both pass before this driver is allowed to write a single timing row. The
# width gate exists because that is how the same SLP-merge defect reached two
# consecutive kernels unnoticed; see scripts/audit_width.sh. The result is echoed
# into the log below, so a campaign's own raw evidence shows the gate ran.
if ! make -s gate >/dev/null 2>&1; then
    echo "mo_measure: refusing to measure - 'make gate' failed; output follows" >&2
    make audit audit-width >&2 || true
    exit 2
fi

mkdir -p "$(dirname "$LOG")"
: > "$LOG"
# The window settings are part of the measurement, not decoration: H5
# (results/h4_diagnostics.md) showed that the per-window spread is dominated by
# how long the timed window is, and Kernel 4's first campaign was run with the
# default 100-sample cap before that was known. A log that does not record
# iters/budget cannot be audited afterwards, so it is recorded here.
printf '# mo_measure target=%s impls=%s op=%s sizes=%s reps=%s pin=cpu%s maxwin=%s minwin=%s stable=%s%% iters=%s budget=%s warmup=%s run_to_cap=%s\n' \
    "$TARGET" "$IMPLS" "$OP" "$SIZES" "$REPS" "$PIN" "$MAXWIN" "$MINWIN" "$STABLE_PCT" \
    "$ITERS" "$BUDGET" "$WARMUP" "$RUN_TO_CAP" >> "$LOG"
printf '# gate: make audit PASSED and make audit-width PASSED (rc=0) before window 1\n' >> "$LOG"

# Rule 2 stability: min_of_mins is the smallest min_ns over ALL reps in ALL
# windows, so the quantity that must stabilise is the CUMULATIVE running
# minimum, not a single window's minimum.
#
# The first version of this function compared window w-1's minimum with window
# w's minimum and reported 44-79% swings on every window, even though the
# cumulative metric was already flat to 0.00% for most keys: per-window minima
# are exactly as noisy as medians, which is why the protocol uses min_of_mins.
# Nothing was re-measured to fix this; the metric was recomputed from the same
# log.
#
# Prints: <max relative change over keys, %> <worst key>
#
# NOTE on scope: the protocol states rule 2 per (impl, target, op, size), but a
# campaign measures many keys at once, so a campaign-level stop can only be the
# worst key. That is stricter than the rule, which means this driver tends to run
# to MAXWIN rather than stopping early; the per-key stability is published by
# scripts/mo_analyze.sh so a single still-moving key can be qualified as
# unusable instead of silently poisoning the rest. Only keys for the measured op
# count, so unrelated memset rows cannot hold the campaign hostage.
stability() {
    awk -v last="$1" -v wantop="$OP" '
        $1 == "#" && $2 == "WINDOW" { w = $3 + 0; next }
        $1 == "REPS" {
            impl = $2; op = $3; n = 0; mn = -1; mig = 0
            for (i = 4; i <= NF; i++) {
                split($i, kv, "="); k = kv[1]; v = kv[2] + 0
                if (k == "n") n = v
                else if (k == "min_ns") mn = v
                else if (k == "migrated") mig = v
            }
            if (mn <= 0 || mig == 1) next
            if (op != wantop) next
            key = impl "|" op "|" n
            if (!((key) in cur) || mn < cur[key]) cur[key] = mn
            cum[w "|" key] = cur[key]
            keys[key] = 1
        }
        END {
            worst = 0; wk = "-"
            for (key in keys) {
                a = cum[(last - 1) "|" key]; b = cum[last "|" key]
                if (!(a > 0) || !(b > 0)) continue
                d = 100 * (b - a) / a
                if (d < 0) d = -d
                if (d > worst) { worst = d; wk = key }
            }
            printf "%.4f %s\n", worst, wk
        }
    ' "$LOG"
}

stable=0
win=1
while [ "$win" -le "$MAXWIN" ]; do
    printf '# WINDOW %d %s\n' "$win" "$(date +%s)" >> "$LOG"
    if ! taskset -c "$PIN" "$BIN" --impl "$IMPLS" --sizes "$SIZES" --reps "$REPS" \
            --dump-reps --budget "$BUDGET" --warmup "$WARMUP" --iters "$ITERS" \
            >> "$LOG" 2>&1; then
        echo "mo_measure: window $win failed; see $LOG" >&2
        exit 1
    fi

    nwin=$(grep -c '^# WINDOW' "$LOG" || true)
    nrep=$(grep -c '^REPS' "$LOG" || true)
    nmig=$(grep -c 'migrated=1' "$LOG" || true)
    echo "window $nwin: $nrep rep lines total, $nmig migrated rep(s) dropped"

    if [ "$nwin" -ge "$MINWIN" ] && [ "$RUN_TO_CAP" != "1" ]; then
        sres=$(stability "$nwin")
        chg=$(printf '%s' "$sres" | cut -d" " -f1)
        wkey=$(printf '%s' "$sres" | cut -d" " -f2)
        echo "  cumulative min_of_mins change vs previous window: ${chg}% (worst key: ${wkey}; stable at <=${STABLE_PCT}%)"
        if awk -v c="$chg" -v t="$STABLE_PCT" 'BEGIN { exit !(c <= t) }'; then
            echo "  STABLE -> stopping at $nwin windows"
            stable=1
            break
        fi
    elif [ "$RUN_TO_CAP" = "1" ] && [ "$nwin" -ge "$MINWIN" ]; then
        sres=$(stability "$nwin")
        echo "  cumulative change vs previous window: $(printf '%s' "$sres" | cut -d" " -f1)% (worst key: $(printf '%s' "$sres" | cut -d" " -f2)); RUN_TO_CAP=1, continuing"
    fi
    win=$((win + 1))
done

nwin=$(grep -c '^# WINDOW' "$LOG" || true)
if [ "$stable" = "0" ] && [ "$nwin" -ge "$MAXWIN" ]; then
    echo "  note: reached MAXWIN=$MAXWIN without meeting the ${STABLE_PCT}% stability rule;"
    echo "        per-key stability is published by mo_analyze.sh; keys still moving"
    echo "        are unusable for a claim, not silently averaged in."
fi
echo "log: $LOG ($nwin windows)"
