/* src/memcpy_avx2_mid_gcc.c — Phase 3 P1: the dedicated 1 KiB..64 KiB kernel.
 *
 * Knob set against memcpy_avx2_final_v2_gcc_noxj (the Phase 2.6 deliverable):
 *   * a main loop of 256 B per iteration instead of 128 B (unroll 8, P1),
 *   * the prefetch stream ON for the whole range instead of gated at 64 KB,
 *   * two source streams, one and two blocks ahead (P2),
 *   * a head peel that puts every store on a 64 B boundary (P4's mechanism),
 *   * the below-1 KiB path unchanged, so this rung cannot disturb 16-512 B.
 * Built by gcc with the deliverable's -fno-crossjumping, so the comparison is
 * one knob at a time and not a compiler change.
 */
#define MID_FN      memcpy_avx2_mid
#define MID_UNROLL  8
#define MID_PF1     256          /* one block ahead  (+4 lines) */
#define MID_PF2     512          /* two blocks ahead (+8 lines) */
#define MID_PEEL    1
#define MID_NTPF    0
#include "memcpy_avx2_mid_impl.h"
