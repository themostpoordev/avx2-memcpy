/* src/memcpy_avx2_mid_pf1_gcc.c — Phase 3 P2: one prefetch stream instead of two.
 *
 * Identical to memcpy_avx2_mid except MID_PF2 = 0, so the pair answers P2's
 * "which stream count wins" at every measured size: 0 streams (the deliverable,
 * which does not prefetch below 64 KB), 1 stream (this rung), 2 streams (`mid`).
 * Each prefetch is a uop on the load ports of this core, so a second stream is
 * not free and the answer is expected to be size-dependent. */
#define MID_FN      memcpy_avx2_mid_pf1
#define MID_UNROLL  8
#define MID_PF1     256
#define MID_PF2     0
#define MID_PEEL    1
#define MID_NTPF    0
#include "memcpy_avx2_mid_impl.h"
