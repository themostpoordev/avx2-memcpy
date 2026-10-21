/* SPDX-License-Identifier: MIT */
/* src/memcpy_rep.c — KERNEL 7 of the ladder: `rep movsb`, size-gated.
 *
 * The question: this host has no ERMS and no FSRM (Phase 0 established that, and
 * it is why glibc cannot use its `rep movsb` tier below the stop threshold here
 * in the way it would on a modern core). `rep movsb` on such a CPU is
 * microcoded: it copies a few bytes per cycle at best and has a fixed start-up
 * cost of tens of cycles. The prediction is therefore that this rung loses to
 * every vector rung at every size below the memory-bandwidth cliff, and is
 * roughly level with them above it. It is measured to see that prediction
 * confirmed or refuted, not because it is expected to win.
 *
 * Shape
 *   n >= REP_MIN : one `rep movsb` for the whole copy.
 *   n <  REP_MIN : kernel 2's word loop (8 B via __builtin_memcpy + byte tail).
 *   The gate is not a tune-to-win: with no FSRM the fixed start-up cost makes
 *   `rep movsb` a bad choice for short copies on any implementation, and a rung
 *   that paid that cost at 16 B would only be measuring the start-up. REP_MIN is
 *   256 B, chosen *before* the measurement from the start-up cost (tens of
 *   cycles) versus 256 B at 32 B/cycle (~8 cycles): below that the start-up
 *   alone is the whole copy. The report states both sides of the gate so a
 *   reader can see exactly what was measured.
 *
 * Contract and assumptions
 *   - Non-overlapping src/dst (a copy, not a move), same as every rung above.
 *     With DF=0 and non-overlapping ranges, forward `rep movsb` is well defined.
 *   - Direction flag: the SysV ABI requires DF=0 on function entry and exit, and
 *     nothing in this project ever sets it. The instruction below therefore does
 *     not need a `cld`, and adding one would charge this rung for a guarantee
 *     the ABI already gives us.
 *   - `rep movsb` reads %rsi/%rdi/%rcx, writes all three, and returns with
 *     %rcx = 0. It does not fault on unaligned addresses, and it never writes
 *     outside [dst, dst+n): the count is exact, so the canaries in bench.c are
 *     safe by construction rather than by argument.
 *   - n == 0: `rep movsb` with %rcx = 0 executes zero iterations and touches
 *     nothing, which is what the memcpy contract requires. The word-loop path
 *     also does nothing at n == 0.
 *   - No strict-aliasing issue: the transfer happens inside the asm statement,
 *     so the compiler sees a memory clobber, not typed accesses.
 *
 * Why the asm and not __builtin_memcpy(dst, src, n)
 *   The Makefile compiles every kernel with -fno-builtin-memcpy precisely so the
 *   compiler cannot turn our kernels into libc calls. An explicit `rep movsb`
 *   is the only way to measure this instruction rather than glibc's choice of it.
 */
#include "common.h"

#define REP_MIN 256u

void *memcpy_rep(void *dst, const void *src, size_t n)
{
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    size_t i = 0;

    if (n >= REP_MIN) {
        size_t cnt = n;
        /* %rdi <- d, %rsi <- s, %rcx <- cnt; all three are updated in place. */
        __asm__ volatile("rep movsb"
                         : "+D"(d), "+S"(s), "+c"(cnt)
                         :
                         : "memory");
        return dst;
    }

    /* ---- below the gate: kernel 2's word loop, so the comparison is honest -- */
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
    for (; i + 8 <= n; i += 8) {
        uint64_t w;
        __builtin_memcpy(&w, s + i, 8);
        __builtin_memcpy(d + i, &w, 8);
    }
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
    for (; i < n; i++)
        d[i] = s[i];

    return dst;
}
