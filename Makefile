.DEFAULT_GOAL := all

# Thin wrapper. CXX must be g++-16: only it parses tensor's reflection.
# Not ?=: make predefines CXX as c++, so the default arrives via origin.
ifeq ($(origin CXX),default)
CXX := /opt/homebrew/bin/g++-16
endif
JOBS ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc)
PREFIX ?= $(HOME)/Projects/toolchains/sdl3

# Local-source overrides for development: TENSOR_SRC / GPUD_SRC point
# FetchContent at checkouts instead of the pinned github fetch.
FC_ARGS :=
ifdef TENSOR_SRC
FC_ARGS += -DFETCHCONTENT_SOURCE_DIR_TENSOR=$(TENSOR_SRC)
endif
ifdef GPUD_SRC
FC_ARGS += -DFETCHCONTENT_SOURCE_DIR_GPUD=$(GPUD_SRC)
endif

build/CMakeCache.txt:
	CMAKE_PREFIX_PATH=$(PREFIX) cmake -B build \
	    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=$(CXX) $(FC_ARGS)

.PHONY: all run clean
all: build/CMakeCache.txt
	cmake --build build -j $(JOBS)

run: all
	./build/simview

clean:
	rm -rf build