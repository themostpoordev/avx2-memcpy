#!/bin/sh
# scripts/gen/gen_routing_table.sh — Phase 2.5: generate the hybrid dispatcher's
# routing table FROM MEASUREMENT. Nothing in the emitted header is hand-picked.
#
#   sh scripts/gen/gen_routing_table.sh
#
# OUTPUTS
#   src/routing_table.h            the generated table (compiled into the rung)
#   results/docs/analysis/phase2_hybrid_routing.txt   the evidence: both source tables, the
#                                       per-cell winner and margin, the
#                                       source-disagreement list, and the
#                                       emitted table. Tracked in the repo.
#
# WHY THERE ARE TWO SOURCES, AND WHICH ONE DECIDES
#   The routing decision must answer "which rung is fastest at this size?" and
#   the only way to answer it is to compare rungs MEASURED IN THE SAME WINDOWS.
#   The frozen committed CSVs are one file per rung, and each file comes from
#   that rung's own campaign (results/committed/phase2_sse2_*.csv is Kernel 3's
#   session, phase2_avx2_*.csv is Kernel 4's, ...). Their glibc rows prove the
#   point: the same glibc binary at the same size measures 116990 / 117813 /
#   120347 / 127490 ns at 4 MiB across four of those files. Unioning them would
#   route on numbers that are not comparable at the percent level.
#
#   So:
#     SOURCE A (decides)  the Kernel 6 campaign log, which contains EVERY ladder
#                         rung AND glibc in ONE window set (9 windows, 5 reps,
#                         cpu 2). Same campaign, same windows, comparable.
#     SOURCE B (checked)  the union of the frozen committed CSVs, restricted to
#                         the ladder rungs, taking each rung's best campaign.
#                         This is the source the Phase 2.5 request names, and it
#                         is printed and diffed against SOURCE A in full.
#
#   Where A and B disagree the table uses A (see the disagreement section of the
#   provenance file for every such cell, with both values and the margin). The
#   extractor for A is itself validated: for the three rungs that the same
#   campaign ALSO froze as a committed CSV (avx2, asm, glibc), every extracted
#   min_of_mins must equal the CSV to the last digit or this script fails.
#
# ROUTING RULES (from the Phase 2.5 request, section 3)
#   * measured points are the ladder the campaigns ran: 16 B .. 4 MiB (13 sizes);
#     16 MiB / 64 MiB / 256 MiB were never measured in Phase 2, so they are NOT
#     in the table — the request's own "use the lower measured point" rule then
#     sends everything above 4 MiB to the 4 MiB winner;
#   * a size between two measured points uses the LOWER point's winner;
#   * a size below 16 B uses the 16 B winner;
#   * a size above the top measured point uses the top point's winner.
#
# ELIGIBILITY (documented exclusion)
#   Only the six LADDER rungs are routable: naive, word, sse2, avx2, avx2_nt,
#   asm. avx2_pf2/avx2_pf4 are Kernel 4 prefetch-distance VARIANTS, not rungs,
#   and no single campaign measured them together with the asm rung, so routing
#   to them would require exactly the cross-campaign mixing this script exists
#   to avoid. glibc is the baseline being chased and is never routed to; ref_scalar
#   is the verification reference. The excluded impls are still printed from
#   SOURCE B so the reader can see what the exclusion costs.
#
# TIE RULE
#   Exact raw min_ns decides. If two rungs are equal to the raw value, the
#   earlier rung in ladder order wins (naive < word < sse2 < avx2 < avx2_nt <
#   asm): deterministic, and it never prefers a wider rung for free.
set -eu
cd "$(dirname "$0")/../.."

OUT_H=experiments/hybrid/routing_table.h
OUT_TXT=results/docs/analysis/phase2_hybrid_routing.txt
TMP=build/routing
mkdir -p "$TMP" results

