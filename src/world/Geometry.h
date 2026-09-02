#pragma once

#include "../core/Math.h"

#include <cstdint>
#include <vector>

namespace sv {
namespace impl {

// Positions and normals interleaved, six floats a vertex, read by the
// shader out of a plain buffer — there is no vertex input layout
// anywhere in this engine and this is not where one starts.
struct MeshData {
    std::vector<float> vertices; // x y z nx ny nz
    std::vector<std::uint32_t> indices;

    std::uint32_t vertex_count() const {
        return std::uint32_t(vertices.size() / 6);
    }
    std::uint32_t index_count() const { return std::uint32_t(indices.size()); }
    std::uint32_t triangle_count() const { return index_count() / 3; }
};

// A unit cube, [-0.5, 0.5] on every axis, six faces that do NOT share
// vertices: a cube's corners have three normals each, and a shared
// vertex could only carry one of them.
MeshData make_cube();

// `subdivisions` is per face and per axis: 0 gives 12 triangles, 2
// gives 108, 16 gives 3468. The cube-to-sphere map is the analytic one.
MeshData make_sphere(unsigned subdivisions);

} // namespace impl
} // namespace sv
