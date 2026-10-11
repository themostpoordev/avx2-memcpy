#!/bin/sh
# scripts/mo_analyze.sh <log> <kernel> <prev_kernel> <target> [op]
#
#   scripts/mo_analyze.sh results/phase2_word_znver2.windows.log word naive znver2 memcpy
#
# Applies the approved protocol (results/MEASUREMENT_PROTOCOL.md) to one target's
# raw window log and prints markdown:
#
#   - min_of_mins and the cycles/byte of the rep that produced it, per size, for
#     <prev_kernel>, <kernel> and glibc;
#   - delta vs previous kernel and vs glibc, with the 1.5% claim floor applied
#     (WIN / LOSS / within-noise);
#   - the same rolled up per size class;
#   - per-window min_of_mins for <kernel>, i.e. the evidence that rule 2's
#     stability stop was actually met rather than asserted.
#
# Writes results/phase2_<kernel>_<target>.csv. No number is entered by hand.
set -eu

LOG="${1:?usage: mo_analyze.sh <log> <kernel> <prev> <target> [op]}"
K="${2:?}"
PREV="${3:?}"
TARGET="${4:?}"
OP="${5:-memcpy}"

FLOOR="${FLOOR:-1.5}"
CSV="results/phase2_${K}_${TARGET}.csv"
mkdir -p results

awk -v K="$K" -v PREV="$PREV" -v TARGET="$TARGET" -v OP="$OP" \
    -v FLOOR="$FLOOR" -v CSV="$CSV" '
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
function claim(d) {
    ad = (d < 0) ? -d : d
    if (ad <= FLOOR) return "within-noise"
    return (d > 0) ? "WIN" : "LOSS"
}
function median(A, n,   b, i, j, t) {
    if (n <= 0) return 0
    for (i = 1; i <= n; i++) b[i] = A[i]
    for (i = 1; i < n; i++)
        for (j = i + 1; j <= n; j++)
            if (b[j] < b[i]) { t = b[i]; b[i] = b[j]; b[j] = t }
    return (n % 2) ? b[(n + 1) / 2] : (b[n / 2] + b[n / 2 + 1]) / 2
}

/^# WINDOW/ { w = $3 + 0; if (w > winmax) winmax = w; next }
/^#/        { next }

/^REPS / {
    impl = $2; op = $3; n = 0; mn = -1; rep = 0; cp = -1; mig = 0
    for (i = 4; i <= NF; i++) {
        split($i, kv, "="); k = kv[1]; v = kv[2] + 0
        if (k == "n") n = v
        else if (k == "min_ns") mn = v
        else if (k == "rep") rep = v
        else if (k == "cpb") cp = v
        else if (k == "migrated") mig = v
    }
    if (mn <= 0) next
    if (mig == 1) { nmig++; next }        # rule 5: a migrated rep is unusable
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
    split($0, f, ",")
    if (f[1] == "impl") next
    if (f[2] != "") logtarget = f[2]
    next
}