LADDER="naive word sse2 avx2 avx2_nt asm"
EXCLUDED="ref_scalar glibc avx2_pf2 avx2_pf4"
SIZES="16 32 64 128 256 512 1024 4096 16384 65536 262144 1048576 4194304"
TARGETS="znver2 znver3 v3"
CAMPAIGN_LOGS="results/live/phase2_asm_campaign"     # Kernel 6 campaign (all rungs)

# ---------------------------------------------------------------------------
# SOURCE A: same-window minima out of the Kernel 6 campaign logs.
#   min over reps and windows of min_ns, per (target, impl, size); migrated=1
#   reps dropped exactly as the analyzer drops them.
# ---------------------------------------------------------------------------
: > "$TMP/srcA.txt"
for t in $TARGETS; do
    log="${CAMPAIGN_LOGS}_${t}.windows.log"
    if [ ! -f "$log" ]; then
        echo "gen_routing_table: missing $log - cannot build a same-window table" >&2
        exit 2
    fi
    awk -v T="$t" '
        $1 == "REPS" {
            impl = $2; op = $3; n = 0; mn = -1; mig = 0
            for (i = 4; i <= NF; i++) {
                split($i, kv, "="); k = kv[1]; v = kv[2] + 0
                if (k == "n") n = v; else if (k == "min_ns") mn = v; else if (k == "migrated") mig = v
            }
            if (op != "memcpy" || n == 0 || mn <= 0 || mig == 1) next
            key = impl "|" n
            if (!((key) in best) || mn < best[key]) best[key] = mn
        }
        END { for (key in best) { split(key, a, "|"); printf "%s|%s|%s|%.4f\n", T, a[1], a[2], best[key] } }
    ' "$log" >> "$TMP/srcA.txt"
done

# ---------------------------------------------------------------------------
# SOURCE B: union of the frozen committed CSVs (ladder rungs only).
#   Each rung appears in more than one file (its own campaign and the next
#   kernel's, which lists it as the "previous" rung). Rule: keep the rung's
#   best value across those files, and RECORD WHICH FILE it came from, so the
#   provenance of every number is visible.
# ---------------------------------------------------------------------------
: > "$TMP/srcB.txt"
for t in $TARGETS; do
    for f in results/committed/phase2_*_"$t".csv; do
        [ -f "$f" ] || continue
        awk -F, -v T="$t" -v SRC="$(basename "$f")" '
            NR > 1 && NF >= 6 {
                n = $4 + 0; v = $6 + 0
                if (n == 0 || v <= 0) next
                printf "%s|%s|%d|%.4f|%s\n", T, $1, n, v, SRC
            }
        ' "$f"
    done
done | awk -F'|' '{ k = $1"|"$2"|"$3; if (!((k) in b) || $4 + 0 < b[k]) { b[k] = $4 + 0; s[k] = $5 } }
                END { for (k in b) { split(k, a, "|"); printf "%s|%s|%s|%.4f|%s\n", a[1], a[2], a[3], b[k], s[k] } }' \
     | sort > "$TMP/srcB.txt"

# null band per size, taken from the frozen Kernel 6 analyzer output (same
# campaign and windows as SOURCE A): column 11 of the asm CSV.
: > "$TMP/band.txt"
for t in $TARGETS; do
    awk -F, -v T="$t" 'NR > 1 && $1 == "asm" { printf "%s|%d|%.4f\n", T, $4, $11 }' \
        "results/committed/phase2_asm_${t}.csv" >> "$TMP/band.txt"
done

