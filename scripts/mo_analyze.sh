#!/bin/sh
# scripts/mo_analyze.sh <log> <kernel> <prev_kernel> <target> [op]
#
#   scripts/mo_analyze.sh results/phase2_word_znver2.windows.log word naive znver2 memcpy
#
# Applies the approved protocol (results/MEASUREMENT_PROTOCOL.md) to one target's
# raw window log and prints markdown, then writes
# results/phase2_<kernel>_<target>.csv.
#
#   - min_of_mins and the cycles/byte of the rep that produced it, per size;
#   - delta vs the previous kernel and vs glibc, with the mandated 1.5% floor;
#   - THE NULL BAND: `naive` (kernel 1) and `ref_scalar` are two translation
#     units holding the same byte loop with byte-identical instruction streams
#     (results/codegen_diff_naive_*.diff), so the difference between them IS the
#     measurement artefact at that size, measured in these same windows. Any
#     claim smaller than that band is not a claim. Both verdicts are printed side
#     by side: the rule-3 verdict at the mandated 1.5% floor, and the band-aware
#     verdict at max(1.5%, band). Neither is hidden.
#   - per-key rule-2 verdict: the cumulative min_of_mins change between the last
#     two windows, which window first reached the final floor, and whether the
#     key may be claimed on at all.
#
# No number is entered by hand.
set -eu

LOG="${1:?usage: mo_analyze.sh <log> <kernel> <prev> <target> [op]}"
K="${2:?}"
PREV="${3:?}"
TARGET="${4:?}"
OP="${5:-memcpy}"

FLOOR="${FLOOR:-1.5}"
BAND_FLOOR="${BAND_FLOOR:-1}"
STABLE_PCT="${STABLE_PCT:-0.5}"
CSV="results/phase2_${K}_${TARGET}.csv"
mkdir -p results

awk -v K="$K" -v PREV="$PREV" -v TARGET="$TARGET" -v OP="$OP" \
    -v FLOOR="$FLOOR" -v BAND_FLOOR="$BAND_FLOOR" -v STABLE_PCT="$STABLE_PCT" \
    -v CSV="$CSV" '
function cls(n) {
    if (n < 64)        return "tiny_<64B"
    if (n <= 4096)     return "small_64B-4KB"
    if (n <= 65536)    return "mid_4KB-64KB"
    if (n <= 1048576)  return "big_64KB-1MB"
    return "huge_>1MB"
}
function clsname(c) {
    if (c == "tiny_<64B")     return "tiny (<64B)"
    if (c == "small_64B-4KB") return "small (64B-4KB)"
    if (c == "mid_4KB-64KB")  return "mid (4KB-64KB)"
    if (c == "big_64KB-1MB")  return "big (64KB-1MB)"
    return "huge (>1MB)"
}
function abs(x) { return (x < 0) ? -x : x }
function claim(d, fl) {
    if (abs(d) <= fl) return "within-noise"
    return (d > 0) ? "WIN" : "LOSS"
}
function median_of(D, n,   a, i, j, t) {
    if (n <= 0) return 0
    for (i = 1; i <= n; i++) a[i] = D[i]
    for (i = 1; i < n; i++)
        for (j = i + 1; j <= n; j++)
            if (a[j] < a[i]) { t = a[i]; a[i] = a[j]; a[j] = t }
    return (n % 2) ? a[(n + 1) / 2] : (a[n / 2] + a[n / 2 + 1]) / 2
}

/^# WINDOW/ { w = $3 + 0; if (w > winmax) winmax = w; next }
/^#/        { next }

/^REPS / {
    impl = $2; op = $3; n = 0; mn = -1; cp = -1; mig = 0
    for (i = 4; i <= NF; i++) {
        split($i, kv, "="); k = kv[1]; v = kv[2] + 0
        if (k == "n") n = v
        else if (k == "min_ns") mn = v
        else if (k == "cpb") cp = v
        else if (k == "migrated") mig = v
    }
    if (mn <= 0) next
    if (mig == 1) { nmig++; next }
    nrep++
    key = impl "|" op "|" n
    if (!((key) in gmin) || mn < gmin[key]) { gmin[key] = mn; gcpb[key] = cp }
    wk = w "|" key
    if (!((wk) in wmin) || mn < wmin[wk]) wmin[wk] = mn
    wseen[wk] = 1
    if (!((impl) in implseen)) { implseen[impl] = 1; nimpl++ }
    if (!((n) in sizeseen)) { sizeseen[n] = 1; nsizes++ }
    next
}

/,/ {
    split($0, cf, ",")     # not `f`: `f` is the floor-window scalar in END
    if (cf[1] == "impl") next
    if (cf[2] != "") logtarget = cf[2]
    next
}

