// P16-SIZE-001: canonical mesh sizing intent.
//
// SIZING IS NOT QUALIFIED BY ELEMENT COUNT. A backend makes discrete
// decisions, so "more tetrahedra" is weak evidence and in places no evidence
// at all. These tests measure the CHARACTERISTIC EDGE LENGTH of the
// tetrahedra -- mean, median and maximum -- which is what a sizing control is
// actually about, and check geometric validity alongside it. A finer mesh that
// breaks the geometry is not a sizing pass.
#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/MeshSizing.hpp>
#include <bettercad/meshing/MeshValidation.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <format>
#include <numbers>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;
using meshing::LocalMeshSizing;
using meshing::MeshSizingControls;
using meshing::Node;
using meshing::NodeId;
using meshing::ResolvedSizing;
using meshing::SizingIssueKind;
using meshing::SizingSelectionState;
using meshing::Tetrahedron;
using meshing::VolumeMesh;
using meshing::VolumeMeshControls;
using meshing::VolumeMeshFailure;

namespace {

constexpr double kExact = 1e-9;

/// Edge-length statistics over the tetrahedra: the characteristic scale a
/// sizing control is about.
struct EdgeStats {
    std::size_t count = 0;
    double mean = 0.0;
    double median = 0.0;
    double maximum = 0.0;
};

/// The six edges of each tetrahedron, in millimetres.
[[nodiscard]] EdgeStats edgeStatsOf(const meshing::Mesh& mesh) {
    static constexpr std::array<std::array<std::size_t, 2>, 6> kEdges{
        {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}}};
    std::vector<double> lengths;
    for (const Tetrahedron& tet : mesh.tetrahedra()) {
        for (const std::array<std::size_t, 2>& edge : kEdges) {
            const Node* a = mesh.findNode(tet.nodes[edge[0]]);
            const Node* b = mesh.findNode(tet.nodes[edge[1]]);
            if (a == nullptr || b == nullptr) {
                continue;
            }
            lengths.push_back(distance(a->position, b->position).si() * 1e3);
        }
    }
    if (lengths.empty()) {
        return EdgeStats{};
    }
    std::ranges::sort(lengths);
    EdgeStats stats;
    stats.count = lengths.size();
    stats.mean = std::accumulate(lengths.begin(), lengths.end(), 0.0) /
                 static_cast<double>(lengths.size());
    stats.median = lengths[lengths.size() / 2];
    stats.maximum = lengths.back();
    return stats;
}

/// Edge statistics for the tetrahedra whose centroid lies within @p reach mm
/// of the plane z = @p z, which is how "near the selected face" is measured.
[[nodiscard]] EdgeStats edgeStatsNearZ(const meshing::Mesh& mesh, double zMillimetres,
                                       double reachMillimetres) {
    static constexpr std::array<std::array<std::size_t, 2>, 6> kEdges{
        {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}}};
    std::vector<double> lengths;
    for (const Tetrahedron& tet : mesh.tetrahedra()) {
        double centroidZ = 0.0;
        bool resolved = true;
        for (const NodeId id : tet.nodes) {
            const Node* node = mesh.findNode(id);
            if (node == nullptr) {
                resolved = false;
                break;
            }
            centroidZ += node->position.z.si() * 1e3;
        }
        if (!resolved) {
            continue;
        }
        centroidZ /= 4.0;
        if (std::abs(centroidZ - zMillimetres) > reachMillimetres) {
            continue;
        }
        for (const std::array<std::size_t, 2>& edge : kEdges) {
            const Node* a = mesh.findNode(tet.nodes[edge[0]]);
            const Node* b = mesh.findNode(tet.nodes[edge[1]]);
            if (a != nullptr && b != nullptr) {
                lengths.push_back(distance(a->position, b->position).si() * 1e3);
            }
        }
    }
    if (lengths.empty()) {
        return EdgeStats{};
    }
    std::ranges::sort(lengths);
    EdgeStats stats;
    stats.count = lengths.size();
    stats.mean = std::accumulate(lengths.begin(), lengths.end(), 0.0) /
                 static_cast<double>(lengths.size());
    stats.median = lengths[lengths.size() / 2];
    stats.maximum = lengths.back();
    return stats;
}

/// A 40 x 40 x 40 mm block whose top (EndCap, z = 40) and bottom (StartCap,
/// z = 0) are separately nameable: two identifiable regions, which is what the
/// wrong-region test needs.
struct Block {
    Document document{"Part"};
    features::Regenerator regenerator;
    ObjectId feature{};
    ObjectId profile{};
    std::array<EntityId, 4> lines{};

    explicit Block(Length side = 40_mm, Length depth = 40_mm) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        lines = addRectangle(*sketch, 0_mm, 0_mm, side, side);
        profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = depth});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));
        requireReport(regenerator, document);
    }

    [[nodiscard]] FaceName top() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}};
    }
    [[nodiscard]] FaceName bottom() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::StartCap}};
    }

    [[nodiscard]] Result<VolumeMesh> mesh(const VolumeMeshControls& controls = {}) const {
        return meshing::volumeMeshFor(document, regenerator, feature, controls);
    }

    [[nodiscard]] VolumeMesh require_(const VolumeMeshControls& controls = {}) const {
        Result<VolumeMesh> result = mesh(controls);
        if (!result.has_value()) {
            FAIL("volume mesh refused: " << result.error().message);
        }
        return *result;
    }
};

