#pragma once

// Internal to src/ — the one error channel. A refusal is a null
// handle or a false, with its sentence logged at the refusal site and
// kept for LastError(). Every layer may call it; it is L0.

#include <string>

namespace sv {

void set_error(std::string msg);

}
