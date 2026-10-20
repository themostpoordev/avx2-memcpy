/* src/memcpy_avx2_final_ifunc_gcc.c — F2: the dispatch moved to load time.
 *
 * An IFUNC symbol resolves ONCE, when the dynamic loader relocates it, and the
 * resolver sees no arguments: it cannot know whether a given call's dst and src
 * are aligned. So the "resolve to memcpy_aligned vs memcpy_unaligned" of the
 * proposal is not something an IFUNC can do for per-call alignment - the only
 * thing it can move out of the hot path is a decision that is constant for the
 * process (CPU features, environment). What it *adds* is an indirect call: every
 * call goes through the resolver's result via the PLT, one extra branch at entry.
 *
 * This rung therefore measures the part of F2 that is real: the indirection, on
 * top of the exact same kernel body as avx2_final_v2_gcc_noxj. If it is slower at
 * 128/256 B (it should be - it is a call through a pointer where the control is a
 * direct call), then F2 cannot be the fix for those cells; and the per-call
 * decision it cannot make is exactly the one that would have been useful.
 */
#define TUNED_FN         memcpy_avx2_final_ifunc_core
#define TUNED_UNROLL     4
#define TUNED_ALIGNED    1
#define TUNED_PF_BYTES   128
#define TUNED_PF_CLASS   0
#define TUNED_SMALL_AVX  0
#define TUNED_PF_MIN     65536
#define TUNED_SMALL_PATH 1
#include "memcpy_avx2_tuned_impl.h"

/* The ifunc itself. The resolver is allowed to return any function with a
 * compatible type; here it returns the body above, because a resolver cannot
 * inspect pointers it will not be given. */
void *memcpy_avx2_final_ifunc(void *dst, const void *src, size_t n);
static void *resolve_ifunc(void)
{
    return (void *)&memcpy_avx2_final_ifunc_core;
}
void *memcpy_avx2_final_ifunc(void *, const void *, size_t)
    __attribute__((ifunc("resolve_ifunc")));
