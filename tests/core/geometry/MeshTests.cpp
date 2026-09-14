#include "GeometryTestSupport.hpp"
#include "TestHelpers.hpp"
#include "support/MeshAnalysis.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Mesh.hpp>
#include <bettercad/core/geometry/Primitives.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>

using namespace bettercad;
using namespace bettercad::literals;
using bettercad::test::checkSurface;
using bettercad::test::enclosedVolume;
using bettercad::test::errorCode;
using bettercad::test::requireProperties;
using bettercad::test::trianglesOf;
using Catch::Matchers::WithinRel;

namespace {

geometry::Mesh requireMesh(const geometry::Body& body, const geometry::MeshOptions& options = {}) {
    auto mesh = geometry::triangulate(body, options);
    REQUIRE(mesh.has_value());
    return *mesh;
}

} // namespace

TEST_CASE("A box triangulates exactly into a closed, outward-facing surface", "[geometry][mesh]") {
    const auto box = geometry::makeBox(100_mm, 50_mm, 20_mm);
    REQUIRE(box.has_value());
    const geometry::Mesh mesh = requireMesh(*box);

    // Six rectangles, two triangles each.
    CHECK(mesh.triangles.size() == 12);
    const auto triangles = trianglesOf(mesh);
    const auto surface = checkSurface(triangles);
    CHECK(surface.watertight());
    CHECK(surface.vertices == 8);
    // Planar faces are represented exactly; a positive volume means the
    // triangles face outwards.
    CHECK_THAT(enclosedVolume(triangles), WithinRel(100000.0, 1e-12));
    CHECK_THAT(test::surfaceArea(triangles), WithinRel(2.0 * (5000.0 + 2000.0 + 1000.0), 1e-12));
}

TEST_CASE("Curved surfaces are approximated within the requested deflection", "[geometry][mesh]") {
    const double deflectionMm = GENERATE(0.1, 0.01, 0.001);
    CAPTURE(deflectionMm);
    const auto cylinder = geometry::makeCylinder(10_mm, 20_mm);
    REQUIRE(cylinder.has_value());
    const geometry::MeshOptions options{.linearDeflection = deflectionMm * units::mm,
                                        .angularDeflection = 0.5 * units::rad};
    const auto triangles = trianglesOf(requireMesh(*cylinder, options));

    CHECK(checkSurface(triangles).watertight());
    const double exact = std::numbers::pi * 100.0 * 20.0;
    const double area = requireProperties(*cylinder).surfaceArea.in(units::mm2);
    const double volume = enclosedVolume(triangles);
    // The mesh vertices lie on the surface and the chords are inside the
    // cylinder, so the mesh is slightly smaller; the gap is at most the
    // deflection over the whole surface.
    CHECK(volume < exact);
    CHECK(exact - volume <= deflectionMm * area);
    // No vertex is off the true surface: every vertex is on the axis-aligned
    // cylinder of radius 10 or on the end caps.
    for (const auto& triangle : triangles) {
        for (const auto& v : triangle) {
            const double r = std::hypot(v[0], v[1]);
            const bool onSide = std::abs(r - 10.0) <= 1e-9;
            const bool onCap = (std::abs(v[2]) <= 1e-9 || std::abs(v[2] - 20.0) <= 1e-9) && r <= 10.0 + 1e-9;
            CHECK((onSide || onCap));
        }
    }
}

TEST_CASE("Triangulation is deterministic and leaves the body unchanged", "[geometry][mesh]") {
    const auto sphere = geometry::makeSphere(10_mm);
    REQUIRE(sphere.has_value());
    const geometry::MeshOptions coarse{.linearDeflection = 1_mm, .angularDeflection = 0.5 * units::rad};
    const geometry::MeshOptions fine{.linearDeflection = 0.01_mm, .angularDeflection = 0.1 * units::rad};

    const geometry::Mesh first = requireMesh(*sphere, coarse);
    const geometry::Mesh detailed = requireMesh(*sphere, fine);
    const geometry::Mesh again = requireMesh(*sphere, coarse);
    CHECK(detailed.triangles.size() > first.triangles.size());
    // A finer mesh in between does not change later results: the body keeps
    // no triangulation of its own.
    CHECK(again.triangles == first.triangles);
    CHECK(again.vertices == first.vertices);
    const geometry::Body copy = *sphere;
    CHECK(requireMesh(copy, coarse).vertices == first.vertices);
}

TEST_CASE("Triangulation rejects empty bodies and invalid options", "[geometry][mesh]") {
    CHECK(errorCode(geometry::triangulate(geometry::Body{})) == ErrorCode::FailedPrecondition);
    const auto box = geometry::makeBox(1_mm, 1_mm, 1_mm);
    REQUIRE(box.has_value());
    CHECK(errorCode(geometry::triangulate(*box, {.linearDeflection = 0_mm})) == ErrorCode::InvalidArgument);
    CHECK(errorCode(geometry::triangulate(*box, {.linearDeflection = -(1_mm)})) == ErrorCode::InvalidArgument);
    CHECK(errorCode(geometry::triangulate(*box, {.angularDeflection = 0.0 * units::rad})) ==
          ErrorCode::InvalidArgument);
}
