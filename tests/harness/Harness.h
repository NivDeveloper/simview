#pragma once

// What every headless check needs before it can check anything: a
// device-or-skip, somewhere to write, and a shot read back as pixels.
// The boilerplate lives here so a new check is the assertions and
// nothing else.

#include "harness/Bmp.h"
#include "harness/Check.h"

#include <simview/simview.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace harness {

// Unbuffered from the first line: a check killed by a timeout must
// still have said how far it got.
inline void begin() { std::setvbuf(stdout, nullptr, _IONBF, 0); }

inline std::string tmp_path(const std::string &name) {
    const char *t = std::getenv("TMPDIR");
    return std::string(t ? t : ".") + "/simview_" + name;
}

// Render the current state offscreen and read it back. False when the
// shot or the read failed — the caller's CHECK names which.
inline bool shot(sv::App &app, const std::string &name, Bmp &out) {
    const std::string p = tmp_path(name + ".bmp");
    return app.Shot(p.c_str()) && load_bmp(p, out);
}

} // namespace harness
