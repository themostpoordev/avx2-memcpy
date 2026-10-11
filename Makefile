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
SCALAR_SRCS  := src/ref_scalar.c
SCALAR_FLAGS := -fno-vectorize -fno-slp-vectorize -fno-unroll-loops

# Any new src/*.c kernel is picked up automatically (no Makefile edit per kernel).
SRCS   := $(wildcard src/*.c) $(wildcard bench/*.c)
SRC_OBJ := $(patsubst %.c,%.o,$(SRCS))
BINS   := $(addprefix bench/bench_,$(TARGETS))

TARGET ?= znver2
ifeq ($(filter $(TARGET),$(TARGETS)),)
$(error TARGET must be one of: $(TARGETS))
endif

.PHONY: all bench single perf objdump plot clean list audit

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
	@fail=0; \
	for t in $(TARGETS); do \
	  echo "--- target $$t"; \
	  for o in $$(ls build/$$t/src/*.o 2>/dev/null); do \
	    n=$$(objdump -d --no-show-raw-insn $$o | grep -cE '\bv[a-z0-9]+[[:space:]]+\(?%?[xy]mm'); \
	    src=$${o%.o}.c; \
	    case "$$o" in \
	      */ref_scalar.o) mark="[must be scalar]";; \
	      *) mark="";; \
	    esac; \
	    printf '  %-34s vector_insns=%-4s %s\n' "$$src" "$$n" "$$mark"; \
	    case "$$o" in \
	      */ref_scalar.o) if [ "$$n" != "0" ]; then echo "  ERROR: $$o has vector instructions"; fail=1; fi;; \
	    esac; \
	  done; \
	done; \
	exit $$fail

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
