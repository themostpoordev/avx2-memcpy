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

# Any new src/*.c kernel is picked up automatically (no Makefile edit per kernel).
SRCS   := $(wildcard src/*.c) $(wildcard bench/*.c)
SRC_OBJ := $(patsubst %.c,%.o,$(SRCS))
BINS   := $(addprefix bench/bench_,$(TARGETS))

TARGET ?= znver2
ifeq ($(filter $(TARGET),$(TARGETS)),)
$(error TARGET must be one of: $(TARGETS))
endif

.PHONY: all bench single perf objdump plot clean list audit snapshot

all: $(BINS)
bench: $(BINS)

# ---- per-target object + link rules ---------------------------------------
# build/<t>/<path>.o  from  <path>.c   with the flags of target <t>.
define TARGET_RULES
build/$(1)/%.o: %.c
	@mkdir -p $$(dir $$@)
	$$(CC) $$(STD) $$(OPT) $$(WARN) $$(NOBUILTIN) $$(FLAGS_$(1)) $$(INC) \
	  -DTARGET_NAME='"$(1)"' $$(if $$(filter $$<,$$(SCALAR_SRCS)),$$(SCALAR_FLAGS),) \
	  -MMD -MP -c $$< -o $$@

bench/bench_$(1): $$(addprefix build/$(1)/,$$(SRC_OBJ))
	@mkdir -p bench
	$$(CC) $$(STD) $$(OPT) $$(WARN) $$(NOBUILTIN) $$(FLAGS_$(1)) $$(INC) \
	  -DTARGET_NAME='"$(1)"' $$^ $$(LDFLAGS) -o $$@
endef

$(foreach t,$(TARGETS),$(eval $(call TARGET_RULES,$(t))))

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
	    rel=$${o#build/*/}; src=$${rel%.o}.c; \
	    must=$$(echo "$(SCALAR_SRCS)" | tr ' ' '\n' | grep -Fx "$$src" || true); \
	    if [ -n "$$must" ]; then mark="[SCALAR_SRCS: must be scalar]"; else mark=""; fi; \
	    printf '  %-34s vector_insns=%-4s %s\n' "$$src" "$$n" "$$mark"; \
	    if [ -n "$$must" ] && [ "$$n" != "0" ]; then \
	      echo "  ERROR: $$src is in SCALAR_SRCS but has $$n vector instruction line(s)"; fail=1; \
	    fi; \
	  done; \
	done; \
	exit $$fail

# ---- committed CSV snapshots (frozen record of finalized kernels) ----------
# results/*.csv is gitignored (rewritten every run). A kernel that has passed
# verify and completed its benchmark is copied into results/committed/, which IS
# tracked. A snapshot that already exists is FROZEN: this target refuses to
# overwrite one that differs from the current run, because a record that
# silently changes is not a record. Same data re-copied is a no-op.
SNAPSHOT_IMPLS := ref_scalar naive word
snapshot: $(BINS)
	@mkdir -p results/committed; n=0; refused=0; \
	for impl in $(SNAPSHOT_IMPLS); do \
	  for t in $(TARGETS); do \
	    src=results/phase2_$$impl_$$t.csv; \
	    dst=results/committed/phase2_$$impl_$$t.csv; \
	    [ -f "$$src" ] || continue; \
	    if [ -f "$$dst" ] && ! cmp -s "$$src" "$$dst"; then \
	      echo "  REFUSED  $$dst (frozen file differs from $$src)"; refused=$$((refused+1)); \
	    else \
	      cp "$$src" "$$dst"; echo "  snapshot $$dst"; n=$$((n+1)); \
	    fi; \
	  done; \
	done; \
	echo "snapshot: $$n file(s) in results/committed, $$refused refused (frozen)"

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
