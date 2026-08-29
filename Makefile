.DEFAULT_GOAL := all

# The SYSTEM compiler on purpose: simview is plain C++20 and must
# build with AppleClang/gcc/MSVC — no toolchain pinning here.
JOBS ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc)
PREFIX ?= $(HOME)/Projects/toolchains/sdl3

build/CMakeCache.txt:
	CMAKE_PREFIX_PATH=$(PREFIX) cmake -B build -DCMAKE_BUILD_TYPE=Release

.PHONY: all test lint install-check run clean
all: build/CMakeCache.txt
	cmake --build build -j $(JOBS)

test: all
	ctest --test-dir build --output-on-failure

lint:
	sh tools/lint.sh

install-check: all
	ctest --test-dir build -R installed_surface --output-on-failure

run: all
	./build/examples/hello/hello

clean:
	rm -rf build