/// A cylinder, r 6 x h 20 mm, with both discs separately nameable.
///
/// THE FIXTURE THAT CAN SHOW SIZING AT ALL, and the reason is worth stating.
/// A volume element cannot be smaller than the boundary triangles it must
/// conform to, and OCCT triangulates a PLANAR face with two triangles whatever
/// the deflection -- a 40 mm block's boundary is irreducibly 40 mm, so a global
/// target of 20 or 10 mm has nowhere to act. A curved wall subdivides with the
/// deflection, so a cylinder has a boundary fine enough for the volume target
/// to govern. This is a property of the approved pipeline (a validated surface
/// goes in, the backend only fills it), not a limit of the sizing controls;
/// refining the BOUNDARY is SurfaceMeshControls' job.
struct Cylinder {
    Document document{"Cyl"};
    features::Regenerator regenerator;
    ObjectId feature{};

    explicit Cylinder(Length radius = 6_mm, Length height = 20_mm) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        REQUIRE(sketch->addCircle(Point2D{0_mm, 0_mm}, radius).has_value());
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = height});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));
        requireReport(regenerator, document);
    }

    [[nodiscard]] FaceName top() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}};
    }
    [[nodiscard]] FaceName bottom() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::StartCap}};
    }

    [[nodiscard]] Result<VolumeMesh> mesh(const VolumeMeshControls& controls) const {
        return meshing::volumeMeshFor(document, regenerator, feature, controls);
    }
    [[nodiscard]] VolumeMesh require_(const VolumeMeshControls& controls) const {
        Result<VolumeMesh> result = mesh(controls);
        if (!result.has_value()) {
            FAIL("volume mesh refused: " << result.error().message);
        }
        return *result;
    }
};

[[nodiscard]] VolumeMeshControls globalTarget(Length size) {
    VolumeMeshControls controls;
    controls.sizing.globalTargetSize = size;
    return controls;
}

/// Global target plus a boundary fine enough not to be the binding constraint.
[[nodiscard]] VolumeMeshControls curvedTarget(Length size) {
    VolumeMeshControls controls;
    controls.sizing.globalTargetSize = size;
    // 0.05 mm: fine enough that the boundary is not the binding constraint,
    // coarse enough that the suite -- which the qualification runs three times
    // -- stays quick. Test cost is part of test design (brief 58).
    controls.surface.linearDeflection = Length::fromSi(5e-5);
    return controls;
}

} // namespace

// ---------------------------------------------------------------------------
// Validation: everything that needs no geometry
// ---------------------------------------------------------------------------

TEST_CASE("Sizing_AcceptsAPositiveFiniteGlobalTarget", "[meshing][size][validation]") {
    MeshSizingControls controls;
    controls.globalTargetSize = 5_mm;
    CHECK(meshing::validate(controls).valid());
}

TEST_CASE("Sizing_AcceptsNoGlobalTargetAtAll", "[meshing][size][validation]") {
    // Absent is valid: BetterCAD supplies its own default rather than letting
    // the backend's default decide.
    CHECK(meshing::validate(MeshSizingControls{}).valid());
}

TEST_CASE("Sizing_RejectsANonPositiveGlobalTarget", "[meshing][size][validation]") {
    for (const double size : {0.0, -1e-3, -5.0}) {
        MeshSizingControls controls;
        controls.globalTargetSize = Length::fromSi(size);
        const meshing::SizingValidationReport report = meshing::validate(controls);
        INFO("size " << size);
        REQUIRE_FALSE(report.valid());
        REQUIRE(report.issues.size() == 1);
        CHECK(report.issues.front().kind == SizingIssueKind::NonPositiveSize);
        CHECK_FALSE(report.issues.front().control.has_value()); // the global target
    }
}

TEST_CASE("Sizing_RejectsANonFiniteGlobalTarget", "[meshing][size][validation]") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (const double size : {nan, inf, -inf}) {
        MeshSizingControls controls;
        controls.globalTargetSize = Length::fromSi(size);
        const meshing::SizingValidationReport report = meshing::validate(controls);
        INFO("size " << size);
        REQUIRE_FALSE(report.valid());
        REQUIRE(report.issues.size() == 1);
        // NON-FINITE, not non-positive. The comparison that decides "positive"
        // answers false for NaN, so a naive order would report a NaN as
        // non-positive -- true, but it hides that the value is not a number.
        CHECK(report.issues.front().kind == SizingIssueKind::NonFiniteSize);
    }
}

TEST_CASE("Sizing_RejectsANonPositiveOrNonFiniteLocalTarget", "[meshing][size][validation]") {
    const FaceName face{ObjectId::fromValue(1), FaceSelector{.role = FaceRole::EndCap}};
    for (const double size : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::infinity()}) {
        MeshSizingControls controls;
        controls.local.push_back(LocalMeshSizing{face, Length::fromSi(size)});
        const meshing::SizingValidationReport report = meshing::validate(controls);
        INFO("size " << size);
        REQUIRE_FALSE(report.valid());
        CHECK(report.issues.front().control == std::size_t{0});
        CHECK(report.issues.front().face == face);
    }
}

TEST_CASE("Sizing_RejectsTwoControlsNamingTheSameFace", "[meshing][size][validation]") {
    // Two sizes for one face is a modelling mistake the author should see, so
    // it is refused rather than merged. Overlap between DIFFERENT faces is a
    // separate question and resolves by minimum.
    const FaceName face{ObjectId::fromValue(1), FaceSelector{.role = FaceRole::EndCap}};
    MeshSizingControls controls;
    controls.local.push_back(LocalMeshSizing{face, 5_mm});
    controls.local.push_back(LocalMeshSizing{face, 2_mm});

    const meshing::SizingValidationReport report = meshing::validate(controls);
    REQUIRE_FALSE(report.valid());
    REQUIRE(report.issues.size() == 1);
    CHECK(report.issues.front().kind == SizingIssueKind::DuplicateFaceControl);
    CHECK(report.issues.front().control == std::size_t{1}); // the second one
}

