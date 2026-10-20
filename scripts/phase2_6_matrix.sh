#!/bin/sh
# scripts/phase2_6_matrix.sh <log> [baseline-impl] [op]
#
#   sh scripts/phase2_6_matrix.sh results/phase2_6_screen3_znver2.windows.log glibc memcpy
#
# prints, for every (impl, size) in the log, the quantities the frozen protocol
# defines, in one greppable line per pair:
#
#   size impl min_of_mins_ns gbps delta_vs_baseline_pct rule3_verdict band_verdict
#        tier tier_floor_pct tier_verdict spread_pct windows
#
# Definitions are copied from scripts/mo_analyze.sh, NOT re-invented, so a number
# here and a number there cannot disagree:
#   min_of_mins   the smallest min_ns over ALL reps in ALL windows for that pair
#                 (reps with migrated=1 dropped; the log records whether any were);
#   spread        (max - min) of the PER-WINDOW minimum over that pair's windows,
#                 as a percentage of the minimum - rule 2's stability measure;
#   tier          A when spread <= 10%, else B (protocol section 3c);
#   band          |naive vs ref_scalar| at that size, two TUs holding the same
#                 byte loop with byte-identical instruction streams, measured in
#                 these same windows - the measurement artefact, not a result;
#   rule3 floor   the mandated flat 1.5%;
#   tier floor    max(1.5%, band) for Tier A, 25% flat for Tier B;
#   verdicts      "measured" when the delta is inside the applicable floor (NOT a
#                 win, NOT a loss, NOT a tie-breaker), otherwise WIN or LOSS.
#
# Why a second analyser at all: mo_analyze.sh is built around one kernel against
# one predecessor and one baseline (3 impls), which is what a ladder rung needs.
# A screening campaign measures ten candidates in the same windows against the
# same baseline, and comparing them one at a time would either bury the matrix or
# require ten separate runs with ten different "prev" columns. Both tools read the
# same raw log and share these definitions; neither is a replacement for the
# other, and the committed CSVs still come from mo_analyze.sh.
#
# No number is entered by hand. Every input is a raw window log.
set -eu

LOG="${1:?usage: phase2_6_matrix.sh <log> [baseline] [op]}"
BASE="${2:-glibc}"
OP="${3:-memcpy}"

FLOOR="${FLOOR:-1.5}"
TIERB_FLOOR="${TIERB_FLOOR:-25}"
SPREAD_MAX="${SPREAD_MAX:-10}"

awk -v LOG="$LOG" -v BASE="$BASE" -v OP="$OP" -v FLOOR="$FLOOR" \
    -v TIERB="$TIERB_FLOOR" -v SPREAD_MAX="$SPREAD_MAX" '
function abs(x) { return (x < 0) ? -x : x }
function tier_of(sp)  { return (sp <= SPREAD_MAX) ? "A" : "B" }
function floor_of(ti, bd,   e) {
    if (ti == "B") return TIERB
    e = (bd > FLOOR) ? bd : FLOOR
    return e
}
function verdict(d, fl) {
    if (abs(d) <= fl) return "measured"
    return (d > 0) ? "WIN" : "LOSS"
}
function cls(n) {
    if (n < 64)       return "tiny"
    if (n <= 4096)    return "small"
    if (n <= 65536)   return "mid"
    if (n <= 1048576) return "big"
    return "huge"
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
    if (op != WANTOP) next
    if (mn <= 0) next
    if (mig == 1) { nmig++; next }
    nrep++
    key = impl "|" n
    if (!((key) in gmin) || mn < gmin[key]) { gmin[key] = mn; gcpb[key] = cp }
    wk = w "|" key
    if (!((wk) in wmin) || mn < wmin[wk]) wmin[wk] = mn
    wseen[wk] = 1
    if (!((impl) in implseen)) { implseen[impl] = 1; impls[++nimpl] = impl }
    if (!((n) in sizeseen)) { sizeseen[n] = 1; sizes[++nsz] = n }
    next
}
BEGIN { WANTOP = OP }

END {
    if (winmax == 0) { print "phase2_6_matrix: no windows in " LOG > "/dev/stderr"; exit 1 }
    # sort sizes and impls (ASCII sort would put 1024 before 128; sort numerically)
    for (i = 1; i < nsz; i++)
        for (j = i + 1; j <= nsz; j++)
            if (sizes[j] < sizes[i]) { t = sizes[i]; sizes[i] = sizes[j]; sizes[j] = t }
    for (i = 1; i <= nsz; i++) S[i] = sizes[i]
    # impls in the order they first appear in the log, which is the order the
    # campaign asked for them - so the matrix columns match the command line.
    printf "# log=%s  baseline=%s  op=%s  windows=%d  rep_lines=%d  migrated_dropped=%d\n",
           LOG, BASE, OP, winmax, nrep, nmig
    printf "# null band = |naive vs ref_scalar| in THESE windows (same byte loop, identical instruction streams)\n"
    printf "# columns: size impl min_of_mins_ns gbps cpb delta_vs_%s_pct rule3 band tier floor tier_verdict spread_pct windows\n", BASE
    for (si = 1; si <= nsz; si++) {
        n = S[si]
        nk = "naive|" n; rk = "ref_scalar|" n
        bd = -1
        if (((nk) in gmin) && ((rk) in gmin) && gmin[rk] > 0)
            bd = abs(100 * (gmin[rk] - gmin[nk]) / gmin[rk])
        bm = (BASE "|" n)
        printf "# band %d = %s\n", n, (bd >= 0) ? sprintf("%.2f%%", bd) : "NOT MEASURED (naive/ref_scalar absent)"
        for (ii = 1; ii <= nimpl; ii++) {
            impl = impls[ii]
            key = impl "|" n
            if (!((key) in gmin)) continue
            lo = 0; hi = 0; nw = 0; first = 0
            for (w = 1; w <= winmax; w++) {
                wk = w "|" key
                if (!((wk) in wmin)) continue
                nw++
                if (lo == 0 || wmin[wk] < lo) lo = wmin[wk]
                if (wmin[wk] > hi) hi = wmin[wk]
            }
            sp = (lo > 0) ? 100 * (hi - lo) / lo : 0
            ti = tier_of(sp)
            fl = floor_of(ti, bd)
            d = 0
            if (impl != BASE && ((bm) in gmin) && gmin[bm] > 0)
                d = 100 * (gmin[bm] - gmin[key]) / gmin[bm]
            gb = (n > 0) ? n / gmin[key] : 0
            eff = (bd > FLOOR) ? bd : FLOOR
            printf "%d %s %.1f %.4f %.4f %+.2f %s %s %s %.1f %s %.2f %d\n",
                   n, impl, gmin[key], gb, ((key) in gcpb) ? gcpb[key] : 0,
                   d,
                   (impl == BASE) ? "-" : verdict(d, FLOOR),
                   (impl == BASE) ? "-" : ((bd >= 0) ? verdict(d, eff) : "n/a"),
                   ti, fl, (impl == BASE) ? "-" : verdict(d, fl),
                   sp, nw
        }
    }
}
' "$LOG"
