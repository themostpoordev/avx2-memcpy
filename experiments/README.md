# experiments/ — complete experiments whose results are negative

These directories hold experiments that were built, measured and rejected. They
are here so the negative results in `docs/NEGATIVE_RESULTS.md` can be checked by
reading the code and re-running the measurement, instead of being taken on
trust. They are not part of the library and are not built by default: the
default build (`make`) produces the benchmark binaries for the shipped rungs,
and `make gate` audits only those. `make experiments` builds what is in this
directory.

What is here:

| experiment | what it is | why it is here |
|--|--|--|
| `hybrid/` | a size-class dispatch layer over four ladder rungs, with a table generated from measured `min_of_mins` | it lost to glibc in 22 of 51 measured cells and only won 2, so the "glibc wins by dispatching" hypothesis is refuted from our own data |

The other rejected ideas (PREFETCHW, IFUNC, non-temporal stores as a general
choice, `rep movsb`, hand-written assembly against the compiler, PGO, LTO) are
covered in `docs/NEGATIVE_RESULTS.md` and, where a rung survives, in `src/rungs/`
— the evidence for them is the frozen CSVs in `docs/data/` plus that document,
so they do not need a separate tree here.

Build and run:

```sh
make experiments                 # hybrid + its route checker (znver2 by default)
./experiments/hybrid/route_check # class-mapping and per-class correctness, no clocks
./experiments/hybrid/bench_hybrid --list
```

`make experiments EXP_T=znver3` compiles the other target's routing table; the
header carries all three.
