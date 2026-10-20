/* src/memcpy_avx2_mid_disp_gcc.c — Phase 3 Task 1: the shippable single symbol.
 *
 * WHY THIS EXISTS, AND WHY THE MID KERNEL IS NOT A DROP-IN
 *   avx2_mid_pm wins the mid band, but it is not a drop-in replacement for the
 *   Phase 2.6 deliverable: its sub-1 KiB path is the deliverable's body INLINED
 *   into a larger function, and the p3mid/p3mid2 campaigns show that inlining
 *   moved those classes (16 B 2.2 ns against the standalone symbol's 1.5-1.6,
 *   32/64 B 2.8 against 2.2, 128 B -30.1% against -20.1%). Code layout is not a
 *   neutral change at these sizes - the F1 rung proved that by moving both ends of
 *   the band at once - so "the small path is the same code" would have been a false
 *   claim and the sources no longer make it.
 *
 *   This rung is the honest way to combine them: TWO UNTOUCHED SYMBOLS and one
 *   size test. Below 4 KiB it tail-calls the deliverable exactly as that symbol is
 *   certified (its bytes are unchanged, so the 16-512 B wins keep their evidence);
 *   from 4 KiB up it tail-calls the mid kernel, which is where the measured win
 *   is. Both are tail calls, so no stack frame is created and the register set the
 *   callee sees is the one the caller passed.
 *
 * THE PRICE, STATED BEFORE MEASURING
 *   One compare and one unconditional jump per call. At 16 B the deliverable takes
 *   about 1.5 ns (5 cycles), so the jump is a plausible 10-20% of it - which may
 *   cost enough to turn 32/64 B (2.2 ns against glibc's 2.5, a +12.9% win inside a
 *   1.5% floor) into a "measured" row. That is exactly what this rung is for: if
 *   the dispatch eats the small-size wins, the answer is that the mid kernel is a
 *   caller-selected variant and not a better general memcpy, and the report says
 *   so instead of quoting the 16 KiB number as if it were free.
 *
 * THRESHOLD 4 KiB
 *   The same boundary avx2_mid_pm uses for its peel, chosen the same way: 1 KiB is
 *   level or slightly behind for the mid loop, 16 KiB is a certified win on all
 *   three targets, 4 KiB is level either way. One boundary for both the dispatch
 *   and the peel keeps this to a single compare in each place.
 */
#include "common.h"

void *memcpy_avx2_mid_disp(void *dst, const void *src, size_t n)
{
    if (n >= 4096)
        return memcpy_avx2_mid_pm(dst, src, n);
    return memcpy_avx2_final_v2_gcc_noxj(dst, src, n);
}
