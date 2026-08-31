#!/bin/sh
# The surface hygiene gate (CLAUDE.md's include graph, mechanical):
#   (a) core public headers include only std and sibling simview headers
#   (b) SDK tokens (SDL_/ImGui/ImPlot/gpud) are forbidden in the core
#       surface — only the opt-in doors may speak them
#   (c) the ONE opt-in door, gpud.h, includes only gpud's SDK-free
#       interface header. No public header names ImGui or ImPlot at
#       all: the builder is the surface, and the UI stack is an
#       implementation detail of it
#   (e) include/ carries no comments: the public surface explains
#       itself or gets renamed until it does
#   (f) examples never print: a showcase draws, it does not narrate,
#       and the library reports its own failures
#   (g) clang-format, pinned to major 20: a missing or mismatched tool
#       is a NAMED SKIP, never a failure — a format gate failing on
#       its tool version says nothing about the code. third_party/ is
#       exempt: it is upstream's code, and reformatting it would make
#       every update a conflict
#   (h) every vendored dependency records its upstream PIN and copies
#       its LICENSE (third_party/README.md is the contract)
set -eu
cd "$(dirname "$0")/.."
fail=0

for h in $(find include/simview -name "*.h" | sort); do
    base=$(basename "$h")
    # (a): every include is <std-ish> or "sibling.h"
    bad_inc=$(grep -nE '^#include' "$h" | grep -vE '#include <[a-z_]+>' \
              | grep -vE '#include "(\.\./)?((scene|sync|world)/)?(Types|App|Event|Field|Panel|Plots|Scene|Sync|Particles|Lines|World|Cloud|simview)\.h"' || true)
    if [ "$base" = "gpud.h" ]; then
        bad_inc=$(echo "$bad_inc" | grep -v '#include <gpud/Device\.h>' || true)
    fi
    if [ -n "$bad_inc" ]; then
        echo "LINT: $base: non-std, non-sibling include:"; echo "$bad_inc"
        fail=1
    fi
    # (b)/(c): SDK tokens — each door speaks ONE stack
    case "$base" in
    gpud.h)
        if grep -nE 'SDL_|ImGui|ImPlot' "$h"; then
            echo "LINT: gpud.h names another stack's token — it speaks gpud"
            fail=1
        fi
        if grep -nE '#include <gpud/(Sdl|Auto|Mock|Vulkan|Metal|Cuda)' "$h"; then
            echo "LINT: gpud.h includes a gpud backend header — Device.h only"
            fail=1
        fi ;;
    *)
        if grep -nE 'SDL_|ImGui|ImPlot|gpud' "$h"; then
            echo "LINT: $base names an SDK token — only the doors may"
            fail=1
        fi ;;
    esac
done

# (i): test-only code lives in ONE namespace and ONE place. sv::probe
# is compiled into an archive that is never installed, so a release
# build cannot contain it — this rule is what stops a new test-only
# function being written somewhere that ships instead.
stray=$(grep -rln 'sv::probe\|namespace probe' src include 2>/dev/null \
        | grep -v '^src/testing/' || true)
if [ -n "$stray" ]; then
    echo "LINT: sv::probe outside src/testing/ — test-only code must not"
    echo "      be compiled into the library:"; echo "$stray"
    fail=1
fi

# (j): the include DAG inside src/, core -> platform -> scene -> world
# -> ui.
# scene/ may name nothing from ui/ and never the composed App: a kind
# sees a device and a counter block, not the world. And the layer
# STATE headers (platform/Device.h, platform/Input.h) may not name ui/
# either — the .cpp files that orchestrate a frame or a lifecycle are
# L4 by content and may, which is why this rule is on headers and on
# scene/, not on every file under platform/.
dag=$( (grep -ln '"\.\./ui/\|core/App\.h' src/scene/*.cpp src/scene/*.h; \
        grep -ln '"\.\./ui/\|core/App\.h' src/world/*.cpp src/world/*.h; \
        grep -ln '"\.\./ui/\|core/App\.h' src/platform/Device.h src/platform/Input.h) \
       2>/dev/null || true)
if [ -n "$dag" ]; then
    echo "LINT: a layer reaches UP the DAG (core -> platform -> scene ->"
    echo "      world -> ui):"
    echo "$dag"
    fail=1
fi

# The 2D and 3D strata are SIBLINGS, and the arrow points one way: a
# world may borrow the target and the device a scene defined, a scene
# may not know a world exists.
crossed=$(grep -ln '"\.\./world/' src/scene/*.cpp src/scene/*.h 2>/dev/null || true)
if [ -n "$crossed" ]; then
    echo "LINT: scene/ reaches into world/ — the arrow points the other way:"
    echo "$crossed"
    fail=1
fi

# (g)
CF=/opt/homebrew/bin/clang-format
command -v "$CF" >/dev/null 2>&1 || CF=clang-format
if command -v "$CF" >/dev/null 2>&1 \
        && "$CF" --version | grep -q 'version 20\.'; then
    # shellcheck disable=SC2046
    if ! "$CF" --dry-run -Werror \
            $(git ls-files '*.cpp' '*.h' | grep -v bytecode \
              | grep -v '^third_party/') 2>&1; then
        echo "LINT: clang-format drift - run clang-format -i on the above"
        fail=1
    fi
else
    echo "SKIP: clang-format major 20 not found - format gate not run"
fi

# (h)
for d in third_party/*/; do
    [ -d "$d" ] || continue
    for need in PIN LICENSE; do
        if [ ! -f "$d$need" ]; then
            echo "LINT: $d has no $need (third_party/README.md's rules)"
            fail=1
        fi
    done
done

# (f)
if grep -n 'printf\|std::cout\|std::cerr\|std::print\|puts(\|SDL_Log' \
        examples/*/*.cpp examples/*/*.h 2>/dev/null; then
    echo "LINT: an example prints — showcases draw, the library reports"
    fail=1
fi

# (e)
if grep -rn '//\|/\*' --include='*.h' include/simview; then
    echo "LINT: a comment in include/ — the surface must be self-explanatory"
    fail=1
fi

[ $fail -eq 0 ] && echo "lint: the surface is clean" || exit 1
