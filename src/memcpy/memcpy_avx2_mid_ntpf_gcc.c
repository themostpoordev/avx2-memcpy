/* src/memcpy_avx2_mid_ntpf_gcc.c — Phase 3 P3: prefetchnta on the source.
 *
 * The Kernel 5 NT rung lost because it made the STORES non-temporal on a
 * destination that is reused (L3-resident), so the fill bypassed the cache that
 * was about to read it. This is a different mechanism: only the SOURCE stream is
 * prefetched with NTA, and only from 256 KiB up. A source read once and never
 * reused does not want to evict the destination from L2/L3, which is exactly what
 * T0 prefetch on that stream does at these sizes.
 *
 * Below 256 KiB this rung is byte-identical to `mid` (the branch is resolved once
 * per call, not per iteration), so the 1-64 KiB target band cannot be moved by
 * it; its effect is only visible at 256 KiB and above. */
#define MID_FN      memcpy_avx2_mid_ntpf
#define MID_UNROLL  8
#define MID_PF1     256
#define MID_PF2     512
#define MID_PEEL    1
#define MID_NTPF    1
#define MID_NT_SIZE 262144
#include "memcpy_avx2_mid_impl.h"
