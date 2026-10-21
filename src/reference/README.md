# src/reference — kernels kept for comparison, not shipped

**V9c** (`fast_v9c.c`, measured as `memcpy_avx2_final_v2_gcc_noxj`): the
predecessor to the current deliverable. It fixed the 16–64 B band with a
one-compare tiny path, and paid for it at 128 B and 256 B; F4 adds the dedicated
128–256 B window that recovers the 256 B class and halves the 128 B loss.

It is kept because `docs/NEGATIVE_RESULTS.md` and the analysis matrices reference
its numbers, and because a reader comparing the two releases should be able to
build both. It is **not built by default** and not audited by `make gate`: it is a
reference, not a shipped rung.

```sh
make reference        # build/reference/fast_v9c.o
```

The object defines `memcpy_fast_v9c`, so it can be linked beside `memcpy_fast`
without a collision. Its measured numbers against glibc: 16 B +38%, 32 B +12.9%,
64 B +12.9%, 128 B −20.1%, 256 B −16%, 512 B +13.5%, 1 MiB +8..13%.