TEST_CASE("Sizing_RejectsAMalformedFaceSelector", "[meshing][size][validation]") {
    // An EndCap that names an entity: only a Side face takes one, and the core
    // selector validation says so. Sizing does not re-implement that rule.
    MeshSizingControls controls;
    controls.local.push_back(LocalMeshSizing{
        FaceName{ObjectId::fromValue(1),
                 FaceSelector{.role = FaceRole::EndCap, .entity = EntityId::fromValue(3)}},
        5_mm});
    const meshing::SizingValidationReport report = meshing::validate(controls);
    REQUIRE_FALSE(report.valid());
    CHECK(report.issues.front().kind == SizingIssueKind::InvalidFaceSelector);
}

TEST_CASE("Sizing_IsUnitSafeSoTenMillimetresEqualsAHundredthOfAMetre",
          "[meshing][size][units]") {
    // THE FACTOR-OF-1000 TEST. Length is dimensioned and SI internally, so
    // these are the same intent and must compare equal -- not merely produce
    // similar meshes.
    MeshSizingControls inMillimetres;
    inMillimetres.globalTargetSize = 10_mm;
    MeshSizingControls inMetres;
    inMetres.globalTargetSize = Length::fromSi(0.01);

    CHECK(inMillimetres == inMetres);
    CHECK(inMillimetres.globalTargetSize->si() == 0.01);
    // And 10 metres is NOT the same thing, which is the defect being guarded.
    MeshSizingControls tenMetres;
    tenMetres.globalTargetSize = Length::fromSi(10.0);
    CHECK_FALSE(inMillimetres == tenMetres);
}

TEST_CASE("Sizing_DefaultGlobalTargetScalesWithTheBody", "[meshing][size][default]") {
    // A CONSTANT could not serve both: a default finer than a 1 mm body is
    // ruinous, and one coarser than a 1 m body is no control at all. The
    // diagonal is scale-relative.
    const meshing::MeshBounds small{Point3D{0_mm, 0_mm, 0_mm}, Point3D{1_mm, 1_mm, 1_mm}};
    const meshing::MeshBounds large{Point3D{0_mm, 0_mm, 0_mm}, Point3D{1000_mm, 1000_mm, 1000_mm}};

    const Length smallDefault = meshing::defaultGlobalTargetSize(small);
    const Length largeDefault = meshing::defaultGlobalTargetSize(large);
    CHECK_THAT(smallDefault.si(), WithinRel(std::sqrt(3.0) * 1e-3, kExact));
    CHECK_THAT(largeDefault.si(), WithinRel(std::sqrt(3.0), kExact));
    CHECK(largeDefault.si() > smallDefault.si());

    // Always positive and finite, even for a degenerate box, because the
    // caller's contract is that a resolved target always is.
    const meshing::MeshBounds degenerate{};
    CHECK(meshing::defaultGlobalTargetSize(degenerate).si() > 0.0);
    CHECK(std::isfinite(meshing::defaultGlobalTargetSize(degenerate).si()));
}

TEST_CASE("SizingIssueKind_EveryKindHasADistinctName", "[meshing][size]") {
    constexpr std::array kinds{
        SizingIssueKind::NonPositiveSize,     SizingIssueKind::NonFiniteSize,
        SizingIssueKind::InvalidFaceSelector, SizingIssueKind::DuplicateFaceControl,
        SizingIssueKind::UnresolvedFace,      SizingIssueKind::UnsupportedFaceGeometry,
    };
    std::vector<std::string_view> names;
    for (const SizingIssueKind kind : kinds) {
        CHECK_FALSE(meshing::toString(kind).empty());
        names.push_back(meshing::toString(kind));
    }
    std::ranges::sort(names);
    CHECK(std::ranges::adjacent_find(names) == names.end());

    constexpr std::array states{SizingSelectionState::Resolved, SizingSelectionState::Unresolved,
                                SizingSelectionState::Unsupported};
    for (const SizingSelectionState state : states) {
        CHECK_FALSE(meshing::toString(state).empty());
    }
}

#ifdef BETTERCAD_TESTS_EXPECT_NETGEN

// ---------------------------------------------------------------------------
// Global sizing: coarse, medium, fine
// ---------------------------------------------------------------------------

