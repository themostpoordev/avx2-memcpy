/* src/memcpy_avx2_mid_asm.S — Phase 3 P5: the winning mid-range loop in asm.
 *
 * The candidate it twins is memcpy_avx2_mid_pm (src/memcpy_avx2_mid_pm_gcc.c):
 * head peel to a 64 B dst boundary, then 256 B per iteration as eight 32 B moves
 * with three prefetch streams, then one overlapping 256 B block as the tail.
 * Same shape, same distances, same order - so a measured difference between the
 * two is the compiler and nothing else, which is the only way this comparison
 * means anything. P5's bar is >2% for adopting the asm; below that the C stays,
 * because a .S file has to earn its maintenance cost.
 *
 * WHY THE ASM HAS TO BE CHECKED RATHER THAN ARGUED
 *   Kernel 6 (the ladder's asm rung) already showed that clang/gcc and a
 *   hand-written loop match on this core. The risk in this file is not speed but
 *   correctness: a register clobber or a missing bound is silent until --verify
 *   runs, and the harness walks n = 0..4096 at random offsets with canaries on
 *   both sides, so anything wrong here fails the gate before a timing row.
 *
 * CALLING CONVENTION
 *   void *memcpy_avx2_mid_asm(void *dst, const void *src, size_t n)
 *     %rdi dst, %rsi src, %rdx n, returns dst in %rax
 *   %rax dst (return, written once at entry and never again)
 *   %rdi/%rsi  cursors (advanced past the peeled head)
 *   %rdx       remaining bytes after the peel (m = n - r)
 *   %r8        peel count r        %r9  loop offset j        %r10/%r11 scratch
 *   %ymm0-%ymm7 the eight 32 B lanes   %xmm0-%xmm1 the 16 B peel pair
 *   All caller-saved: no frame, no spill, no callee-saved register, so this is a
 *   leaf except for the one tail call below.
 *
 * BELOW 1 KiB IT IS NOT THIS KERNEL
 *   n < 1024 jumps to the C deliverable's entry point and never comes back (a tail
 *   call, rdi/rsi/rdx still intact). The rung's sub-1 KiB rows are therefore the
 *   C kernel's numbers plus one jump, not an asm implementation, and the report
 *   says so rather than quoting them as if this file produced them.
 *
 * PREFETCH DISTANCES
 *   +256 for both streams and +512 for the source, i.e. one and two 256 B blocks
 *   ahead: the distances memcpy_avx2_mid_pm measured as the winner over one stream
 *   (pf1) and over the literal +128/+256 pair (pfnear). Prefetching past the end of
 *   the mapping is legal - the hint never faults - so no bounds test is needed.
 *
 * PATCH: the .Lpeelbyte loop (r < 8) is at most seven byte pairs and only runs for
 * unaligned callers; the harness's buffers are 64 B aligned, so r is normally 0
 * and the peel collapses to one test. It is written as a loop rather than an
 * unrolled cascade because it must be correct for every r in 0..7, which is what
 * --verify's random offsets exercise.
 */
	.text
	.globl	memcpy_avx2_mid_asm
	.type	memcpy_avx2_mid_asm,@function
	.p2align 4
memcpy_avx2_mid_asm:
	movq	%rdi, %rax		/* return value = original dst */
	cmpq	$1024, %rdx
	jae	.Lmid
	jmp	memcpy_avx2_final_v2_gcc_noxj	/* below the mid range: the C deliverable */

	.p2align 4
.Lmid:
	movq	%rdi, %r8
	negq	%r8
	andq	$63, %r8		/* r = (-dst) & 63, in 0..63 */
	jz	.Lpeeled
	cmpq	$32, %r8
	jb	.Lpeel16
	vmovdqu	(%rsi), %ymm0
	vmovdqu	-32(%rsi,%r8), %ymm1
	vmovdqu	%ymm0, (%rdi)
	vmovdqu	%ymm1, -32(%rdi,%r8)
	jmp	.Lpeeled
