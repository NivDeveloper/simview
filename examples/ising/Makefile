# The standalone build: tensor requires g++-16 -freflection, so this
# example configures apart and builds EVERYTHING — simview included —
# with that one compiler.
.DEFAULT_GOAL := all
ifeq ($(origin CXX),default)
CXX := /opt/homebrew/bin/g++-16
endif
PREFIX ?= $(HOME)/Projects/toolchains/sdl3
ROOT := $(abspath ../..)

build/CMakeCache.txt:
	CMAKE_PREFIX_PATH=$(PREFIX) cmake -B build \
	    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=$(CXX) \
	    -DSIMVIEW_ROOT=$(ROOT)

.PHONY: all run clean
all: build/CMakeCache.txt
	cmake --build build -j

run: all
	./build/ising

clean:
	rm -rf build