TEST_CASE("SizeGlobal_SmallerTargetGivesAFinerCharacteristicScale",
          "[meshing][size][global]") {
    const Cylinder cylinder;
    constexpr double kCadVolume = std::numbers::pi * 6e-3 * 6e-3 * 20e-3;

    struct Row {
        const char* label;
        Length target;
    };
    const std::array<Row, 3> rows{{{"coarse", 6_mm}, {"medium", 3_mm}, {"fine", 1.5_mm}}};

    std::vector<EdgeStats> stats;
    for (const Row& row : rows) {
        const VolumeMesh mesh = cylinder.require_(curvedTarget(row.target));
        const EdgeStats edges = edgeStatsOf(mesh.mesh());
        WARN("cylinder r6 h20 mm, " << row.label << " target " << row.target.si() * 1e3
                                     << " mm: " << mesh.nodeCount() << " nodes, "
                                     << mesh.tetrahedronCount() << " tets, mean edge "
                                     << edges.mean << " mm, median " << edges.median
                                     << " mm, max " << edges.maximum << " mm, volume "
                                     << mesh.tetrahedralVolume().si() << " m^3");

        // GEOMETRIC VALIDITY AT EVERY SIZE. A finer mesh that breaks the
        // geometry is not a sizing pass.
        CHECK(meshing::validate(mesh.mesh()).dataValid());
        CHECK(mesh.conformity().conforms());
        // Curved, so the facet volume is below the CAD volume and converging:
        // the right assertion is one-sided, as P16-VOL-001 established.
        CHECK(mesh.tetrahedralVolume().si() < kCadVolume);
        CHECK(mesh.tetrahedralVolume().si() > kCadVolume * 0.99);
        CHECK(mesh.sizing().globalTargetSize == row.target);
        CHECK_FALSE(mesh.sizing().globalIsDefault);
        stats.push_back(edges);
    }

    // THE CHARACTERISTIC SCALE FALLS, asserted on the median and the mean
    // rather than on element count: a backend makes discrete decisions and a
    // count can move either way between two nearby targets.
    CHECK(stats[1].median < stats[0].median);
    CHECK(stats[2].median < stats[1].median);
    CHECK(stats[1].mean < stats[0].mean);
    CHECK(stats[2].mean < stats[1].mean);
    CHECK(stats[2].maximum < stats[0].maximum);
}

TEST_CASE("SizeGlobal_CannotBeFinerThanTheBoundaryItMustConformTo",
          "[meshing][size][global][bound]") {
    // AN ARCHITECTURAL BOUND, PINNED RATHER THAN LEFT AS A SURPRISE.
    //
    // A tetrahedron must conform to the boundary triangles, so its edges
    // cannot be shorter than they are. OCCT triangulates a PLANAR face with
    // two triangles whatever the deflection, so a 40 mm block's boundary is
    // irreducibly 40 mm and a global target of 20 or 10 mm has nowhere to act:
    // the three meshes below are the same mesh.
    //
    // This is the approved pipeline working as designed -- a validated surface
    // goes in and the backend only fills it -- and NOT a sizing defect.
    // Refining the boundary is SurfaceMeshControls' job, which is exactly the
    // separation P16-SURF-001 exists to keep. The test is here so that a
    // future reader measuring a block and finding no effect learns why from
    // the suite instead of from a debugger.
    const Block block;
    const VolumeMesh coarse = block.require_(globalTarget(20_mm));
    const VolumeMesh medium = block.require_(globalTarget(10_mm));
    WARN("block 40^3 mm (boundary is 12 triangles of 40-57 mm): target 20 mm -> "
         << coarse.tetrahedronCount() << " tets, median edge "
         << edgeStatsOf(coarse.mesh()).median << " mm; target 10 mm -> "
         << medium.tetrahedronCount() << " tets, median edge "
         << edgeStatsOf(medium.mesh()).median << " mm");

    CHECK(coarse.tetrahedronCount() == medium.tetrahedronCount());
    // Both still valid and both still recovering the volume exactly, which is
    // what makes this a bound rather than a failure.
    for (const VolumeMesh* mesh : {&coarse, &medium}) {
        CHECK(meshing::validate(mesh->mesh()).dataValid());
        CHECK(mesh->conformity().conforms());
        CHECK_THAT(mesh->tetrahedralVolume().si(),
                   WithinRel(40e-3 * 40e-3 * 40e-3, kExact));
    }

    // DELIBERATELY NOT ASSERTED HERE: that some smaller target "eventually
    // acts" on this block. Measured, a 5 mm target gives 25 tetrahedra while a
    // 4 mm target gives 12 again -- the backend's discrete decisions are not
    // monotonic on a fixture whose boundary dominates, and the brief is
    // explicit that element count must not be the gate. That sizing DOES act
    // is proven on the cylinder, where the boundary leaves room for it.
}

TEST_CASE("SizeGlobal_DefaultTargetIsBetterCadsAndIsRecorded", "[meshing][size][global]") {
    const Block block;
    const VolumeMesh mesh = block.require_();

    // No request, so BetterCAD's default applied -- and the mesh says so,
    // rather than leaving a reader to wonder what the backend chose.
    CHECK(mesh.sizing().globalIsDefault);
    CHECK(mesh.sizing().globalTargetSize.si() > 0.0);
    CHECK(std::isfinite(mesh.sizing().globalTargetSize.si()));
    // The block's diagonal: sqrt(3) * 40 mm.
    CHECK_THAT(mesh.sizing().globalTargetSize.si(), WithinRel(std::sqrt(3.0) * 40e-3, 1e-6));
}

TEST_CASE("SizeGlobal_RefusesInvalidSizesBeforeReachingTheBackend",
          "[meshing][size][global][failure]") {
    const Block block;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (const double size : {0.0, -5e-3, nan, std::numeric_limits<double>::infinity()}) {
        const Result<VolumeMesh> mesh = block.mesh(globalTarget(Length::fromSi(size)));
        INFO("size " << size);
        REQUIRE_FALSE(mesh.has_value());
        CHECK(mesh.error().code == ErrorCode::InvalidArgument);
        CHECK_THAT(std::string{mesh.error().message}, ContainsSubstring("mesh sizing"));
    }
}

