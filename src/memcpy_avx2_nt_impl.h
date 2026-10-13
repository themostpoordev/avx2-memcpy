/* src/memcpy_avx2_nt_impl.h — shared body of the Kernel 5 (AVX2 non-temporal) rung.
 *
 * One variant is built from this body (src/memcpy_avx2_nt.c), because the
 * prefetch distance question was already answered in Kernel 4: +2 cache lines
 * won the 64 B-1 KB band on all three targets and is not beaten there by any
 * other distance, so the NT rung inherits +2 rather than re-tuning it here.
 *
 * What "non-temporal" means for this kernel
 *   Above MEMCPY_AVX2_NT_THRESHOLD, 32-byte stores use `_mm256_stream_si256`,
 *   which bypasses the cache for the destination. That trades a cheaper bulk
 *   transfer (no read-for-ownership traffic, no cache pollution) for two costs:
 *   the store buffer must drain, and the destination is cold for the next reader.
 *   A single `_mm_sfence()` orders the NT stores before the function returns.
 *
 * Why there is a threshold at all
 *   Below it, NT is a pessimisation: a small copy's destination is usually read
 *   back immediately, so bypassing the cache throws away the only benefit while
 *   paying the fence. The threshold is NOT a measured crossover: the probe in
 *   scripts/nt_threshold_probe.sh found NO size at which NT wins on this box (it
 *   loses by 69-371% at every size; see the Kernel 5 report). 256 KB is an
 *   EXPERIMENTAL GATE, chosen so that the campaign exercises NT on the standing
 *   ladder (>= 256 KB) and runs byte-for-byte rung 4's cached loop below it - that
 *   is what lets the campaign measure the NT effect at all. The campaign's verdict
 *   for this rung is a measured LOSS, not a win. Why NT is slow on this box is an
 *   inference (a bandwidth-capped streaming write path; no L3/LLC events are
 *   exposed here, MEASUREMENT_PROTOCOL.md section 5), and is labelled as one.
 *
 * Alignment
 *   `_mm256_stream_si256` REQUIRES a 32-byte aligned destination. An unaligned
 *   destination is not a refusal: the first <=31 bytes are copied with cached
 *   stores until the destination is 32-byte aligned, and the NT loop then runs on
 *   the aligned remainder. The cost of that handling is the cached lead plus a
 *   slightly shorter NT body, and it is measured with the harness's
 *   `--align`/`--align2d` modes rather than asserted.
 *
 * Prefetching the destination is deliberately NOT done in the NT loop
 *   Kernel 4 prefetched both streams because a cached store needs ownership. An
 *   NT store does not: pulling the destination line into cache is exactly what it
 *   is trying to avoid, so only the source is prefetched here. This is a
 *   difference from the cached path and is called out rather than hidden.
 *
 * Bytes at a time, after the audit-width gate
 *   Widest access is 32 B (`_mm256_loadu_si256` / `_mm256_stream_si256`), which is
 *   what scripts/audit_width.sh declares for this rung. Nothing here may widen:
 *   `make audit-width` compares the built object against that declaration and
 *   fails the build if the compiler produces anything wider, which is how the
 *   same class of merge was caught twice (43f01dc, 25faacb).
 */
#ifndef MEMCPY_AVX2_NT_FN
#error "define MEMCPY_AVX2_NT_FN before including memcpy_avx2_nt_impl.h"
#endif
#ifndef MEMCPY_AVX2_NT_PF_LINES
#define MEMCPY_AVX2_NT_PF_LINES 2
#endif
/* Measured threshold; overridable at compile time for the threshold probe. */
#ifndef MEMCPY_AVX2_NT_THRESHOLD
#define MEMCPY_AVX2_NT_THRESHOLD 262144u
#endif

#include <immintrin.h>
#include <stdint.h>
#include "common.h"

#define MEMCPY_AVX2_NT_PF_BYTES ((size_t)MEMCPY_AVX2_NT_PF_LINES * 64u)

