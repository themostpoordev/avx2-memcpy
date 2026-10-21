/* SPDX-License-Identifier: MIT */
/* include/memcpy_mid.h — opt-in memcpy for the 1 KiB .. 64 KiB band.
 *
 * This is NOT the general memcpy path. memcpy_fast is. memcpy_mid exists because
 * one band of sizes rewards a different schedule (aligned 32 B loads with the
 * software prefetch placed ahead of the first store), and a caller who knows their
 * workload sits in that band can opt in to it.
 *
 * Contract:
 *   - 1 KiB <= n <= 64 KiB:  the tuned mid kernel, +6-7% over glibc at 16 KiB on
 *                            Zen 2 (docs/RESULTS.md, Tier A, certified).
 *   - n outside that range:  forwarded to memcpy_fast, which is the right choice
 *                            there. One predicted branch decides.
 *
 * The kernel is exported as memcpy_mid_kernel so the measurement harness can time
 * it without the caller-side range check; memcpy_mid adds that check and is what
 * a caller should use.
 *
 * Same overlap rule as memcpy: src and dst must not overlap.
 */
#ifndef MEMCPY_MID_H
#define MEMCPY_MID_H

#include <stddef.h>
#include "memcpy_fast.h"

#ifdef __cplusplus
extern "C" {
#endif

void *memcpy_mid_kernel(void *dst, const void *src, size_t n);

static inline void *memcpy_mid(void *dst, const void *src, size_t n)
{
    if (n - 1024u > (65536u - 1024u))
        return memcpy_fast(dst, src, n);
    return memcpy_mid_kernel(dst, src, n);
}

#ifdef __cplusplus
}
#endif

#endif /* MEMCPY_MID_H */
