#!/bin/sh
# scripts/plot.sh — plotting status check.
#
# No packages may be installed on this host. Findings (Phase 0):
#   gnuplot   : MISSING
#   python3   : 3.14.8 (numpy 2.5.4 present, matplotlib MISSING, svgwrite MISSING)
# Therefore scripts/plot.gnuplot is kept only as a reference/portable recipe and
# is NOT used to produce results. Phase 5 delivers a self-contained SVG
# generator written in C (tools/csv2svg) that reads the results/*.csv files and
# emits SVG with zero external dependencies.
set -eu

echo "== plotting toolchain =="
printf 'gnuplot : '; command -v gnuplot || echo MISSING
printf 'python3 : '; command -v python3 || echo MISSING
python3 - <<'EOF' 2>/dev/null || true
mods = []
for m in ("numpy", "matplotlib", "svgwrite"):
    try:
        __import__(m); mods.append(m + "=yes")
    except Exception:
        mods.append(m + "=no")
print("python modules: " + " ".join(mods))
EOF
echo
echo "gnuplot recipe retained at scripts/plot.gnuplot (unused: gnuplot absent)."
echo "Phase 5 plan: tools/csv2svg (pure C, no deps) reads results/*.csv -> results/*.svg"
