/* src/memcpy_avx2_final_gcc.c — V9 fused kernel, built by gcc (V1 adopted).
 *
 * Byte-identical knob set to src/memcpy_avx2_final.c: the ONLY difference between
 * the two objects is the compiler, so the pair measures V1's verdict on the final
 * kernel rather than on Kernel 4. See memcpy_avx2_final.c for what each knob is
 * and which measurement chose it.
 */
#define TUNED_FN        memcpy_avx2_final_gcc
#define TUNED_UNROLL    4
#define TUNED_ALIGNED   1
#define TUNED_PF_BYTES  128
#define TUNED_PF_CLASS  0
#define TUNED_SMALL_AVX 1
#define TUNED_PF_MIN    65536
#include "memcpy_avx2_tuned_impl.h"
