.PHONY: all clean check test

UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

CC          = g++
LD          = g++

SHELL=bash

# SIMD backend selection is now a RUN-TIME property: every backend the host
# architecture can compile is linked into one binary (x86_64: scalar+sse2+
# avx2, ARM64: scalar+neon) and chosen at start-up by CPU auto-detection
# (`-S auto`, the default) or explicitly with `-S <backend>` (e.g. -S sse2).
# The SIMD variable only changes the RUNTIME default; acceptable values:
#   auto (default) | scalar | sse2 | avx2 | neon
SIMD ?= auto

GITHASH := $(shell git rev-parse --short HEAD)

# Neutral flags: baseline instruction sets only (-march=native would pin the
# whole binary to the build machine; per-backend objects carry their own
# flags instead, so one binary runs on any CPU of that family).
CFLAGS      = \
    -w                                  \
    -x c++                              \
    -std=c++17                          \
    -I src/libdvbcsa/dvbcsa             \
    -O2                                 \
    -flto                               \
    -DGITHASH=\"$(GITHASH)\"

# Neutral translation units (main.cpp, ts.c, ocl.cpp, libdvbcsa, tsgen.c)
# include config.h which pulls in a bs_* header; give them the most
# conservative parallel mode of the architecture.
ifeq ($(UNAME_M),x86_64)
    CFLAGS += -DPARALLEL_MODE=2
else ifeq ($(UNAME_M),arm64)
    CFLAGS += -DPARALLEL_MODE=3
else ifeq ($(UNAME_M),aarch64)
    CFLAGS += -DPARALLEL_MODE=3
else
    CFLAGS += -DPARALLEL_MODE=1
endif

# ---------------------------------------------------------------------
# SIMD backends. Per backend: PARALLEL_MODE, extra compiler flags, and the
# source file that provides the key transpose (and bit2byteslice).
# ---------------------------------------------------------------------
BACKEND_mode_scalar  := 1
BACKEND_flags_scalar :=
BACKEND_src_scalar   := bs_uint32.c

ifeq ($(UNAME_M),x86_64)
    BACKENDS := scalar sse2 avx2
    BACKEND_mode_sse2  := 2
    BACKEND_flags_sse2 := -msse2
    BACKEND_src_sse2   := bs_sse2.c
    BACKEND_mode_avx2  := 4
    BACKEND_flags_avx2 := -mavx2
    BACKEND_src_avx2   := bs_avx2.c
else ifeq ($(UNAME_M),arm64)
    BACKENDS := scalar neon
    BACKEND_mode_neon  := 3
    BACKEND_flags_neon :=
    BACKEND_src_neon   := bs_neon.c
else ifeq ($(UNAME_M),aarch64)
    BACKENDS := scalar neon
    BACKEND_mode_neon  := 3
    BACKEND_flags_neon :=
    BACKEND_src_neon   := bs_neon.c
else
    BACKENDS := scalar
endif

# code shared by every backend object build: -DPARALLEL_MODE=<mode>
# -DAYCW_BACKEND=<backend> triggers the per-backend symbol renaming
# (src/bs_rename.h) so all backends can live in one binary.
BACKEND_DEFS = -DPARALLEL_MODE=$(BACKEND_mode_$*) -DAYCW_BACKEND=$* $(BACKEND_flags_$*)

# second expansion so the prereq for the transpose file can reference the stem
.SECONDEXPANSION:

# ---- per-backend objects (stem = backend name) ----
obj/%/bs_algo.o       : src/bs_algo.c
	@mkdir -p $(@D)
	$(CC) -c -MD $(CFLAGS) $(BACKEND_DEFS) -o $@ $<

obj/%/bs_stream.o     : src/bs_stream.c
	@mkdir -p $(@D)
	$(CC) -c -MD $(CFLAGS) $(BACKEND_DEFS) -o $@ $<

obj/%/bs_block_ab.o   : src/bs_block_ab.c
	@mkdir -p $(@D)
	$(CC) -c -MD $(CFLAGS) $(BACKEND_DEFS) -o $@ $<

obj/%/bs_testcases.o  : src/bs_testcases.c
	@mkdir -p $(@D)
	$(CC) -c -MD $(CFLAGS) $(BACKEND_DEFS) -o $@ $<

obj/%/bs_keytrans.o : src/$$(BACKEND_src_$$*)
	@mkdir -p $(@D)
	$(CC) -c -MD $(CFLAGS) $(BACKEND_DEFS) -o $@ $<

obj/%/bs_driver.o     : src/bs_driver_impl.cpp
	@mkdir -p $(@D)
	$(CC) -c -MD $(CFLAGS) $(BACKEND_DEFS) -o $@ $<

# ---- neutral objects (compiled once, architecture baseline) ----
obj/%.o : src/%.c
	@mkdir -p $(@D)
	$(CC) -c -MD $(CFLAGS) -o $@ $<

obj/%.o : src/%.cpp
	@mkdir -p $(@D)
	$(CC) -c -MD $(CFLAGS) -o $@ $<

obj/libdvbcsa/%.o : src/libdvbcsa/%.c
	@mkdir -p $(@D)
	$(CC) -c -MD $(CFLAGS) -o $@ $<

LDFLAGS     = -flto
ifeq ($(UNAME_S),Linux)
    LDFLAGS += -s   # + -static added below for CPU-only (non-OpenCL) builds
endif

OPENCL_ON :=

