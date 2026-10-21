/* SPDX-License-Identifier: MIT */
/* src/fast.c — the shipped memcpy. Builds `memcpy_fast` (include/memcpy_fast.h).
 *
 * The F4 shape, plus the 128 B case that Phase 7 settled.
 *
 * WHAT CHANGED, AND WHY IT IS HERE
 *   F4 is V9c plus a straight-line window for 128 <= n <= 256, entered with
 *   ONE compare and no further branches. The window used a 128-byte block pair
 *   (T_BLOCKPAIR(4) = four 32 B blocks at the head plus four at the tail).
 *
 *   That is the wrong shape at exactly n = 128. The ladder's own rule is that a
 *   class window is (H, 2H] and the block pair uses H = the class's LOWER
 *   bound, so the two blocks tile [0, n) exactly at n = 2H; for the (64, 128]
 *   class that is H = 64. The F4 window instead used H = 128 over the range
 *   that starts at 128, so at n = 128 its head block and its tail block were
 *   the SAME four 32 B blocks: eight loads and eight stores for a 128 B copy
 *   where glibc issues four and four. The signature was in the shipped
 *   numbers - this kernel's 128 B copy (3.4238 ns) cost MORE than its own
 *   256 B copy (3.4023 ns) on all three targets, while glibc's 128 B copy
 *   (3.1113 ns) was 16% cheaper than its 256 B copy (3.7148 ns).
 *
 *   This kernel therefore gives n == 128 the H = 64 pair inside the window:
 *   T_BLOCKPAIR(2) copies [0,64) + [n-64,n), which at n = 128 is [0,64) +
 *   [64,128) - four loads and four stores, every byte once, nothing written
 *   or read outside [0, n). The window's other sizes (129..256) keep the H =
 *   128 pair unchanged.
 *
 *   The measured result: 128 B goes from a certified loss to a win against
 *   glibc. The price is 512 B, which loses 2.24 points of margin (it stays a
 *   certified win). The mechanism is in docs/NEGATIVE_RESULTS.md: the window
 *   body sits BEFORE the flat class chain, so the branch that dispatches the
 *   H = 64 pair grows the code in front of the chain and displaces it, which
 *   moves the 512 B path's position in the code stream.
 *
 * THE ALIGNMENT FLAG IS PART OF THE KERNEL
 *   This translation unit is built with -fno-crossjumping AND
 *   -falign-labels=64 (Makefile, GCC_EXTRA_fast.c). The alignment flag is not
 *   cosmetic: without it the 32..64 B path loses the 64-byte alignment of its
 *   out-of-line branch target and that costs 16% at 32 B and 64 B, which are
 *   certified wins. The flag is per-file and does not affect any other
 *   kernel. Removing it changes this kernel and invalidates its certification.
 *
 * THE CERTIFIED NUMBERS
 *   See docs/RESULTS.md for the certified table, docs/METHODOLOGY.md for the
 *   claim model, and docs/data/raw/ for the window logs every row is derived
 *   from. They are not restated here because they are read from the committed
 *   campaign, not from a comment.
 */
#define TUNED_FN         memcpy_fast
#define TUNED_UNROLL     4
#define TUNED_ALIGNED    1
#define TUNED_PF_BYTES   128
#define TUNED_PF_CLASS   0
#define TUNED_SMALL_AVX  0
#define TUNED_PF_MIN     65536
#define TUNED_SMALL_PATH 1
#define TUNED_WINDOW     1
#define TUNED_WIN_LO     128
#define TUNED_WIN64      1
#include "fast_impl.h"
