# ---------------------------------------------------------------------------
# AVX2 memcpy/memset benchmark harness
# Plain Makefile. clang only. No CMake/meson/autotools. No external runtime.
#
# Three codegen targets are produced so the znver2-vs-znver3-vs-v3 question is
# a measured deliverable, not an assumption:
#   bench/bench_znver2   (matches the measured silicon: 2x256b load/cyc,
#                         1x256b store/cyc  -> Zen2 class)
#   bench/bench_znver3   (as originally prescribed)
#   bench/bench_v3       (-march=x86-64-v3 generic baseline)
#
# Usage:
#   make            build all three binaries
#   make TARGET=znver2 single   build only one (TARGET in {znver2,znver3,v3})
#   make perf                   run scripts/run_perf.sh (PMU counter validation)
#   make objdump                dump per-target disassembly for codegen diffing
#   make plot                   report plotting toolchain status
#   make clean
# ---------------------------------------------------------------------------

CC      := clang
STD     := -std=c11
WARN    := -Wall -Wextra -Wno-unused-parameter
OPT     := -O3 -fomit-frame-pointer
# We reimplement memcpy/memset; never let the compiler synthesize libc calls
# or fold our loops into them. This is a correctness-of-measurement flag.
NOBUILTIN := -fno-builtin-memcpy -fno-builtin-memset -fno-builtin-memmove -fno-builtin-bzero
INC     := -Isrc -Ibench
LDFLAGS := -static -lm

FLAGS_znver2 := -march=znver2 -mtune=znver2 -mavx2 -mfma
FLAGS_znver3 := -march=znver3 -mtune=znver3 -mavx2 -mfma
FLAGS_v3     := -march=x86-64-v3 -mtune=generic -mavx2 -mfma

TARGETS := znver2 znver3 v3

# Kernels whose loops MUST stay scalar. clang -O3 -march=znver2 turns a byte
# loop into a 4x32B vmovups loop otherwise, which would make a "naive" baseline
# silently be an AVX2 kernel and make every comparison meaningless.
SCALAR_SRCS  := src/ref_scalar.c src/memcpy_naive.c src/memcpy_word.c
SCALAR_FLAGS := -fno-vectorize -fno-slp-vectorize -fno-unroll-loops

# Kernel 3 must really be 128-bit SSE2: the ladder's point is that rung 3 is 16B-wide
# rung 4 is 32B-wide. With -mavx2 in the target flags, clang's SLP vectorizer
# merged two adjacent _mm_loadu_si128 intrinsics into ONE 32-byte vmovups, so the
# "SSE2" rung compiled to ymm registers and was really rung 4. Caught by reading
# the generated assembly before any timing was taken. Disabling AVX for this TU
# forces the legacy 128-bit encodings (movdqu) and keeps the ladder monotonic;
# the AVX2 rung that follows is built with AVX enabled, so rung3-vs-rung4 measures
# exactly the width step it is supposed to.
SSE2_SRCS  := src/memcpy_sse2.c
SSE2_FLAGS := -mno-avx -mno-avx2 -mno-fma -fno-slp-vectorize -fno-vectorize

