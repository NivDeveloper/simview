#!/bin/sh
# The surface hygiene gate (CLAUDE.md's include graph, mechanical):
#   (a) core public headers include only std and sibling simview headers
#   (b) SDL_/ImGui/ImPlot tokens are forbidden in the core surface
#   (c) native.h may NAME SDL types (forward declarations) but must
#       not INCLUDE anything of SDL's
#   (d) the attic never re-enters the build
#   (e) include/ carries no comments: the public surface explains
#       itself or gets renamed until it does
#   (f) examples never print: a showcase draws, it does not narrate,
#       and the library reports its own failures
set -eu
cd "$(dirname "$0")/.."
fail=0

for h in include/simview/*.h; do
    base=$(basename "$h")
    # (a): every include is <std-ish> or "sibling.h"
    bad_inc=$(grep -nE '^#include' "$h" | grep -vE '#include <[a-z_]+>' \
              | grep -vE '#include "(Types|App|Event|Field|Plots|Sync|simview)\.h"' || true)
    if [ -n "$bad_inc" ]; then
        echo "LINT: $base: non-std, non-sibling include:"; echo "$bad_inc"
        fail=1
    fi
    # (b)/(c): SDK tokens
    if [ "$base" = "native.h" ]; then
        if grep -nE '#include.*SDL' "$h"; then
            echo "LINT: native.h includes SDL — forward declarations only"
            fail=1
        fi
    else
        if grep -nE 'SDL_|ImGui|ImPlot' "$h"; then
            echo "LINT: $base names an SDK token — only native.h may"
            fail=1
        fi
    fi
done

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

# (d)
if grep -rn "attic" CMakeLists.txt examples/*/CMakeLists.txt \
       tests/*/CMakeLists.txt 2>/dev/null; then
    echo "LINT: the attic is referenced from the build"
    fail=1
fi

[ $fail -eq 0 ] && echo "lint: the surface is clean" || exit 1
