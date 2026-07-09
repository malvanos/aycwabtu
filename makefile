.PHONY: all clean

UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

CC          = g++
LD          = g++

SHELL=bash

GITHASH := $(shell git rev-parse --short HEAD)

CFLAGS      = \
    -w                                  \
    -x c++                              \
    -std=c++17                          \
    -I src/libdvbcsa/dvbcsa             \
    -O2                                 \
    -DGITHASH=\"$(GITHASH)\"

ifeq ($(UNAME_M),x86_64)
    CFLAGS += -msse2 -msse4.2 -DPARALLEL_MODE=2
else
    CFLAGS += -DPARALLEL_MODE=1
endif

LDFLAGS     =
ifeq ($(UNAME_S),Linux)
    LDFLAGS += -static -s
endif

obj/%.o : src/%.c
	@mkdir -p $(@D)
	$(CC) -c -MD $(CFLAGS) -o obj/$*.o $<

obj/%.o : src/%.cpp
	@mkdir -p $(@D)
	$(CC) -c -MD $(CFLAGS) -o obj/$*.o $<

ayc_src = \
	main.cpp           \
	bs_algo.c          \
	bs_block.c         \
	bs_block_ab.c      \
	bs_sse2.c          \
	bs_stream.c        \
	bs_uint32.c        \
	ts.c

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


