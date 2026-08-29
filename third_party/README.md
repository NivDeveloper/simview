# third_party — vendored dependencies

What lands here: sources simview builds directly (Dear ImGui, ImPlot,
ImPlot3D when they arrive), vendored rather than fetched so a consumer
needs no package and a CI runner no network beyond the checkout.

Four rules, gated by `tools/lint.sh` rule (h):

1. **One directory per dependency**, named for it (`imgui/`, `implot/`).
2. **`PIN` records the exact upstream commit** — one line,
   `<url> <40-char sha>`, plus a date comment. A tag is not a pin.
3. **`LICENSE` is copied verbatim** from upstream.
4. **No local edits.** If upstream needs changing, carry a patch file
   beside the sources and say why in `PIN`'s comment — an edit made in
   place is invisible at the next update.

Vendored sources are exempt from the formatting gate (rule (g)): they
are upstream's code, and reformatting them would make every future
update a merge conflict. They are NOT exempt from the build's warning
flags — a vendored TU that warns gets its warnings silenced at the
target level, deliberately and visibly, never by editing the source.
