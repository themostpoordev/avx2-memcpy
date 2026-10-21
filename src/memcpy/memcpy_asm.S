/* src/memcpy_asm.S — KERNEL 6 of the ladder: hand-written AVX2 assembly.
 *
 * Tooling: clang's integrated assembler, GAS/AT&T syntax. This is a .S file, so
 * it IS preprocessed: every comment below is a C comment on purpose. `#` is a
 * line comment to GAS but a preprocessor directive to cpp, which is the classic
 * way a .S file stops assembling for a reason that has nothing to do with the
 * code.
 *
 * CALLING CONVENTION (System V AMD64, the ABI the harness calls through)
 *   void *memcpy_asm(void *dst, const void *src, size_t n)
 *     %rdi = dst, %rsi = src, %rdx = n
 *     returns dst in %rax
 *   Contract: src and dst do NOT overlap (this is a copy, not a move), exactly
 *   as rungs 1-5. No stack frame is created, no stack slot is read or written,
 *   and no callee-saved register (rbx, rbp, r12-r15) is touched, so there is
 *   nothing to save, nothing to restore, and no red-zone claim. Every register
 *   used below is caller-saved, which is why this function can be a leaf.
 *   (x86-64 SysV: ymm0-ymm15 are all caller-saved/volatile, so the vector
 *   registers need no preservation either. Only the upper halves have to be
 *   zeroed before returning to non-AVX code — see vzeroupper at the epilogue.)
 *
 * SHAPE — taken from Kernel 4's C hot loop, not invented
 *   Kernel 4's `memcpy_avx2` compiles (build/<target>/src/memcpy_avx2.o) to:
 *   a 4x-unrolled loop of four 32 B `vmovups` loads + four 32 B `vmovups`
 *   stores per 128 B, a 32 B remainder loop, an overlapping-pair tail, and a
 *   < 64 B path. This rung keeps that structure deliberately, so the comparison
 *   is asm-vs-compiler on the SAME ALGORITHM and not asm-with-a-clevera-idea
 *   vs C-with-a-worse-one. What actually differs is listed under "Differences"
 *   below, and each difference is named there so the report can attribute a
 *   delta to a mechanism instead of guessing.
 *
 * REGISTER ALLOCATION (and why it is this small)
 *   %rax  return value (the original dst), written once at entry
 *   %rdi  dst cursor          %rsi  src cursor          %rdx  n
 *   %r8   dst + n, the end of the destination
 *   %r9   loop bound (dst+n-128 for the big loop, dst+n-32 for the small one)
 *   %rcx  tail byte count r = end - dst, in [0,32)
 *   %r10  scratch for the tail's second word
 *   ymm0-ymm3   the four 32 B lanes of one 128 B block
 *   xmm0-1      the tail's 16 B overlapping pair
 *   That is 8 GPRs and 4 vector registers, all caller-saved: no spill, no
 *   frame, and the four ymm registers exactly cover the four lanes in flight,
 *   which is what lets the four loads be independent. A fifth lane (5x unroll)
 *   was rejected: the store port is the bottleneck (below), so a 5th lane buys
 *   nothing and adds a register pressure risk on a compiler that would have to
 *   spill. Four is the largest unroll that stays inside the volatile set.
 *
 * SCHEDULING
 *   Per 128 B iteration: prefetch, then the four LOADS, then the four STORES.
 *   Grouping the loads first keeps both load ports (Zen2: 2x32 B/cycle) busy
 *   while the store queue drains, and issuing the stores in address order lets
 *   the store buffer coalesce them. Zen2 retires only 1x32 B store per cycle,
 *   so 4 stores need at least 4 cycles per 128 B = 32 B/cycle: the store port,
 *   not the loads and not the prefetch, sets this loop's ceiling. That is why
 *   the unroll is 4 (covers exactly one 256 B iteration quantum of two cache
 *   lines) and not larger, and why there is no software pipelining of loads
 *   into the next iteration: the loads already complete two cycles before the
 *   stores need them.
 *
 * PREFETCH DISTANCE — inherited, not re-tuned
 *   One `prefetcht0` per stream per iteration at +128 B, i.e. TWO cache lines
 *   past the iteration start, which is the NEXT 128 B block. Kernel 4's
 *   three-way distance study (report section 8) measured +2 lines as the
 *   winner over +1 and +4 in the 64 B-1 KB band on all three targets, and this
 *   rung uses that answer rather than re-running the experiment. Both streams
 *   are prefetched: the store stream still needs ownership of the destination
 *   line, and a read-for-ownership request is what that prefetch issues. The
 *   address may legally run past the end of the mapping — prefetch hints never
 *   fault — so no bounds check is needed or wanted.
 *
 * DIFFERENCES FROM KERNEL 4 (each one is a mechanism the report can test)
 *   1. 32 B ALIGNED FAST PATH. Kernel 4 stores with `vmovups` (unaligned)
 *      everywhere. Here, if dst is 32 B aligned, the stores are `vmovdqa`
 *      (aligned). A 32 B store at a 32 B-aligned address can never straddle a
 *      64 B cache line, so it can never pay the line-split penalty; the aligned
 *      path exists to remove that case. The branch is a single `test $31,%edi`
 *      resolved once per call, and under the harness it is perfectly predicted
 *      (buffers are 64 B aligned, offset 0). The unaligned path is a separate
 *      loop, not a fallback to the small path: misalignment is a normal case
 *      and `vmovdqu` is a full-speed instruction on this core.
 *      The LOADS stay unaligned in both paths. Making the load stream aligned
 *      would require repacking 32 B lanes across registers (`vperm2i128` /
 *      `vpalignr`) whenever src and dst do not share an alignment, which costs
 *      more than the line-split it avoids; that is a different algorithm, not a
 *      wider version of this one, and it is not attempted here.
 *   2. SCALAR BELOW 64 B. Kernel 4's < 64 B path is 16 B `xmm` accesses (and
 *      clang merged the adjacent ones to 32 B at 32-63 B). This rung's < 64 B
 *      path is GPR-only — 8/4/2/1 byte overlapping pairs — and never touches a
 *      vector register. This is a deliberate divergence required by the rung's
 *      definition, and it is expected to be slower than Kernel 4 at small
 *      sizes; the report measures it and says so rather than tuning it away.
 *   3. The tail is shared by both store paths and is 16 B `vmovdqu` + GPR
 *      pairs, as in Kernel 4. Widest access anywhere in this file is 32 B.
 *
 * CORRECTNESS ARGUMENT (why nothing overruns)
 *   - The 128 B loop runs only while dst <= dst+n-128, so all four loads and
 *     stores cover [i, i+128) with i+128 <= n.
 *   - The 32 B loop runs only while dst <= dst+n-32, so each access covers
 *     [i, i+32) with i+32 <= n.
 *   - The tail handles r = n - i in [0,32) with OVERLAPPING accesses: for
 *     r >= 16 it writes [i, i+16) and [i+r-16, i+r); for r in [8,16) two 8 B
 *     words at i and i+r-8; and so on down to one byte. Because the second
 *     access starts at r-sz >= 0, every access lies inside [0, n) of both
 *     buffers, so the 64 B canaries either side of dst in bench.c are never
 *     touched. src and dst do not overlap, so writing a byte twice — once as
 *     "head", once as "tail" — is harmless.
 *   - The < 64 B path uses the same overlapping-pair idea at 8/4/2/1 B
 *     granularity: [0,32) plus [n-32,n) for n in [32,64), and so on down, which
 *     covers [0,n) exactly and never leaves it.
 *   - n == 0 falls through the whole small path and returns without touching
 *     memory, as the contract requires.
 *
 * WIDTH DECLARATION: widest memory access is 32 B (256 bits), which is what
 * scripts/audit_width.sh declares for memcpy_asm.S. The gate disassembles this
 * object and fails the build if the widest access is anything else, so the
 * declaration is checked against the binary, not trusted from this comment.
 */

        .text
        .globl  memcpy_asm
        .type   memcpy_asm, @function
        .p2align 4
