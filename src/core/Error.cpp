// The one error channel: a refusal is a null handle or a false, with
// its sentence logged at the refusal site and kept for LastError().
// Thread-local, because two Apps on two threads must not read each
// other's sentence.

#include "Engine.h"

#include <simview/Types.h>

namespace sv {

namespace {
thread_local std::string g_error;
} // namespace

void set_error(std::string msg) {
    g_error = std::move(msg);
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "simview: %s", g_error.c_str());
}

namespace impl {

const char *version() { return SIMVIEW_VERSION; }
const char *last_error() { return g_error.c_str(); }

} // namespace impl
} // namespace sv
