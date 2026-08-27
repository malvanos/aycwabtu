.PHONY: all clean

UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

CC          = g++
LD          = g++

SHELL=bash

# SIMD backend for x86_64: sse2 (default, 128-bit) or avx2 (256-bit)
SIMD ?= sse2

GITHASH := $(shell git rev-parse --short HEAD)

CFLAGS      = \
    -w                                  \
    -x c++                              \
    -std=c++17                          \
    -I src/libdvbcsa/dvbcsa             \
    -O2                                 \
    -flto                               \
    -march=native                       \
    -DGITHASH=\"$(GITHASH)\"

ifeq ($(UNAME_M),x86_64)
    ifeq ($(SIMD),avx2)
        CFLAGS += -msse2 -msse4.2 -mavx2 -DPARALLEL_MODE=4
    else
        CFLAGS += -msse2 -msse4.2 -DPARALLEL_MODE=2
    endif
else ifeq ($(UNAME_M),arm64)
    CFLAGS += -DPARALLEL_MODE=3
else ifeq ($(UNAME_M),aarch64)
    CFLAGS += -DPARALLEL_MODE=3
else
    CFLAGS += -DPARALLEL_MODE=1
endif

LDFLAGS     = -flto
ifeq ($(UNAME_S),Linux)
    LDFLAGS += -static -s
endif

# ---------------------------------------------------------------------
# OpenCL support.
#   WITH_OPENCL=1  force ON   WITH_OPENCL=0  force OFF
#   default: auto-detect (macOS always has it; elsewhere we confirm the
#   header is present and -lOpenCL actually links).
# When OpenCL is unavailable the build still succeeds and runs CPU-only;
# ocl.cpp/ocl.hpp degrade to stubs under !HAVE_OPENCL.
# ---------------------------------------------------------------------
ifeq ($(WITH_OPENCL),1)
    OPENCL_ON := 1
else ifeq ($(WITH_OPENCL),0)
    OPENCL_ON :=
else
    ifeq ($(UNAME_S),Darwin)
        OPENCL_ON := 1
    else
        # Probe compile+link with the SAME flags/order used for the real link
        # (so we only enable OpenCL when it can actually be linked in).  On
        # Linux the static build needs a static libOpenCL, otherwise linking
        # the shared one under -static would fail.
        OPENCL_LNK := -lOpenCL
        ifeq ($(UNAME_S),Linux)
            OPENCL_LNK := -static -lOpenCL
        endif
        # Probe: does CL/cl.h compile and does the OpenCL link succeed?
        # (\# escapes the hash so GNU make does not treat `#include` as a comment.)
        OPENCL_PROBE := $(shell printf '\#include <CL/cl.h>\nint main(void){cl_int e=0;return e;}\n' > .ocl_probe.c && $(CC) .ocl_probe.c $(OPENCL_LNK) -o /dev/null 2>/dev/null && echo yes; rm -f .ocl_probe.c)
        OPENCL_ON := $(if $(filter yes,$(OPENCL_PROBE)),1,)
    endif
endif

ifeq ($(OPENCL_ON),1)
    CFLAGS  += -DHAVE_OPENCL
    ifeq ($(UNAME_S),Darwin)
        LDFLAGS += -framework OpenCL
    else
        LDFLAGS += -lOpenCL
    endif
    $(info OpenCL: enabled)
else
    $(info OpenCL: not found - building CPU-only (set WITH_OPENCL=1 to force, WITH_OPENCL=0 to disable when present))
endif

obj/%.o : src/%.c
	@mkdir -p $(@D)
	$(CC) -c -MD $(CFLAGS) -o obj/$*.o $<

obj/%.o : src/%.cpp
	@mkdir -p $(@D)
	$(CC) -c -MD $(CFLAGS) -o obj/$*.o $<

ayc_src = \
	main.cpp           \
	ocl.cpp            \
	bs_algo.c          \
	bs_block.c         \
	bs_block_ab.c      \
	bs_stream.c        \
	bs_testcases.c     \
	ts.c

ifeq ($(UNAME_M),x86_64)
    ayc_src += bs_sse2.c bs_avx2.c
else ifeq ($(UNAME_M),arm64)
    ayc_src += bs_neon.c
else ifeq ($(UNAME_M),aarch64)
    ayc_src += bs_neon.c
else
    ayc_src += bs_uint32.c
endif

tsgen_src = tsgen.c

libdvbcsa_src = \
	libdvbcsa/dvbcsa_algo.c     \
	libdvbcsa/dvbcsa_block.c    \
	libdvbcsa/dvbcsa_key.c      \
	libdvbcsa/dvbcsa_stream.c

ayc_obj         = $(ayc_src:%.c=obj/%.o)
ayc_obj         := $(ayc_obj:%.cpp=obj/%.o)
tsgen_obj       = $(tsgen_src:%.c=obj/%.o)
libdvbcsa_obj   = $(libdvbcsa_src:%.c=obj/%.o)

all: aycwabtu
   

aycwabtu: $(ayc_obj) $(libdvbcsa_obj)
	$(LD) $(LDFLAGS) -o $@ $(ayc_obj) $(libdvbcsa_obj)
	@echo $@ created

tsgen: $(tsgen_obj) $(libdvbcsa_obj)
	$(LD) $(LDFLAGS) -o $@ $(tsgen_obj) $(libdvbcsa_obj)
	@echo $@ created


check: aycwabtu tsgen always
# just 'timeout' will let windows find C:\Windows\System32\timeout.exe first :(
	/usr/bin/timeout 5 ./aycwabtu -t test/Testfile_CW_7FFAE9A02486.ts -a 7FFAE9A00000
	cd test && /usr/bin/timeout 60 ./testframe.sh

always:


aycwabtu tsgen : makefile

include $(wildcard obj/*.d) $(wildcard obj/libdvbcsa/*.d)

clean:
	@rm -rf aycwabtu tsgen aycwabtu.exe tsgen.exe obj


