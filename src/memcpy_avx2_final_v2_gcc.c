/* src/memcpy_avx2_final_v2_gcc.c — V9b: the fused final kernel with V8's small path.
 *
 * Knob-for-knob identical to src/memcpy_avx2_final_gcc.c (unroll 4, 32 B-aligned
 * main loop, flat +128 B prefetch gated at 64 KB, 32..63 B as four 16 B moves)
 * except for ONE addition: TUNED_SMALL_PATH 1, which takes n <= 64 from a single
 * compare instead of walking the flat-class chain into it.
 *
 * Why this rung exists (measured, not argued). The final1 campaign priced the
 * fused kernel at 16 B / 32 B / 64 B at 3.1 ns against glibc's 2.5 ns - a 25%
 * loss, and the largest one the fused kernel has anywhere - while the older
 * Kernel 4 body, whose small path is the same overlapping moves, was 2.2 ns and
 * WON +12.5% at 16 B. Nothing about the copies differs; only the entry chain
 * does (cmp 512, 256, 128, 64, 32, 16 before the tiny case is reached). The same
 * campaign's PGO rung - which lets the compiler move the cold classes out of the
 * way - recovered exactly that: 1.9 ns at 16 B and 32 B, +25% over glibc on all
 * three targets. So the hypothesis is that the chain, not the moves, is the 25%,
 * and this rung tests it statically, with no profile data in the build.
 *
 * Adopted only if it beats avx2_final_gcc at 16/32/64 B by more than that pair's
 * tier floor AND leaves 256 B..4 MiB where they were; the latter is the control,
 * because a small-path change that moves the big sizes has done something else.
 *
 * Built by gcc because V1 measured gcc ahead of clang on identical source across
 * 32 B..4 KB, which is exactly the range this rung is about.
 */
#define TUNED_FN         memcpy_avx2_final_v2_gcc
#define TUNED_UNROLL     4
#define TUNED_ALIGNED    1
#define TUNED_PF_BYTES   128
#define TUNED_PF_CLASS   0
#define TUNED_SMALL_AVX  0
#define TUNED_PF_MIN     65536
#define TUNED_SMALL_PATH 1
#include "memcpy_avx2_tuned_impl.h"
