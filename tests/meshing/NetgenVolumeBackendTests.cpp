// P16-VOL-001: the backend seam (ADR-033).
//
// These drive `generateTetrahedra` with SYNTHETIC boundaries, not CAD
// geometry, so a failure here is the backend's or the adapter's and cannot be
// the kernel's. The end-to-end pipeline is VolumeMeshTests.cpp's.
//
// Nothing in this file mentions nglib. The seam speaks in Point3D and indices,
// which is the containment ADR-033 requires and which
// tests/architecture/CheckLayering.cmake enforces independently.
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/VolumeBackend.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace bettercad;
using namespace bettercad::meshing;

namespace {

[[nodiscard]] Point3D mm(double x, double y, double z) {
    return Point3D{Length::fromSi(x * 1e-3), Length::fromSi(y * 1e-3), Length::fromSi(z * 1e-3)};
}

using Tri = std::array<std::uint32_t, 3>;

/// The reference tetrahedron, 10 mm on its axis-aligned legs.
///
/// The four faces are wound to face OUT, which is the same convention
/// `signedVolume` fixes for the element itself: with a positive signed volume
/// on (0,1,2,3), these are the outward windings.
struct ReferenceTet {
    std::vector<Point3D> points{mm(0, 0, 0), mm(10, 0, 0), mm(0, 10, 0), mm(0, 0, 10)};
    std::vector<Tri> triangles{{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {0, 3, 2}};
};

/// A closed, outward-oriented 10 mm cube: 8 corners, 12 triangles.
struct ReferenceCube {
    std::vector<Point3D> points{mm(0, 0, 0),   mm(10, 0, 0),  mm(10, 10, 0),  mm(0, 10, 0),
                                mm(0, 0, 10),  mm(10, 0, 10), mm(10, 10, 10), mm(0, 10, 10)};
    std::vector<Tri> triangles{
        {0, 3, 2}, {0, 2, 1}, // z = 0, outward -z
        {4, 5, 6}, {4, 6, 7}, // z = 10, outward +z
        {0, 1, 5}, {0, 5, 4}, // y = 0, outward -y
        {1, 2, 6}, {1, 6, 5}, // x = 10, outward +x
        {2, 3, 7}, {2, 7, 6}, // y = 10, outward +y
        {3, 0, 4}, {3, 4, 7}, // x = 0, outward -x
    };
};

} // namespace

TEST_CASE("VolumeBackend_RefusesAnEmptyBoundary", "[meshing][vol][backend]") {
    const Result<VolumeBackendMesh> empty = generateTetrahedra(VolumeBackendRequest{});
    REQUIRE_FALSE(empty.has_value());
}

TEST_CASE("VolumeBackendFailure_EveryReasonHasADistinctName", "[meshing][vol][backend]") {
    constexpr std::array reasons{
        VolumeBackendFailure::NotAvailable,       VolumeBackendFailure::EmptyBoundary,
        VolumeBackendFailure::MalformedRequest,   VolumeBackendFailure::InvalidElementSize,
        VolumeBackendFailure::SurfaceRejected,    VolumeBackendFailure::GenerationFailed,
        VolumeBackendFailure::NoTetrahedra,       VolumeBackendFailure::InconsistentOutput,
    };
    std::vector<std::string_view> names;
    for (const VolumeBackendFailure reason : reasons) {
        const std::string_view name = toString(reason);
        INFO("reason " << static_cast<int>(reason));
        CHECK_FALSE(name.empty());
        names.push_back(name);
    }
    std::ranges::sort(names);
    CHECK(std::ranges::adjacent_find(names) == names.end());
}

#ifdef BETTERCAD_TESTS_EXPECT_NETGEN

TEST_CASE("VolumeBackend_FillsTheReferenceTetrahedronWithOneElement",
          "[meshing][vol][backend][tet]") {
    const ReferenceTet tet;
    const Result<VolumeBackendMesh> filled = generateTetrahedra(
        VolumeBackendRequest{.points = tet.points, .triangles = tet.triangles});
    REQUIRE(filled.has_value());

    // A tetrahedron needs exactly one tetrahedron and no new nodes. Anything
    // else means the backend refined geometry it was asked to fill.
    CHECK(filled->points.size() == 4);
    REQUIRE(filled->tetrahedra.size() == 1);

    // The element names all four nodes, each once.
    std::set<std::uint32_t> named(filled->tetrahedra.front().begin(),
                                  filled->tetrahedra.front().end());
    CHECK(named.size() == 4);
}

TEST_CASE("VolumeBackend_FillsACubeAndKeepsItsCorners", "[meshing][vol][backend][cube]") {
    const ReferenceCube cube;
    const Result<VolumeBackendMesh> filled = generateTetrahedra(
        VolumeBackendRequest{.points = cube.points, .triangles = cube.triangles});
    REQUIRE(filled.has_value());

    CHECK(filled->tetrahedra.size() >= 5); // a cube cannot be filled with fewer
    CHECK(filled->points.size() >= 8);

    // Every element index resolves, which the adapter also checks but which is
    // worth asserting independently of it.
    for (const std::array<std::uint32_t, 4>& tet : filled->tetrahedra) {
        for (const std::uint32_t index : tet) {
            CHECK(index < filled->points.size());
        }
        std::set<std::uint32_t> named(tet.begin(), tet.end());
        CHECK(named.size() == 4);
    }
}

TEST_CASE("VolumeBackend_RefusesAnOpenSurfaceRatherThanReportingSuccess",
          "[meshing][vol][backend][failure]") {
    // THE TEST THIS WHOLE SEAM EXISTS FOR.
    //
    // A tetrahedron missing one face. nglib returns NG_OK -- success -- having
    // printed its own complaint, and produces zero tetrahedra. A backend's
    // return code is not evidence, so the adapter checks the element count and
    // turns this into a failure. Expect Netgen to print "Meshing of domain 1
    // failed" while this runs; that is the backend being honest in the one
    // channel it has, and it is not an error in the test.
    ReferenceTet tet;
    tet.triangles.pop_back();

    const Result<VolumeBackendMesh> filled = generateTetrahedra(
        VolumeBackendRequest{.points = tet.points, .triangles = tet.triangles});
    REQUIRE_FALSE(filled.has_value());
    // Internal, not InvalidArgument: the request was well formed, the
    // dependency let us down.
    CHECK(filled.error().code == ErrorCode::Internal);
    CHECK(filled.error().message.find(toString(VolumeBackendFailure::NoTetrahedra)) !=
          std::string::npos);
}

TEST_CASE("VolumeBackend_RefusesATriangleIndexOutOfRange", "[meshing][vol][backend][failure]") {
    ReferenceTet tet;
    tet.triangles.front()[0] = 4; // one past the last point
    const Result<VolumeBackendMesh> filled = generateTetrahedra(
        VolumeBackendRequest{.points = tet.points, .triangles = tet.triangles});
    REQUIRE_FALSE(filled.has_value());
    // Checked BEFORE the backend sees it: nglib takes raw int arrays and would
    // read out of bounds rather than complain.
    CHECK(filled.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("VolumeBackend_RefusesATriangleThatRepeatsANode", "[meshing][vol][backend][failure]") {
    ReferenceTet tet;
    tet.triangles.front() = Tri{0, 1, 1};
    const Result<VolumeBackendMesh> filled = generateTetrahedra(
        VolumeBackendRequest{.points = tet.points, .triangles = tet.triangles});
    REQUIRE_FALSE(filled.has_value());
    CHECK(filled.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("VolumeBackend_RefusesANonPositiveElementSize", "[meshing][vol][backend][failure]") {
    const ReferenceTet tet;
    for (const double size : {0.0, -1.0}) {
        const Result<VolumeBackendMesh> filled =
            generateTetrahedra(VolumeBackendRequest{.points = tet.points,
                                                    .triangles = tet.triangles,
                                                    .maxElementSize = Length::fromSi(size)});
        INFO("size " << size);
        REQUIRE_FALSE(filled.has_value());
        CHECK(filled.error().code == ErrorCode::InvalidArgument);
    }
}

TEST_CASE("VolumeBackend_ReturnsEveryElementInBetterCadsOrientation",
          "[meshing][vol][backend][orientation]") {
    // THE BACKEND'S NODE-ORDERING CONVENTION, pinned rather than assumed.
    //
    // Netgen orders a tetrahedron's four nodes so that the determinant
    // det(p1-p0, p2-p0, p3-p0) comes out NEGATIVE -- the opposite of
    // BetterCAD's convention, where a positive signed volume is the valid
    // orientation (ADR-032). The adapter therefore applies a single node swap,
    // an ODD permutation, to every element it translates.
    //
    // This test exists because that translation is only correct if the
    // convention is UNIFORM. If Netgen returned a mixture, a fixed permutation
    // would be wrong for half the elements, and the honest response would be to
    // refuse the mesh rather than to canonicalise each element by measuring its
    // sign -- which would silently repair a genuinely inverted element and
    // destroy the evidence that the sign exists to carry.
    //
    // So: after translation, EVERY element is positive. A future Netgen that
    // changed convention would fail here, loudly, instead of producing a mesh
    // of uniformly inverted elements.
    const ReferenceCube cube;
    const Result<VolumeBackendMesh> filled = generateTetrahedra(
        VolumeBackendRequest{.points = cube.points, .triangles = cube.triangles});
    REQUIRE(filled.has_value());
    REQUIRE_FALSE(filled->tetrahedra.empty());

    std::size_t positive = 0;
    std::size_t negative = 0;
    std::size_t zero = 0;
    for (const std::array<std::uint32_t, 4>& tet : filled->tetrahedra) {
        const Volume volume =
            meshing::signedVolume(filled->points[tet[0]], filled->points[tet[1]],
                                  filled->points[tet[2]], filled->points[tet[3]]);
        if (volume.si() > 0.0) {
            ++positive;
        } else if (volume.si() < 0.0) {
            ++negative;
        } else {
            ++zero;
        }
    }
    INFO("positive " << positive << ", negative " << negative << ", zero " << zero);
    CHECK(negative == 0);
    CHECK(zero == 0);
    CHECK(positive == filled->tetrahedra.size());
}

TEST_CASE("VolumeBackend_IsDeterministicAcrossRepeatedCalls",
          "[meshing][vol][backend][determinism]") {
    // Five runs, as the milestone requires. Compared on node count, element
    // count AND the exact connectivity in order -- a count-only comparison
    // would pass for a backend that returned the same mesh with its elements
    // shuffled, which is not determinism a test can build on.
    const ReferenceCube cube;
    const Result<VolumeBackendMesh> first = generateTetrahedra(
        VolumeBackendRequest{.points = cube.points, .triangles = cube.triangles});
    REQUIRE(first.has_value());

    for (int run = 1; run < 5; ++run) {
        const Result<VolumeBackendMesh> again = generateTetrahedra(
            VolumeBackendRequest{.points = cube.points, .triangles = cube.triangles});
        INFO("run " << run);
        REQUIRE(again.has_value());
        REQUIRE(again->points.size() == first->points.size());
        REQUIRE(again->tetrahedra.size() == first->tetrahedra.size());
        CHECK(again->tetrahedra == first->tetrahedra);
        CHECK(again->points == first->points);
    }
}

#endif // BETTERCAD_TESTS_EXPECT_NETGEN
