/* src/memcpy_avx2_mid_pfnear_gcc.c — Phase 3 P2, the brief's literal distances.
 *
 * P1/P2 asked for "+2 and +4 cache lines". On a 256 B block those distances sit
 * inside the block the loop is already loading, so the literal specification is
 * measured here rather than dismissed in a comment: this rung is `mid` with
 * MID_PF1 = 128 (+2 lines) and MID_PF2 = 256 (+4 lines).
 *
 * Prediction before measuring: no better than `mid`, because pf@+128 is issued
 * for bytes the next four loads read anyway. If it comes out equal, the two
 * streams were dead work and the stream count - not the distance - is the knob. */
#define MID_FN      memcpy_avx2_mid_pfnear
#define MID_UNROLL  8
#define MID_PF1     128
#define MID_PF2     256
#define MID_PEEL    1
#define MID_NTPF    0
#include "memcpy_avx2_mid_impl.h"
