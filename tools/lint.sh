#!/bin/sh
# The surface hygiene gate (CLAUDE.md's include graph, mechanical):
#   (a) core public headers include only std and sibling simview headers
#   (b) SDK tokens (SDL_/ImGui/ImPlot/gpud) are forbidden in the core
#       surface — only the opt-in doors may speak them
#   (c) gpud.h may include gpud's SDK-free interface header
#       <gpud/Device.h> and nothing else foreign — never SDL's
#   (e) include/ carries no comments: the public surface explains
#       itself or gets renamed until it does
#   (f) examples never print: a showcase draws, it does not narrate,
#       and the library reports its own failures
#   (g) clang-format, pinned to major 20: a missing or mismatched tool
#       is a NAMED SKIP, never a failure — a format gate failing on
#       its tool version says nothing about the code
set -eu
cd "$(dirname "$0")/.."
fail=0

for h in include/simview/*.h; do
    base=$(basename "$h")
    # (a): every include is <std-ish> or "sibling.h"
    bad_inc=$(grep -nE '^#include' "$h" | grep -vE '#include <[a-z_]+>' \
              | grep -vE '#include "(Types|App|Event|Field|Plots|Sync|simview)\.h"' || true)
    if [ "$base" = "gpud.h" ]; then
        bad_inc=$(echo "$bad_inc" | grep -v '#include <gpud/Device\.h>' || true)
    fi
    if [ -n "$bad_inc" ]; then
        echo "LINT: $base: non-std, non-sibling include:"; echo "$bad_inc"
        fail=1
    fi
    # (b)/(c): SDK tokens
    if [ "$base" = "gpud.h" ]; then
        if grep -nE 'SDL_|ImGui|ImPlot' "$h"; then
            echo "LINT: gpud.h names an SDK token — it speaks gpud only"
            fail=1
        fi
        if grep -nE '#include <gpud/(Sdl|Auto|Mock|Vulkan|Metal|Cuda)' "$h"; then
            echo "LINT: gpud.h includes a gpud backend header — Device.h only"
            fail=1
        fi
    else
        if grep -nE 'SDL_|ImGui|ImPlot|gpud' "$h"; then
            echo "LINT: $base names an SDK token — only the doors may"
            fail=1
        fi
    fi
done

# (g)
CF=/opt/homebrew/bin/clang-format
command -v "$CF" >/dev/null 2>&1 || CF=clang-format
if command -v "$CF" >/dev/null 2>&1 \
        && "$CF" --version | grep -q 'version 20\.'; then
    # shellcheck disable=SC2046
    if ! "$CF" --dry-run -Werror \
            $(git ls-files '*.cpp' '*.h' | grep -v bytecode) 2>&1; then
        echo "LINT: clang-format drift - run clang-format -i on the above"
        fail=1
    fi
else
    echo "SKIP: clang-format major 20 not found - format gate not run"
fi

# (f)
if grep -n 'printf\|std::cout\|std::cerr\|std::print\|puts(\|SDL_Log' \
        examples/*/*.cpp examples/*/*.h 2>/dev/null; then
    echo "LINT: an example prints — showcases draw, the library reports"
    fail=1
fi

# (e)
if grep -n '//\|/\*' include/simview/*.h; then
    echo "LINT: a comment in include/ — the surface must be self-explanatory"
    fail=1
fi

[ $fail -eq 0 ] && echo "lint: the surface is clean" || exit 1