TEST_CASE("SizeGlobal_AcceptsATargetCoarserThanTheBody", "[meshing][size][global][extremes]") {
    // A target larger than the whole block. Validity matters, element count
    // does not: requiring a one-element mesh would be requiring the impossible.
    const Block block;
    const VolumeMesh mesh = block.require_(globalTarget(500_mm));
    CHECK(mesh.tetrahedronCount() > 0);
    CHECK(meshing::validate(mesh.mesh()).dataValid());
    CHECK(mesh.conformity().conforms());
    CHECK_THAT(mesh.tetrahedralVolume().si(), WithinRel(40e-3 * 40e-3 * 40e-3, kExact));
}

TEST_CASE("SizeGlobal_IsScaleRobust", "[meshing][size][global][scale]") {
    // Geometrically similar blocks at 1x and 10x, with targets scaled to
    // match. A metres/millimetres slip anywhere in the chain shows up here as
    // one of the two meshing absurdly.
    const Block small(4_mm, 4_mm);
    const Block large(40_mm, 40_mm);

    const VolumeMesh smallMesh = small.require_(globalTarget(1_mm));
    const VolumeMesh largeMesh = large.require_(globalTarget(10_mm));
    const EdgeStats smallEdges = edgeStatsOf(smallMesh.mesh());
    const EdgeStats largeEdges = edgeStatsOf(largeMesh.mesh());
    WARN("scale 1x (4 mm block, 1 mm target): median edge " << smallEdges.median
         << " mm; 10x (40 mm block, 10 mm target): median edge " << largeEdges.median << " mm");

    // The 10x model's edges are ~10x longer. A wide band, because the mesher
    // is not obliged to produce similar meshes -- but a factor-of-1000 unit
    // slip would miss it by orders of magnitude.
    const double ratio = largeEdges.median / smallEdges.median;
    INFO("median edge ratio " << ratio);
    CHECK(ratio > 5.0);
    CHECK(ratio < 20.0);
    CHECK_THAT(smallMesh.tetrahedralVolume().si(), WithinRel(4e-3 * 4e-3 * 4e-3, kExact));
    CHECK_THAT(largeMesh.tetrahedralVolume().si(), WithinRel(40e-3 * 40e-3 * 40e-3, kExact));
}

// ---------------------------------------------------------------------------
// Local sizing
// ---------------------------------------------------------------------------

TEST_CASE("SizeLocal_RefinesTheSelectedFaceAndNotTheRest", "[meshing][size][local]") {
    const Cylinder cylinder;

    VolumeMeshControls controls = curvedTarget(6_mm);
    controls.sizing.local.push_back(LocalMeshSizing{cylinder.top(), 1.5_mm});

    const VolumeMesh refined = cylinder.require_(controls);
    const VolumeMesh plain = cylinder.require_(curvedTarget(6_mm));

    REQUIRE(refined.sizing().local.size() == 1);
    CHECK(refined.sizing().local.front().state == SizingSelectionState::Resolved);
    CHECK(refined.sizing().local.front().nodeCount > 0);
    // Both a point set and a slab: see BoxSizeRestriction for why the slab is
    // the one that does the work.
    CHECK_FALSE(refined.sizing().restrictions.empty());
    REQUIRE(refined.sizing().regions.size() == 1);

    // THE RESTRICTED REGION IS THE TOP. Asserted on the resolved slab rather
    // than inferred from the mesh, so a mapping error shows up here as a wrong
    // z range instead of as a confusing density measurement.
    const meshing::BoxSizeRestriction& slab = refined.sizing().regions.front();
    INFO("slab z range " << slab.min.z.si() * 1e3 << " to " << slab.max.z.si() * 1e3 << " mm");
    CHECK(slab.max.z.si() * 1e3 > 19.0);
    // Entirely in the upper half, so it is the top disc's slab and not the
    // bottom's. The slab reaches three target sizes into the material, so its
    // lower face sits at 20 - 3*1.5 = 15.5 mm.
    CHECK(slab.min.z.si() * 1e3 > 10.0);
    CHECK_THAT(slab.min.z.si() * 1e3, WithinRel(15.5, 1e-6));
    CHECK(slab.maxSize == 1.5_mm);

    // THE NON-TARGET REGION IS THE MID-BODY, not the far disc. Both end discs
    // are finely triangulated by the surface mesher, so comparing one disc
    // with the other measures the SURFACE and not the sizing control. The
    // middle of the wall is where the global target governs.
    const EdgeStats target = edgeStatsNearZ(refined.mesh(), 19.0, 3.0);
    const EdgeStats middle = edgeStatsNearZ(refined.mesh(), 10.0, 3.0);
    const EdgeStats targetPlain = edgeStatsNearZ(plain.mesh(), 19.0, 3.0);
    const EdgeStats middlePlain = edgeStatsNearZ(plain.mesh(), 10.0, 3.0);

    const auto nodesNear = [](const meshing::Mesh& m, double z, double reach) {
        std::size_t n = 0;
        for (const Node& node : m.nodes()) {
            if (std::abs(node.position.z.si() * 1e3 - z) <= reach) {
                ++n;
            }
        }
        return n;
    };

    WARN("local 1.5 mm on the TOP disc of a r6 h20 cylinder, global 6 mm: "
         << refined.nodeCount() << " nodes, " << refined.tetrahedronCount() << " tets | TARGET"
         << " region mean edge " << target.mean << " mm (" << nodesNear(refined.mesh(), 19.0, 3.0)
         << " nodes), MID-BODY " << middle.mean << " mm ("
         << nodesNear(refined.mesh(), 10.0, 3.0) << " nodes) | global-only: target "
         << targetPlain.mean << " mm (" << nodesNear(plain.mesh(), 19.0, 3.0) << " nodes), "
         << "mid-body " << middlePlain.mean << " mm, " << plain.tetrahedronCount() << " tets");

    REQUIRE(target.count > 0);
    REQUIRE(middle.count > 0);

    // 1. THE TARGET REGION IS FINER THAN IT WAS WITHOUT THE CONTROL.
    CHECK(target.mean < targetPlain.mean);
    // 2. AND DENSER.
    CHECK(nodesNear(refined.mesh(), 19.0, 3.0) > nodesNear(plain.mesh(), 19.0, 3.0));
    // 3. AND FINER THAN THE NON-TARGET REGION OF THE SAME MESH, which is what
    //    separates local refinement from meshing everything finer.
    CHECK(target.mean < middle.mean);
    // 4. AND THE REST OF THE BODY IS NOT DRAGGED DOWN WITH IT: the mid-body is
    //    no finer than it was without the control, so this is refinement and
    //    not a global size change in disguise.
    CHECK(middle.mean >= middlePlain.mean);

    CHECK(meshing::validate(refined.mesh()).dataValid());
    CHECK(refined.conformity().conforms());
}