# ---------------------------------------------------------------------------
# CONSISTENCY: every cell SOURCE A shares with the frozen analyzer output must
# agree with it, or the extractor is wrong and nothing is emitted.
#
# The frozen CSV's min_of_mins_ns column is written by scripts/analyze/mo_analyze.sh with
# printf %.1f, i.e. the record carries ONE decimal. This extractor reads the raw
# min_ns out of the log (4 decimals), so agreement is not string equality: the
# check is |extracted - frozen| <= 0.051 ns, which is exactly the bound the
# record's own rounding can produce. It is not looser in any way that matters:
# a wrong campaign, a wrong impl/size mapping or a dropped rep moves a cell by
# far more than 0.051 ns, and the per-cell maxima are printed into the evidence
# so the agreement is visible rather than asserted.
# ---------------------------------------------------------------------------
mismatch=0
: > "$TMP/chk.txt"
for t in $TARGETS; do
    for impl in avx2 asm glibc; do
        awk -F, -v T="$t" -v I="$impl" 'NR > 1 && $1 == I { printf "%s|%s|%d|%.4f\n", T, $1, $4, $6 }' \
            "results/committed/phase2_asm_${t}.csv" | sort > "$TMP/chk.csv"
        awk -F'|' -v T="$t" -v I="$impl" '$1 == T && $2 == I { printf "%s|%s|%s|%.4f\n", $1, $2, $3, $4 }' \
            "$TMP/srcA.txt" | sort > "$TMP/chk.log"
        if ! awk -F'|' -v TGT="$t" -v IMPL="$impl" '
                FNR == NR { frozen[$1 "|" $2 "|" $3] = $4 + 0; next }
                {
                    k = $1 "|" $2 "|" $3
                    if (!((k) in frozen)) { printf "  MISSING from the frozen CSV: %s\n", k; bad = 1; next }
                    d = frozen[k] - ($4 + 0); if (d < 0) d = -d
                    ncheck++
                    if (d > maxd) { maxd = d; maxk = k }
                    if (d > 0.051) {
                        printf "  MISMATCH %s: frozen %.1f vs extracted %.4f (%.4f ns)\n", k, frozen[k], $4 + 0, d
                        bad = 1
                    }
                }
                END {
                    printf "  %-7s %-6s cells=%2d  max|frozen-extracted|=%.4f ns (%s)\n", TGT, IMPL, ncheck, maxd, maxk
                    exit (bad ? 1 : 0)
                }' "$TMP/chk.csv" "$TMP/chk.log" >> "$TMP/chk.txt" 2>&1; then
            cat "$TMP/chk.txt" >&2
            echo "gen_routing_table: extractor disagrees with the frozen analyzer output ($t/$impl)" >&2
            mismatch=1
        fi
    done
done
[ "$mismatch" = 0 ] || { echo "gen_routing_table: refusing to emit a table from an unvalidated extractor" >&2; exit 3; }

# ---------------------------------------------------------------------------
# WINNER SELECTION (SOURCE A decides), with margin against the runner-up.
# ---------------------------------------------------------------------------
awk -F'|' -v LADDER="$LADDER" -v SIZES="$SIZES" '
    { val[$1 "|" $2 "|" $3] = $4 + 0 }
    END {
        nl = split(LADDER, L, " "); nsz = split(SIZES, S, " ")
        for (t = 1; t <= 3; t++) {
            T = (t == 1) ? "znver2" : ((t == 2) ? "znver3" : "v3")
            for (si = 1; si <= nsz; si++) {
                n = S[si] + 0; best = 0; w = ""; second = 0
                for (li = 1; li <= nl; li++) {
                    k = T "|" L[li] "|" n
                    if (!((k) in val)) continue
                    v = val[k]
                    if (best == 0 || v < best) { second = best; best = v; w = L[li] } else if (v < second || second == 0) second = v
                }
                if (w == "") { print "gen_routing_table: no data for " T " " n > "/dev/stderr"; exit 1 }
                margin = (second > 0) ? 100 * (second - best) / second : 0
                printf "%s|%d|%s|%.4f|%.4f|%.2f\n", T, n, w, best, second, margin
            }
        }
    }
' "$TMP/srcA.txt" | sort -t'|' -k1,1 -k2,2n > "$TMP/winA.txt"