# Any new src/*.c kernel is picked up automatically (no Makefile edit per kernel).
SRCS   := $(wildcard src/*.c) $(wildcard bench/*.c)
SRC_OBJ := $(patsubst %.c,%.o,$(SRCS))
# Hand-written assembly rungs (src/*.S) are picked up the same way, so a kernel
# written in asm needs no per-kernel Makefile edit either. Tooling is clang's
# INTEGRATED assembler in GAS/AT&T syntax: no nasm (not installed, and installing
# one is not permitted). The capital .S means the file also goes through the C
# preprocessor, which is what the kernel's comments rely on.
ASM_SRCS := $(wildcard src/*.S)
ASM_OBJ  := $(patsubst %.S,%.o,$(ASM_SRCS))
SRC_OBJ  += $(ASM_OBJ)
BINS   := $(addprefix bench/bench_,$(TARGETS))

TARGET ?= znver2
ifeq ($(filter $(TARGET),$(TARGETS)),)
$(error TARGET must be one of: $(TARGETS))
endif

.PHONY: all bench single perf objdump plot clean list audit audit-width gate snapshot ntprobe routecheck \
        perf6probe perf6gen \
        $(addprefix ntprobe-,$(TARGETS)) $(addprefix perf6probe-,$(TARGETS)) \
        $(addprefix perf6gen-,$(TARGETS))

all: $(BINS)
bench: $(BINS)

# ---- per-target object + link rules ---------------------------------------
# build/<t>/<path>.o  from  <path>.c   with the flags of target <t>.
define TARGET_RULES
build/$(1)/%.o: %.c
	@mkdir -p $$(dir $$@)
	$$(CC) $$(STD) $$(OPT) $$(WARN) $$(NOBUILTIN) $$(FLAGS_$(1)) $$(INC) \
	  -DTARGET_NAME='"$(1)"' $$(if $$(filter $$<,$$(SCALAR_SRCS)),$$(SCALAR_FLAGS),) \
	  $$(if $$(filter $$<,$$(SSE2_SRCS)),$$(SSE2_FLAGS),) \
	  -DROUTING_TARGET_NAME=$(1) -MMD -MP -c $$< -o $$@

# Assembly rung: same per-target -march/-mavx2 flags, so the integrated assembler
# accepts the AVX2 it is told to use. -std/-fno-builtin are C concepts and are
# deliberately not passed here.
build/$(1)/%.o: %.S
	@mkdir -p $$(dir $$@)
	$$(CC) $$(OPT) $$(WARN) $$(FLAGS_$(1)) $$(INC) -c $$< -o $$@

bench/bench_$(1): $$(addprefix build/$(1)/,$$(SRC_OBJ))
	@mkdir -p bench
	$$(CC) $$(STD) $$(OPT) $$(WARN) $$(NOBUILTIN) $$(FLAGS_$(1)) $$(INC) \
	  -DTARGET_NAME='"$(1)"' $$^ $$(LDFLAGS) -o $$@
endef

$(foreach t,$(TARGETS),$(eval $(call TARGET_RULES,$(t))))

# ---- gcc-built rungs (Phase 2.6 V1/V6/V7) ---------------------------------
# Every other src/*.c is built by clang. Files named src/*_gcc.c (and
# src/*_gcc_*.c, for flag variants of one gcc rung) are built by gcc instead, via
# EXPLICIT per-source rules: an explicit rule beats the pattern rule above, so
# there is no ambiguity about which compiler produced which object. The flags are
# the same shape as the clang ones - same per-target -march/-mtune/-mavx2/-mfma,
# same -fno-builtin so neither compiler is allowed to call libc's memcpy for us -
# so a measured difference between an _gcc rung and its clang twin is the
# compiler, not the flag set. GCC_EXTRA_<file> adds per-rung flags (V6).
CC_GCC := gcc
GCC_SRCS := $(wildcard src/*_gcc.c) $(wildcard src/*_gcc_*.c)

# ---- V6 per-rung compiler flags -------------------------------------------
# Each _gcc_<flag> rung is memcpy_avx2_final_gcc.c's source plus exactly one flag,
# so the measured per-flag delta is the flag. Adopted only if `--verify` and `make
# gate` still pass with the flag on (a flag that changes the answer is not a
# speed flag).
GCC_EXTRA_memcpy_avx2_final_gcc_ofast.c  := -Ofast
GCC_EXTRA_memcpy_avx2_final_gcc_unroll.c := -funroll-loops
GCC_EXTRA_memcpy_avx2_final_gcc_noplt.c  := -fno-plt
# -fomit-frame-pointer is already in OPT for every rung, so the "gain from it"
# cannot be measured by adding it. This rung forces it OFF instead: if it is
# slower by X%, the flag is worth X%. (See the source header.)
GCC_EXTRA_memcpy_avx2_final_gcc_noomit.c := -fno-omit-frame-pointer
# V9c: V9b's small path costs 256 B one step because gcc cross-jumped the
# T_BLOCKPAIR(2) and T_BLOCKPAIR(4) heads (see the source header). This rung
# forbids that one transform and nothing else.
GCC_EXTRA_memcpy_avx2_final_v2_gcc_noxj.c := -fno-crossjumping
# F1-F4 (the 128/256 B pass) are all measured against V9c, so they carry its
# one flag too; their own knob is in the source's TUNED_* set, not here.
GCC_EXTRA_memcpy_avx2_final_f1_gcc.c      := -fno-crossjumping
GCC_EXTRA_memcpy_avx2_final_f3_gcc.c      := -fno-crossjumping
GCC_EXTRA_memcpy_avx2_final_f4_gcc.c      := -fno-crossjumping
GCC_EXTRA_memcpy_avx2_final_ifunc_gcc.c   := -fno-crossjumping

# NOTE: $$(dir $$@) and the flag variables need the DOUBLE dollar - a single $ is
# consumed by make, not by the shell (the same trap documented for `snapshot`).
define GCC_RULE_ONE
build/$(1)/$(2:.c=.o): $(2)
	@mkdir -p $$(dir $$@)
	$$(CC_GCC) $$(STD) $$(OPT) $$(WARN) $$(NOBUILTIN) $$(FLAGS_$(1)) $$(INC) \
	  -DTARGET_NAME='"$(1)"' -fno-strict-aliasing $$(GCC_EXTRA_$(notdir $(2))) \
	  -MMD -MP -c $$< -o $$@
endef

$(foreach t,$(TARGETS),$(foreach s,$(GCC_SRCS),$(eval $(call GCC_RULE_ONE,$(t),$(s)))))

# ---- Phase 2.6 V6/V7 probe binary: -flto and PGO, one binary ----------------
# -flto and PGO are whole-TU / link-time properties, so their rungs cannot be
# rows in the normal bench_<t> binaries: the honest control for either is a
# binary holding BOTH the variant object and its non-variant twin, so the two are
# measured interleaved inside one process (protocol rule 4) in the same windows.
# `make perf6probe-<t>` therefore builds bench/bench_<t>_perf6 out of every normal
# object, plus build/<t>/src/perf6/{memcpy_avx2_final_lto,memcpy_avx2_final_pgo}.o,
# plus a registry.o recompiled with -DPERF6_PROBE, which appends exactly two rows
# (avx2_final_lto, avx2_final_pgo) and nothing else.
#
# The two sources live in src/perf6/, not src/, so the normal wildcard build never
# sees them and `make bench` never depends on profile data existing. They are
# still width-gated: scripts/audit_width.sh walks src/perf6/*.c too, so "not in
# the ladder binaries" does not mean "not checked".
#
# PGO is two stages and both write the SAME object path on purpose: gcc derives
# the .gcda name from the object path, so a -fprofile-use object built anywhere
# else compiles with no profile at all (a warning, not an error - exactly the
# silent no-op this project keeps getting bitten by). Stage 1 is a workload
# binary only: an instrumented kernel is slower by construction, so it is never
# benchmarked. Stage 2 is what enters a claim.
PERF6_SRCS   := src/perf6/memcpy_avx2_final_lto.c src/perf6/memcpy_avx2_final_pgo.c
PERF6_LADDER := 16,32,64,128,256,512,1024,4096,16384,65536,262144,1048576,4194304

# NOTE: $$(...) needs the double dollar everywhere below - a single $ is eaten by
# make, not by the shell (same trap as the ntprobe and snapshot recipes).
define PERF6_TARGET
perf6gen-$(1): $(BINS)
	@mkdir -p build/$(1)/src/perf6 build/$(1)/probe results
	@rm -f build/$(1)/src/perf6/memcpy_avx2_final_pgo.o build/$(1)/src/perf6/memcpy_avx2_final_pgo.gcda
	@echo "perf6[$(1)] stage 1: instrumented kernel (-fprofile-generate) and profile workload"
	@$$(CC_GCC) $$(STD) $$(OPT) $$(WARN) $$(NOBUILTIN) $$(FLAGS_$(1)) $$(INC) \
	  -DTARGET_NAME='"$(1)"' -fno-strict-aliasing -fprofile-generate \
	  -c src/perf6/memcpy_avx2_final_pgo.c -o build/$(1)/src/perf6/memcpy_avx2_final_pgo.o
	@$$(CC_GCC) $$(STD) $$(OPT) $$(WARN) $$(NOBUILTIN) $$(FLAGS_$(1)) $$(INC) \
	  -DTARGET_NAME='"$(1)"' -fno-strict-aliasing -flto \
	  -c src/perf6/memcpy_avx2_final_lto.c -o build/$(1)/src/perf6/memcpy_avx2_final_lto.o
	@$$(CC) $$(STD) $$(OPT) $$(WARN) $$(NOBUILTIN) $$(FLAGS_$(1)) $$(INC) \
	  -DTARGET_NAME='"$(1)"' -DROUTING_TARGET_NAME=$(1) -DPERF6_PROBE \
	  -c src/registry.c -o build/$(1)/probe/registry.o
	@$$(CC_GCC) $$(OPT) $$(WARN) $$(FLAGS_$(1)) \
	  $$(filter-out build/$(1)/src/registry.o,$$(addprefix build/$(1)/,$$(SRC_OBJ))) \
	  build/$(1)/probe/registry.o \
	  build/$(1)/src/perf6/memcpy_avx2_final_lto.o build/$(1)/src/perf6/memcpy_avx2_final_pgo.o \
	  $$(LDFLAGS) -fprofile-generate -o bench/bench_$(1)_perf6gen
	@taskset -c 2 ./bench/bench_$(1)_perf6gen --impl avx2_final_pgo \
	  --sizes $(PERF6_LADDER) --reps 3 --iters 2000 --warmup 2 --budget 0.05 \
	  > results/perf6_$(1)_pgo_workload.log 2>&1 \
	  || { echo "perf6[$(1)] profile workload FAILED - see results/perf6_$(1)_pgo_workload.log" >&2; tail -5 results/perf6_$(1)_pgo_workload.log >&2; exit 1; }
	@test -f build/$(1)/src/perf6/memcpy_avx2_final_pgo.gcda \
	  || { echo "perf6[$(1)] FAILED: no .gcda was written, so stage 2 would compile with NO profile" >&2; exit 1; }
	@echo "perf6[$(1)] stage 1 done: .gcda present, workload log results/perf6_$(1)_pgo_workload.log"

perf6probe-$(1): perf6gen-$(1)
	@echo "perf6[$(1)] stage 2: -fprofile-use kernel + -flto link -> bench/bench_$(1)_perf6"
	@$$(CC_GCC) $$(STD) $$(OPT) $$(WARN) $$(NOBUILTIN) $$(FLAGS_$(1)) $$(INC) \
	  -DTARGET_NAME='"$(1)"' -fno-strict-aliasing -fprofile-use \
	  -c src/perf6/memcpy_avx2_final_pgo.c -o build/$(1)/src/perf6/memcpy_avx2_final_pgo.o
	@$$(CC_GCC) $$(OPT) $$(WARN) $$(FLAGS_$(1)) -flto \
	  $$(filter-out build/$(1)/src/registry.o,$$(addprefix build/$(1)/,$$(SRC_OBJ))) \
	  build/$(1)/probe/registry.o \
	  build/$(1)/src/perf6/memcpy_avx2_final_lto.o build/$(1)/src/perf6/memcpy_avx2_final_pgo.o \
	  $$(LDFLAGS) -o bench/bench_$(1)_perf6
	@echo "built bench/bench_$(1)_perf6 (V6 -flto + V7 PGO rungs beside their non-flagged twins)"
endef
$(foreach t,$(TARGETS),$(eval $(call PERF6_TARGET,$(t))))
perf6probe: $(addprefix perf6probe-,$(TARGETS))

single: bench/bench_$(TARGET)

# ---- disassembly for codegen diffing (directive 4) -------------------------
objdump: $(BINS)
	@for t in $(TARGETS); do \
	  objdump -d --no-show-raw-insn bench/bench_$$t > build/$$t/disasm.txt 2>/dev/null || mkdir -p build/$$t; \
	  objdump -d --no-show-raw-insn bench/bench_$$t > build/$$t/disasm.txt 2>/dev/null; \
	  echo "wrote build/$$t/disasm.txt"; \
	done

# ---- vectorisation audit ---------------------------------------------------
# Proves, per target, whether a translation unit contains vector instructions.
# Files listed in SCALAR_SRCS must report 0 or the build is wrong.
audit: $(BINS)
	@fail=0; SCALAR_SRCS='$(SCALAR_SRCS)'; \
	for t in $(TARGETS); do \
	  echo "--- target $$t"; \
	  for o in $$(ls build/$$t/src/*.o 2>/dev/null); do \
	    n=$$(objdump -d --no-show-raw-insn $$o | grep -cE '\bv[a-z0-9]+[[:space:]]+\(?%?[xy]mm'); \
	    rel=$${o#build/*/}; stem=$${rel%.o}; \
	    src=$$stem.c; [ -f "$$src" ] || src=$$stem.S; \
	    must=$$(echo "$(SCALAR_SRCS)" | tr ' ' '\n' | grep -Fx "$$src" || true); \
	    if [ -n "$$must" ]; then mark="[SCALAR_SRCS: must be scalar]"; else mark=""; fi; \
	    printf '  %-34s vector_insns=%-4s %s\n' "$$src" "$$n" "$$mark"; \
	    if [ -n "$$must" ] && [ "$$n" != "0" ]; then \
	      echo "  ERROR: $$src is in SCALAR_SRCS but has $$n vector instruction line(s)"; fail=1; \
	    fi; \
	  done; \
	done; \
	exit $$fail

