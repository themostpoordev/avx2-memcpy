# ---------------------------------------------------------------------------
# memcpy_fast / memcpy_mid — AVX2 copy kernels for Zen 2, plus the harness that
# measured them. MIT licensed; see README.md and LICENSE.
#
# Layout
#   include/   public API (memcpy_fast.h, memcpy_mid.h)
#   src/       fast.c, mid.c (gcc-built kernels), ref.c, common.h, registry.c
#   src/rungs/ the reference ladder used by the benchmark
#   bench/     harness: CSV output, correctness checks, PMU wrapper
#   scripts/   gate.sh (width audit), measure.sh, analyze.sh, matrix.sh
#   experiments/hybrid/  a documented loss, built only by `make experiments`
#   docs/      RESULTS.md, METHODOLOGY.md, NEGATIVE_RESULTS.md, data/
#
# Three codegen targets exist because the tuning target is a measured variable,
# not an assumption:
#   znver2  matches the measured silicon (Zen 2 class)
#   znver3  as originally prescribed
#   v3      -march=x86-64-v3, generic
#
# Usage
#   make                 build all three benchmark binaries (parallel: -j)
#   make gate            audit + width gate; mandatory before any benchmark
#   make bench           alias for all
#   make experiments     build the hybrid-experiment binaries (not default)
#   make bench/bench_znver2 --list        list implementations
#   make clean
# ---------------------------------------------------------------------------

CC       := clang
CC_GCC   := gcc
STD      := -std=c11
WARN     := -Wall -Wextra -Wno-unused-parameter
OPT      := -O3 -fomit-frame-pointer
# We reimplement memcpy/memset; never let the compiler synthesize libc calls or
# fold our loops into them. This is a correctness-of-measurement flag.
NOBUILTIN := -fno-builtin-memcpy -fno-builtin-memset -fno-builtin-memmove -fno-builtin-bzero
INC      := -Isrc -Iinclude -Ibench
LDFLAGS  := -static -lm

FLAGS_znver2 := -march=znver2 -mtune=znver2 -mavx2 -mfma
FLAGS_znver3 := -march=znver3 -mtune=znver3 -mavx2 -mfma
FLAGS_v3     := -march=x86-64-v3 -mtune=generic -mavx2 -mfma

TARGETS := znver2 znver3 v3
TARGET  ?= znver2
ifeq ($(filter $(TARGET),$(TARGETS)),)
$(error TARGET must be one of: $(TARGETS))
endif

# Kernels whose loops MUST stay scalar: clang -O3 turns a byte loop into a 4x32B
# vmovups loop otherwise, which would make the "naive" baseline an AVX2 kernel
# and every comparison meaningless.
SCALAR_SRCS := src/ref.c src/rungs/naive.c src/rungs/word.c src/rungs/rep_movsb.c \
               src/rungs/memset_naive.c src/rungs/memset_word.c
SCALAR_FLAGS := -fno-vectorize -fno-slp-vectorize -fno-unroll-loops

# The SSE2 rung must really be 128-bit: with -mavx2 in the target flags clang's
# SLP vectorizer merges two adjacent _mm_loadu_si128 into one 32-byte vmovups,
# which made rung 3 silently equal rung 4. Disabling AVX for these TUs keeps the
# ladder monotonic.
SSE2_SRCS  := src/rungs/sse2.c src/rungs/memset_sse2.c
SSE2_FLAGS := -mno-avx -mno-avx2 -mno-fma -fno-slp-vectorize -fno-vectorize

