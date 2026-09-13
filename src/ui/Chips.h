#pragma once

#include <simview/Input.h>
#include <simview/Types.h>

#include <string>
#include <vector>

namespace sv {

// One entry of the key bar or a settings cell: what to press, drawn a
// keycap a word or a glyph, then what it does. An empty chip ends a
// line; `hold` is a key held with the glyph's gesture.
struct Chip {
    std::vector<std::string> caps; // the words, as a check reads them
    std::vector<int> icons;        // Icons drawn in the caps' place
    std::string joiner = " ";      // between caps: " " or " + "
    std::string hold;
    int hold_icon = -1;
    std::string label;
    bool lit = false;
    bool blank() const {
        return caps.empty() && icons.empty() && label.empty();
    }
};

// The ONE spelling of a binding: the bar and the settings page agree
// by construction.
Chip chip_for(const Binding &, const char *label, bool lit);

// The chip's caps and label as one line, for a check to read.
std::string chip_text(const Chip &);

namespace impl {

// A keycap: the word centred on a rounded slab, a pill for a pad's,
// sized from the font so it sits on the text's own line.
void keycap(const char *word, bool round, bool lit);

// A run of chips, a line each time a blank one comes.
void draw_chips(const std::vector<Chip> &);

} // namespace impl
} // namespace sv