TEST_CASE("SizeLocal_RefinesTheFaceItWasGivenAndNotTheOppositeOne",
          "[meshing][size][local][wrong-region]") {
    // THE SELECTION-MAPPING GUARD, as a CROSS-comparison.
    //
    // Both end discs are naturally fine because the surface mesher triangulates
    // them finely, so "is the top finer than the bottom" measures the surface.
    // Comparing the SAME region between two meshes -- one refining the top, one
    // refining the bottom -- isolates the control. A resolution that mapped
    // either name to the wrong face fails one of the two.
    const Cylinder cylinder;

    VolumeMeshControls onTop = curvedTarget(6_mm);
    onTop.sizing.local.push_back(LocalMeshSizing{cylinder.top(), 1.5_mm});
    VolumeMeshControls onBottom = curvedTarget(6_mm);
    onBottom.sizing.local.push_back(LocalMeshSizing{cylinder.bottom(), 1.5_mm});

    const VolumeMesh topRefined = cylinder.require_(onTop);
    const VolumeMesh bottomRefined = cylinder.require_(onBottom);

    // The slabs themselves must be at opposite ends.
    REQUIRE(topRefined.sizing().regions.size() == 1);
    REQUIRE(bottomRefined.sizing().regions.size() == 1);
    CHECK(topRefined.sizing().regions.front().min.z.si() >
          bottomRefined.sizing().regions.front().min.z.si());

    // NODE DENSITY, not mean edge length. Mean edge over a window mixes the
    // refined elements with the transition elements that grading creates
    // around them, and the mixture moved the wrong way by 8% in one direction
    // -- enough to fail a test on a mesh that is in fact refined correctly.
    // Counting nodes asks the question directly: did the mesher put more
    // points here? The brief lists node density as a sizing metric for exactly
    // this reason.
    const auto nodesNear = [](const meshing::Mesh& m, double z, double reach) {
        std::size_t n = 0;
        for (const Node& node : m.nodes()) {
            if (std::abs(node.position.z.si() * 1e3 - z) <= reach) {
                ++n;
            }
        }
        return n;
    };
    {
        const auto& ts = topRefined.sizing().regions.front();
        const auto& bs = bottomRefined.sizing().regions.front();
        WARN("SLABS  top-control slab z [" << ts.min.z.si() * 1e3 << ", " << ts.max.z.si() * 1e3
             << "] mm, h=" << ts.maxSize.si() * 1e3 << " | bottom-control slab z ["
             << bs.min.z.si() * 1e3 << ", " << bs.max.z.si() * 1e3 << "] mm, h="
             << bs.maxSize.si() * 1e3 << " | tets " << topRefined.tetrahedronCount() << " vs "
             << bottomRefined.tetrahedronCount());
    }

    const std::size_t topOfTop = nodesNear(topRefined.mesh(), 19.0, 3.0);
    const std::size_t topOfBottom = nodesNear(bottomRefined.mesh(), 19.0, 3.0);
    const std::size_t bottomOfTop = nodesNear(topRefined.mesh(), 1.0, 3.0);
    const std::size_t bottomOfBottom = nodesNear(bottomRefined.mesh(), 1.0, 3.0);

    WARN("node density -- TOP region: " << topOfTop << " when the top is refined vs "
         << topOfBottom << " when the bottom is | BOTTOM region: " << bottomOfBottom
         << " when the bottom is refined vs " << bottomOfTop << " when the top is");

    // Each region is denser in the mesh that asked for it. Both directions, so
    // neither can pass by accident.
    CHECK(topOfTop > topOfBottom);
    CHECK(bottomOfBottom > bottomOfTop);
}

TEST_CASE("SizeLocal_ResolutionDoesNotDependOnControlOrder",
          "[meshing][size][local][order]") {
    // ORDER INDEPENDENCE BY CONSTRUCTION, not by a rule someone must honour:
    // every backend restriction is a MAXIMUM, so a node claimed by two faces
    // takes the smaller size, and the smaller of two numbers does not depend
    // on which arrived first.
    const Cylinder cylinder;

    VolumeMeshControls first = curvedTarget(6_mm);
    first.sizing.local.push_back(LocalMeshSizing{cylinder.top(), 1.5_mm});
    first.sizing.local.push_back(LocalMeshSizing{cylinder.bottom(), 3_mm});

    VolumeMeshControls second = curvedTarget(6_mm);
    second.sizing.local.push_back(LocalMeshSizing{cylinder.bottom(), 3_mm});
    second.sizing.local.push_back(LocalMeshSizing{cylinder.top(), 1.5_mm});

    const VolumeMesh a = cylinder.require_(first);
    const VolumeMesh b = cylinder.require_(second);

    // The RESOLVED restrictions are identical as a set, in the same order,
    // because they are stored sorted by position.
    REQUIRE(a.sizing().restrictions.size() == b.sizing().restrictions.size());
    CHECK(a.sizing().restrictions == b.sizing().restrictions);
    // And so is the mesh.
    CHECK(a.nodeCount() == b.nodeCount());
    CHECK(a.tetrahedronCount() == b.tetrahedronCount());
    CHECK(a.tetrahedralVolume().si() == b.tetrahedralVolume().si());
}