memcpy_asm:
        mov     %rdi, %rax                  /* return value = original dst */
        cmp     $64, %rdx
        jb      .Lsmall                     /* < 64 B: scalar, no vector regs */

        /* ---------------- main path: n >= 64 ---------------- */
        lea     (%rdi,%rdx), %r8            /* r8 = dst + n */
        test    $31, %edi                   /* is dst 32 B aligned? */
        jnz     .Lmain_unaligned

/* ============ 32 B aligned destination: vmovdqa stores ============ */
.Laligned_128:
        lea     -128(%r8), %r9              /* last start of a full 128 B block */
        cmp     %r9, %rdi
        ja      .Laligned_32
        .p2align 4
.Laligned_128_loop:
        prefetcht0 128(%rsi)                /* +2 lines: next 128 B block, src */
        prefetcht0 128(%rdi)                /* +2 lines: dst needs ownership */
        vmovdqu  0(%rsi), %ymm0             /* loads unaligned: src alignment is */
        vmovdqu 32(%rsi), %ymm1             /* independent of dst's, and a full-  */
        vmovdqu 64(%rsi), %ymm2             /* width realign would need lane       */
        vmovdqu 96(%rsi), %ymm3             /* repacking (see header, difference 1)*/
        vmovdqa %ymm0,   0(%rdi)            /* aligned stores: cannot split a line */
        vmovdqa %ymm1,  32(%rdi)
        vmovdqa %ymm2,  64(%rdi)
        vmovdqa %ymm3,  96(%rdi)
        add     $128, %rdi
        add     $128, %rsi
        cmp     %r9, %rdi
        jbe     .Laligned_128_loop
