/* tools/route_check.c — Phase 2.5 verification of the hybrid dispatcher.
 *
 * NOT part of the benchmark: it never touches a clock. It links against the
 * ALREADY BUILT src/ objects (including build/<t>/src/memcpy_hybrid.o), so what
 * it checks is the shipped object, not a recompile of the source.
 *
 *   sh scripts/hybrid_route_check.sh     builds and runs it for all targets
 *
 * Three checks, in order of what each would catch:
 *
 *   1. CLASS MAPPING (exhaustive). For every n in [0, 8 MiB] and for SIZE_MAX,
 *      the class the compiled object returns must equal the class the Phase 2.5
 *      rules define: largest measured point <= n, clamped to [0, 12]. This is
 *      what catches an off-by-one in the generated small-size table or in the
 *      boundary walk — the kind of defect that would silently route 255 B to
 *      the 256 B winner (a wider rung than the lower-point rule allows).
 *
 *   2. THE TABLE AS COMPILED. Prints class -> bytes -> rung name read out of
 *      the object, plus the class each interesting size lands in. The names come
 *      from the object's own table, so this is the binary's answer, not the
 *      generator's.
 *
 *   3. PER-CLASS CORRECTNESS. For every class boundary and for sizes that fall
 *      between boundaries, copies with the hybrid and with memcpy_ref (the
 *      project's byte-at-a-time golden reference) at seven src/dst offsets, and
 *      compares the bytes plus 64 B canaries on both sides of the destination.
 *      --verify in the harness does this too, but this checker drives the exact
 *      sizes the routing rules are defined at, including the boundary +-1 cases.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "common.h"
#include "routing_table.h"

/* the rung's own introspection API (src/memcpy_hybrid.c) */
unsigned    memcpy_hybrid_class(size_t n);
const char *memcpy_hybrid_route_name(size_t n);
unsigned    memcpy_hybrid_classes(void);
size_t      memcpy_hybrid_class_bytes(unsigned c);
const char *memcpy_hybrid_provenance(void);

#define LIMIT (8u * 1024u * 1024u)
#define CANARY 64u

static unsigned expected_class(size_t n)
{
    unsigned classes = memcpy_hybrid_classes();
    unsigned c = 0;
    for (unsigned i = 0; i < classes; i++)
        if (memcpy_hybrid_class_bytes(i) <= n)
            c = i;
    return c;
}

/* one copy compared against memcpy_ref, with canaries both sides */
static int check_copy(size_t n, size_t soff, size_t doff)
{
    static unsigned char srcbuf[LIMIT + 256];
    static unsigned char dst_h[LIMIT + 512];
    static unsigned char dst_r[LIMIT + 512];
    const size_t span = CANARY + doff + n + CANARY;

    if (span > sizeof dst_h) return 0;

    for (size_t i = 0; i < sizeof srcbuf; i++) srcbuf[i] = (unsigned char)(i * 31u + 7u);
    memset(dst_h, 0xA5, sizeof dst_h);
    memset(dst_r, 0x5A, sizeof dst_r);

    memcpy_hybrid(dst_h + CANARY + doff, srcbuf + CANARY + soff, n);
    memcpy_ref(dst_r + CANARY + doff, srcbuf + CANARY + soff, n);

    /* payload */
    if (memcmp(dst_h + CANARY + doff, dst_r + CANARY + doff, n) != 0) return 0;
    /* canaries on both sides of the destination */
    for (size_t i = 0; i < CANARY; i++) {
        if (dst_h[CANARY + doff - 1 - i] != 0xA5) return 0;   /* before */
        if (dst_h[CANARY + doff + n + i] != 0xA5) return 0;   /* after  */
    }
    /* Canaries further out, not just the 64 B immediately after the payload.
     * The loop must stop while CANARY + i is still INSIDE dst_h: the first
     * version ran i to sizeof dst_h and indexed dst_h[CANARY + i], reading up to
     * 64 B past the array (into the neighbouring static, which is 0x5A) and
     * reporting 24 false failures out of 420 copies. The bound is the check. */
    for (size_t i = doff + n + CANARY; CANARY + i < sizeof dst_h; i += 997)
        if (dst_h[CANARY + i] != 0xA5) return 0;
    return 1;
}

