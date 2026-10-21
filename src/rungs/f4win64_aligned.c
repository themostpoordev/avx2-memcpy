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
 *       shipped kernel (64-byte aligned) and 0x1a0 in H1b, which is 32-byte
 *       aligned but NOT 64-byte aligned (0x1a0 = 416 = 6*64 + 32), while the
 *       n <= 15 path took the 64-byte-aligned 0x180 instead. A 16% swing on a
 *       2 ns copy, from alignment alone.
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
 *
 *   Two properties of the flag that P1 checks rather than assumes:
 *     - the padding it inserts is never EXECUTED here; every pad sits after a
 *       `ret` or `jmp`, so no nop is added to a hot path;
 *     - the ABSOLUTE alignment of the function is a link-time property. The
 *       .p2align that -falign-labels emits raises the object's .text section
 *       alignment to 64 and the linker honours it, and this rung is the only
 *       symbol in its section, so the function lands 64-byte aligned. P1 is
 *       therefore checked on the LINKED BINARY, not on the object: an
 *       object-level offset check alone would not prove the hardware
 *       alignment, because the function's absolute address comes from the
 *       section's alignment.
 *
 * PRE-REGISTERED CRITERIA
 *   Fixed before any measurement was run. The evidence for P1 and P2 is
 *   pasted in below, after the build and before the measurement.
 *
 *   P1. ALIGNMENT PRECONDITION. For each target t in {znver2, znver3, v3} and
 *       for the LINKED binary bench/bench_<t>: every direct branch target of
 *       memcpy_f4win64_aligned reachable when n <= 64 has an absolute address
 *       A with (A & 0x3f) == 0. Each of those addresses is the first
 *       instruction of a basic block, which is what makes -falign-labels=64
 *       sufficient; no branch lands inside a block. Object-level offsets are
 *       supporting evidence only. If any target fails, REJECT WITHOUT
 *       MEASURING - alignment is a precondition, not a variable.
 *
 *   P2. INSTRUCTION IDENTITY. The 16..64 B path - the entry block through its
 *       first `ret`, plus the 32..64 B block the entry's `cmp $0x1f` branch
 *       targets - of memcpy_f4win64_aligned is instruction-identical: same
 *       mnemonics, same operands, same order - to the same region of
 *       memcpy_avx2_f4win64 (src/rungs/avx2_f4win64.c), which is the same
 *       source with the same knob and WITHOUT -falign-labels=64. That rung
 *       is the CONTROL for this flag. The shipped kernel is reported
 *       alongside but is NOT a valid control, because the TUNED_WIN64 knob
 *       itself changes the v3 build's 16..64 B path (shipped v3: 20
 *       instructions, no `lea`, dst in %rdi; the knob builds: 21
 *       instructions, with `lea`, dst copy in %rax). The shipped comparison
 *       is identical on znver2 and znver3 and different on v3, and that
 *       difference is the knob's, not the flag's.
 *       Mechanical form: objdump -d --no-show-raw-insn, padding nops dropped
 *       (nop/nopl/nopw/nopq/data16+xchg %ax,%ax), branch targets masked to
 *       a placeholder, sequences compared; the branch target offsets are then
 *       listed side by side, where they are EXPECTED to differ.
 *
 *   A1. 128 B delta vs F4 >= -2% (both min and median). The min delta is
 *       analyze.sh's delta_vs_prev_pct. The median is computed from the raw
 *       log's 45 per-rep mins, with the same median_of_mins definition
 *       scripts/f128b_summary.py uses, because analyze.sh's CSV carries no
 *       per-size median.
 *
 *   A2. For each of 16, 32, 64, 256, 512 B and 1 MiB, on every target: the
 *       delta against F4 must satisfy |delta| <= max(1.5%, null_band_pct),
 *       where null_band_pct is the naive-vs-ref_scalar difference measured in
 *       the SAME windows as this comparison; AND the tier verdict against F4
 *       must not be LOSS. No certified row may be worse than -5% against F4,
 *       whatever the band.
 *       The max(1.5%, band) floor is the protocol's own rule-3 floor applied
 *       to the band, and it is stated here BEFORE the campaign because the
 *       band computed from two byte-identical kernels' floors can be 0.00%,
 *       which is a sampling artefact of the min statistic rather than a zero
 *       noise floor; the tier verdict is reported alongside for every size.
 *
 *   A3. The .text md5 of build/<t>/src/fast.o is unchanged per target:
 *       znver2 and znver3 0b2d0837e25a53b2570ef33a6606edfa, v3
 *       71c9cefef2d67698267abc118fe55e1a. Extract with
 *       `objcopy -O binary --only-section=.text build/<t>/src/fast.o out.bin;
 *       md5sum out.bin` - always WITH an output argument, or objcopy
 *       overwrites the input object. src/fast.c is not edited until the
 *       promote criteria pass.
 *
 *   S1.  If P1 fails after one alignment attempt -> REJECT, do not measure.
 *        Document the alignment gap.
 *
 *   S1b. If P2 fails on any target -> REJECT without measuring.
 *
 *   S2.  If A1 fails on attempt 1 -> one retry allowed with a different
 *        alignment strategy. If attempt 2 fails -> REJECT. 128 B becomes
 *        permanent loss.
 *
 *   S3.  If A2 fails on either attempt -> REJECT. Do not trade certified rows
 *        for 128 B.
 *
 * AMENDMENTS AFTER CODE REVIEW (all declared BEFORE the measurement)
 *   A code review of this file was run before any measurement, and the
 *   criteria above are amended as follows, for the reasons given:
 *     - P1 is checked on the linked binary, on absolute addresses, with the
 *       block-start requirement stated, and the "built rung" ambiguity
 *       resolved to "for each of the three targets".
 *     - P2's control is the H1b rung, not the shipped kernel, because the
 *       knob itself changes the v3 build's 16..64 B path. The shipped
 *       comparison is reported separately, and the v3 caveat is recorded.
 *     - A1's median is defined as computed from the raw log, because
 *       analyze.sh's CSV has no per-size median (its "median" is a median
 *       over the sizes in a class).
 *     - A2's band rule is floored at the protocol's own 1.5%, because a band
 *       computed as 0.00% is a sampling artefact rather than a zero noise
 *       floor; the tier verdict is reported for every size regardless.
 *     - A3's md5 is stated per target, because the v3 build is different
 *       code (-march=x86-64-v3) and has its own md5.
 *     - S1b is added: a P2 failure is a rejection, like a P1 failure.
 *     - The alignment arithmetic is corrected: 0x1c0 is 64-byte aligned and
 *       0x1a0 is 32-byte aligned (not 64). The earlier Phase 7 write-up
 *       said "only 16-byte aligned", which was imprecise.
 *     - The v3 caveat is added to the premise: the claim that H1b's 16..64 B
 *       code was byte-identical to the SHIPPED kernel's holds on znver2 and
 *       znver3, and does NOT hold on v3, where the knob's own codegen
 *       differs. The v3 H1b regression is therefore confounded between the
 *       knob and the alignment, and is reported as such.
 *     - The n <= 15 class is excluded from the 16..64 B region (it is a
 *       different size class) but its layout also moves, so 8 B is reported
 *       as a non-gate row.
 *
 * WHAT PROMOTION MEANS
 *   Promotion is NOT a copy of this file. It is: add `#define TUNED_WIN64 1`
 *   to src/fast.c, and add -falign-labels=64 to GCC_EXTRA_fast.c in the
 *   Makefile, so the deliverable is built with the same shape and the same
 *   alignment as this rung. The result is a DIFFERENT artefact from the
 *   shipped one - a larger .text - and it therefore needs its own
 *   certification campaign, committed under docs/data/raw/, with the
 *   RESULTS.md headline rewritten against it (METHODOLOGY.md §11 rule 4). A3
 *   forbids changing fast.c during the experiment, so this happens only
 *   after the criteria pass.
 *
 * P1 EVIDENCE (from scripts/phase7_2_layout.py, on the LINKED binaries)
 *
 *   znver2  entry=0x405a40
 *           branch targets reachable at n <= 64:
 *             0x405a80 0x405bc0 0x405c00 0x405d80 0x405e80 0x4060c0 0x406100
 *           all 64-byte aligned: YES  -> PASS
 *   znver3  entry=0x406000
 *           branch targets reachable at n <= 64:
 *             0x406040 0x406180 0x4061c0 0x406340 0x406440 0x406680 0x4066c0
 *           all 64-byte aligned: YES  -> PASS
 *   v3      entry=0x405bc0
 *           branch targets reachable at n <= 64:
 *             0x405c00 0x405d40 0x405d80 0x405f00 0x406000 0x406240 0x406280
 *           all 64-byte aligned: YES  -> PASS
 *
 *   P1 PASS on all three targets.
 *
 * P2 EVIDENCE (from scripts/phase7_2_layout.py, same binaries)
 *
 *   znver2  vs CONTROL (memcpy_avx2_f4win64): IDENTICAL  (21 instructions)
 *           vs SHIPPED (memcpy_fast):         IDENTICAL  (control vs shipped: IDENTICAL)
 *   znver3  vs CONTROL (memcpy_avx2_f4win64): IDENTICAL  (21 instructions)
 *           vs SHIPPED (memcpy_fast):         IDENTICAL  (control vs shipped: IDENTICAL)
 *   v3      vs CONTROL (memcpy_avx2_f4win64): IDENTICAL  (21 instructions)
 *           vs SHIPPED (memcpy_fast):         DIFFERS
 *                   (the v3 knob effect: 20 instructions, no `lea`, dst in
 *                    %rdi in the shipped kernel; 21 instructions with `lea`
 *                    and dst copy in %rax in both knob builds - present in the
 *                    H1b rung too, so it is the knob's and not the flag's)
 *
 *   The instruction-level diff of the 16..64 B region against the CONTROL is
 *   EMPTY on all three targets - same mnemonics, same operands, same order.
 *   Only the branch target offsets differ, which is the point of the flag.
 *
 *   P2 PASS on all three targets.
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
