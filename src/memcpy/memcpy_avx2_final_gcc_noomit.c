/* src/memcpy_avx2_final_gcc_noomit.c — V6: -fomit-frame-pointer control.
 *
 * The directive lists -fomit-frame-pointer as a flag to report a gain for, but
 * it is ALREADY in the Makefile's OPT for every rung (-O3 -fomit-frame-pointer,
 * and gcc -O3 implies it besides), so there is nothing to add: the fused kernel
 * measured against glibc already has it on. What can be measured is the other
 * direction - what it is worth - so this rung is the fused kernel with the flag
 * forced OFF (GCC_EXTRA_..._noomit.c := -fno-omit-frame-pointer in the Makefile).
 * If this rung is slower by X%, the gain from having the flag on is X%; if the
 * two are inside the noise band, the flag is worth nothing at these sizes, which
 * is the honest answer and is reported as such.
 */
#define TUNED_FN        memcpy_avx2_final_noomit
#define TUNED_UNROLL    4
#define TUNED_ALIGNED   1
#define TUNED_PF_BYTES  128
#define TUNED_PF_CLASS  0
#define TUNED_SMALL_AVX 1
#define TUNED_PF_MIN    65536
#include "memcpy_avx2_tuned_impl.h"