.Laligned_32:
        lea     -32(%r8), %r9               /* last start of a full 32 B block */
        cmp     %r9, %rdi
        ja      .Ltail
        .p2align 4
.Laligned_32_loop:
        vmovdqu 0(%rsi), %ymm0
        vmovdqa %ymm0, 0(%rdi)
        add     $32, %rdi
        add     $32, %rsi
        cmp     %r9, %rdi
        jbe     .Laligned_32_loop
        jmp     .Ltail

/* ============ unaligned destination: vmovdqu stores ============ */
        .p2align 4
.Lmain_unaligned:
        lea     -128(%r8), %r9
        cmp     %r9, %rdi
        ja      .Lunaligned_32
        .p2align 4
.Lunaligned_128_loop:
        prefetcht0 128(%rsi)
        prefetcht0 128(%rdi)
        vmovdqu  0(%rsi), %ymm0
        vmovdqu 32(%rsi), %ymm1
        vmovdqu 64(%rsi), %ymm2
        vmovdqu 96(%rsi), %ymm3
        vmovdqu %ymm0,   0(%rdi)
        vmovdqu %ymm1,  32(%rdi)
        vmovdqu %ymm2,  64(%rdi)
        vmovdqu %ymm3,  96(%rdi)
        add     $128, %rdi
        add     $128, %rsi
        cmp     %r9, %rdi
        jbe     .Lunaligned_128_loop
.Lunaligned_32:
        lea     -32(%r8), %r9
        cmp     %r9, %rdi
        ja      .Ltail
        .p2align 4
.Lunaligned_32_loop:
        vmovdqu 0(%rsi), %ymm0
        vmovdqu %ymm0, 0(%rdi)
        add     $32, %rdi
        add     $32, %rsi
        cmp     %r9, %rdi
        jbe     .Lunaligned_32_loop

/* ---------------- shared tail: 0 <= r < 32 ---------------- */
.Ltail:
        mov     %r8, %rcx
        sub     %rdi, %rcx                  /* rcx = r, in [0,32) */
        cmp     $16, %rcx
        jb      .Ltail8
        vmovdqu  0(%rsi), %xmm0
        vmovdqu  -16(%rsi,%rcx,1), %xmm1
        vmovdqu  %xmm0, 0(%rdi)
        vmovdqu  %xmm1, -16(%rdi,%rcx,1)
        jmp     .Lexit
.Ltail8:
        cmp     $8, %rcx
        jb      .Ltail4
        mov     0(%rsi), %r9
        mov     -8(%rsi,%rcx,1), %r10
        mov     %r9, 0(%rdi)
        mov     %r10, -8(%rdi,%rcx,1)
        jmp     .Lexit
