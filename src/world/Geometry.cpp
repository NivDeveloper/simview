// The built-in shapes. Both are the same walk: six faces, each a grid
// of quads, laid out from a basis; the sphere then pushes every vertex
// onto the unit sphere and takes the direction as its normal.

#include "Geometry.h"

#include <cmath>

namespace sv {
namespace impl {
namespace {

struct Face {
    Vec3 normal, right, up;
};

// The six faces, wound so a front face is counter-clockwise seen from
// outside. Nothing culls today, but a mesh whose winding is arbitrary
// cannot start culling later without a hunt.
constexpr Face kFaces[6] = {
    {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
    {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
    {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
};

// A subdivided cube: `n` vertices along each edge of each face. The
// faces are independent, so a corner appears three times with three
// normals, which is what makes a cube's edges look like edges.
MeshData faceted_cube(unsigned n, bool spherical) {
    MeshData m;
    const float h = 0.5f;
    const float step = n > 1 ? 1.0f / float(n - 1) : 0.0f;
    m.vertices.reserve(std::size_t(6) * n * n * 6);
    m.indices.reserve(std::size_t(6) * (n - 1) * (n - 1) * 6);

    for (const Face &f : kFaces) {
        const std::uint32_t base = std::uint32_t(m.vertices.size() / 6);
        for (unsigned j = 0; j < n; ++j)
            for (unsigned i = 0; i < n; ++i) {
                const float a = (float(i) * step * 2.0f - 1.0f) * h;
                const float b = (float(j) * step * 2.0f - 1.0f) * h;
                Vec3 p = f.normal * h + f.right * a + f.up * b;
                Vec3 nrm = f.normal;
                if (spherical) {
                    // Cobb's map: it spreads a subdivided cube evenly
                    // over the sphere, where normalizing alone crowds
                    // the vertices into the corners.
                    const Vec3 u = p * (1.0f / h);
                    const float x2 = u.x * u.x, y2 = u.y * u.y, z2 = u.z * u.z;
                    const Vec3 s{u.x * std::sqrt(1.0f - 0.5f * y2 - 0.5f * z2 +
                                                 y2 * z2 / 3.0f),
                                 u.y * std::sqrt(1.0f - 0.5f * x2 - 0.5f * z2 +
                                                 x2 * z2 / 3.0f),
                                 u.z * std::sqrt(1.0f - 0.5f * x2 - 0.5f * y2 +
                                                 x2 * y2 / 3.0f)};
                    nrm = normalize(s);
                    p = nrm * h;
                }
                m.vertices.insert(m.vertices.end(),
                                  {p.x, p.y, p.z, nrm.x, nrm.y, nrm.z});
            }

        for (unsigned j = 0; j + 1 < n; ++j)
            for (unsigned i = 0; i + 1 < n; ++i) {
                const std::uint32_t v = base + j * n + i;
                m.indices.insert(m.indices.end(),
                                 {v, v + n, v + 1, v + 1, v + n, v + n + 1});
            }
    }
    return m;
}

} // namespace

MeshData make_cube() { return faceted_cube(2, false); }

MeshData make_sphere(unsigned subdivisions) {
    return faceted_cube(subdivisions + 2, true);
}

} // namespace impl
} // namespace sv
