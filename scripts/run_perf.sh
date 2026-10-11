#!/bin/sh
# scripts/run_perf.sh — PMU validation pass (Phase 4 plumbing).
#
# The `perf` CLI is NOT installed on this host and nothing may be installed, so
# this drives the harness's own perf_event_open(2) wrapper instead:
#
#   bench/bench_<target> --pmu <impl> <op> <size>
#
# which prints every counter plus derived IPC / miss ratios, and reports the
# group multiplexing scale factor (counts are scaled and flagged when the group
# is wider than the hardware can schedule at once).
#
# Usage: scripts/run_perf.sh [target] [impl] [op] [size ...]
set -eu

TARGET="${1:-znver2}"
IMPL="${2:-ref_scalar}"
OP="${3:-memcpy}"
shift 2>/dev/null || true
shift 2>/dev/null || true
shift 2>/dev/null || true
SIZES="${*:-1024 65536 1048576 16777216 268435456}"

BIN="./bench/bench_${TARGET}"
if [ ! -x "$BIN" ]; then
    echo "run_perf.sh: $BIN missing; run 'make' first" >&2
    exit 1
fi

echo "# run_perf.sh target=$TARGET impl=$IMPL op=$OP"
echo "# perf_event_paranoid=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo '?')"
echo "# governor=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo 'unavailable (no cpufreq sysfs)')"
# Measurements are pinned to one core; run_perf.sh always pins to CPU 2.
# (A bare `taskset -cp $$` inside this script reports the shell's own mask,
#  which is not the mask the measurement runs under - do not quote that.)
echo "# pinning: taskset -c 2 (measurement); shell mask is $(taskset -cp $$ 2>/dev/null | sed 's/.*: //')"

for s in $SIZES; do
    echo
    echo "=== $IMPL $OP $s ==="
    taskset -c 2 "$BIN" --pmu "$IMPL" "$OP" "$s"
done
