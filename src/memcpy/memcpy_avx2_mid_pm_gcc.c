/* src/memcpy_avx2_mid_pm_gcc.c — Phase 3 P1, take 2: the mid loop with the peel
 * gated at 4 KiB.
 *
 * The p3mid campaign priced the peel at both ends of the target band: with it,
 * 16 KiB is +4.76/+5.95/+7.02% ahead of glibc (all three targets); without it,
 * 1 KiB is +1.41% ahead on all three while the peeled rung is 4.22% BEHIND. So
 * the peel pays once the copy is long enough for store lines to dominate (1 KiB
 * is four iterations of the 256 B block, and the peel's address arithmetic is
 * then a real fraction of the work) and costs nothing when it is amortised.
 *
 * This rung is that finding as one knob: MID_PEEL_MIN = 4096. Everything else is
 * identical to `mid`, so at 4 KiB and up the two are the same code, and below
 * 4 KiB this is `nopeel`. It exists so the adopted rung has no new certified loss
 * at 1 KiB - a 4.22% loss there would be worse than the 5.95% win at 16 KiB is
 * good, because it is the size glibc's own dispatch is strongest at.
 *
 * The threshold is a measured boundary, not a fitted one: 1 KiB is on the losing
 * side and 16 KiB on the winning side on every target, 4 KiB is level either way,
 * and 4 KiB is the first size where the loop runs 16 iterations.
 */
#define MID_FN        memcpy_avx2_mid_pm
#define MID_UNROLL    8
#define MID_PF1       256
#define MID_PF2       512
#define MID_PEEL      1
#define MID_PEEL_MIN  4096
#define MID_NTPF      0
#include "memcpy_avx2_mid_impl.h"