END {
    if (winmax == 0) { print "mo_analyze: no windows found in log"; exit 1 }
    if (logtarget != "" && logtarget != TARGET)
        printf "WARNING: log says target=%s but %s was requested\n", logtarget, TARGET

    # window count per (impl,op,size)
    for (wk in wseen) { split(wk, a, "|"); wcount[a[2] "|" a[3] "|" a[4]]++ }

    # sorted size list
    for (n in sizeseen) S[++ns] = n + 0
    for (i = 1; i < ns; i++)
        for (j = i + 1; j <= ns; j++)
            if (S[j] < S[i]) { t = S[i]; S[i] = S[j]; S[j] = t }

    printf "target=%s op=%s kernel=%s prev=%s\n", TARGET, OP, K, PREV
    printf "windows=%d  rep_lines=%d  migrated_dropped=%d  impls=%d  sizes=%d\n\n",
           winmax, nrep, nmig, nimpl, nsizes
    printf "claim floor: |delta min_of_mins| > %.1f%%  (rule 3)\n\n", FLOOR

    # ---- detail by size -------------------------------------------------
    printf "| size | %s min_ns | %s min_ns | glibc min_ns | %s c/B | %s c/B | glibc c/B | d vs %s | d vs glibc | vs %s | vs glibc |\n",
           PREV, K, K, PREV, PREV, PREV
    print  "|--:|--:|--:|--:|--:|--:|--:|--:|--:|:--|:--|"
    for (si = 1; si <= ns; si++) {
        n = S[si]
        pk = PREV "|" OP "|" n; kk = K "|" OP "|" n; gk = "glibc|" OP "|" n
        if (!((kk) in gmin)) continue
        pm = (pk in gmin) ? gmin[pk] : 0
        gm = (gk in gmin) ? gmin[gk] : 0
        pc = (pk in gcpb) ? gcpb[pk] : 0
        gc = (gk in gcpb) ? gcpb[gk] : 0
        kc = gcpb[kk]
        dvp = pm > 0 ? 100 * (pm - gmin[kk]) / pm : 0
        dvg = gm > 0 ? 100 * (gm - gmin[kk]) / gm : 0
        printf "| %d | %.1f | %.1f | %.1f | %.4f | %.4f | %.4f | %+.2f%% | %+.2f%% | %s | %s |\n",
               n, pm, gmin[kk], gm, kc, pc, gc, dvp, dvg,
               (pk in gmin) ? claim(dvp) : "-", (gk in gmin) ? claim(dvg) : "-"
        if (!((n) in cseen)) { cseen[n] = 1 }
        cl = cls(n)
        if (pk in gmin) { A[cl "|prev", ++ap[cl "|prev"]] = dvp }
        if (gk in gmin) { A[cl "|glib", ++ap[cl "|glib"]] = dvg }
    }

    # ---- size class roll-up ---------------------------------------------
    printf "\n| class | median d vs %s | median d vs glibc | sizes | vs %s (W/L/N) | vs glibc (W/L/N) |\n",
           PREV, PREV
    print  "|:--|--:|--:|--:|:--|:--|"
    ncl = split("tiny_<64B small_64B-4KB mid_4KB-64KB big_64KB-1MB huge_>1MB", CL, " ")
    for (ci = 1; ci <= ncl; ci++) {
        c = CL[ci]
        pv = median(A, ap[c "|prev"]); gv = median(A, ap[c "|glib"])
        np = ap[c "|prev"]; ng = ap[c "|glib"]
        if (np == 0 && ng == 0) continue
        delete W; delete L; delete N
        for (i = 1; i <= np; i++) { s = claim(A[c "|prev", i]); if (s == "WIN") W["p"]++; else if (s == "LOSS") L["p"]++; else N["p"]++ }
        for (i = 1; i <= ng; i++) { s = claim(A[c "|glib", i]); if (s == "WIN") W["g"]++; else if (s == "LOSS") L["g"]++; else N["g"]++ }
        printf "| %s | %+.2f%% | %+.2f%% | %d | %d/%d/%d | %d/%d/%d |\n",
               clsname(c), pv, gv, (np > ng ? np : ng),
               W["p"] + 0, L["p"] + 0, N["p"] + 0, W["g"] + 0, L["g"] + 0, N["g"] + 0
    }

    # ---- rule 2 evidence: per-window min_of_mins for the kernel ----------
    printf "\nper-window min_of_mins for %s (ns) — rule 2 evidence\n", K
    printf "| size |"
    for (w = 1; w <= winmax; w++) printf " w%d |", w
    printf " spread across windows | windows counted |\n|--:|"
    for (w = 1; w <= winmax; w++) printf "--:|"
    print " --:|--:|"
    for (si = 1; si <= ns; si++) {
        n = S[si]
        kk = K "|" OP "|" n
        if (!((kk) in gmin)) continue
        printf "| %d |", n
        lo = 0; hi = 0
        for (w = 1; w <= winmax; w++) {
            wk = w "|" kk
            if ((wk) in wmin) {
                printf " %.1f |", wmin[wk]
                if (lo == 0 || wmin[wk] < lo) lo = wmin[wk]
                if (wmin[wk] > hi) hi = wmin[wk]
            } else printf " - |"
        }
        printf " %.2f%% | %d |\n", lo > 0 ? 100 * (hi - lo) / lo : 0, wcount[kk]
    }

    # ---- CSV -------------------------------------------------------------
    printf "impl,target,op,size_bytes,windows,min_of_mins_ns,cpb_at_min,gbps_at_min,delta_vs_prev_pct,delta_vs_glibc_pct,claim_vs_prev,claim_vs_glibc\n" > CSV
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
            csvp = (impl == PREV) ? "-" : claim(dvp)
            csvg = (impl == "glibc") ? "-" : claim(dvg)
            printf "%s,%s,%s,%d,%d,%.1f,%.4f,%.4f,%+.2f,%+.2f,%s,%s\n", impl, TARGET, OP, n, wcount[kk] + 0, gmin[kk], cpv, gbs, dvp, dvg, csvp, csvg > CSV
        }
    }
    printf "\nwrote %s\n", CSV
}
' "$LOG"
