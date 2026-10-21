/* SPDX-License-Identifier: MIT */
/* src/rungs/f4win64_aligned.c — Phase 7.2: the H1b shape WITH the branch-target
 * alignment restored.
 *
 * WHY THIS RUNG EXISTS
 *   Phase 7 measured the 128 B cause and rejected the first attempt:
 *
 *     - The 128 B loss is a block-shape defect. The class ladder's rule is
 *       that a class window is (H, 2H] and the block pair uses H = the class's
 *       LOWER bound, so the two blocks tile [0, n) exactly at n = 2H. The F4
 *       window covers 128 <= n <= 256 with T_BLOCKPAIR(4) (H = 128) and is
 *       tested BEFORE the flat class chain, so at n = 128 the window's head
 *       and tail blocks are the same four 32 B blocks: 8 loads + 8 stores for
 *       a 128 B copy where glibc issues 4 + 4. The signature is in the
 *       shipped numbers - this kernel's 128 B copy (3.4238 ns) costs MORE than
 *       its own 256 B copy (3.4023 ns) on all three targets.
 *     - src/rungs/avx2_f4win64.c (H1b) gave n == 128 the H = 64 pair and
 *       gained +18.31% at 128 B, identically on znver2, znver3 and v3. The
 *       hypothesis is CONFIRMED.
 *     - H1b was nevertheless REJECTED, because 32 B and 64 B regressed by
 *       -16.3% to -16.9% and 512 B by -2.7%. The regression was NOT the
 *       algorithm: the 16..64 B code was byte-identical to the shipped
 *       kernel's, at the same offsets 0x0..0x34. What changed was the
 *       ALIGNMENT of the out-of-line 32..64 B branch target: 0x1c0 in the
 *       shipped kernel (64-byte aligned) and 0x1a0 in H1b (16-byte aligned
 *       only), while the n <= 15 path took the 64-byte-aligned 0x180
 *       instead. A 16% swing on a 2 ns copy, from alignment alone.
 *
 *   So the shape is right and the layout is wrong. This rung keeps H1b's
 *   shape and forces the alignment.
 *
 * ALIGNMENT STRATEGY, ATTEMPT 1
 *   The rung's translation unit is compiled with -falign-labels=64, so every
 *   label gcc emits is 64-byte aligned and therefore every branch target the
 *   tiny path can take is on a 64-byte boundary. The flag is per-file: it is
 *   added to this rung's compile rule only, and src/fast.c keeps its own
 *   flags, so the shipped deliverable's object is unaffected.
 *   P1 below checks the result against the built object rather than trusting
 *   the flag.
 *
 * PRE-REGISTERED CRITERIA
 *   Fixed before any measurement was run. Committed as the pre-registration;
 *   the evidence for P1 and P2 is pasted in below after the build, before the
 *   measurement.
 *
 *   P1. ALIGNMENT PRECONDITION. objdump -d on the built rung shows every
 *       branch target used at n <= 64 is at a 64-byte boundary
 *       (offset & 0x3f == 0). Paste the offsets in the header. If any target
 *       fails, REJECT without measuring - alignment is a precondition, not a
 *       variable.
 *
 *   P2. INSTRUCTION IDENTITY. The 16-64 B code path is instruction-identical
 *       to the shipped kernel. Verify by objdump -d comparison - same
 *       mnemonics, same operands, same logical layout. Only byte offsets may
 *       differ. Paste the diff in the header (should be empty for
 *       instructions, only offsets).
 *
 *   A1. 128 B delta vs F4 >= -2% (both min and median).
 *
 *   A2. 16, 32, 64, 256, 512 B, 1 MiB all within null band vs F4 (measured in
 *       the same windows). No regression on certified rows.
 *
 *   A3. F4 .text md5 0b2d0837e25a53b2570ef33a6606edfa unchanged (src/fast.c
 *       not edited until promote criteria pass).
 *
 *   S1. If P1 fails after one alignment attempt -> REJECT, do not measure.
 *       Document the alignment gap.
 *
 *   S2. If A1 fails on attempt 1 -> one retry allowed with a different
 *       alignment strategy. If attempt 2 fails -> REJECT. 128 B becomes
 *       permanent loss.
 *
 *   S3. If A2 fails on either attempt -> REJECT. Do not trade certified rows
 *       for 128 B.
 *
 *   NOTE ON A2's "WITHIN THE NULL BAND": the band is the measured difference
 *   between naive and ref_scalar - two byte-identical instruction streams -
 *   in the SAME windows as this comparison. It is the instrument's own noise
 *   floor, and a size passes only if the delta against F4 is inside it, not
 *   because a tier floor happened to be wide.
 *
 * P1 EVIDENCE (filled in after the build, before any measurement)
 *
 *   PENDING - see the build step.
 *
 * P2 EVIDENCE (filled in after the build, before any measurement)
 *
 *   PENDING - see the build step.
 */
#define TUNED_FN         memcpy_f4win64_aligned
#define TUNED_UNROLL     4
#define TUNED_ALIGNED    1
#define TUNED_PF_BYTES   128
#define TUNED_PF_CLASS   0
#define TUNED_SMALL_AVX  0
#define TUNED_PF_MIN     65536
#define TUNED_SMALL_PATH 1
#define TUNED_WINDOW     1
#define TUNED_WIN_LO     128
#define TUNED_WIN64      1
#include "fast_impl.h"
