/* SPDX-License-Identifier: MIT */
/* src/reference/fast_v9c.c — REFERENCE KERNEL, not the deliverable.
 *
 * V9c (avx2_final_v2_gcc_noxj): the V9b small-path fix plus -fno-crossjumping.
 * It was the shipped kernel until F4 replaced it; it builds `memcpy_fast_v9c`
 * and is never linked into the benchmark binaries. `make reference` builds it
 * so its measured numbers can be reproduced and compared.
 *
 * One knob against avx2_final_v2_gcc: the compiler flag. Same source, same
 * TUNED_* set; the Makefile adds -fno-crossjumping for this file.
 *
 * Why. The v9b campaign fixed the tiny class (16 B certified +38% over glibc,
 * 32 and 64 B level with it) but cost one timer step at 128 B and about 0.6 ns
 * at 256 B, where the fused kernel had been ~0.3 ns AHEAD of glibc. The object
 * says where that came from: gcc cross-jumped the head of T_BLOCKPAIR(2)
 * (64..128 B) with the head of T_BLOCKPAIR(4) (256..512 B), so at n = 256 the
 * function runs two 32 B moves, a compare, two more moves and a jmp instead of
 * one straight-line block pair (bench/bench_znver2, memcpy_avx2_final_v2_gcc
 * +0x1c0 -> +0x2f0 -> +0x27b). That is a code-layout consequence of the chain,
 * not of the copies, and -fno-crossjumping is the smallest knob that forbids it.
 *
 * If it restores 256 B without moving the tiny classes, the small-path fix costs
 * nothing and is adopted. If it does not, the honest trade stands as measured:
 * three sizes fixed, two moved by at most one timer step.
 */
#define TUNED_FN         memcpy_fast_v9c
#define TUNED_UNROLL     4
#define TUNED_ALIGNED    1
#define TUNED_PF_BYTES   128
#define TUNED_PF_CLASS   0
#define TUNED_SMALL_AVX  0
#define TUNED_PF_MIN     65536
#define TUNED_SMALL_PATH 1
#include "fast_impl.h"