# Per-platform OpenCL link flags, used for both the auto-detect probe and the
# real link so what we test is exactly what we link.
#   Darwin: OpenCL ships with macOS -> link via -framework OpenCL.
#   Linux : Mesa/ocl-icd install the runtime libOpenCL.so.1; the dev symlink
#           libOpenCL.so comes separately (ocl-icd-opencl-dev). Prefer -lOpenCL
#           and fall back to the runtime lib by exact name so linking works
#           with just the base install.
#   other : plain -lOpenCL
ifeq ($(UNAME_S),Darwin)
    OPENCL_LIB := -framework OpenCL
else ifeq ($(UNAME_S),Linux)
    BPATH := /usr/lib/$(shell $(CC) -dumpmachine)
    ifeq ($(shell test -f $(BPATH)/libOpenCL.so && echo yes),yes)
        OPENCL_LIB := -lOpenCL
    else ifeq ($(shell test -f $(BPATH)/libOpenCL.so.1 && echo yes),yes)
        OPENCL_LIB := -L$(BPATH) -l:libOpenCL.so.1
    else
        OPENCL_LIB := -lOpenCL
    endif
else
    OPENCL_LIB := -lOpenCL
endif

# ---------------------------------------------------------------------
# Decide whether OpenCL is used.
#   WITH_OPENCL=1 force ON    WITH_OPENCL=0 force OFF
#   default: auto-detect (macOS always has it; elsewhere probe a tiny
#   program that includes <CL/cl.h> and links with OPENCL_LIB).
# When OpenCL is unavailable the build still succeeds and runs CPU-only;
# ocl.cpp/ocl.hpp degrade to stubs under !HAVE_OPENCL.
# ---------------------------------------------------------------------
ifeq ($(WITH_OPENCL),1)
    OPENCL_ON := 1
else ifeq ($(WITH_OPENCL),0)
    OPENCL_ON :=
else ifeq ($(UNAME_S),Darwin)
    OPENCL_ON := 1
else
    # (\# escapes the hash so GNU make does not treat `#include` as a comment.)
    OPENCL_PROBE := $(shell printf '\#include <CL/cl.h>\nint main(void){cl_int e=0;return e;}\n' > .ocl_probe.c && $(CC) .ocl_probe.c $(OPENCL_LIB) -o /dev/null 2>/dev/null && echo yes; rm -f .ocl_probe.c)
    OPENCL_ON := $(if $(filter yes,$(OPENCL_PROBE)),1,)
endif

ifeq ($(OPENCL_ON),1)
    CFLAGS  += -DHAVE_OPENCL
    LDLIBS  += $(OPENCL_LIB)
    $(info OpenCL: enabled)
else
    $(info OpenCL: not found - building CPU-only (set WITH_OPENCL=1 to force, WITH_OPENCL=0 to disable when present))
endif

# A fully-static (-static) Linux binary is self-contained but cannot link the
# shared OpenCL runtime, so keep -static for the CPU-only build and link
# dynamic (shared OpenCL) when OpenCL is enabled. macOS links the framework
# either way.
ifeq ($(UNAME_S),Linux)
    ifneq ($(OPENCL_ON),1)
        LDFLAGS += -static
    endif
endif

# one copy of the algorithm + driver per SIMD backend, plus the neutral units
ayc_src        := bs_algo.c bs_stream.c bs_block_ab.c bs_testcases.c
neutral_src    := main.cpp ocl.cpp ts.c bs_dispatch.cpp
tsgen_src      := tsgen.c
libdvbcsa_src  := \
	libdvbcsa/dvbcsa_algo.c     \
	libdvbcsa/dvbcsa_block.c    \
	libdvbcsa/dvbcsa_key.c      \
	libdvbcsa/dvbcsa_stream.c

backend_obj  = $(foreach b,$(BACKENDS),$(addprefix obj/$(b)/,$(ayc_src:.c=.o) bs_keytrans.o bs_driver.o))
neutral_obj  = $(neutral_src:%.cpp=obj/%.o)
neutral_obj := $(neutral_obj:%.c=obj/%.o) $(addprefix obj/libdvbcsa/, $(libdvbcsa_src:libdvbcsa/%.c=%.o))
tsgen_obj    = $(tsgen_src:%.c=obj/%.o)

all: aycwabtu

aycwabtu: $(backend_obj) $(neutral_obj)
	$(LD) $(LDFLAGS) -o $@ $(backend_obj) $(neutral_obj) $(LDLIBS)
	@echo $@ created

tsgen: $(tsgen_obj) $(addprefix obj/libdvbcsa/, $(libdvbcsa_src:libdvbcsa/%.c=%.o))
	$(LD) $(LDFLAGS) -o $@ $(tsgen_obj) $(addprefix obj/libdvbcsa/, $(libdvbcsa_src:libdvbcsa/%.c=%.o)) $(LDLIBS)
	@echo $@ created

# run the SIMD unit tests (self-tests for every backend + manual selection)
test: aycwabtu
	cd test && ./test_simd.sh

check: aycwabtu tsgen always
# just 'timeout' will let windows find C:\Windows\System32\timeout.exe first :(
	/usr/bin/timeout 5 ./aycwabtu -t test/Testfile_CW_7FFAE9A02486.ts -a 7FFAE9A00000
	cd test && ./test_simd.sh
	cd test && /usr/bin/timeout 60 ./testframe.sh

always:

aycwabtu tsgen : makefile

include $(wildcard obj/*/*.d) $(wildcard obj/libdvbcsa/*.d) $(wildcard obj/*.d)

clean:
	@rm -rf aycwabtu tsgen aycwabtu.exe tsgen.exe obj