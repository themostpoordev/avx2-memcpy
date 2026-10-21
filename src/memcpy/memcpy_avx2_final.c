/* src/memcpy_avx2_final.c — Phase 2.6 V9: the fused kernel (clang build).
 *
 * Every knob here is the winner of a measured vector, not a preference:
 *   UNROLL 4      t4 beat t8/t12 at 64 B-4 KB (round 1 screen, 3 targets) and the
 *                 unroll differences at 16 KB-256 KB were inside the noise band;
 *   ALIGNED 1     V3; t8ap vs t8al showed the aligned store path plus prefetch is
 *                 worth 10% at 4 MiB (110448 vs 123624 ns, znver2);
 *   PF 128 gated  V5b; a flat +128 B prefetch hurt 4 KB (43.2 -> 47.6 ns) and won
 *     at 64 KB    10% at 4 MiB, so it is gated on at 64 KB and above;
 *   SMALL_AVX 1   the 32..63 B class uses two overlapping 32 B moves (one per end)
 *                 instead of four 16 B moves. The gcc-built Kernel 4 body suggested
 *                 the 16 B shape might be faster at 32 B, which is why the final
 *                 campaign also carries a SMALL_AVX 0 build (memcpy_avx2_final16_gcc)
 *                 rather than settling it by argument;
 *   flat <= 512   V8; 512 B and below is copied by the flat block pairs (no loop,
 *                 no tail at all), which the corrected classes made cheap.
 */
#define TUNED_FN        memcpy_avx2_final
#define TUNED_UNROLL    4
#define TUNED_ALIGNED   1
#define TUNED_PF_BYTES  128
#define TUNED_PF_CLASS  0
#define TUNED_SMALL_AVX 1
#define TUNED_PF_MIN    65536
#include "memcpy_avx2_tuned_impl.h"