.Lpeel16:
	cmpq	$16, %r8
	jb	.Lpeel8
	vmovdqu	(%rsi), %xmm0
	vmovdqu	-16(%rsi,%r8), %xmm1
	vmovdqu	%xmm0, (%rdi)
	vmovdqu	%xmm1, -16(%rdi,%r8)
	jmp	.Lpeeled
.Lpeel8:
	cmpq	$8, %r8
	jb	.Lpeelbyte
	movq	(%rsi), %r10
	movq	-8(%rsi,%r8), %r11
	movq	%r10, (%rdi)
	movq	%r11, -8(%rdi,%r8)
	jmp	.Lpeeled
.Lpeelbyte:
	xorl	%r10d, %r10d
1:	movzbl	(%rsi,%r10), %r11d
	movb	%r11b, (%rdi,%r10)
	incq	%r10
	cmpq	%r8, %r10
	jb	1b

.Lpeeled:
	addq	%r8, %rdi		/* dst cursor now 64 B aligned */
	addq	%r8, %rsi
	subq	%r8, %rdx		/* m = n - r >= 961 for every n >= 1024 */
	xorl	%r9d, %r9d		/* j = 0 */

	.p2align 4
.Lloop:
	leaq	256(%r9), %r10
	cmpq	%rdx, %r10
	ja	.Ltail			/* while j + 256 <= m */
	prefetcht0 256(%rsi,%r9)
	prefetcht0 256(%rdi,%r9)
	prefetcht0 512(%rsi,%r9)
	vmovdqu	(%rsi,%r9), %ymm0
	vmovdqu	32(%rsi,%r9), %ymm1
	vmovdqu	64(%rsi,%r9), %ymm2
	vmovdqu	96(%rsi,%r9), %ymm3
	vmovdqu	128(%rsi,%r9), %ymm4
	vmovdqu	160(%rsi,%r9), %ymm5
	vmovdqu	192(%rsi,%r9), %ymm6
	vmovdqu	224(%rsi,%r9), %ymm7
	vmovdqu	%ymm0, (%rdi,%r9)
	vmovdqu	%ymm1, 32(%rdi,%r9)
	vmovdqu	%ymm2, 64(%rdi,%r9)
	vmovdqu	%ymm3, 96(%rdi,%r9)
	vmovdqu	%ymm4, 128(%rdi,%r9)
	vmovdqu	%ymm5, 160(%rdi,%r9)
	vmovdqu	%ymm6, 192(%rdi,%r9)
	vmovdqu	%ymm7, 224(%rdi,%r9)
	movq	%r10, %r9
	jmp	.Lloop

.Ltail:
	cmpq	%rdx, %r9
	jae	.Ldone			/* exact multiple of 256: nothing left */
	leaq	-256(%rdx), %r10	/* one overlapping block: [m-256, m) */
	vmovdqu	(%rsi,%r10), %ymm0
	vmovdqu	32(%rsi,%r10), %ymm1
	vmovdqu	64(%rsi,%r10), %ymm2
	vmovdqu	96(%rsi,%r10), %ymm3
	vmovdqu	128(%rsi,%r10), %ymm4
	vmovdqu	160(%rsi,%r10), %ymm5
	vmovdqu	192(%rsi,%r10), %ymm6
	vmovdqu	224(%rsi,%r10), %ymm7
	vmovdqu	%ymm0, (%rdi,%r10)
	vmovdqu	%ymm1, 32(%rdi,%r10)
	vmovdqu	%ymm2, 64(%rdi,%r10)
	vmovdqu	%ymm3, 96(%rdi,%r10)
	vmovdqu	%ymm4, 128(%rdi,%r10)
	vmovdqu	%ymm5, 160(%rdi,%r10)
	vmovdqu	%ymm6, 192(%rdi,%r10)
	vmovdqu	%ymm7, 224(%rdi,%r10)

.Ldone:
	vzeroupper
	ret
	.size	memcpy_avx2_mid_asm,.-memcpy_avx2_mid_asm
	.section .note.GNU-stack,"",@progbits
