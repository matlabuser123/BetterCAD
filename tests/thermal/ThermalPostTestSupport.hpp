#pragma once

// Shared fixtures for the P18-POST-001 tests.
//
// THE ORACLES ARE INDEPENDENT BY CONSTRUCTION. The expected gradients and fluxes are
// typed in from a hand derivation, computed from the known coefficients of an affine
// field, or obtained from `denseTetGradient`, which solves the 4 x 4 system
// `[1 x y z] [a b c d]^T = Te` with the test's own Gaussian elimination. None of it
// calls `computeTet4Geometry`, `gradientFrom` or any production recovery: the
// production path is the thing under test, and a test that asked it for the answer
// would be a mirror.

#include "ThermalSolveTestSupport.hpp"

#include <bettercad/thermal/ThermalPost.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace pstest {

using namespace sotest;
using thermal::FieldLocation;
using thermal::PostFailure;
using thermal::PostProblem;
using thermal::ThermalElementFields;
using thermal::ThermalFieldDescriptor;
using thermal::ThermalPostField;
using thermal::ThermalPostResult;
using thermal::ThermalPostState;
using thermal::TransientThermalSolution;

using Vec3 = std::array<double, 3>;
using Mat3 = std::array<Vec3, 3>;

[[nodiscard]] inline double dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
[[nodiscard]] inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
[[nodiscard]] inline double norm(const Vec3& a) { return std::sqrt(dot(a, a)); }
[[nodiscard]] inline Vec3 applyMat(const Mat3& m, const Vec3& v) { return {dot(m[0], v), dot(m[1], v), dot(m[2], v)}; }
[[nodiscard]] inline Vec3 scaled(const Vec3& v, double s) { return {v[0] * s, v[1] * s, v[2] * s}; }
[[nodiscard]] inline double maxDifference(const Vec3& a, const Vec3& b) {
    return std::max({std::abs(a[0] - b[0]), std::abs(a[1] - b[1]), std::abs(a[2] - b[2])});
}

/// The rotation by @p angle (radians) about the unit-normalised @p axis: Rodrigues' formula, written out.
/// A proper rotation: `det = +1`.
[[nodiscard]] inline Mat3 rotationAbout(Vec3 axis, double angle) {
    const double n = norm(axis);
    axis = scaled(axis, 1.0 / n);
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double t = 1.0 - c;
    const double x = axis[0];
    const double y = axis[1];
    const double z = axis[2];
    return {{{t * x * x + c, t * x * y - s * z, t * x * z + s * y},
             {t * x * y + s * z, t * y * y + c, t * y * z - s * x},
             {t * x * z - s * y, t * y * z + s * x, t * z * z + c}}};
}

[[nodiscard]] inline double determinant(const Mat3& m) { return dot(m[0], cross(m[1], m[2])); }

[[nodiscard]] inline Vec3 vec(const thermal::TemperatureGradient3& g) { return {g.x.si(), g.y.si(), g.z.si()}; }
[[nodiscard]] inline Vec3 vec(const thermal::HeatFlux3& q) { return {q.x.si(), q.y.si(), q.z.si()}; }

