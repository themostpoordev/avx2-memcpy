/* SPDX-License-Identifier: MIT */
/* src/memset_asm.S — KERNEL 8, rung 6: hand-written AVX2 assembly.
 *
 * Tooling: clang's integrated assembler, GAS/AT&T syntax. A .S file goes through
 * cpp, so comments are C comments on purpose ('#' would start a preprocessor
 * directive and stop the file assembling for a reason unrelated to the code).
 *
 * CALLING CONVENTION (SysV AMD64)
 *   void *memset_asm(void *dst, int c, size_t n)
 *     %rdi = dst, %esi = c (only the low byte means anything), %rdx = n
 *     returns dst in %rax
 *   Leaf function: no stack frame, no stack slot touched, no callee-saved
 *   register (rbx, rbp, r12-r15) used. Every register below is caller-saved.
 *   ymm0/ymm1 are caller-saved too; the upper halves are zeroed with vzeroupper
 *   before returning so the caller is not left with dirty ymm state.
 *
 * SHAPE — the same algorithm as the C rung 4, so the comparison is asm-vs-compiler
 *   vmovd + vpbroadcastb of the fill byte into ymm0,  (NOT vpbroadcastb from a
 *   GPR: that encoding does not exist before AVX-512. The assembler accepts
 *   `vpbroadcastb %esi, %ymm0` and emits an EVEX-prefixed form - byte 0x62 -
 *   which is an illegal instruction on this AVX2-only silicon. Found by
 *   --verify on the first build; the object now shows vmovd (C5) + vpbroadcastb
 *   (C4) and no 0x62 prefix anywhere.)
 *   a 128 B loop of four 32 B vmovdqu stores (two cache lines per iteration),
 *   a 32 B remainder loop,
 *   and a < 32 B tail made of overlapping stores: 16 B pair for 16..31, 8 B pair
 *     for 8..15, and a byte loop for 1..7.
 *   No prefetch: a fill has no read stream to prefetch, and the store stream is
 *   already covered by the store buffer plus the hardware prefetcher on the
 *   destination. Unlike the memcpy asm rung there are no four-lane loads to
 *   schedule, because every lane stores the same register.
 *
 * WHY THE TAIL IS OVERLAPPING AND NOT A BYTE LOOP
 *   For a fill, writing the same byte twice is invisible, and each pair of stores
 *   covers [0,sz) and [r-sz,r) with r-sz >= 0, so both lie inside the caller's
 *   buffer: nothing outside [dst, dst+n) is touched, which is what the 64 B
 *   canaries check. Below 8 bytes there is nothing to overlap, so the byte loop
 *   takes over; that is at most 7 iterations and cannot overrun by construction.
 */
    .text
    .globl  memset_asm
    .type   memset_asm, @function
    .p2align 5
memset_asm:
    mov     %rdi, %rax              /* return value = original dst */
    vmovd   %esi, %xmm0             /* xmm0[7:0] = low byte of c (AVX form) */
    vpbroadcastb %xmm0, %ymm0       /* ymm0 = that byte, 32 times (AVX2 form) */

    cmp     $128, %rdx
    jb      .Lbytes32

    .p2align 4
.Lloop128:
    vmovdqu %ymm0, (%rdi)           /* 32 B */
    vmovdqu %ymm0, 32(%rdi)         /* 32 B */
    vmovdqu %ymm0, 64(%rdi)         /* 32 B */
    vmovdqu %ymm0, 96(%rdi)         /* 32 B */
    add     $128, %rdi
    sub     $128, %rdx
    cmp     $128, %rdx
    jae     .Lloop128

.Lbytes32:
    cmp     $32, %rdx
    jb      .Ltail
    .p2align 4
.Lloop32:
    vmovdqu %ymm0, (%rdi)           /* 32 B */
    add     $32, %rdi
    sub     $32, %rdx
    cmp     $32, %rdx
    jae     .Lloop32

/* r = %rdx in [0,32); %rdi is the cursor at the first unwritten byte. */
.Ltail:
    test    %rdx, %rdx
    je      .Ldone
    cmp     $16, %rdx
    jb      .Ltail_lt16
    vmovdqu %xmm0, (%rdi)           /* [0,16)  */
    vmovdqu %xmm0, -16(%rdi,%rdx)   /* [r-16,r) */
    jmp     .Ldone

.Ltail_lt16:
    cmp     $8, %rdx
    jb      .Ltail_lt8
    vmovq   %xmm0, (%rdi)           /* [0,8)  */
    vmovq   %xmm0, -8(%rdi,%rdx)    /* [r-8,r) */
    jmp     .Ldone

.Ltail_lt8:
    mov     %sil, (%rdi)            /* 1..7 bytes, one at a time */
    inc     %rdi
    dec     %rdx
    jne     .Ltail_lt8

.Ldone:
    vzeroupper
    ret
    .size   memset_asm, .-memset_asm

    .section .note.GNU-stack,"",@progbits
