.DEFAULT_GOAL := all

# The SYSTEM compiler on purpose: simview is plain C++20 and must
# build with AppleClang/gcc/MSVC — no toolchain pinning here.
JOBS ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc)
PREFIX ?= $(HOME)/Projects/toolchains/sdl3

build/CMakeCache.txt:
	CMAKE_PREFIX_PATH=$(PREFIX) cmake -B build -DCMAKE_BUILD_TYPE=Release

.PHONY: all test lint install-check run san tsan flagship clean
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

# ASan + UBSan over the whole suite, its own tree. The device tests
# still SKIP without a GPU; what this gates is the host half.
# NOT RUNNABLE on macOS 26+: the AppleClang sanitizer runtime spins in
# get_dyld_hdr() during its own startup, before main. .github/
# workflows/weekly.yml runs both sanitizers on Linux instead.
san:
	CMAKE_PREFIX_PATH=$(PREFIX) cmake -B build-san -DCMAKE_BUILD_TYPE=Debug \
	    -DSIMVIEW_SANITIZE=address,undefined
	cmake --build build-san -j $(JOBS)
	ctest --test-dir build-san --output-on-failure --timeout 300 \
	    -E installed_surface

# TSan over the threaded layer alone — the Executor/Channel handoff is
# the only place in the library where two threads meet.
tsan:
	CMAKE_PREFIX_PATH=$(PREFIX) cmake -B build-tsan -DCMAKE_BUILD_TYPE=Debug \
	    -DSIMVIEW_SANITIZE=thread
	cmake --build build-tsan -j $(JOBS)
	ctest --test-dir build-tsan --output-on-failure --timeout 300 -L pure

# The flagship (examples/xy-gpu) needs tensor's compiler, so no runner
# and no in-tree build reaches it — this is the local gate that keeps
# it from rotting when the door changes.
flagship: all
	$(MAKE) -C examples/xy-gpu
	$(MAKE) -C examples/ising

clean:
	rm -rf build build-san build-tsan
