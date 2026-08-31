#pragma once

// Internal to src/ — the built-in shapes, generated rather than
// shipped. Two of them, because two is what a scientific picture
// needs: a sphere for a particle and a box for a cell or a bound.
//
// Each comes in TIERS. A sphere drawn once wants to look round; a
// sphere drawn fifty thousand times wants to be cheap, and the same
// mesh cannot be both — a display-quality sphere at that count is
// thousands of triangles each and the frame is gone. So the caller
// asks for a shape and the item picks the tier from how many there
// are, which is a decision no user should have to make.

#include "Math.h"

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

// A sphere of radius 0.5, built by pushing a subdivided cube onto the
// sphere. `subdivisions` is per face and per axis: 0 gives 12
// triangles, 2 gives 108, 16 gives 3468.
//
// The cube-to-sphere map is the analytic one rather than a normalize,
// because normalizing a subdivided cube bunches vertices at the
// corners and the shading shows it.
MeshData make_sphere(unsigned subdivisions);

} // namespace impl
} // namespace sv