# Every shipped source. The two measured kernels are gcc-built on purpose: that
# is the compiler the certified numbers were produced with, and -fno-crossjumping
# is part of the measured configuration (see src/fast.c). Everything else is
# clang.
SRCS      := $(wildcard src/*.c) $(wildcard src/rungs/*.c) $(wildcard bench/*.c)
ASM_SRCS  := $(wildcard src/rungs/*.S)
SRC_OBJ   := $(patsubst %.c,%.o,$(SRCS)) $(patsubst %.S,%.o,$(ASM_SRCS))

GCC_SRCS  := src/fast.c src/mid.c src/rungs/avx2_f4win64.c src/rungs/avx2_f4win129.c src/rungs/f4win64_aligned.c src/rungs/f4_baseline.c

# The library half of the build, without the harness's main(). route_check links
# this, because it has its own main() and never touches a clock.
LIB_SRCS  := $(wildcard src/*.c) $(wildcard src/rungs/*.c)
# Recursive (=, not :=): EXP_T is defined further down, and these expand at use.
LIB_OBJ    = $(addprefix build/$(EXP_T)/,$(patsubst %.c,%.o,$(LIB_SRCS)) \
                                      $(patsubst %.S,%.o,$(ASM_SRCS)))
BENCH_OBJ  = $(addprefix build/$(EXP_T)/,bench/bench.o bench/pmu.o)
# The deliverable carries -falign-labels=64: it is part of the kernel, not
# cosmetics. Without it the 32..64 B path loses the 64-byte alignment of its
# out-of-line branch target, which costs 16% at 32 B and 64 B - certified wins.
# See src/fast.c.
GCC_EXTRA_fast.c := -fno-crossjumping -falign-labels=64
GCC_EXTRA_mid.c  := -fno-crossjumping
# Phase 7.2/7.3 rungs: the F4 shape with one knob changed, so they must be
# compiled by the same compiler with the same flags as the kernel they are
# compared against, or the comparison measures the compiler and not the knob.
GCC_EXTRA_avx2_f4win64.c  := -fno-crossjumping
GCC_EXTRA_avx2_f4win129.c := -fno-crossjumping
# Phase 7.2: the alignment attempt. -falign-labels=64 is per-file, so it
# affects this rung's translation unit only and leaves the shipped
# deliverable's object byte-identical.
GCC_EXTRA_f4win64_aligned.c := -fno-crossjumping -falign-labels=64
# The pre-promote F4, kept as the control for the Phase 7.4 comparison. It
# must NOT get -falign-labels=64: that flag is what the promoted kernel adds,
# and a baseline with it would be the candidate, not the baseline.
GCC_EXTRA_f4_baseline.c := -fno-crossjumping

BINS := $(addprefix bench/bench_,$(TARGETS))
EXP_T := znver2

.PHONY: all bench single gate audit audit-width clean list objdump experiments \
        reference $(addprefix single-,$(TARGETS))

all: $(BINS)
bench: $(BINS)

# ---- per-target object + link rules ---------------------------------------
# build/<t>/<path>.o from <path>.c with the flags of target <t>.
# NOTE: $$(dir $$@) needs the DOUBLE dollar - a single $ is consumed by make.
define TARGET_RULES
build/$(1)/%.o: %.c
	@mkdir -p $$(dir $$@)
	$$(CC) $$(STD) $$(OPT) $$(WARN) $$(NOBUILTIN) $$(FLAGS_$(1)) $$(INC) \
	  -DTARGET_NAME='"$(1)"' $$(if $$(filter $$<,$$(SCALAR_SRCS)),$$(SCALAR_FLAGS),) \
	  $$(if $$(filter $$<,$$(SSE2_SRCS)),$$(SSE2_FLAGS),) \
	  -MMD -MP -c $$< -o $$@

# Assembly rungs: same per-target -march/-mavx2 flags; -std/-fno-builtin are C
# concepts and are deliberately not passed.
build/$(1)/%.o: %.S
	@mkdir -p $$(dir $$@)
	$$(CC) $$(OPT) $$(WARN) $$(FLAGS_$(1)) $$(INC) -c $$< -o $$@

bench/bench_$(1): $$(addprefix build/$(1)/,$$(SRC_OBJ))
	@mkdir -p bench
	$$(CC) $$(STD) $$(OPT) $$(WARN) $$(NOBUILTIN) $$(FLAGS_$(1)) $$(INC) \
	  -DTARGET_NAME='\"$(1)\"' $$^ $$(LDFLAGS) -o $$@
endef
$(foreach t,$(TARGETS),$(eval $(call TARGET_RULES,$(t))))

# ---- gcc-built kernels ----------------------------------------------------
# Explicit per-source rules: an explicit rule beats the pattern rule, so there is
# no ambiguity about which compiler produced which object.
define GCC_RULE
build/$(1)/$(2:.c=.o): $(2)
	@mkdir -p $$(dir $$@)
	$$(CC_GCC) $$(STD) $$(OPT) $$(WARN) $$(NOBUILTIN) $$(FLAGS_$(1)) $$(INC) \
	  -DTARGET_NAME='"$(1)"' -fno-strict-aliasing $$(GCC_EXTRA_$(notdir $(2))) \
	  -MMD -MP -c $$< -o $$@
endef
$(foreach t,$(TARGETS),$(foreach s,$(GCC_SRCS),$(eval $(call GCC_RULE,$(t),$(s)))))

single: bench/bench_$(TARGET)
$(addprefix single-,$(TARGETS)): single-%: bench/bench_%

# ---- vectorisation audit ---------------------------------------------------
# Proves per target, per TU, whether the object contains vector instructions.
# TUs listed in SCALAR_SRCS must report 0 or the build is wrong; any EVEX
# (AVX-512) encoding is a hard failure because AVX2-only targets would SIGILL.
audit: $(BINS)
	@fail=0; SCALAR_SRCS='$(SCALAR_SRCS)'; \
	for t in $(TARGETS); do \
	  echo "--- target $$t"; \
	  for o in $$(find build/$$t/src -name '*.o' 2>/dev/null | sort); do \
	    n=$$(objdump -d --no-show-raw-insn $$o | grep -cE '\bv[a-z0-9]+[[:space:]]+\(?%?[xy]mm'); \
	    rel=$${o#build/*/}; stem=$${rel%.o}; \
	    src=$$stem.c; [ -f "$$src" ] || src=$$stem.S; \
	    must=$$(echo "$(SCALAR_SRCS)" | tr ' ' '\n' | grep -Fx "$$src" || true); \
	    if [ -n "$$must" ]; then mark="[SCALAR_SRCS: must be scalar]"; else mark=""; fi; \
	    printf '  %-34s vector_insns=%-4s %s\n' "$$src" "$$n" "$$mark"; \
	    if [ -n "$$must" ] && [ "$$n" != "0" ]; then \
	      echo "  ERROR: $$src is in SCALAR_SRCS but has $$n vector instruction line(s)"; fail=1; \
	    fi; \
	    evex=$$(objdump -d $$o | awk '/^[[:space:]]*[0-9a-f]+:\t62 /' | wc -l); \
	    if [ "$$evex" != "0" ]; then \
	      echo "  ERROR: $$src has $$evex EVEX(0x62)-prefixed instruction(s): AVX-512 on an AVX2-only target is SIGILL"; \
	      objdump -d $$o | awk '/^[[:space:]]*[0-9a-f]+:\t62 /' | head -3; fail=1; \
	    fi; \
	  done; \
	done; \
	exit $$fail