# same, from SOURCE B (for the comparison section only)
awk -F'|' -v LADDER="$LADDER" -v SIZES="$SIZES" '
    { val[$1 "|" $2 "|" $3] = $4 + 0 }
    END {
        nl = split(LADDER, L, " "); nsz = split(SIZES, S, " ")
        for (t = 1; t <= 3; t++) {
            T = (t == 1) ? "znver2" : ((t == 2) ? "znver3" : "v3")
            for (si = 1; si <= nsz; si++) {
                n = S[si] + 0; best = 0; w = ""; second = 0
                for (li = 1; li <= nl; li++) {
                    k = T "|" L[li] "|" n
                    if (!((k) in val)) continue
                    v = val[k]
                    if (best == 0 || v < best) { second = best; best = v; w = L[li] } else if (v < second || second == 0) second = v
                }
                if (w == "") continue
                margin = (second > 0) ? 100 * (second - best) / second : 0
                printf "%s|%d|%s|%.4f|%.4f|%.2f\n", T, n, w, best, second, margin
            }
        }
    }
' "$TMP/srcB.txt" | sort -t'|' -k1,1 -k2,2n > "$TMP/winB.txt"

# ---------------------------------------------------------------------------
# EMIT src/routing_table.h
# ---------------------------------------------------------------------------
{
    echo "/* src/routing_table.h — GENERATED by scripts/gen/gen_routing_table.sh. DO NOT EDIT."
    echo " *"
    echo " * Regenerate with: sh scripts/gen/gen_routing_table.sh"
    echo " * Evidence:        results/docs/analysis/phase2_hybrid_routing.txt (tracked)"
    echo " *"
    echo " * What it is: for each build target and each size class, the rung whose"
    echo " * min_of_mins was lowest when every ladder rung was measured in ONE window"
    echo " * set (Kernel 6 campaign: 9 windows x 5 reps, taskset -c 2). The values and"
    echo " * the margins are in the evidence file; nothing here was chosen by hand."
    echo " *"
    echo " * Size classes are the 13 sizes the Phase 2 campaigns measured (16 B .. 4 MiB)."
    echo " * 16 MiB / 64 MiB / 256 MiB were never measured; the lookup clamps, so they"
    echo " * route to the 4 MiB winner, and n < 16 routes to the 16 B winner."
    echo " *"
    echo " * ROUTING_TARGET_NAME must be defined by the build (the Makefile passes the"
    echo " * target token for every target); it selects which array is the table."
    echo " */"
    echo "#ifndef AVX2_MEM_ROUTING_TABLE_H"
    echo "#define AVX2_MEM_ROUTING_TABLE_H"
    echo ""
    echo "#include <stddef.h>"
    echo "#include \"common.h\""
    echo ""
    echo "typedef void *(*rung_fn)(void *dst, const void *src, size_t n);"
    echo ""
    echo "#define ROUTING_CLASSES 13"
    echo ""
    echo "/* Class byte bounds, ascending: a size routes to the LAST bound that is"
    echo " * <= it (rounded down to a measured point), clamped at both ends. */"
    printf 'static const size_t routing_class_bytes[ROUTING_CLASSES] = {'
    i=0
    for s in $SIZES; do
        i=$((i + 1))
        [ $((i % 4)) = 1 ] && printf '\n    '
        printf ' %su,' "$s"
    done
    printf '\n};\n\n'
    echo "/* n < 4096: (n >> 4) indexes this, so the small-size lookup is one load and"
    echo " * no loop. Generated from routing_class_bytes above, not typed in. */"
    echo "static const unsigned char routing_small[256] = {"
    awk -v SIZES="$SIZES" 'BEGIN {
        nsz = split(SIZES, S, " ")
        for (j = 0; j < 256; j++) {
            base = 16 * j; c = 0
            for (i = 1; i <= nsz; i++) if (S[i] + 0 <= base) c = i - 1
            printf "    %d,%s", c, (((j + 1) % 16 == 0) ? "\n" : "")
        }
    }'
    echo "};"
    echo ""
    for t in $TARGETS; do
        printf 'static const rung_fn routing_%s[ROUTING_CLASSES] = {\n' "$t"
        awk -F'|' -v T="$t" '$1 == T {
                c++
                printf "    memcpy_%s,%s", $3, ((c % 4 == 0) ? "\n" : "")
            }
            END { if (c % 4 != 0) printf "\n" }' "$TMP/winA.txt"
        printf '};\n'
        printf 'static const char *const routing_names_%s[ROUTING_CLASSES] = {' "$t"
        awk -F'|' -v T="$t" '$1 == T {
                c++
                printf "%s\"%s\",", ((c % 4 == 1) ? "\n    " : " "), $3
            }
            END { printf "\n};\n\n" }' "$TMP/winA.txt"
        printf '\n#define ROUTING_TABLE_FOR_%s routing_%s\n' "$t" "$t"
    done
    echo ""
    echo "#if !defined(ROUTING_TARGET_NAME)"
    echo "#error \"ROUTING_TARGET_NAME must name the build target (see the Makefile)\""
    echo "#endif"
    echo "/* Two levels are required: the operands of ## are not macro-expanded, so a"
    echo " * single paste would look for a symbol literally named"
    echo " * ROUTING_TABLE_FOR_ROUTING_TARGET_NAME. The indirection expands the target"
    echo " * token first, then pastes. (Caught by the first build of this rung.) */"
    echo "#define ROUTING_CONCAT_(a, b) a ## b"
    echo "#define ROUTING_CONCAT(a, b)  ROUTING_CONCAT_(a, b)"
    echo "#define ROUTING_TABLE       ROUTING_CONCAT(ROUTING_TABLE_FOR_, ROUTING_TARGET_NAME)"
    echo "#define ROUTING_TABLE_NAMES ROUTING_CONCAT(ROUTING_NAMES_FOR_, ROUTING_TARGET_NAME)"
    echo "#define ROUTING_NAMES_FOR_znver2 routing_names_znver2"
    echo "#define ROUTING_NAMES_FOR_znver3 routing_names_znver3"
    echo "#define ROUTING_NAMES_FOR_v3     routing_names_v3"
    echo ""
    echo "/* Provenance string, compiled in so the binary can state where its table"
    echo " * came from. */"
    echo "static const char routing_provenance[] ="
    echo "    \"min_of_mins per size per rung, Kernel 6 campaign (9 windows x 5 reps, cpu 2);\""
    echo "    \" routed rungs: $LADDER; source: results/docs/analysis/phase2_hybrid_routing.txt\";"
    echo ""
    echo "#endif /* AVX2_MEM_ROUTING_TABLE_H */"
} > "$OUT_H"