TEST_CASE("SizeLocal_OverlappingFacesTakeTheSmallerSize", "[meshing][size][local][precedence]") {
    // A node on an edge shared by two refined faces is claimed twice. The
    // smaller size wins, in either order.
    const Cylinder cylinder;
    VolumeMeshControls controls = curvedTarget(6_mm);
    controls.sizing.local.push_back(LocalMeshSizing{cylinder.top(), 4_mm});
    controls.sizing.local.push_back(LocalMeshSizing{cylinder.bottom(), 3_mm});

    const VolumeMesh mesh = cylinder.require_(controls);
    // Every restriction is one of the two requested sizes, never something
    // in between or a sum.
    for (const meshing::SizeRestriction& restriction : mesh.sizing().restrictions) {
        const double mm = restriction.maxSize.si() * 1e3;
        INFO("restriction at z=" << restriction.at.z.si() * 1e3 << " mm is " << mm << " mm");
        CHECK((std::abs(mm - 4.0) < 1e-9 || std::abs(mm - 3.0) < 1e-9));
    }
}

TEST_CASE("SizeLocal_RefusesAFaceThatDoesNotResolve", "[meshing][size][local][unresolved]") {
    // A HoleBottom on a block that has no hole. The name is well formed and
    // names nothing, so the control is explicitly unresolved and the mesh is
    // REFUSED -- not meshed with the control quietly dropped.
    const Block block;
    VolumeMeshControls controls = globalTarget(20_mm);
    controls.sizing.local.push_back(
        LocalMeshSizing{FaceName{block.feature, FaceSelector{.role = FaceRole::HoleBottom}}, 4_mm});

    const Result<VolumeMesh> mesh = block.mesh(controls);
    REQUIRE_FALSE(mesh.has_value());
    CHECK_THAT(std::string{mesh.error().message},
               ContainsSubstring(std::string{toString(VolumeMeshFailure::SizingNotResolved)}));
}

TEST_CASE("SizeLocal_RefusesAFaceOfAnotherObject", "[meshing][size][local][unresolved]") {
    // A face name whose feature is not the one being meshed. It must not
    // resolve, and must not silently fall back to a face of this body.
    const Block block;
    VolumeMeshControls controls = globalTarget(20_mm);
    controls.sizing.local.push_back(LocalMeshSizing{
        FaceName{ObjectId::fromValue(9999), FaceSelector{.role = FaceRole::EndCap}}, 4_mm});

    const Result<VolumeMesh> mesh = block.mesh(controls);
    REQUIRE_FALSE(mesh.has_value());
    CHECK_THAT(std::string{mesh.error().message}, ContainsSubstring("did not resolve"));
}

// ---------------------------------------------------------------------------
// Regeneration, invalidation and separation
// ---------------------------------------------------------------------------

TEST_CASE("SizeLocal_SurvivesATopologyPreservingEdit", "[meshing][size][regeneration]") {
    Block block;
    VolumeMeshControls controls = globalTarget(20_mm);
    controls.sizing.local.push_back(LocalMeshSizing{block.top(), 4_mm});

    const VolumeMesh before = block.require_(controls);
    REQUIRE(before.sizing().local.front().state == SizingSelectionState::Resolved);

    // Make the block taller. The extrude still has an EndCap, so the name
    // still resolves -- the control is not re-created, it simply still works.
    REQUIRE(block.document
                .modifyObject<features::ExtrudeFeature>(
                    block.feature,
                    [](features::ExtrudeFeature& extrude) -> Result<bool> {
                        auto definition = extrude.definition();
                        definition.depth = 60_mm;
                        return extrude.setDefinition(definition).has_value();
                    })
                .has_value());
    requireReport(block.regenerator, block.document);

    const VolumeMesh after = block.require_(controls);
    REQUIRE(after.sizing().local.size() == 1);
    CHECK(after.sizing().local.front().state == SizingSelectionState::Resolved);
    CHECK(after.sizing().local.front().face == block.top());
    CHECK_THAT(after.tetrahedralVolume().si(), WithinRel(40e-3 * 40e-3 * 60e-3, kExact));

    // And the refinement followed the face to its new place.
    const EdgeStats nearNewTop = edgeStatsNearZ(after.mesh(), 60.0, 8.0);
    const EdgeStats nearBottom = edgeStatsNearZ(after.mesh(), 0.0, 8.0);
    REQUIRE(nearNewTop.count > 0);
    REQUIRE(nearBottom.count > 0);
    CHECK(nearNewTop.median < nearBottom.median);
}

