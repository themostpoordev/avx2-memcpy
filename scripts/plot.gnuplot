# scripts/plot.gnuplot — retained for portability/documentation.
# gnuplot is NOT installed on the benchmark host and no packages may be
# installed, so this file is not used to produce results/ figures.
# The worked recipe (GB/s vs size, log-x, one line per impl):
#
#   set terminal svg size 1200,700 font 'DejaVu Sans,11'
#   set output 'results/gbps_vs_size.svg'
#   set logscale x 2
#   set xlabel 'size (bytes)'
#   set ylabel 'GB/s (10^9 B/s)'
#   set key outside right
#   set datafile separator ','
#   plot for [i=1:8] '<parse>' using 4:10:xticlabels(1) with linespoints title columnhead
#
# Actual column layout of results/*.csv (see bench/bench.c csv_header()):
#   ../../results/<file>.csv using 4:10
#     1 impl  2 target  3 op  4 size_bytes  5 batch  6 iters
#     7 median_ns  8 min_ns  9 p99_ns  10 gbps  11 gbps_wall
#    12 cycles_per_byte  13 tsc_ticks_per_byte  14 tsc_ghz  15 core_ghz
#    16 cycles_total
set datafile separator ","
