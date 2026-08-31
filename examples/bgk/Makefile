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

.PHONY: all run debug trace clean
all: build/CMakeCache.txt
	cmake --build build -j

# The same tree at -O0 -g, apart: one compiler, one debugger, and
# every frame of the stack — the example, tensor, gpud, simview —
# steppable. .zed/debug.json and .vscode/launch.json launch it.
build-debug/CMakeCache.txt:
	CMAKE_PREFIX_PATH=$(PREFIX) cmake -B build-debug \
	    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=$(CXX) \
	    -DSIMVIEW_ROOT=$(ROOT) -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

debug: build-debug/CMakeCache.txt
	cmake --build build-debug -j

run: all
	./build/bgk

# The tracing build (Tracy's client in simview), its own tree:
# ./build-trace/<name> then connect tracy-profiler.
build-trace/CMakeCache.txt:
	CMAKE_PREFIX_PATH=$(PREFIX) cmake -B build-trace \
	    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=$(CXX) \
	    -DSIMVIEW_ROOT=$(ROOT) -DSIMVIEW_TRACE=ON

trace: build-trace/CMakeCache.txt
	cmake --build build-trace -j

clean:
	rm -rf build build-debug build-trace
