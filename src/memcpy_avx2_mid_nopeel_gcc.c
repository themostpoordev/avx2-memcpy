/* src/memcpy_avx2_mid_nopeel_gcc.c — Phase 3 P4: the mid loop without the peel.
 *
 * Same 256 B iteration, same two streams, same gcc flags; the ONLY difference is
 * that the main loop starts at dst instead of at dst's next 64 B boundary. So the
 * pair (mid vs this rung) is the measured value of explicit cache-line-split
 * handling on 64 B-aligned buffers, which is what P4 asked for.
 *
 * Scope note, and why P4 is not measured at 64/128/256/512 B here: 128 B is a
 * certified structural loss that this phase is forbidden to touch, and 64/256/512 B
 * are certified wins on an untouched code path. Both rungs route n < 1024 to that
 * exact path, so the only sizes where they differ are 1 KiB and up - the band
 * under attack. */
#define MID_FN      memcpy_avx2_mid_nopeel
#define MID_UNROLL  8
#define MID_PF1     256
#define MID_PF2     512
#define MID_PEEL    0
#define MID_NTPF    0
#include "memcpy_avx2_mid_impl.h"