/// A structured tetrahedral grid of `side` nodes per direction (six positively oriented tetrahedra per cell, as
/// `makeKuhnGrid`), whose nodes are JITTERED by up to `jitter * h` in each coordinate by a deterministic generator,
/// so the tetrahedra are genuinely different in shape and orientation, then rotated by @p rotation and translated by
/// @p translation. Node `(i,j,k)` has handle `(k*side + j)*side + i + 1`, so unknown `(k*side + j)*side + i`.
struct GridSpec {
    std::size_t side = 4;
    double h = 0.01;
    double jitter = 0.18;
    std::uint64_t seed = 12345;
    Mat3 rotation{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    Vec3 translation{0, 0, 0};
};

[[nodiscard]] inline AsmHand makeGrid(const GridSpec& spec) {
    const std::size_t side = spec.side;
    const auto index = [side](std::size_t i, std::size_t j, std::size_t k) { return (k * side + j) * side + i; };
    std::uint64_t state = spec.seed;
    const auto next = [&state]() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>((state >> 33) % 2001) / 1000.0 - 1.0;  // in [-1, 1]
    };
    std::vector<Vec3> raw(side * side * side);
    for (std::size_t k = 0; k < side; ++k) {
        for (std::size_t j = 0; j < side; ++j) {
            for (std::size_t i = 0; i < side; ++i) {
                Vec3 p{spec.h * static_cast<double>(i), spec.h * static_cast<double>(j), spec.h * static_cast<double>(k)};
                // Boundary nodes keep their coordinate in the direction normal to the face so the body stays a block;
                // interior directions are jittered.
                const auto interior = [side](std::size_t v) { return v > 0 && v + 1 < side; };
                const double jx = next();
                const double jy = next();
                const double jz = next();
                p[0] += interior(i) ? spec.jitter * spec.h * jx : 0.0;
                p[1] += interior(j) ? spec.jitter * spec.h * jy : 0.0;
                p[2] += interior(k) ? spec.jitter * spec.h * jz : 0.0;
                raw[index(i, j, k)] = p;
            }
        }
    }
    Nodes nodes;
    for (std::size_t n = 0; n < raw.size(); ++n) {
        const Vec3 moved = applyMat(spec.rotation, raw[n]);
        nodes.emplace_back(static_cast<NodeId::ValueType>(n + 1),
                           at(moved[0] + spec.translation[0], moved[1] + spec.translation[1], moved[2] + spec.translation[2]));
    }
    const auto determinantOf = [&](const std::array<std::size_t, 4>& t) {
        const Vec3 a = raw[t[0]];
        const Vec3 e1{raw[t[1]][0] - a[0], raw[t[1]][1] - a[1], raw[t[1]][2] - a[2]};
        const Vec3 e2{raw[t[2]][0] - a[0], raw[t[2]][1] - a[1], raw[t[2]][2] - a[2]};
        const Vec3 e3{raw[t[3]][0] - a[0], raw[t[3]][1] - a[1], raw[t[3]][2] - a[2]};
        return dot(e1, cross(e2, e3));
    };
    std::vector<std::array<std::size_t, 4>> tets;
    for (std::size_t k = 0; k + 1 < side; ++k) {
        for (std::size_t j = 0; j + 1 < side; ++j) {
            for (std::size_t i = 0; i + 1 < side; ++i) {
                const std::size_t n000 = index(i, j, k);
                const std::size_t n100 = index(i + 1, j, k);
                const std::size_t n010 = index(i, j + 1, k);
                const std::size_t n110 = index(i + 1, j + 1, k);
                const std::size_t n001 = index(i, j, k + 1);
                const std::size_t n101 = index(i + 1, j, k + 1);
                const std::size_t n011 = index(i, j + 1, k + 1);
                const std::size_t n111 = index(i + 1, j + 1, k + 1);
                const std::array<std::array<std::size_t, 4>, 6> cell{{{n000, n100, n110, n111}, {n000, n100, n101, n111},
                                                                      {n000, n010, n110, n111}, {n000, n010, n011, n111},
                                                                      {n000, n001, n101, n111}, {n000, n001, n011, n111}}};
                for (std::array<std::size_t, 4> t : cell) {
                    if (determinantOf(t) < 0.0) {
                        std::swap(t[2], t[3]);
                    }
                    tets.push_back(t);
                }
            }
        }
    }
    return makeAsmHand(nodes, {}, tets);
}

/// The position of node @p id in @p hand, as plain doubles.
[[nodiscard]] inline Vec3 positionOf(const AsmHand& hand, NodeId id) {
    const meshing::Node* node = hand.mesh.findNode(id);
    REQUIRE(node != nullptr);
    return {node->position.x.si(), node->position.y.si(), node->position.z.si()};
}

