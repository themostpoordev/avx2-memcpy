/* SPDX-License-Identifier: MIT */
/* src/rungs/avx2_f4win64.c — Phase 7 rung H1b (PRIMARY CANDIDATE).
 *
 * The shipped F4 shape with ONE knob changed: inside the 128..256 B window,
 * n == 128 gets the H = 64 block pair instead of the H = 128 pair.
 *
 * WHY THIS RUNG EXISTS
 *   The shipped kernel is a certified LOSS at 128 B: -10.04% against glibc at
 *   both the floor and the median, on all three targets. The cause is
 *   structural, and the evidence for it is in the shipped numbers:
 *
 *     - The class ladder's rule (src/fast_impl.h) is that a class window is
 *       (H, 2H] and the block pair uses H = the class's LOWER bound, so the
 *       two blocks tile [0, n) exactly at n = 2H. For the (64, 128] class that
 *       is H = 64, and it tiles n = 128 exactly.
 *     - The F4 window (TUNED_WINDOW) covers 128 <= n <= 256 with
 *       T_BLOCKPAIR(4), i.e. H = 128, and it is tested BEFORE the flat class
 *       chain. So at n = 128 the window's head block and tail block are the
 *       same four 32 B blocks: the kernel issues 8 loads and 8 stores for a
 *       128 B copy, where glibc issues 4 loads and 4 stores.
 *     - The signature: this kernel's 128 B copy (3.4238 ns) costs MORE than
 *       its own 256 B copy (3.4023 ns) on all three targets, while glibc's
 *       128 B copy (3.1113 ns) is 16% CHEAPER than its 256 B copy
 *       (3.7148 ns). Half the bytes, the same work.
 *
 *   This rung keeps the window's single-compare dispatch and gives n == 128
 *   the H = 64 pair, which tiles that size exactly: [0,64) + [n-64,n) =
 *   [0,128), four loads and four stores, no overlap, one extra compare.
 *
 *   Nothing else changes. The knobs are otherwise identical to the shipped
 *   kernel's, so 16/32/64 B, 256 B, 512 B and 1 MiB compile to the same
 *   instructions; the regression gate below is what proves it.
 *
 * ACCEPTANCE TEST
 *   Fixed before any measurement was run, and recorded here so that it cannot
 *   be adjusted afterwards. The rung is measured against the shipped F4 in
 *   the SAME windows, in the SAME process, on the SAME host state. All three
 *   conditions must hold:
 *
 *   A1  128 B: delta vs F4 >= -2%.
 *       RELATIVE, not absolute. The comparison is between two kernels measured
 *       in the same interleaved windows, so the host's own drift cancels
 *       between them; an absolute nanosecond threshold is not usable, because
 *       this host's floor moves with load (METHODOLOGY.md §9). The -2% band is
 *       the same 1.5% rule-3 floor plus a little slack, so a result inside it
 *       is a TIE, not a win: this rung's job is to remove a certified loss, not
 *       to win a size.
 *
 *   A2  REGRESSION GATE. 16 B, 32 B, 64 B, 256 B, 512 B and 1 MiB must ALL
 *       stay within the null band against F4. If ANY ONE of them regresses
 *       outside the band, the rung is REJECTED regardless of what 128 B did.
 *       These are the certified rows; they are not sacrificed for one size.
 *
 *   A3  CORRECTNESS. `make gate` passes, and `--verify` passes for this rung
 *       on all three targets: byte-for-byte against the golden reference for
 *       n = 0..4096 with randomised source/destination offsets and 64 B
 *       canaries on both sides.
 *
 * STOP CONDITION
 *   If A1 or A2 fails, the hypothesis is REJECTED. This rung does NOT go into
 *   src/fast.c; the shipped kernel keeps its 128..256 B window unchanged; and
 *   the result is written up in docs/NEGATIVE_RESULTS.md as a measured loss
 *   with its mechanism. The criteria above stay in this file as the record of
 *   what was tested and what the answer was.
 *
 *   If A1, A2 and A3 all hold, the rung may be PROMOTED to the deliverable —
 *   but only after a fresh certification campaign against the promoted kernel
 *   is committed under docs/data/raw/ and the RESULTS.md headline is rewritten
 *   against that new reference (METHODOLOGY.md §11 rule 4). The current
 *   reference campaign certifies the CURRENT kernel, not this one.
 *
 * NOT A PROMOTION CANDIDATE
 *   src/rungs/avx2_f4win129.c is the H1a CONTROL for this rung: it moves the
 *   window's lower edge to 129, so n = 128 falls through to the flat class
 *   chain and reaches the same H = 64 shape through four extra compares. It
 *   exists to price the compares, exactly as the V9c history predicts it
 *   should (V9c, which had no window at all, was -20.1% at 128 B). It is a
 *   measurement and never a candidate: even if it wins, it is fragile, and
 *   it must not be promoted.
 *
 * RESULT (measured 2026-10-21; the criteria above are UNCHANGED from commit
 * 82c330d, which was made BEFORE any measurement ran)
 *
 *   A1  PASS on all three targets. 128 B: +18.31% against F4, identical on
 *       znver2, znver3 and v3 (2.8 ns against F4's 3.4 ns). The shape
 *       hypothesis is CONFIRMED: giving n = 128 the H = 64 pair is worth
 *       about 18% at that size, and the certified -10.04% loss was caused by
 *       the H = 128 pair's coincident head and tail blocks.
 *
 *   A2  FAIL on all three targets. 32 B and 64 B regress by -16.26% to
 *       -16.87%, and 512 B by -2.70% (znver2, znver3); each is a certified
 *       LOSS against F4 under the two-tier model.
 *
 *   A3  PASS. `make gate` passes, and `--verify` passes for this rung on all
 *       three targets.
 *
 *   The regression is NOT the knob. On znver2 and znver3 the 16..64 B code is
 *   byte-identical to the shipped kernel's - the same instructions at the same
 *   offsets 0x0..0x34 - so the knob cannot be its cause there. (On v3 the
 *   knob's own codegen differs - 20 instructions with no `lea` in the shipped
 *   kernel against 21 with `lea` here - so the v3 regression is confounded
 *   between the knob and the alignment, and is reported as such.) What
 *   changed is the ALIGNMENT of the out-of-line 32..64 B branch target: in the
 *   shipped kernel that target sits at 0x1c0, which is 64-byte aligned; in
 *   this rung it moved to 0x1a0, which is 32-byte aligned but NOT 64-byte
 *   aligned (0x1a0 = 416 = 6*64 + 32), while the n <= 15 path took the
 *   64-byte-aligned 0x180 instead. This is the layout sensitivity the F2/F3
 *   work already documented ("moving the alignment decision around cost more
 *   than it saved"), and it costs 16% on a 2 ns copy.
 *
 * CONSEQUENCE: this rung is REJECTED and is NOT promoted. The shipped
 *   kernel keeps its 128..256 B window unchanged, and its certified 128 B
 *   loss stands. The result is written up in docs/NEGATIVE_RESULTS.md; the
 *   campaign logs are committed under docs/data/raw/ and the acceptance
 *   summary is docs/data/phase7_f4win_acceptance_summary.csv.
 *
 *   The next experiment, if there is one, is a SEPARATE one with its own
 *   criteria fixed in advance: keep the H = 64 semantics at n = 128 and
 *   restore the 64-byte alignment of the 32..64 B branch target, then re-run
 *   this same gate. That is not attempted here.
 */
#define TUNED_FN         memcpy_avx2_f4win64
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
