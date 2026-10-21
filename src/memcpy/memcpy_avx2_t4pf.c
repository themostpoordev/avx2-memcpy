/* src/memcpy_avx2_t4pf.c — Phase 2.6 V5 take 2: unroll 4 + aligned path + prefetch
 * only from 64 KB up (+128 B). In the screening campaign prefetch cost 43.2 ->
 * 47.6 ns at 4 KB (t8 vs t8ap, same unroll and aligned path) and won 10% at
 * 4 MiB (t8al vs t8ap, 123624 -> 110448 ns), so the knob that matters is the
 * size gate, not the distance. */
#define TUNED_FN       memcpy_avx2_t4pf
#define TUNED_UNROLL   4
#define TUNED_ALIGNED  1
#define TUNED_PF_BYTES 128
#define TUNED_PF_CLASS 0
#define TUNED_SMALL_AVX 1
#define TUNED_PF_MIN   65536
#include "memcpy_avx2_tuned_impl.h"