# ---- memory-access width gate (mandatory before any benchmark) ------------
# `audit` (above) proves the SCALAR_SRCS TUs hold no vector instructions. It
# cannot see the width of the accesses inside the vector rungs, which is how the
# same SLP-merge defect passed unnoticed twice (43f01dc: the SSE2 rung compiled to
# 32-byte ymm; 25faacb: the AVX2 rung's small path did too). This target closes
# that gap mechanically: for every kernel TU and every target it disassembles the
# object, finds the widest data-movement access, and fails if it is not the width
# the rung declares. See scripts/audit_width.sh and scripts/audit_width_allow.txt.
audit-width: $(BINS)
	@sh scripts/audit_width.sh

# `gate` is what a campaign must pass before it is allowed to produce a timing
# row: both audits, in one command, with one exit status.
gate: audit audit-width
	@echo "gate: audit + audit-width PASSED - benchmarking permitted"

# ---- Kernel 5 threshold probe (diagnostic, not a rung) ---------------------
# Builds bench/bench_<t>_ntprobe, which is every normal object EXCEPT the
# committed memcpy_avx2_nt.o, plus that same source compiled with its NT
# threshold forced low: sweep sizes with NT enabled everywhere above 64 B to test
# whether a size exists at which NT wins. The Kernel 5 probe found NO winning
# size on this box (NT loses 69-371% at every size), so the committed rung keeps
# a fixed EXPERIMENTAL GATE (MEMCPY_AVX2_NT_THRESHOLD, see its source header),
# NOT a probe-selected crossover. scripts/nt_threshold_probe.sh drives it; the
# probe binary is never snapshotted and never enters a claim.
NT_THRESHOLD ?= 64
# $(FLAGS_$$t) does NOT work here: make looks up a variable literally named
# "FLAGS_$t" and finds nothing, so the probe object was compiled without -mavx2
# at first and clang refused the intrinsics. Expanding per target with $(1) at
# make time is the pattern the normal rules above already use.
# One rule per target via $(eval), the same pattern the normal target rules use.
# Two things this cost, both worth recording: $(foreach) directly in a recipe
# does NOT work (it joins expansions with a space, so the three clang commands
# ran as one), and shell variables / command substitution inside a define that is
# passed through $(eval) are expanded twice by make ("objs=..." reached the shell
# as "bjs=..."), so the object list is computed in make with $(filter-out) and
# $(addprefix) instead of ls|grep.
define NTPROBE_TARGET
ntprobe-$(1): $(BINS)
	@$(CC) $(STD) $(OPT) $(WARN) $(NOBUILTIN) $(FLAGS_$(1)) $(INC) \
	  -DTARGET_NAME='"$(1)"' -DMEMCPY_AVX2_NT_THRESHOLD=$(NT_THRESHOLD) \
	  -c src/memcpy_avx2_nt.c -o build/$(1)/src/memcpy_avx2_nt_probe.o
	@$(CC) $(STD) $(OPT) $(WARN) $(NOBUILTIN) $(FLAGS_$(1)) $(INC) \
	  -DTARGET_NAME='"$(1)"' \
	  $(filter-out build/$(1)/src/memcpy_avx2_nt.o,$(addprefix build/$(1)/,$(SRC_OBJ))) \
	  build/$(1)/src/memcpy_avx2_nt_probe.o $(LDFLAGS) -o bench/bench_$(1)_ntprobe
	@echo "built bench/bench_$(1)_ntprobe (NT threshold forced to $(NT_THRESHOLD) B)"
