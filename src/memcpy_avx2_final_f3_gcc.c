/* src/memcpy_avx2_final_f3_gcc.c — F3, in the only form this project can verify.
 *
 * F3 was "two exported symbols, caller chooses": a symbol whose contract is
 * "dst and src are 32 B aligned", so the runtime alignment test disappears. Two
 * things stop that from being a rung here:
 *
 *   1. bench --verify walks n = 0..4096 at RANDOM src/dst offsets (bench.c
 *      rng_offset), so a kernel that legitimately assumes alignment would write
 *      to the wrong addresses and fail the pass. Weakening `--verify` for one
 *      rung is not a trade this project makes, and `make gate` would refuse it.
 *   2. The test it removes is not executed at 128 or 256 B at all: those sizes
 *      leave through the n <= 512 flat-class chain, before the alignment check.
 *      So no alignment-contract symbol can recover those two cells.
 *
 * What is left is the part that is verifiable and that bounds F3's value: the
 * cost of the test itself. TUNED_ALIGNED 0 deletes the test and the aligned loop
 * and always runs the unaligned loop, which is correct for every pointer. If
 * that is 0.00% at 128/256 B, then F3 has nothing to win there, contract change
 * or not.
 */
#define TUNED_FN         memcpy_avx2_final_f3_notest
#define TUNED_UNROLL     4
#define TUNED_ALIGNED    0
#define TUNED_PF_BYTES   128
#define TUNED_PF_CLASS   0
#define TUNED_SMALL_AVX  0
#define TUNED_PF_MIN     65536
#define TUNED_SMALL_PATH 1
#include "memcpy_avx2_tuned_impl.h"