# ---------------------------------------------------------------------------
# EMIT the provenance file
# ---------------------------------------------------------------------------
{
echo "PHASE 2.5 HYBRID DISPATCHER - ROUTING TABLE PROVENANCE"
echo "Generated by scripts/gen/gen_routing_table.sh; every number below is printed from a"
echo "campaign log or a frozen committed CSV. Nothing is typed in."
echo ""
echo "================================================================================="
echo "1. SOURCES"
echo "================================================================================="
echo "SOURCE A (decides): the Kernel 6 campaign logs, which measured EVERY ladder rung"
echo "  plus glibc in one window set:"
for t in $TARGETS; do
    printf '  results/live/phase2_asm_campaign_%s.windows.log  windows=%s  rep_lines=%s  migrated=%s\n' \
        "$t" "$(grep -c '^# WINDOW' "${CAMPAIGN_LOGS}_$t.windows.log")" \
        "$(grep -c '^REPS' "${CAMPAIGN_LOGS}_$t.windows.log")" \
        "$(grep -c 'migrated=1' "${CAMPAIGN_LOGS}_$t.windows.log")"
done
echo "SOURCE B (checked, not used for the decision): union of results/committed/phase2_*.csv,"
echo "  ladder rungs only, each rung's best value across the campaigns it appears in."
echo "  Why it does not decide: each of those files is from a different campaign. The"
echo "  glibc 4 MiB row across them:"
awk -F, 'NR > 1 && $1 == "glibc" && $4 + 0 == 4194304 { printf "    glibc 4 MiB in %-34s %s ns\n", FILENAME, $6 }' \
    results/committed/phase2_*_znver2.csv
echo "  A spread like that, on one unchanged binary, is the reason the decision uses"
echo "  same-window numbers. Every disagreement is listed in section 4."
echo ""
echo "  Extractor validation: the three rungs SOURCE A shares with a frozen CSV from the"
echo "  SAME campaign (avx2, asm, glibc) were compared cell by cell - 3 targets x 13 sizes"
echo "  x 3 rungs. The frozen record carries one decimal, so the requirement is"
echo "  |extracted - frozen| <= 0.051 ns; a real error would move a cell by far more."
echo "  Per-rung result, from the run that generated this file:"
sed 's/^/    /' "$TMP/chk.txt"
echo ""
echo "  Routable rungs:  $LADDER"
echo "  Never routed to: $EXCLUDED"
echo ""
echo "================================================================================="
echo "2. SOURCE A - same-window min_of_mins (ns) per rung, winner, margin"
echo "================================================================================="
for t in $TARGETS; do
    printf '\n### %s\n\n' "$t"
    printf '| size |%s| winner | margin vs 2nd | null band | cell |\n' "$(for r in $LADDER; do printf ' %s |' "$r"; done)"
    printf '|--:|%s|:--|--:|--:|:--|\n' "$(for r in $LADDER; do printf '%s|' '--:'; done)"
    for s in $SIZES; do
        vals=""
        for r in $LADDER; do
            v=$(awk -F'|' -v T="$t" -v R="$r" -v N="$s" '$1==T && $2==R && $3==N {printf "%.1f", $4}' "$TMP/srcA.txt")
            [ -z "$v" ] && v="-"
            vals="$vals $v |"
        done
        line=$(awk -F'|' -v T="$t" -v N="$s" '$1==T && $2==N {print $3"|"$6}' "$TMP/winA.txt")
        w=${line%%|*}; m=${line##*|}
        band=$(awk -F'|' -v T="$t" -v N="$s" '$1==T && $2==N {printf "%.2f", $3}' "$TMP/band.txt")
        [ -z "$band" ] && band="n/a"
        cell=$(awk -v m="$m" -v b="$band" 'BEGIN { if (b == "n/a") print "-"; else if (m + 0 > b + 0) print "decided"; else print "coin-flip (margin <= band)" }')
        printf '| %s |%s %s | +%s%% | %s | %s |\n' "$s" "$vals" "$w" "$m" "$band" "$cell"
    done
done
echo ""
echo "================================================================================="
echo "3. SOURCE B - the same comparison from the frozen committed CSVs (mixed campaigns)"
echo "================================================================================="
for t in $TARGETS; do
    printf '\n### %s\n\n' "$t"
    printf '| size |%s| winner | margin vs 2nd |\n' "$(for r in $LADDER; do printf ' %s |' "$r"; done)"
    printf '|--:|%s|:--|--:|\n' "$(for r in $LADDER; do printf '%s|' '--:'; done)"
    for s in $SIZES; do
        vals=""
        for r in $LADDER; do
            v=$(awk -F'|' -v T="$t" -v R="$r" -v N="$s" '$1==T && $2==R && $3==N {printf "%.1f", $4}' "$TMP/srcB.txt")
            [ -z "$v" ] && v="-"
            vals="$vals $v |"
        done
        line=$(awk -F'|' -v T="$t" -v N="$s" '$1==T && $2==N {print $3"|"$6}' "$TMP/winB.txt")
        w=${line%%|*}; m=${line##*|}
        [ -z "$line" ] && { w="-"; m="-"; }
        printf '| %s |%s %s | +%s%% |\n' "$s" "$vals" "$w" "$m"
    done
done
echo ""
echo "================================================================================="
echo "4. DISAGREEMENTS BETWEEN THE TWO SOURCES (the table uses SOURCE A)"
echo "================================================================================="
nd=0
for t in $TARGETS; do
    for s in $SIZES; do
        a=$(awk -F'|' -v T="$t" -v N="$s" '$1==T && $2==N {print $3}' "$TMP/winA.txt")
        b=$(awk -F'|' -v T="$t" -v N="$s" '$1==T && $2==N {print $3}' "$TMP/winB.txt")
        if [ "$a" != "$b" ]; then
            va=$(awk -F'|' -v T="$t" -v N="$s" -v R="$a" '$1==T && $3==R && $2==N {print $4}' "$TMP/winA.txt")
            vb=$(awk -F'|' -v T="$t" -v N="$s" -v R="$b" '$1==T && $3==R && $2==N {print $4}' "$TMP/winB.txt")
            printf '  %-7s %-8s  A picks %-8s (%.1f ns)   B picks %-8s (%.1f ns)\n' "$t" "$s" "$a" "$va" "$b" "$vb"
            nd=$((nd + 1))
        fi
    done
done
[ "$nd" = 0 ] && echo "  none: the two sources agree at every size on every target"
printf '\n  %s disagreement(s) out of %s cells.\n' "$nd" "$(( $(echo "$TARGETS" | wc -w) * $(echo "$SIZES" | wc -w) ))"
echo ""
echo "================================================================================="
echo "5. THE EMITTED TABLE (src/routing_table.h), and the rule each size used"
echo "================================================================================="
for t in $TARGETS; do
    printf '\n### %s\n\n' "$t"
    printf 'class | bytes      | rung    | rule\n'
    printf '%s\n' '--:|--:|:--|:--'
    i=0
    for s in $SIZES; do
        r=$(awk -F'|' -v T="$t" -v N="$s" '$1==T && $2==N {print $3"|"$4}' "$TMP/winA.txt")
        rn=${r%%|*}; rv=${r##*|}
        nxt=$(awk -v i="$i" -v SIZES="$SIZES" 'BEGIN{ n = split(SIZES, S, " "); if (i + 2 <= n) print S[i+2] " B"; else print "above (clamped here)" }')
        printf '%s | %s | %s | winner at %s B (%.1f ns); covers up to %s\n' \
            "$i" "$s" "$rn" "$s" "$rv" "$nxt"
        i=$((i + 1))
    done
done
echo ""
echo "  Lookup rule as compiled (src/memcpy_hybrid.c): n < 4096 -> routing_small[n >> 4];"
echo "  otherwise walk the class bounds upwards. Both are generated from the same 13"
echo "  bounds above, and tools/route_check.c re-derives the mapping from the built"
echo "  object exhaustively (0 .. 8 MiB plus every boundary +-1) and prints it."
echo ""
echo "================================================================================="
echo "6. DOES ANY SINGLE RUNG WIN EVERYWHERE? (the hypothesis the hybrid tests)"
echo "================================================================================="
awk -F'|' -v LADDER="$LADDER" '
    { wins[$1 "|" $3]++; tot[$1]++; seen[$1 "|" $3] = 1 }
    END {
        nl = split(LADDER, L, " ")
        printf "  %-8s", "target"
        for (i = 1; i <= nl; i++) printf "%9s", L[i]
        printf "   sizes won\n"
        for (t = 1; t <= 3; t++) {
            T = (t == 1) ? "znver2" : ((t == 2) ? "znver3" : "v3")
            printf "  %-8s", T
            for (i = 1; i <= nl; i++) printf "%9d", wins[T "|" L[i]] + 0
            printf "   %d/%d sizes\n", tot[T], tot[T]
        }
    }
' "$TMP/winA.txt"
echo ""
echo "  A rung that wins 0 sizes is routed to nowhere; a rung that wins some is routed"
echo "  to exactly those. The hybrid can be at best the per-size minimum of this table,"
echo "  which is what section 6 of results/docs/analysis/phase2_hybrid_analysis.txt measures against"
echo "  its own campaign."
} > "$OUT_TXT"

echo "wrote $OUT_H ($(grep -c '' "$OUT_H") lines)"
echo "wrote $OUT_TXT ($(grep -c '' "$OUT_TXT") lines)"
