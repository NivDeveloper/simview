#pragma once

// Internal to src/ — one stage's program: committed SPIR-V plus the
// entry point slangc kept (-fvk-use-entrypoint-name; NVRHI passes it
// to pipeline creation, and a mismatch dies inside the driver's SPIRV
// consumer).
//
// Down here because both strata carry them and neither owns the idea:
// a 2D kind and a 3D world item each name their stages this way.

namespace sv {

struct Shader {
    const unsigned char *code = nullptr;
    unsigned len = 0;
    const char *entry = nullptr;
};

} // namespace sv