/// The nodal temperatures of @p hand for the scalar field @p field, in `ThermalDofIndex` order (ascending `NodeId`,
/// which is `mesh.nodes()` order).
[[nodiscard]] inline std::vector<Temperature> fieldOn(const AsmHand& hand, const std::function<double(const Vec3&)>& field) {
    std::vector<Temperature> out;
    for (const meshing::Node& node : hand.mesh.nodes()) {
        out.push_back(Temperature::fromSi(field({node.position.x.si(), node.position.y.si(), node.position.z.si()})));
    }
    return out;
}

/// `T = a + b x + c y + d z`.
[[nodiscard]] inline std::function<double(const Vec3&)> affineField(double a, double b, double c, double d) {
    return [=](const Vec3& p) { return a + b * p[0] + c * p[1] + d * p[2]; };
}

/// A deterministic NON-affine nodal field: unrelated to the coordinates, so every tetrahedron has its own gradient.
[[nodiscard]] inline std::vector<Temperature> scatteredField(const AsmHand& hand, std::uint64_t seed, double base = 300.0,
                                                             double spread = 50.0) {
    std::uint64_t state = seed;
    std::vector<Temperature> out;
    for (std::size_t i = 0; i < hand.mesh.nodes().size(); ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        out.push_back(Temperature::fromSi(base + spread * static_cast<double>((state >> 33) % 1000) / 1000.0));
    }
    return out;
}

/// The gradient of the linear field through four (position, temperature) pairs, by a dense solve of
/// `[1 x y z] [a b c d]^T = T`. THE TEST'S OWN ORACLE: nothing production is called.
[[nodiscard]] inline Vec3 denseTetGradient(const std::array<Vec3, 4>& corners, const std::array<double, 4>& temperatures) {
    Dense2 a(4, std::vector<double>(4, 0.0));
    std::vector<double> b(4);
    for (std::size_t i = 0; i < 4; ++i) {
        a[i] = {1.0, corners[i][0], corners[i][1], corners[i][2]};
        b[i] = temperatures[i];
    }
    const std::vector<double> coefficients = denseSolve(a, b);
    return {coefficients[1], coefficients[2], coefficients[3]};
}

/// The independent gradient oracle for tetrahedron number @p ordinal of @p hand under the nodal field @p temperatures.
[[nodiscard]] inline Vec3 oracleGradient(const AsmHand& hand, const std::vector<Temperature>& temperatures,
                                         std::size_t ordinal) {
    const meshing::Tetrahedron& tet = hand.mesh.tetrahedra()[ordinal];
    std::array<Vec3, 4> corners;
    std::array<double, 4> values{};
    for (std::size_t i = 0; i < 4; ++i) {
        corners[i] = positionOf(hand, tet.nodes[i]);
        // The nodal temperature of THIS corner, through the numbering the test builds by itself: the position of the
        // node in the ascending node list, never the handle used as an index.
        std::size_t position = 0;
        for (const meshing::Node& node : hand.mesh.nodes()) {
            if (node.id == tet.nodes[i]) {
                break;
            }
            ++position;
        }
        values[i] = temperatures[position].si();
    }
    return denseTetGradient(corners, values);
}

[[nodiscard]] inline ThermalElementFields mustRecover(const AsmHand& hand, double conductivity,
                                                      const std::vector<Temperature>& temperatures) {
    auto recovered = thermal::recoverThermalElementFields(hand.mesh, hand.dofs, ThermalConductivity::fromSi(conductivity), temperatures);
    INFO((recovered.has_value() ? std::string{} : recovered.error().error.message));
    REQUIRE(recovered.has_value());
    return std::move(*recovered);
}

template <typename T>
[[nodiscard]] std::optional<PostProblem> postProblemOf(const std::expected<T, PostFailure>& result) {
    if (result.has_value()) {
        return std::nullopt;
    }
    return result.error().problem;
}

} // namespace pstest