.Ltail4:
        cmp     $4, %rcx
        jb      .Ltail2
        mov     0(%rsi), %r9d
        mov     -4(%rsi,%rcx,1), %r10d
        mov     %r9d, 0(%rdi)
        mov     %r10d, -4(%rdi,%rcx,1)
        jmp     .Lexit
.Ltail2:
        cmp     $2, %rcx
        jb      .Ltail1
        movzwl  0(%rsi), %r9d
        movzwl  -2(%rsi,%rcx,1), %r10d
        mov     %r9w, 0(%rdi)
        mov     %r10w, -2(%rdi,%rcx,1)
        jmp     .Lexit
.Ltail1:
        test    %rcx, %rcx
        jz      .Lexit
        movzbl  0(%rsi), %r9d
        mov     %r9b, 0(%rdi)

/* Every main-path exit lands here so the AVX upper state is zeroed exactly
 * once and on every path that touched a ymm register. Leaving it dirty makes
 * the caller's next legacy-SSE instruction pay a transition stall, which is
 * the caller's cost but our bug. */
.Lexit:
        vzeroupper
        ret

/* ================= small path: 0 <= n < 64, scalar only =================
 * GPR overlapping pairs, no vector register, so this path cannot dirty the
 * AVX state and needs no vzeroupper. Covered ranges (all inside [0,n)):
 *   [32,64)  [0,32) and [n-32,n)      [16,32)  [0,16) and [n-16,n)
 *   [8,16)   [0,8)  and [n-8,n)       [4,8)    [0,4)  and [n-4,n)
 *   [2,4)    [0,2)  and [n-2,n)       [1,2)    one byte
 */
.Lsmall:
        cmp     $32, %rdx
        jb      .Lsmall32
        mov     0(%rsi), %rcx
        mov     8(%rsi), %r8
        mov     16(%rsi), %r9
        mov     24(%rsi), %r10
        mov     %rcx, 0(%rdi)
        mov     %r8, 8(%rdi)
        mov     %r9, 16(%rdi)
        mov     %r10, 24(%rdi)
        mov     -32(%rsi,%rdx,1), %rcx
        mov     -24(%rsi,%rdx,1), %r8
        mov     -16(%rsi,%rdx,1), %r9
        mov     -8(%rsi,%rdx,1), %r10
        mov     %rcx, -32(%rdi,%rdx,1)
        mov     %r8, -24(%rdi,%rdx,1)
        mov     %r9, -16(%rdi,%rdx,1)
        mov     %r10, -8(%rdi,%rdx,1)
        ret
.Lsmall32:
        cmp     $16, %rdx
        jb      .Lsmall16
        mov     0(%rsi), %rcx
        mov     8(%rsi), %r8
        mov     %rcx, 0(%rdi)
        mov     %r8, 8(%rdi)
        mov     -16(%rsi,%rdx,1), %rcx
        mov     -8(%rsi,%rdx,1), %r8
        mov     %rcx, -16(%rdi,%rdx,1)
        mov     %r8, -8(%rdi,%rdx,1)
        ret
.Lsmall16:
        cmp     $8, %rdx
        jb      .Lsmall8
        mov     0(%rsi), %rcx
        mov     -8(%rsi,%rdx,1), %r8
        mov     %rcx, 0(%rdi)
        mov     %r8, -8(%rdi,%rdx,1)
        ret
.Lsmall8:
        cmp     $4, %rdx
        jb      .Lsmall4
        mov     0(%rsi), %ecx
        mov     -4(%rsi,%rdx,1), %r8d
        mov     %ecx, 0(%rdi)
        mov     %r8d, -4(%rdi,%rdx,1)
        ret
.Lsmall4:
        cmp     $2, %rdx
        jb      .Lsmall2
        movzwl  0(%rsi), %ecx
        movzwl  -2(%rsi,%rdx,1), %r8d
        mov     %cx, 0(%rdi)
        mov     %r8w, -2(%rdi,%rdx,1)
        ret
.Lsmall2:
        test    %rdx, %rdx
        jz      .Lsmall_done
        movzbl  0(%rsi), %ecx
        mov     %cl, 0(%rdi)
.Lsmall_done:
        ret
        .size   memcpy_asm, .-memcpy_asm