void *MEMCPY_AVX2_NT_FN(void *dst, const void *src, size_t n)
{
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    if (n >= 64) {
        size_t i = 0;

        if (n >= MEMCPY_AVX2_NT_THRESHOLD) {
            /* ---- cached lead: make the destination 32-byte aligned ------ */
            size_t lead = (size_t)((32u - ((uintptr_t)d & 31u)) & 31u);
            if (lead >= 16) {
                __m128i a = _mm_loadu_si128((const __m128i *)(s));
                __m128i b = _mm_loadu_si128((const __m128i *)(s + lead - 16));
                _mm_storeu_si128((__m128i *)(d), a);
                _mm_storeu_si128((__m128i *)(d + lead - 16), b);
            } else if (lead >= 8) {
                uint64_t a, b;
                __builtin_memcpy(&a, s, 8);
                __builtin_memcpy(&b, s + lead - 8, 8);
                __builtin_memcpy(d, &a, 8);
                __builtin_memcpy(d + lead - 8, &b, 8);
            } else if (lead >= 4) {
                uint32_t a, b;
                __builtin_memcpy(&a, s, 4);
                __builtin_memcpy(&b, s + lead - 4, 4);
                __builtin_memcpy(d, &a, 4);
                __builtin_memcpy(d + lead - 4, &b, 4);
            } else if (lead >= 2) {
                uint16_t a, b;
                __builtin_memcpy(&a, s, 2);
                __builtin_memcpy(&b, s + lead - 2, 2);
                __builtin_memcpy(d, &a, 2);
                __builtin_memcpy(d + lead - 2, &b, 2);
            } else if (lead == 1) {
                d[0] = s[0];
            }
            i = lead;

            /* ---- non-temporal body: 128 B per iteration, dst is aligned - */
            for (; i + 128 <= n; i += 128) {
                _mm_prefetch((const char *)(s + i + MEMCPY_AVX2_NT_PF_BYTES), _MM_HINT_T0);

                __m256i v0 = _mm256_loadu_si256((const __m256i *)(s + i));
                __m256i v1 = _mm256_loadu_si256((const __m256i *)(s + i + 32));
                __m256i v2 = _mm256_loadu_si256((const __m256i *)(s + i + 64));
                __m256i v3 = _mm256_loadu_si256((const __m256i *)(s + i + 96));

                _mm256_stream_si256((__m256i *)(d + i),      v0);
                _mm256_stream_si256((__m256i *)(d + i + 32), v1);
                _mm256_stream_si256((__m256i *)(d + i + 64), v2);
                _mm256_stream_si256((__m256i *)(d + i + 96), v3);
            }
            for (; i + 32 <= n; i += 32) {
                __m256i v = _mm256_loadu_si256((const __m256i *)(s + i));
                _mm256_stream_si256((__m256i *)(d + i), v);
            }
            _mm_sfence();
        } else {
            /* ---- cached path: byte-for-byte Kernel 4's loop ------------- */
            for (; i + 128 <= n; i += 128) {
                _mm_prefetch((const char *)(s + i + MEMCPY_AVX2_NT_PF_BYTES), _MM_HINT_T0);
                _mm_prefetch((const char *)(d + i + MEMCPY_AVX2_NT_PF_BYTES), _MM_HINT_T0);

                __m256i v0 = _mm256_loadu_si256((const __m256i *)(s + i));
                __m256i v1 = _mm256_loadu_si256((const __m256i *)(s + i + 32));
                __m256i v2 = _mm256_loadu_si256((const __m256i *)(s + i + 64));
                __m256i v3 = _mm256_loadu_si256((const __m256i *)(s + i + 96));

                _mm256_storeu_si256((__m256i *)(d + i),      v0);
                _mm256_storeu_si256((__m256i *)(d + i + 32), v1);
                _mm256_storeu_si256((__m256i *)(d + i + 64), v2);
                _mm256_storeu_si256((__m256i *)(d + i + 96), v3);
            }
            for (; i + 32 <= n; i += 32) {
                __m256i v = _mm256_loadu_si256((const __m256i *)(s + i));
                _mm256_storeu_si256((__m256i *)(d + i), v);
            }
        }

        /* ---- tail: 0 <= n - i < 32, always cached ---------------------- */
        size_t r = n - i;
        if (r >= 16) {
            __m128i a = _mm_loadu_si128((const __m128i *)(s + i));
            __m128i b = _mm_loadu_si128((const __m128i *)(s + i + r - 16));
            _mm_storeu_si128((__m128i *)(d + i), a);
            _mm_storeu_si128((__m128i *)(d + i + r - 16), b);
        } else if (r >= 8) {
            uint64_t a, b;
            __builtin_memcpy(&a, s + i, 8);
            __builtin_memcpy(&b, s + i + r - 8, 8);
            __builtin_memcpy(d + i, &a, 8);
            __builtin_memcpy(d + i + r - 8, &b, 8);
        } else if (r >= 4) {
            uint32_t a, b;
            __builtin_memcpy(&a, s + i, 4);
            __builtin_memcpy(&b, s + i + r - 4, 4);
            __builtin_memcpy(d + i, &a, 4);
            __builtin_memcpy(d + i + r - 4, &b, 4);
        } else if (r >= 2) {
            uint16_t a, b;
            __builtin_memcpy(&a, s + i, 2);
            __builtin_memcpy(&b, s + i + r - 2, 2);
            __builtin_memcpy(d + i, &a, 2);
            __builtin_memcpy(d + i + r - 2, &b, 2);
        } else if (r == 1) {
            d[i] = s[i];
        }
        return dst;
    }

    /* ---- small-size fast path: < 64 B never reaches the NT code -------- */
    if (n >= 32) {
        __m128i a = _mm_loadu_si128((const __m128i *)s);
        __m128i b = _mm_loadu_si128((const __m128i *)(s + 16));
        __m128i c = _mm_loadu_si128((const __m128i *)(s + n - 32));
        __m128i e = _mm_loadu_si128((const __m128i *)(s + n - 16));
        _mm_storeu_si128((__m128i *)d, a);
        _mm_storeu_si128((__m128i *)(d + 16), b);
        _mm_storeu_si128((__m128i *)(d + n - 32), c);
        _mm_storeu_si128((__m128i *)(d + n - 16), e);
    } else if (n >= 16) {
        __m128i a = _mm_loadu_si128((const __m128i *)s);
        __m128i b = _mm_loadu_si128((const __m128i *)(s + n - 16));
        _mm_storeu_si128((__m128i *)d, a);
        _mm_storeu_si128((__m128i *)(d + n - 16), b);
    } else if (n >= 8) {
        uint64_t a, b;
        __builtin_memcpy(&a, s, 8);
        __builtin_memcpy(&b, s + n - 8, 8);
        __builtin_memcpy(d, &a, 8);
        __builtin_memcpy(d + n - 8, &b, 8);
    } else if (n >= 4) {
        uint32_t a, b;
        __builtin_memcpy(&a, s, 4);
        __builtin_memcpy(&b, s + n - 4, 4);
        __builtin_memcpy(d, &a, 4);
        __builtin_memcpy(d + n - 4, &b, 4);
    } else if (n >= 2) {
        uint16_t a, b;
        __builtin_memcpy(&a, s, 2);
        __builtin_memcpy(&b, s + n - 2, 2);
        __builtin_memcpy(d, &a, 2);
        __builtin_memcpy(d + n - 2, &b, 2);
    } else if (n == 1) {
        d[0] = s[0];
    }
    /* n == 0 touches nothing, as the contract requires */
    return dst;
}
