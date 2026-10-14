#!/bin/sh
# scripts/hybrid_route_check.sh — Phase 2.5: verify the hybrid dispatcher's
# routing table in the objects that were actually built.
#
#   sh scripts/hybrid_route_check.sh
#
# tools/route_check.c is linked against build/<target>/src/*.o (the shipped
# objects, NOT a fresh compile of the source), so the class mapping and the
# per-class copies it checks are the ones the benchmark binary will run. The
# harness's own bench/*.o are deliberately excluded: they carry main().
#
# Prints one block per target and exits non-zero if any target fails.
set -eu
cd "$(dirname "$0")/.."

rc=0
for t in znver2 znver3 v3; do
    [ -d "build/$t/src" ] || { echo "hybrid_route_check: build/$t/src missing - run make first" >&2; exit 2; }
    objs=$(ls build/$t/src/*.o)
    # -fno-builtin-memcpy: the checker must not let the compiler synthesise the
    # reference copy it is comparing against.
    clang -std=c11 -O2 -I src -DROUTING_TARGET_NAME="$t" -fno-builtin-memcpy \
        tools/route_check.c $objs -static -o "build/$t/route_check_$t"
    echo "================================================================================="
    echo "route_check target=$t (linked against build/$t/src/*.o)"
    echo "================================================================================="
    "build/$t/route_check_$t" || rc=1
    echo ""
done
if [ "$rc" = 0 ]; then
    echo "hybrid_route_check: PASS on all targets"
else
    echo "hybrid_route_check: FAIL" >&2
fi
exit $rc
