.DEFAULT_GOAL := all

# The SYSTEM compiler on purpose: simview is plain C++20 and must
# build with AppleClang/gcc/MSVC — no toolchain pinning here.
JOBS ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc)
PREFIX ?= $(HOME)/Projects/toolchains/sdl3

build/CMakeCache.txt:
	CMAKE_PREFIX_PATH=$(PREFIX) cmake -B build -DCMAKE_BUILD_TYPE=Release

.PHONY: all test lint install-check run debug san tsan flagship validate trace bench clean
all: build/CMakeCache.txt
	cmake --build build -j $(JOBS)

# -O0 -g, its own tree: the checks and the in-tree examples steppable.
# The GPU examples have their own (make -C examples/ising debug).
build-debug/CMakeCache.txt:
	CMAKE_PREFIX_PATH=$(PREFIX) cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug

debug: build-debug/CMakeCache.txt
	cmake --build build-debug -j $(JOBS)

# Every gate runs with the waits BOUNDED (SIMVIEW_WAIT_MS, and through
# it gpud's): a hang is then a sentence and a red line, not a stalled
# job. 20 s clears any dispatch a check makes on lavapipe.
WAIT_MS := SIMVIEW_WAIT_MS=20000
test: all
	$(WAIT_MS) ctest --test-dir build --output-on-failure

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

# TSan over the threaded layer alone — the Executor and the Sync gate
# are where two threads meet; the check rotates fresh buffers on
# gpud's mock, so it needs no device.
tsan:
	CMAKE_PREFIX_PATH=$(PREFIX) cmake -B build-tsan -DCMAKE_BUILD_TYPE=Debug \
	    -DSIMVIEW_SANITIZE=thread
	cmake --build build-tsan -j $(JOBS)
	ctest --test-dir build-tsan --output-on-failure --timeout 300 -L pure

# Everything under the validation layers, to an EXIT CODE: the suite
# and the four windowed showcases (SIMVIEW_FRAMES bounds them) with
# Khronos synchronization validation aborting on the first error —
# and on Darwin a second pass under Metal's own API and shader
# validation, the oracle that found the residency use-after-free when
# no Vulkan-level tool could name it. Release tree: layers need no
# debug build. The four swap-era defects were all invisible to a green
# suite and all named here.
VALIDATE_EXAMPLES := hello-window gas ising-cpu plots orbit shapes
validate: all
	$(WAIT_MS) SIMVIEW_VVL=sync,abort ctest --test-dir build \
	    --output-on-failure --timeout 300 -E installed_surface
	for x in $(VALIDATE_EXAMPLES); do \
	    $(WAIT_MS) SIMVIEW_VVL=sync,abort SIMVIEW_FRAMES=120 \
	    ./build/examples/$$x/$$x || exit 1; done
ifeq ($(shell uname),Darwin)
	MTL_DEBUG_LAYER=1 MTL_DEBUG_LAYER_ERROR_MODE=assert \
	    MTL_SHADER_VALIDATION=1 $(WAIT_MS) SIMVIEW_VVL=sync,abort \
	    ctest --test-dir build --output-on-failure --timeout 300 \
	    -E installed_surface
	for x in $(VALIDATE_EXAMPLES); do \
	    MTL_DEBUG_LAYER=1 MTL_DEBUG_LAYER_ERROR_MODE=assert \
	    MTL_SHADER_VALIDATION=1 $(WAIT_MS) SIMVIEW_VVL=sync,abort \
	    SIMVIEW_FRAMES=120 ./build/examples/$$x/$$x || exit 1; done
endif

# What a crowd of instanced geometry costs. A REPORT, not a gate: it
# prints numbers and passes no judgement on them.
bench: all
	SIMVIEW_TIMINGS=1 $(WAIT_MS) ./build/bench/bench_instances

# The tracing build, its own tree: Tracy's client linked, every zone
# and both GPU contexts live. Run any example from it and connect
# ~/Projects/toolchains/tracy/bin/tracy-profiler (or tracy-capture -o
# x.tracy -s 10 for a file); docs/debugging.md has the recipe. The
# flagship traces from its own tree: make -C examples/ising trace.
trace:
	CMAKE_PREFIX_PATH=$(PREFIX) cmake -B build-trace \
	    -DCMAKE_BUILD_TYPE=Release -DSIMVIEW_TRACE=ON
	cmake --build build-trace -j $(JOBS)

# The flagship (examples/xy-gpu) needs tensor's compiler, so no runner
# and no in-tree build reaches it — this is the local gate that keeps
# it from rotting when the door changes.
flagship: all
	$(MAKE) -C examples/xy-gpu
	$(MAKE) -C examples/ising

clean:
	rm -rf build build-debug build-san build-tsan build-trace
