# Phase-2 ladder window logs

These are the raw window logs behind the phase-2 reference-ladder CSVs in
[`docs/data/`](../). They ship so that the "The ladder" table in
[`docs/RESULTS.md`](../../RESULTS.md) is reconstructable from the repo alone,
not only from the committed CSVs — the same standard
[METHODOLOGY.md §11](../../METHODOLOGY.md#11-reproducibility-gate) sets for the
shipped kernel's rows.

## Which log backs which CSV

Each CSV is the output of `scripts/analyze.sh <log> <K> <PREV> <target> memcpy`.
Re-running that command on the log named below reproduces the CSV.

| CSV | log | `K` | `PREV` |
|---|---|---|---|
| `phase2_naive_<t>.csv` | `phase2_naive_<t>.windows.log` | `naive` | `ref_scalar` |
| `phase2_word_<t>.csv` | `phase2_word_<t>.windows.log` | `word` | `naive` |
| `phase2_sse2_<t>.csv` | `phase2_sse2_<t>.windows.log` | `sse2` | `word` |
| `phase2_sse2_longwin_<t>.csv` | `phase2_avx2_campaign_<t>.windows.log` | `sse2` | `word` |
| `phase2_avx2_<t>.csv` | `phase2_avx2_campaign_<t>.windows.log` | `avx2` | `sse2` |
| `phase2_avx2_pf2_<t>.csv` | `phase2_avx2_campaign_<t>.windows.log` | `avx2_pf2` | `avx2` |
| `phase2_avx2_pf4_<t>.csv` | `phase2_avx2_campaign_<t>.windows.log` | `avx2_pf4` | `avx2` |
| `phase2_avx2_nt_<t>.csv` | `phase2_avx2_nt_campaign_<t>.windows.log` | `avx2_nt` | `avx2` |
| `phase2_asm_<t>.csv` | `phase2_asm_campaign_<t>.windows.log` | `asm` | `avx2` |

`<t>` is `znver2`, `znver3` or `v3`. Eighteen logs back twenty-seven CSVs: the
avx2 campaign log carries four families at once (`avx2`, `avx2_pf2`,
`avx2_pf4` and the long-window `sse2` variant of the `sse2_longwin` CSV).

The mapping was established by recomputing each CSV's `min_of_mins` from every
candidate log under the protocol's own rule and requiring a cell-for-cell
match; every CSV had exactly one such log, so the mapping is unambiguous.

## Verifying

```sh
sh scripts/analyze.sh docs/data/raw/ladder/phase2_avx2_campaign_znver2.windows.log \
   avx2 sse2 znver2 memcpy
diff docs/data/phase2_avx2_znver2.csv results/live/phase2_avx2_znver2.csv
```

## One known schema difference

Twenty of the twenty-seven CSVs reproduce **byte-for-byte** from their log.
The `naive` and `word` families (six CSVs) reproduce with an **identical
`min_of_mins` column** but a shorter schema: they were analysed before the
two-tier claim model added the `tier`, `tier_floor_pct`, `tier_verdict_vs_prev`
and `tier_verdict_vs_glibc` columns to `scripts/analyze.sh`, and they were
committed as they stood. The committed files are frozen records and have not
been rewritten; the missing tier columns for those six rungs are a gap in the
committed CSVs, not in the logs behind them.