endef
$(foreach t,$(TARGETS),$(eval $(call NTPROBE_TARGET,$(t))))
ntprobe: $(addprefix ntprobe-,$(TARGETS))

# ---- Phase 2.5: hybrid routing-table verification -------------------------
# Links tools/route_check.c against the built src objects (so it checks the
# shipped table, not a recompile) and walks every size from 0 to 8 MiB plus each
# class boundary +-1, comparing the compiled class mapping with the Phase 2.5
# routing rules and the per-class copies against memcpy_ref. No clock is read.
routecheck: $(BINS)
	@sh scripts/hybrid_route_check.sh

# ---- committed CSV snapshots (frozen record of finalized kernels) ----------
# results/*.csv is gitignored (rewritten every run). A kernel that has passed
# verify and completed its benchmark is copied into results/committed/, which IS
# tracked. A snapshot that already exists is FROZEN: this target refuses to
# overwrite one that differs from the current run, because a record that
# silently changes is not a record. Same data re-copied is a no-op.
SNAPSHOT_IMPLS := ref_scalar naive word sse2 avx2 avx2_pf2 avx2_pf4 avx2_nt asm hybrid
# NOTE on the quoting below - both of these were bugs in the first version:
#   * `$${impl}_$${t}` needs the BRACES. `$impl_$t` parses as the variable
#     `impl_` (underscore is an identifier character) followed by `$t`, so every
#     path became results/phase2_znver2.csv and the target copied 0 files.
#   * `$$n`, `$$refused` and `$$((...))` need the DOUBLE dollar: a single `$` is
#     consumed by make, not by the shell, so `$n` expanded to empty and the
#     arithmetic was eaten as a make variable reference.
snapshot: $(BINS)
	@for f in results/phase2_*.csv; do \
	  [ -f "$$f" ] || continue; \
	  case "$$f" in *.median_based.csv) continue ;; esac; \
	  impl=$$(basename "$$f" .csv | cut -d_ -f2); \
	  case " $(SNAPSHOT_IMPLS) " in \
	    *" $$impl "*) ;; \
	    *) echo "  WARNING: $$f not in SNAPSHOT_IMPLS ($$impl); NOT snapshotted" ;; \
	  esac; \
	done
	@mkdir -p results/committed; n=0; refused=0; \
	for impl in $(SNAPSHOT_IMPLS); do \
	  for t in $(TARGETS); do \
	    src=results/phase2_$${impl}_$${t}.csv; \
	    dst=results/committed/phase2_$${impl}_$${t}.csv; \
	    [ -f "$$src" ] || continue; \
	    if [ -f "$$dst" ] && ! cmp -s "$$src" "$$dst"; then \
	      echo "  REFUSED  $$dst (frozen file differs from $$src)"; refused=$$((refused+1)); \
	    else \
	      cp "$$src" "$$dst"; echo "  snapshot $$dst"; n=$$((n+1)); \
	    fi; \
	  done; \
	done; \
	echo "snapshot: $$n file(s) in results/committed, $$refused refused (frozen)"; \
	[ "$$refused" = "0" ] || { echo "snapshot: refusing to finish with frozen files needing review" >&2; exit 1; }

# ---- phase 4 counter validation -------------------------------------------
perf: $(BINS)
	./scripts/run_perf.sh

plot:
	@./scripts/plot.sh

list:
	@./bench/bench_$(TARGET) --list

clean:
	rm -rf build bench/bench_znver2 bench/bench_znver3 bench/bench_v3

-include $(wildcard build/*/*/*.d)