int main(void)
{
    int fail = 0;

    printf("provenance: %s\n", memcpy_hybrid_provenance());
    printf("classes=%u\n", memcpy_hybrid_classes());

    printf("\nclass | bytes      | rung as compiled\n");
    printf("--:|--:|:--\n");
    for (unsigned c = 0; c < memcpy_hybrid_classes(); c++)
        printf("%u | %zu | %s\n", c, memcpy_hybrid_class_bytes(c),
               memcpy_hybrid_route_name(memcpy_hybrid_class_bytes(c)));

    /* ---- 1. class mapping, exhaustively -------------------------------- */
    unsigned long checked = 0, bad = 0; size_t first_bad = 0;
    for (size_t n = 0; n <= LIMIT; n++) {
        checked++;
        if (memcpy_hybrid_class(n) != expected_class(n)) {
            if (bad == 0) first_bad = n;
            bad++;
        }
    }
    for (size_t n = LIMIT; n < LIMIT + 3; n++) { checked++; if (memcpy_hybrid_class(n) != expected_class(n)) { if (!bad) first_bad = n; bad++; } }
    checked++; if (memcpy_hybrid_class(SIZE_MAX) != expected_class(SIZE_MAX)) { if (!bad) first_bad = SIZE_MAX; bad++; }
    checked++; if (memcpy_hybrid_class(SIZE_MAX - 1) != expected_class(SIZE_MAX - 1)) { if (!bad) first_bad = SIZE_MAX - 1; bad++; }

    printf("\ncheck 1 class mapping: %lu sizes checked, %lu mismatch", checked, bad);
    if (bad) { printf(" (first at n=%zu)\n", first_bad); fail = 1; } else printf(" -> PASS\n");

    /* explicit clamps, printed so the rules are visible in the evidence */
    printf("  n=0 -> class %u (%s)   [below 16 B -> 16 B winner]\n",
           memcpy_hybrid_class(0), memcpy_hybrid_route_name(0));
    printf("  n=15 -> class %u      n=16 -> class %u      n=17 -> class %u\n",
           memcpy_hybrid_class(15), memcpy_hybrid_class(16), memcpy_hybrid_class(17));
    for (unsigned c = 1; c < memcpy_hybrid_classes(); c++) {
        size_t b = memcpy_hybrid_class_bytes(c);
        printf("  boundary %8zu: n-1 -> class %u, n -> class %u, n+1 -> class %u\n",
               b, memcpy_hybrid_class(b - 1), memcpy_hybrid_class(b), memcpy_hybrid_class(b + 1));
    }
    printf("  n=SIZE_MAX -> class %u (%s)   [above the top point -> top winner]\n",
           memcpy_hybrid_class(SIZE_MAX), memcpy_hybrid_route_name(SIZE_MAX));
    printf("\n  the sizes the hybrid campaign measures that are NOT class boundaries:\n");
    {
        static const size_t between[] = { 100u, 1000u, 20000u, 300000u };
        for (unsigned i = 0; i < sizeof between / sizeof between[0]; i++)
            printf("    n=%-8zu -> class %u -> %s\n", between[i],
                   memcpy_hybrid_class(between[i]), memcpy_hybrid_route_name(between[i]));
    }

    /* ---- 3. per-class correctness --------------------------------------- */
    static const size_t offs[] = { 0, 1, 3, 7, 15, 31, 63 };
    unsigned long copies = 0; unsigned long cbad = 0;
    for (unsigned c = 0; c < memcpy_hybrid_classes(); c++) {
        size_t b = memcpy_hybrid_class_bytes(c);
        size_t sizes[4]; unsigned ns = 0;
        sizes[ns++] = b;
        if (b > 0) sizes[ns++] = b - 1;
        sizes[ns++] = b + 1;
        sizes[ns++] = (c + 1 < memcpy_hybrid_classes()) ? (b + memcpy_hybrid_class_bytes(c + 1)) / 2 : b + 4096;
        for (unsigned si = 0; si < ns; si++) {
            for (unsigned oi = 0; oi < sizeof offs / sizeof offs[0]; oi++) {
                copies++;
                if (!check_copy(sizes[si], offs[oi], offs[(oi + 3) % 7])) cbad++;
            }
        }
    }
    /* and the between-boundary sizes, plus 0 and 1 */
    size_t extra[] = { 0, 1, 2, 100, 1000, 20000, 300000, LIMIT };
    for (unsigned i = 0; i < sizeof extra / sizeof extra[0]; i++) {
        for (unsigned oi = 0; oi < sizeof offs / sizeof offs[0]; oi++) {
            copies++;
            if (!check_copy(extra[i], offs[oi], offs[(oi + 5) % 7])) cbad++;
        }
    }
    printf("\ncheck 3 per-class correctness vs memcpy_ref (canaries both sides): %lu copies, %lu bad",
           copies, cbad);
    if (cbad) { printf(" -> FAIL\n"); fail = 1; } else printf(" -> PASS\n");

    if (fail) { printf("\nroute_check: FAIL\n"); return 1; }
    printf("\nroute_check: PASS (table mapping + per-class copies)\n");
    return 0;
}