TEST_CASE("Sizing_EditInvalidatesTheDerivedMesh", "[meshing][size][invalidation]") {
    const Block block;
    const VolumeMeshControls coarse = globalTarget(20_mm);
    const VolumeMesh mesh = block.require_(coarse);

    // Same controls, same geometry: current.
    CHECK_FALSE(meshing::isStale(block.document, mesh, coarse));

    // A different global target makes the mesh stale.
    CHECK(meshing::isStale(block.document, mesh, globalTarget(10_mm)));

    // So does adding a local control.
    VolumeMeshControls withLocal = coarse;
    withLocal.sizing.local.push_back(LocalMeshSizing{block.top(), 4_mm});
    CHECK(meshing::isStale(block.document, mesh, withLocal));

    // And an EQUIVALENT request does not: 20 mm expressed in metres is the
    // same intent, so nothing is invalidated.
    VolumeMeshControls sameInMetres = coarse;
    sameInMetres.sizing.globalTargetSize = Length::fromSi(0.02);
    CHECK_FALSE(meshing::isStale(block.document, mesh, sameInMetres));
}

TEST_CASE("Sizing_IsNotChangedByDisplayTessellationSettings", "[meshing][size][separation]") {
    // P16-SURF's separation, restated for sizing: the surface deflection is a
    // BOUNDARY control and the sizing is a VOLUME control. They are different
    // fields of different types, so a change to one cannot be a change to the
    // other -- and the staleness check sees them as different requests.
    const Block block;
    VolumeMeshControls a = globalTarget(20_mm);
    VolumeMeshControls b = a;
    b.surface.linearDeflection = Length::fromSi(5e-5);

    CHECK(a.sizing == b.sizing);     // sizing intent untouched
    CHECK_FALSE(a == b);             // but the request differs, so a mesh is stale
    const VolumeMesh mesh = block.require_(a);
    CHECK(meshing::isStale(block.document, mesh, b));
    CHECK(mesh.controls().sizing == a.sizing);
}

TEST_CASE("Sizing_IsNotChangedByAMaterialEdit", "[meshing][size][separation]") {
    // MESHING DOES NOT DEPEND ON MATERIAL, and this milestone must not quietly
    // make it so. Assigning a material, and later changing its density, must
    // leave the sizing intent untouched and must NOT make a derived mesh
    // stale: a mesh is a discretisation of geometry, and nothing about how
    // heavy the part is changes where the elements go.
    //
    // The converse -- a sizing edit invalidating the mesh -- is
    // Sizing_EditInvalidatesTheDerivedMesh. Both directions matter: a false
    // dependency wastes a remesh every time a property is edited, and a
    // missing one serves a stale mesh.
    Block block;
    const VolumeMeshControls controls = globalTarget(20_mm);
    const VolumeMesh mesh = block.require_(controls);
    REQUIRE_FALSE(meshing::isStale(block.document, mesh, controls));

    features::MaterialDefinition steel;
    steel.designation = "Synthetic";
    steel.mechanical.density =
        materials::MaterialProperty<Density>::known(Density::fromSi(7850.0));
    const Result<MaterialId> id =
        features::createMaterial(block.document, "Synthetic", steel);
    REQUIRE(id.has_value());
    REQUIRE(features::assignMaterial(block.document, *id).has_value());

    // The sizing intent is the same object it was.
    CHECK(mesh.controls().sizing == controls.sizing);
    // And the mesh is still current: a material assignment is not a geometry
    // change and not a sizing change.
    CHECK_FALSE(meshing::isStale(block.document, mesh, controls));

    // Changing the density afterwards likewise.
    features::MaterialDefinition lighter = steel;
    lighter.mechanical.density =
        materials::MaterialProperty<Density>::known(Density::fromSi(2700.0));
    REQUIRE(features::setMaterialDefinition(block.document, *id, lighter).has_value());
    CHECK_FALSE(meshing::isStale(block.document, mesh, controls));
}

TEST_CASE("Sizing_RefusesStaleGeometryRatherThanMeshingIt", "[meshing][size][stale]") {
    Block block;
    const VolumeMeshControls controls = globalTarget(20_mm);
    REQUIRE(block.mesh(controls).has_value());

    // Edit the profile without regenerating. P16-GEOM-001's currency check
    // refuses before sizing does anything, so sizing cannot become a way past
    // it.
    REQUIRE(block.document
                .modifyObject<sketch::Sketch>(block.profile,
                                              [&](sketch::Sketch& sk) -> Result<bool> {
                                                  return sk.addDistance(block.lines[0], 25_mm)
                                                      .has_value();
                                              })
                .has_value());

    const Result<VolumeMesh> mesh = block.mesh(controls);
    REQUIRE_FALSE(mesh.has_value());
    CHECK(mesh.error().code == ErrorCode::FailedPrecondition);
}

TEST_CASE("Sizing_IsDeterministicAcrossRepeatedGeneration", "[meshing][size][determinism]") {
    const Cylinder cylinder;
    VolumeMeshControls controls = curvedTarget(3_mm);
    controls.sizing.local.push_back(LocalMeshSizing{cylinder.top(), 1.5_mm});

    const VolumeMesh first = cylinder.require_(controls);
    for (int run = 1; run < 5; ++run) {
        const VolumeMesh again = cylinder.require_(controls);
        INFO("run " << run);
        REQUIRE(again.nodeCount() == first.nodeCount());
        REQUIRE(again.tetrahedronCount() == first.tetrahedronCount());
        CHECK(again.tetrahedralVolume().si() == first.tetrahedralVolume().si());
        CHECK(again.sizing().restrictions == first.sizing().restrictions);
        for (std::size_t i = 0; i < again.mesh().tetrahedra().size(); ++i) {
            CHECK(again.mesh().tetrahedra()[i].nodes == first.mesh().tetrahedra()[i].nodes);
        }
    }
}

#endif // BETTERCAD_TESTS_EXPECT_NETGEN
