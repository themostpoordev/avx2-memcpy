/* src/memcpy_avx2_mid_u4pf1_gcc.c — Phase 3 P1 control, diagnostic only.
 *
 * The p3mid campaign's unroll-4 rung lost 25-32% at 1-4 KiB and 14-15% at 16 KiB.
 * Its disassembly shows a correct 128 B iteration (four 32 B moves, one backward
 * branch), so "the compiler did not unroll it" is not the explanation; the
 * remaining candidate is prefetch density - three prefetch uops per four moves in
 * a loop whose data is already in L1 costs more than the same three uops per eight
 * moves.
 *
 * This rung removes that factor: unroll 4 (BLOCK 128) with ONE stream at +128 B.
 * If it recovers most of the u4 deficit, the anomaly is prefetch density and not
 * block size; if it does not, the anomaly is in the 128 B shape itself, which
 * contradicts the deliverable (unroll 4, no prefetch) being only 1.2% behind. It
 * is never adopted: it exists to make the u4 row reportable.
 */
#define MID_FN      memcpy_avx2_mid_u4pf1
#define MID_UNROLL  4
#define MID_PF1     128
#define MID_PF2     0
#define MID_PEEL    1
#define MID_NTPF    0
#include "memcpy_avx2_mid_impl.h"
