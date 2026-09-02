#pragma once

namespace sv {

struct Shader {
    const unsigned char *code = nullptr;
    unsigned len = 0;
    const char *entry = nullptr;
};

} // namespace sv