# Memory-access width gate: every kernel's widest data movement must equal the
# width its ladder rung declares. See scripts/gate.sh.
audit-width: $(BINS)
	@sh scripts/gate.sh

gate: audit audit-width
	@echo "gate: audit + audit-width PASSED - benchmarking permitted"

# ---- reference kernel ------------------------------------------------------
# V9c, the predecessor of the shipped kernel. Not a rung, not audited, not
# linked into the benchmark binaries: it exists so its measured numbers stay
# reproducible. See src/reference/README.md.
reference: build/reference/fast_v9c.o

build/reference/%.o: src/reference/%.c
	@mkdir -p $(dir $@)
	$(CC_GCC) $(STD) $(OPT) $(WARN) $(NOBUILTIN) $(FLAGS_znver2) $(INC) \
	  -fno-strict-aliasing -fno-crossjumping -MMD -MP -c $< -o $@

# ---- documented-loss experiment: the hybrid dispatcher ---------------------
# Built only on request. `make gate` does not audit it: it is not a shipped rung,
# and its whole point is that the dispatch layer loses. See experiments/README.md.
# EXP_T selects which target's routing table is compiled in; the header carries
# all three. `make experiments EXP_T=znver3` builds the other one.
experiments: experiments/hybrid/route_check experiments/hybrid/bench_hybrid

build/$(EXP_T)/experiments/hybrid/%.o: experiments/hybrid/%.c
	@mkdir -p $(dir $@)
	$(CC) $(STD) $(OPT) $(WARN) $(NOBUILTIN) $(FLAGS_$(EXP_T)) $(INC) \
	  -Iexperiments/hybrid -DTARGET_NAME='"$(EXP_T)"' \
	  -DROUTING_TARGET_NAME=$(EXP_T) -MMD -MP -c $< -o $@

build/$(EXP_T)/probe/registry_hybrid.o: src/registry.c
	@mkdir -p $(dir $@)
	$(CC) $(STD) $(OPT) $(WARN) $(NOBUILTIN) $(FLAGS_$(EXP_T)) $(INC) \
	  -DTARGET_NAME='"$(EXP_T)"' -DHYBRID_PROBE -MMD -MP -c $< -o $@

experiments/hybrid/route_check: build/$(EXP_T)/experiments/hybrid/route_check.o \
                                build/$(EXP_T)/experiments/hybrid/hybrid.o \
                                $(LIB_OBJ)
	$(CC) $(STD) $(OPT) $(WARN) $(NOBUILTIN) $(FLAGS_$(EXP_T)) $(INC) $^ $(LDFLAGS) -o $@

experiments/hybrid/bench_hybrid: build/$(EXP_T)/experiments/hybrid/hybrid.o \
                                 build/$(EXP_T)/probe/registry_hybrid.o \
                                 $(filter-out build/$(EXP_T)/src/registry.o,$(LIB_OBJ)) \
                                 $(BENCH_OBJ)
	$(CC) $(STD) $(OPT) $(WARN) $(NOBUILTIN) $(FLAGS_$(EXP_T)) $(INC) $^ $(LDFLAGS) -o $@

# ---- disassembly for codegen inspection -----------------------------------
objdump: $(BINS)
	@for t in $(TARGETS); do \
	  mkdir -p build/$$t; objdump -d --no-show-raw-insn bench/bench_$$t > build/$$t/disasm.txt; \
	  echo "wrote build/$$t/disasm.txt"; \
	done

list:
	@./bench/bench_$(TARGET) --list

clean:
	rm -rf build bench/bench_znver2 bench/bench_znver3 bench/bench_v3 \
	       experiments/hybrid/route_check experiments/hybrid/bench_hybrid

-include $(wildcard build/*/*/*.d build/*/*/*/*.d build/*/*/*/*/*.d build/*/*/*/*/*/*.d)
