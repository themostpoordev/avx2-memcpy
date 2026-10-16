/* src/memcpy_avx2_final_pgo.c — V7: the fused final kernel built with PGO.
 *
 * Knob-for-knob identical to src/memcpy_avx2_final_gcc.c. This translation unit
 * is compiled TWICE by the perf6 rules in the Makefile:
 *
 *   stage 1  -fprofile-generate  -> bench/<t>_perf6gen runs the profile workload
 *                                  (the harness calling this kernel over the
 *                                  13-point ladder, pinned to cpu 2), which
 *                                  writes build/<t>/src/memcpy_avx2_final_pgo.gcda
 *   stage 2  -fprofile-use       -> the same object path, so gcc finds the .gcda
 *                                  it just wrote, and bench/<t>_perf6 contains
 *                                  this object next to the non-PGO one
 *
 * Both stages write the object at the same path on purpose: gcc derives the .gcda
 * name from the object file path, so building the use-stage object anywhere else
 * would silently compile with no profile at all ("missing profile" is a warning,
 * not an error, which is exactly the kind of silent no-op this project keeps
 * getting bitten by). The stage-1 binary is a workload only - it is never
 * benchmarked, because an instrumented kernel is slower by construction and would
 * be a fake comparison.
 *
 * The profile is collected on the harness's own ladder at index 0, i.e. the same
 * call shapes the final campaign measures, so the branch probabilities PGO
 * optimises for are the ones being reported on.
 */
#define TUNED_FN        memcpy_avx2_final_pgo
#define TUNED_UNROLL    4
#define TUNED_ALIGNED   1
#define TUNED_PF_BYTES  128
#define TUNED_PF_CLASS  0
#define TUNED_SMALL_AVX 1
#define TUNED_PF_MIN    65536
#include "memcpy_avx2_tuned_impl.h"
