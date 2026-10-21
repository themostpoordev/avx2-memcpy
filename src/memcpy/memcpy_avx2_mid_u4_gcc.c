/* src/memcpy_avx2_mid_u4_gcc.c — Phase 3 P1 control: the same mid kernel at
 * unroll 4 (128 B per iteration), prefetch distances scaled to its block size so
 * the two streams keep their meaning (one and two blocks ahead).
 *
 * This is the rung that makes `mid` interpretable: its delta IS the value of
 * 256 B per iteration on this core. Everything else about the two files is
 * identical (peel, stream count, gcc flags). */
#define MID_FN      memcpy_avx2_mid_u4
#define MID_UNROLL  4
#define MID_PF1     128
#define MID_PF2     256
#define MID_PEEL    1
#define MID_NTPF    0
#include "memcpy_avx2_mid_impl.h"