END {
    if (winmax == 0) { print "mo_analyze: no windows found in log"; exit 1 }
    if (logtarget != "" && logtarget != TARGET)
        printf "WARNING: log says target=%s but %s was requested\n", logtarget, TARGET

    # `wa`, not `a`: `a` is used as a scalar later in this END block
    for (wk in wseen) { split(wk, wa, "|"); wcount[wa[2] "|" wa[3] "|" wa[4]]++ }

    for (n in sizeseen) S[++ns] = n + 0
    for (i = 1; i < ns; i++)
        for (j = i + 1; j <= ns; j++)
            if (S[j] < S[i]) { t = S[i]; S[i] = S[j]; S[j] = t }

    # ---- null band: naive vs ref_scalar are the same byte loop -------------
    # Cumulative min_of_mins per window, per key, so rule 2 and the band both
    # use the same cumulative definition of min_of_mins.
    for (key in gmin) {
        run = 0
        for (w = 1; w <= winmax; w++) {
            wk = w "|" key
            if ((wk) in wmin) { if (run == 0 || wmin[wk] < run) run = wmin[wk] }
            cum[w "|" key] = run
        }
    }
    for (si = 1; si <= ns; si++) {
        n = S[si]
        nk = "naive|" OP "|" n; rk = "ref_scalar|" OP "|" n
        if (((nk) in gmin) && ((rk) in gmin) && gmin[rk] > 0)
            band[n] = abs(100 * (gmin[rk] - gmin[nk]) / gmin[rk])
        else
            band[n] = -1
    }

    printf "target=%s op=%s kernel=%s prev=%s\n", TARGET, OP, K, PREV
    printf "windows=%d  rep_lines=%d  migrated_dropped=%d  impls=%d  sizes=%d\n\n",
           winmax, nrep, nmig, nimpl, nsizes

    printf "claim floors: mandated rule 3 = %.1f%%; band-aware = max(%.1f%%, null band)\n", FLOOR, FLOOR
    printf "null band = |naive vs ref_scalar|, two kernels with byte-identical instruction streams\n\n"

    # ---- detail by size ---------------------------------------------------
    printf "| size | %s min_ns | %s min_ns | glibc min_ns | %s c/B | ours:glibc | d vs %s | rule3 vs %s | d vs glibc | rule3 vs glibc | null band | band-aware vs %s |\n",
           PREV, K, K, PREV, PREV, PREV, PREV
    print  "|--:|--:|--:|--:|--:|--:|--:|:--|--:|:--|--:|:--|"
    for (si = 1; si <= ns; si++) {
        n = S[si]
        pk = PREV "|" OP "|" n; kk = K "|" OP "|" n; gk = "glibc|" OP "|" n
        if (!((kk) in gmin)) continue
        pm = (pk in gmin) ? gmin[pk] : 0
        gm = (gk in gmin) ? gmin[gk] : 0
        kc = ((kk) in gcpb) ? gcpb[kk] : 0
        dvp = pm > 0 ? 100 * (pm - gmin[kk]) / pm : 0
        dvg = gm > 0 ? 100 * (gm - gmin[kk]) / gm : 0
        bd = band[n]
        eff = (bd > FLOOR) ? bd : FLOOR
        printf "| %d | %.1f | %.1f | %.1f | %.4f | %.2fx | %+.2f%% | %s | %+.2f%% | %s | %s | %s |\n",
               n, pm, gmin[kk], gm, kc, (gm > 0 ? gmin[kk] / gm : 0),
               dvp, (pm > 0) ? claim(dvp, FLOOR) : "-",
               dvg, (gm > 0) ? claim(dvg, FLOOR) : "-",
               (bd >= 0) ? sprintf("%.2f%%", bd) : "n/a",
               (pm > 0) ? claim(dvp, eff) : "-"
        cl = cls(n)
        if (pm > 0) { B[cl "|prev", ++bn[cl "|prev"]] = dvp }
        if (gm > 0) { B[cl "|glib", ++bn[cl "|glib"]] = dvg }
    }

    # ---- size class roll-up ----------------------------------------------
    # The bucket must be copied into a dense array before taking a median: the
    # earlier version passed the whole aggregate array and every class median
    # read back as 0.00%.
    printf "\n| class | median d vs %s | median d vs glibc | sizes | vs %s (W/L/N) | vs glibc (W/L/N) |\n",
           PREV, PREV
    print  "|:--|--:|--:|--:|:--|:--|"
    ncl = split("tiny_<64B small_64B-4KB mid_4KB-64KB big_64KB-1MB huge_>1MB", CL, " ")
    for (ci = 1; ci <= ncl; ci++) {
        c = CL[ci]
        delete tmp
        np = bn[c "|prev"]; for (i = 1; i <= np; i++) tmp[i] = B[c "|prev", i]
        pv = median_of(tmp, np)
        delete tmp
        ng = bn[c "|glib"]; for (i = 1; i <= ng; i++) tmp[i] = B[c "|glib", i]
        gv = median_of(tmp, ng)
        if (np == 0 && ng == 0) continue
        delete W; delete L; delete N
        for (i = 1; i <= np; i++) { s = claim(B[c "|prev", i], FLOOR); if (s == "WIN") W["p"]++; else if (s == "LOSS") L["p"]++; else N["p"]++ }
        for (i = 1; i <= ng; i++) { s = claim(B[c "|glib", i], FLOOR); if (s == "WIN") W["g"]++; else if (s == "LOSS") L["g"]++; else N["g"]++ }
        printf "| %s | %+.2f%% | %+.2f%% | %d | %d/%d/%d | %d/%d/%d |\n",
               clsname(c), pv, gv, (np > ng ? np : ng),
               W["p"] + 0, L["p"] + 0, N["p"] + 0, W["g"] + 0, L["g"] + 0, N["g"] + 0
    }

    # ---- rule 2, per key --------------------------------------------------
    printf "\nrule 2 evidence for %s: cumulative min_of_mins per window (ns)\n", K
    printf "| size |"
    for (w = 1; w <= winmax; w++) printf " w%d |", w
    printf " floor reached at | last-2 change | rule 2 |\n|--:|"
    for (w = 1; w <= winmax; w++) printf "--:|"
    print " --:| --:|:--|"
    for (si = 1; si <= ns; si++) {
        n = S[si]
        kk = K "|" OP "|" n
        if (!((kk) in gmin)) continue
        printf "| %d |", n
        for (w = 1; w <= winmax; w++) {
            wk = w "|" kk
            if ((wk) in cum) printf " %.1f |", cum[wk]
            else             printf " - |"
        }
        f = 0
        for (w = 1; w <= winmax; w++) if ((w "|" kk) in cum && cum[w "|" kk] == gmin[kk]) { f = w; break }
        a = cum[(winmax - 1) "|" kk]; b = cum[winmax "|" kk]
        ch = (a > 0) ? 100 * (b - a) / a : 0
        printf " w%d | %+.4f%% | %s |\n", f, ch, (abs(ch) <= STABLE_PCT) ? "stable" : "UNSTABLE"
    }

    # ---- CSV -------------------------------------------------------------
    printf "impl,target,op,size_bytes,windows,min_of_mins_ns,cpb_at_min,gbps_at_min,delta_vs_prev_pct,delta_vs_glibc_pct,null_band_pct,claim_vs_prev,claim_vs_glibc,claim_band_vs_prev,floor_window,rule2_last2_change_pct,rule2\n" > CSV
    for (mi = 1; mi <= 3; mi++) {
        impl = (mi == 1) ? PREV : ((mi == 2) ? K : "glibc")
        for (si = 1; si <= ns; si++) {
            n = S[si]
            kk = impl "|" OP "|" n
            if (!((kk) in gmin)) continue
            kkP = PREV "|" OP "|" n; kkG = "glibc|" OP "|" n
            dvp = 0; dvg = 0
            if (impl != PREV && ((kkP) in gmin) && gmin[kkP] > 0) dvp = 100 * (gmin[kkP] - gmin[kk]) / gmin[kkP]
            if (impl != "glibc" && ((kkG) in gmin) && gmin[kkG] > 0) dvg = 100 * (gmin[kkG] - gmin[kk]) / gmin[kkG]
            cpv = ((kk) in gcpb) ? gcpb[kk] : 0
            gbs = (n > 0) ? n / gmin[kk] : 0
            bd = band[n]
            eff = (bd > FLOOR) ? bd : FLOOR
            f = 0
            for (w = 1; w <= winmax; w++) if ((w "|" kk) in cum && cum[w "|" kk] == gmin[kk]) { f = w; break }
            a = cum[(winmax - 1) "|" kk]; b = cum[winmax "|" kk]
            ch = (a > 0) ? 100 * (b - a) / a : 0
            r2 = (abs(ch) <= STABLE_PCT) ? "stable" : "UNSTABLE"
            csvp = (impl == PREV) ? "-" : claim(dvp, FLOOR)
            csvg = (impl == "glibc") ? "-" : claim(dvg, FLOOR)
            csvb = (impl == PREV) ? "-" : claim(dvp, eff)
            printf "%s,%s,%s,%d,%d,%.1f,%.4f,%.4f,%+.2f,%+.2f,%.2f,%s,%s,%s,%d,%+.4f,%s\n", impl, TARGET, OP, n, wcount[kk] + 0, gmin[kk], cpv, gbs, dvp, dvg, bd, csvp, csvg, csvb, f, ch, r2 > CSV
        }
    }
    printf "\nwrote %s\n", CSV
}
' "$LOG"
