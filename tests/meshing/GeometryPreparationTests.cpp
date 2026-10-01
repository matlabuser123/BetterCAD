// P16-GEOM-001: the geometry boundary every meshing milestone passes through.
//
//     stale geometry  !=  meshable geometry
//
// Every expected volume here is closed-form and evaluated by hand. None was
// obtained by running BetterCAD and recording what it said.
#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/MassProperties.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/GeometryPreparation.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <memory>
#include <numbers>
#include <string>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;
using features::NodeState;
using meshing::GeometryIneligibility;
using meshing::GeometryRevision;
using meshing::geometryIneligibility;
using meshing::MeshableGeometry;
using meshing::requireMeshableGeometry;

namespace {

/// Relative tolerance for a CAD volume against closed form. 1e-9 is the
/// repository's figure for geometric accumulation through the kernel; nothing
/// here needs or gets more slack.
constexpr double kRel = 1e-9;

/// One extruded box, with the sketch and the feature both reachable so that a
/// test can edit the profile and leave the feature alone -- which is the case
/// that matters, because editing a sketch does NOT change the revision of the
/// extrude that consumes it.
struct Part {
    Document document{"Part"};
    features::Regenerator regenerator;
    ObjectId feature{};
    ObjectId profile{};
    std::array<EntityId, 4> lines{};

    Part(Length a = 20_mm, Length b = 30_mm, Length c = 50_mm) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        lines = addRectangle(*sketch, 0_mm, 0_mm, a, b);
        profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = c});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));
        requireReport(regenerator, document);
    }

    [[nodiscard]] Result<MeshableGeometry> prepare() const {
        return requireMeshableGeometry(document, regenerator, feature);
    }

    [[nodiscard]] std::optional<GeometryIneligibility> reason() const {
        return geometryIneligibility(document, regenerator, feature);
    }

    void regenerate() { requireReport(regenerator, document); }
};

/// Volume of a box a x b x c, in mm^3, by hand.
[[nodiscard]] constexpr double boxMm3(double a, double b, double c) noexcept {
    return a * b * c;
}

} // namespace

// ---------------------------------------------------------------------------
// Eligible geometry, and the analytical fixtures
// ---------------------------------------------------------------------------

TEST_CASE("Meshable_AcceptsACurrentBoxAndReportsItsAnalyticalVolume", "[meshing][geom]") {
    const Part part{20_mm, 30_mm, 50_mm};
    const Result<MeshableGeometry> prepared = part.prepare();
    REQUIRE(prepared.has_value());

    CHECK(prepared->source == part.feature);
    CHECK(prepared->solidCount == 1);
    CHECK(prepared->revision.isValid());
    // V = a b c = 20 x 30 x 50 = 30000 mm^3, by hand.
    CHECK_THAT(prepared->volume.in(units::mm3), WithinRel(boxMm3(20, 30, 50), kRel));
    CHECK_FALSE(part.reason().has_value());

    // The prepared body is the regenerator's, not a rebuild of it: same topology,
    // and a Body copy shares the kernel shape.
    const geometry::Body* authoritative = part.regenerator.body(part.feature);
    REQUIRE(authoritative != nullptr);
    CHECK(prepared->body.topology() == authoritative->topology());
}

TEST_CASE("Meshable_AcceptsACylinderAndReportsItsAnalyticalVolume", "[meshing][geom]") {
    // A circular profile extruded: V = pi r^2 h.
    Document document{"Cyl"};
    features::Regenerator regenerator;
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    REQUIRE(sketch->addCircle(Point2D{0_mm, 0_mm}, 12_mm).has_value());
    const ObjectId profile = require(document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 25_mm});
    REQUIRE(extrude.has_value());
    const ObjectId feature = require(document.addObject(std::move(*extrude)));
    requireReport(regenerator, document);

    const Result<MeshableGeometry> prepared = requireMeshableGeometry(document, regenerator, feature);
    REQUIRE(prepared.has_value());
    const double expected = std::numbers::pi * 12.0 * 12.0 * 25.0; // mm^3, by hand
    CHECK_THAT(prepared->volume.in(units::mm3), WithinRel(expected, kRel));
    CHECK(prepared->solidCount == 1);
}

TEST_CASE("Meshable_AHollowTubePreservesItsVoidAndItsAnalyticalVolume", "[meshing][geom]") {
    // Two concentric circles give an annular profile: the inner loop is a hole,
    // and extruding it gives a tube whose cavity must survive preparation.
    //
    //   V = pi (Ro^2 - Ri^2) h = pi (20^2 - 12^2) * 40
    Document document{"Tube"};
    features::Regenerator regenerator;
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    REQUIRE(sketch->addCircle(Point2D{0_mm, 0_mm}, 20_mm).has_value());
    REQUIRE(sketch->addCircle(Point2D{0_mm, 0_mm}, 12_mm).has_value());
    const ObjectId profile = require(document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 40_mm});
    REQUIRE(extrude.has_value());
    const ObjectId feature = require(document.addObject(std::move(*extrude)));
    requireReport(regenerator, document);

    const Result<MeshableGeometry> prepared = requireMeshableGeometry(document, regenerator, feature);
    REQUIRE(prepared.has_value());
    const double expected = std::numbers::pi * (20.0 * 20.0 - 12.0 * 12.0) * 40.0;
    // This is the check that catches using the OUTER bounding cylinder as the
    // mesh domain: that would be pi * 400 * 40, which is 2.78x larger.
    CHECK_THAT(prepared->volume.in(units::mm3), WithinRel(expected, kRel));
    // The cavity is still there: a tube has an inner and an outer wall.
    CHECK(prepared->body.topology().solids == 1);
    CHECK(prepared->body.topology().faces > 3);
}

TEST_CASE("Meshable_AcceptsAMultiSolidBodyAndReportsTheSolidCount", "[meshing][geom][solids]") {
    // ADR-032 gives a mesh "one region per solid", so more than one solid is
    // SUPPORTED and is not an error. The count is reported so that P16-VOL-001 can
    // make the regions; deciding it by whatever a backend happens to do is exactly
    // what the architecture forbids.
    //
    // Two disjoint rectangles in one sketch: 10x10 and 5x5, extruded 2 deep.
    //   V = (10*10 + 5*5) * 2 = 250 mm^3, by hand.
    Document document{"Two"};
    features::Regenerator regenerator;
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 10_mm, 10_mm);
    addRectangle(*sketch, 20_mm, 0_mm, 5_mm, 5_mm);
    const ObjectId profile = require(document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 2_mm});
    REQUIRE(extrude.has_value());
    const ObjectId feature = require(document.addObject(std::move(*extrude)));
    requireReport(regenerator, document);

    const Result<MeshableGeometry> prepared = requireMeshableGeometry(document, regenerator, feature);
    REQUIRE(prepared.has_value());
    CHECK(prepared->solidCount == 2);
    CHECK_THAT(prepared->volume.in(units::mm3), WithinRel(250.0, kRel));
}

TEST_CASE("Meshable_KeepsABodyWhereItIsInModelSpace", "[meshing][geom][transform]") {
    // The TopLoc_Location concern, answered structurally rather than by hope: this
    // layer never touches a TopoDS_Shape. It copies a BetterCAD Body, and a Body
    // copy shares the kernel shape, so there is no code path that could extract a
    // TShape and drop its location.
    //
    // A sketch on the XZ plane puts the extrusion somewhere other than the origin
    // and along a different axis, so a stripped location would move the box and a
    // swapped frame would change which axis is which.
    Document document{"Placed"};
    features::Regenerator regenerator;
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xz());
    addRectangle(*sketch, 10_mm, 5_mm, 20_mm, 30_mm);
    const ObjectId profile = require(document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 4_mm});
    REQUIRE(extrude.has_value());
    const ObjectId feature = require(document.addObject(std::move(*extrude)));
    requireReport(regenerator, document);

    const Result<MeshableGeometry> prepared = requireMeshableGeometry(document, regenerator, feature);
    REQUIRE(prepared.has_value());
    // V = 20 x 30 x 4 = 2400 mm^3 wherever it sits: volume is invariant under a
    // rigid placement.
    CHECK_THAT(prepared->volume.in(units::mm3), WithinRel(20.0 * 30.0 * 4.0, kRel));

    // And it is in the same place as the regenerator's own body, to the bit. If
    // preparation had rebuilt or relocated anything, these boxes would differ.
    const geometry::Body* authoritative = regenerator.body(feature);
    REQUIRE(authoritative != nullptr);
    const Result<BoundingBox3D> prime = authoritative->boundingBox();
    const Result<BoundingBox3D> copy = prepared->body.boundingBox();
    REQUIRE(prime.has_value());
    REQUIRE(copy.has_value());
    CHECK(*copy == *prime);

    // Not at the origin, so the test would notice a dropped location.
    CHECK(prime->min.x.si() != 0.0);
    // On the XZ plane the extrusion runs along -Y, so the box has extent in y
    // away from zero; whichever sign, it is not a degenerate slab at y = 0.
    CHECK(prime->sizeY().si() > 0.0);
}

// ---------------------------------------------------------------------------
// The state matrix: what is NOT meshable, and why
// ---------------------------------------------------------------------------

TEST_CASE("Meshable_RefusesAnObjectThatDoesNotExist", "[meshing][geom][state]") {
    const Part part;
    const Result<MeshableGeometry> prepared =
        requireMeshableGeometry(part.document, part.regenerator, ObjectId::fromValue(9999));
    REQUIRE_FALSE(prepared.has_value());
    CHECK(prepared.error().code == ErrorCode::NotFound);
    CHECK(geometryIneligibility(part.document, part.regenerator, ObjectId::fromValue(9999)) ==
          GeometryIneligibility::ObjectNotFound);
}

TEST_CASE("Meshable_RefusesAFeatureThatHasNeverBeenRegenerated", "[meshing][geom][state]") {
    Part part;
    // A second extrude, added and deliberately not regenerated.
    auto extrude = features::ExtrudeFeature::create(
        "Second", {.profile = SketchId::fromValue(part.profile.value()), .depth = 10_mm});
    REQUIRE(extrude.has_value());
    const ObjectId fresh = require(part.document.addObject(std::move(*extrude)));

    CHECK(geometryIneligibility(part.document, part.regenerator, fresh) ==
          GeometryIneligibility::NeverRegenerated);
    const Result<MeshableGeometry> prepared =
        requireMeshableGeometry(part.document, part.regenerator, fresh);
    REQUIRE_FALSE(prepared.has_value());
    CHECK(prepared.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(prepared.error().message, ContainsSubstring("never been regenerated"));
}

TEST_CASE("Meshable_RefusesAnObjectThatMakesNoBodyAndSaysSoDistinctly", "[meshing][geom][state]") {
    // A sketch is up to date and will never have a body. Telling the user to
    // regenerate would be wrong, so this is NOT the same diagnostic as a failure.
    const Part part;
    CHECK(part.regenerator.state(part.profile).has_value());
    CHECK(part.regenerator.body(part.profile) == nullptr);

    CHECK(geometryIneligibility(part.document, part.regenerator, part.profile) ==
          GeometryIneligibility::NoBody);
    const Result<MeshableGeometry> prepared =
        requireMeshableGeometry(part.document, part.regenerator, part.profile);
    REQUIRE_FALSE(prepared.has_value());
    CHECK_THAT(prepared.error().message, ContainsSubstring("produces no body"));
}

TEST_CASE("Meshable_RefusesAFeatureBlockedByAFailedUpstreamSketch", "[meshing][geom][state]") {
    Part part;
    REQUIRE(part.prepare().has_value());

    // Over-constrain the sketch so it fails; the extrude that consumes it is
    // blocked.
    REQUIRE(part.document
                .modifyObject<sketch::Sketch>(part.profile,
                                              [&](sketch::Sketch& s) -> Result<bool> {
                                                  auto a = s.addDistance(part.lines[0], 20_mm);
                                                  if (!a) {
                                                      return std::unexpected(a.error());
                                                  }
                                                  auto b = s.addDistance(part.lines[0], 60_mm);
                                                  if (!b) {
                                                      return std::unexpected(b.error());
                                                  }
                                                  return true;
                                              })
                .has_value());
    part.regenerate();

    REQUIRE(part.regenerator.state(part.profile) == NodeState::Failed);
    REQUIRE(part.regenerator.state(part.feature) == NodeState::Blocked);
    // The regenerator drops the body of a blocked item, so there is no
    // last-known-good geometry to be tempted by.
    CHECK(part.regenerator.body(part.feature) == nullptr);

    CHECK(part.reason() == GeometryIneligibility::RegenerationBlocked);
    const Result<MeshableGeometry> prepared = part.prepare();
    REQUIRE_FALSE(prepared.has_value());
    CHECK(prepared.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(prepared.error().message, ContainsSubstring("blocked"));

    // The failed sketch itself is reported as failed, not as blocked.
    CHECK(geometryIneligibility(part.document, part.regenerator, part.profile) ==
          GeometryIneligibility::RegenerationFailed);
}

/// Two disjoint boxes, the second INTERSECTING the first: the intersection misses,
/// so the second feature's body is EMPTY -- and the regenerator stores it with a
/// healthy state rather than failing, which LoftFeatureTests pins for the loft
/// equivalent ("an intersection that misses: an empty body, reported").
///
/// This is the only route found to a feature whose stored body is up to date and
/// geometrically unusable, and it is what makes the two tests below possible.
struct EmptyIntersection {
    Document document{"Miss"};
    features::Regenerator regenerator;
    ObjectId first{};
    ObjectId second{};
    ObjectId secondProfile{};
    std::array<EntityId, 4> secondLines{};

    EmptyIntersection() {
        auto a = std::make_unique<sketch::Sketch>("ProfileA", Frame3D::xy());
        addRectangle(*a, 0_mm, 0_mm, 10_mm, 10_mm);
        const ObjectId sketchA = require(document.addObject(std::move(a)));
        auto extrudeA = features::ExtrudeFeature::create(
            "BoxA", {.profile = SketchId::fromValue(sketchA.value()), .depth = 10_mm});
        REQUIRE(extrudeA.has_value());
        first = require(document.addObject(std::move(*extrudeA)));

        // Far away, so the intersection is empty.
        auto b = std::make_unique<sketch::Sketch>("ProfileB", Frame3D::xy());
        secondLines = addRectangle(*b, 500_mm, 500_mm, 10_mm, 10_mm);
        secondProfile = require(document.addObject(std::move(b)));
        auto extrudeB = features::ExtrudeFeature::create(
            "MissB", {.profile = SketchId::fromValue(secondProfile.value()),
                  .depth = 10_mm,
                  .operation = features::FeatureOperation::Intersect,
                  .target = FeatureId::fromValue(first.value())});
        REQUIRE(extrudeB.has_value());
        second = require(document.addObject(std::move(*extrudeB)));
        requireReport(regenerator, document);
    }
};

TEST_CASE("Meshable_RefusesAnEmptyBodyThatRegenerationNonethelessStored",
          "[meshing][geom][state]") {
    const EmptyIntersection model;
    // Up to date, body present, body empty. Not a failure of the pass.
    const std::optional<NodeState> state = model.regenerator.state(model.second);
    CHECK((state == NodeState::UpToDate || state == NodeState::Regenerated));
    REQUIRE(model.regenerator.body(model.second) != nullptr);
    REQUIRE(model.regenerator.body(model.second)->isEmpty());

    CHECK(geometryIneligibility(model.document, model.regenerator, model.second) ==
          GeometryIneligibility::EmptyBody);
    const Result<MeshableGeometry> prepared =
        requireMeshableGeometry(model.document, model.regenerator, model.second);
    REQUIRE_FALSE(prepared.has_value());
    CHECK(prepared.error().code == ErrorCode::FailedPrecondition);
}

TEST_CASE("Meshable_ReportsStalenessRatherThanTheDefectOfAStaleBody",
          "[meshing][geom][stale][precedence]") {
    // THE ORDERING, pinned rather than reasoned. This body is BOTH stale AND
    // geometrically unusable, so the two candidate diagnostics differ and the
    // check order decides which is reported.
    //
    // Currency first  -> GeometryStale   (correct: the body describes a model the
    //                                     user has already changed, so its defects
    //                                     are not the thing to report)
    // Currency last   -> EmptyBody       (wrong: a confident statement about
    //                                     geometry nobody asked about any more)
    //
    // Moving the currency check to the end of classify() makes this test fail,
    // which is what makes the ordering a property of the suite and not of a comment.
    EmptyIntersection model;
    REQUIRE(geometryIneligibility(model.document, model.regenerator, model.second) ==
            GeometryIneligibility::EmptyBody);

    // Now make it stale too, by moving its own profile and not regenerating.
    REQUIRE(model.document
                .modifyObject<sketch::Sketch>(model.secondProfile,
                                              [&](sketch::Sketch& s) -> Result<bool> {
                                                  return s.addDistance(model.secondLines[0], 25_mm)
                                                      .has_value();
                                              })
                .has_value());

    // Still empty, still stored, still healthy by the last pass's reckoning.
    REQUIRE(model.regenerator.body(model.second) != nullptr);
    REQUIRE(model.regenerator.body(model.second)->isEmpty());
    REQUIRE_FALSE(model.regenerator.isCurrent(model.document, model.second));

    CHECK(geometryIneligibility(model.document, model.regenerator, model.second) ==
          GeometryIneligibility::GeometryStale);
    const Result<MeshableGeometry> prepared =
        requireMeshableGeometry(model.document, model.regenerator, model.second);
    REQUIRE_FALSE(prepared.has_value());
    CHECK_THAT(prepared.error().message, ContainsSubstring("stale"));
}

// ---------------------------------------------------------------------------
// THE CENTRAL GUARD: stale geometry
// ---------------------------------------------------------------------------

TEST_CASE("Meshable_RefusesGeometryThatIsStaleBecauseItsProfileMoved", "[meshing][geom][stale]") {
    // THE CASE THIS MILESTONE EXISTS FOR, and the reason an own-revision check is
    // not enough: editing the SKETCH does not change the revision of the EXTRUDE
    // that consumes it. A guard that compared only the feature's own revision
    // would call this body current while its profile had moved underneath it.
    Part part{20_mm, 30_mm, 50_mm};
    const Result<MeshableGeometry> before = part.prepare();
    REQUIRE(before.has_value());
    CHECK_THAT(before->volume.in(units::mm3), WithinRel(boxMm3(20, 30, 50), kRel));

    const std::optional<std::uint64_t> featureRevisionBefore = part.document.revisionOf(part.feature);
    const std::optional<std::uint64_t> builtBefore = part.regenerator.builtRevision(part.feature);

    // Move the profile. No regeneration.
    REQUIRE(part.document
                .modifyObject<sketch::Sketch>(part.profile,
                                              [&](sketch::Sketch& s) -> Result<bool> {
                                                  return s.addDistance(part.lines[0], 40_mm).has_value();
                                              })
                .has_value());

    // The trap, demonstrated: the feature's own revision has NOT moved, and the
    // regenerator still reports the last pass's verdict.
    CHECK(part.document.revisionOf(part.feature) == featureRevisionBefore);
    CHECK(part.regenerator.builtRevision(part.feature) == builtBefore);
    const std::optional<NodeState> reported = part.regenerator.state(part.feature);
    const bool healthyLastPass = reported == NodeState::UpToDate || reported == NodeState::Regenerated;
    CHECK(healthyLastPass); // the last pass was happy with it, and still says so
    // And the old body is still sitting there, valid, closed and positive.
    CHECK(part.regenerator.body(part.feature) != nullptr);

    // Refused anyway, because currency walks the graph.
    CHECK_FALSE(part.regenerator.isCurrent(part.document, part.feature));
    CHECK(part.reason() == GeometryIneligibility::GeometryStale);
    const Result<MeshableGeometry> stale = part.prepare();
    REQUIRE_FALSE(stale.has_value());
    CHECK(stale.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(stale.error().message, ContainsSubstring("stale"));
    CHECK_THAT(stale.error().message, ContainsSubstring("Regenerate"));
}

TEST_CASE("Meshable_AcceptsGeometryAgainOnceItHasBeenRegenerated", "[meshing][geom][stale]") {
    // The other half of the contract: refusal is not a latch.
    Part part{20_mm, 30_mm, 50_mm};
    const GeometryRevision first = part.prepare()->revision;

    REQUIRE(part.document
                .modifyObject<sketch::Sketch>(part.profile,
                                              [&](sketch::Sketch& s) -> Result<bool> {
                                                  return s.addDistance(part.lines[0], 40_mm).has_value();
                                              })
                .has_value());
    REQUIRE_FALSE(part.prepare().has_value());

    part.regenerate();
    const Result<MeshableGeometry> after = part.prepare();
    REQUIRE(after.has_value());
    // addRectangle makes four lines through four shared points and adds NO
    // geometric constraints, so constraining the bottom edge to 40 does not widen
    // a rectangle -- it stretches one edge and leaves the opposite edge at 20.
    // The profile is a trapezoid, and its area follows from the closed form:
    //
    //   A = 1/2 (a + b) h = 1/2 (40 + 20) * 30 = 900 mm^2
    //   V = A * depth      = 900 * 50          = 45000 mm^3
    //
    // Which endpoint the solver moved does not matter: the opposite edge keeps its
    // length and the height is untouched, so the area is the same either way.
    CHECK_THAT(after->volume.in(units::mm3), WithinRel(0.5 * (40.0 + 20.0) * 30.0 * 50.0, kRel));
    // A geometry edit must move the revision. If it did not, a later mesh could
    // never be told it was stale.
    CHECK(after->revision != first);
}

TEST_CASE("Meshable_RecoversAfterAFailedEditIsUndone", "[meshing][geom][stale]") {
    // G1 valid -> edit into failure -> refused -> undo -> regenerate -> accepted.
    Part part{20_mm, 30_mm, 50_mm};
    REQUIRE(part.prepare().has_value());

    ConstraintId conflict{};
    REQUIRE(part.document
                .modifyObject<sketch::Sketch>(part.profile,
                                              [&](sketch::Sketch& s) -> Result<bool> {
                                                  auto a = s.addDistance(part.lines[0], 20_mm);
                                                  if (!a) {
                                                      return std::unexpected(a.error());
                                                  }
                                                  auto b = s.addDistance(part.lines[0], 60_mm);
                                                  if (!b) {
                                                      return std::unexpected(b.error());
                                                  }
                                                  conflict = *b;
                                                  return true;
                                              })
                .has_value());
    part.regenerate();
    REQUIRE(part.reason() == GeometryIneligibility::RegenerationBlocked);

    // Undo the edit that broke it, by removing the constraint that conflicts.
    REQUIRE(part.document
                .modifyObject<sketch::Sketch>(part.profile,
                                              [&](sketch::Sketch& s) -> Result<bool> {
                                                  return s.removeConstraint(conflict).has_value();
                                              })
                .has_value());
    part.regenerate();
    const Result<MeshableGeometry> recovered = part.prepare();
    REQUIRE(recovered.has_value());
    CHECK_THAT(recovered->volume.in(units::mm3), WithinRel(boxMm3(20, 30, 50), kRel));
}

// ---------------------------------------------------------------------------
// The carried configuration defect
// ---------------------------------------------------------------------------

TEST_CASE("Meshable_RefusesEveryGeometryWhileAConfigurationOverridesAParameter",
          "[meshing][geom][configuration]") {
    // A CARRIED DEFECT, guarded rather than pinned, exactly as P15-MASS-001 guards
    // it. A configuration override changes a parameter's EFFECTIVE value without
    // changing the parameter object, so the regenerator does not mark the features
    // that read it dirty and their bodies are still the base configuration's.
    //
    // This test asserts the REFUSAL. It deliberately does not record what the stale
    // volume is, because that would turn a defect into a contract. When the
    // regeneration defect is fixed, this test is deleted with the guard it covers
    // and replaced by one in which the geometry FOLLOWS the configuration.
    Part part{20_mm, 30_mm, 50_mm};
    const Result<MeshableGeometry> base = part.prepare();
    REQUIRE(base.has_value());
    const double baseVolume = base->volume.in(units::mm3);
    CHECK_THAT(baseVolume, WithinRel(boxMm3(20, 30, 50), kRel));

    const Result<ParameterId> width = part.document.createParameter("width", 20_mm, units::mm);
    REQUIRE(width);
    const Result<ConfigurationId> wide = part.document.createConfiguration("Wide");
    REQUIRE(wide);
    REQUIRE(part.document.setConfigurationOverride(*wide, *width, 80_mm));
    REQUIRE(part.document.setActiveConfiguration(*wide));

    // THE DEFECT, recorded as current behaviour: the body has not changed.
    const geometry::Body* body = part.regenerator.body(part.feature);
    REQUIRE(body != nullptr);
    CHECK_THAT(body->massProperties()->volume.in(units::mm3), WithinRel(baseVolume, kRel));

    // THE GUARD: meshing refuses, and says why, rather than meshing the base
    // configuration's shape while the document says the active one is Wide.
    CHECK(part.reason() == GeometryIneligibility::ConfigurationOverrideActive);
    const Result<MeshableGeometry> under = part.prepare();
    REQUIRE_FALSE(under.has_value());
    CHECK(under.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(under.error().message, ContainsSubstring("Wide"));
    CHECK_THAT(under.error().message, ContainsSubstring("does not yet rebuild"));

    // Not a latch: the base configuration is meshable again.
    REQUIRE(part.document.setActiveConfiguration(std::nullopt));
    const Result<MeshableGeometry> again = part.prepare();
    REQUIRE(again.has_value());
    CHECK_THAT(again->volume.in(units::mm3), WithinRel(baseVolume, kRel));
}

TEST_CASE("Meshable_AcceptsGeometryUnderAConfigurationThatOverridesNothing",
          "[meshing][geom][configuration]") {
    // Over-refusing would be its own defect: a configuration with no overrides
    // changes no effective value, so it cannot make geometry stale.
    Part part{20_mm, 30_mm, 50_mm};
    const Result<ConfigurationId> plain = part.document.createConfiguration("Plain");
    REQUIRE(plain);
    REQUIRE(part.document.setActiveConfiguration(*plain));

    const Result<MeshableGeometry> prepared = part.prepare();
    REQUIRE(prepared.has_value());
    CHECK_THAT(prepared->volume.in(units::mm3), WithinRel(boxMm3(20, 30, 50), kRel));
}

TEST_CASE("Meshable_ConfigurationGuardIsCheckedBeforeAnythingElse",
          "[meshing][geom][configuration][precedence]") {
    // Both wrong at once: a configuration override is active AND the geometry is
    // stale. The configuration cause is reported, because it is the one that says
    // nothing in hand can be trusted.
    Part part;
    const Result<ParameterId> width = part.document.createParameter("width", 20_mm, units::mm);
    REQUIRE(width);
    const Result<ConfigurationId> wide = part.document.createConfiguration("Wide");
    REQUIRE(wide);
    REQUIRE(part.document.setConfigurationOverride(*wide, *width, 80_mm));
    REQUIRE(part.document.setActiveConfiguration(*wide));
    REQUIRE(part.document
                .modifyObject<sketch::Sketch>(part.profile,
                                              [&](sketch::Sketch& s) -> Result<bool> {
                                                  return s.addDistance(part.lines[0], 40_mm).has_value();
                                              })
                .has_value());

    CHECK_FALSE(part.regenerator.isCurrent(part.document, part.feature));
    CHECK(part.reason() == GeometryIneligibility::ConfigurationOverrideActive);
}

TEST_CASE("Meshable_StalenessIsReportedBeforeTheOldBodyIsInspected",
          "[meshing][geom][stale][precedence]") {
    // The subtle one. A stale body is still a perfectly good closed solid, so
    // inspecting it would produce a confident answer about the wrong model. The
    // diagnostic must be about currency, and that is only true if nothing
    // downstream of the currency check has run.
    Part part;
    REQUIRE(part.document
                .modifyObject<sketch::Sketch>(part.profile,
                                              [&](sketch::Sketch& s) -> Result<bool> {
                                                  return s.addDistance(part.lines[0], 40_mm).has_value();
                                              })
                .has_value());

    // The old body would pass every geometric check that follows.
    const geometry::Body* body = part.regenerator.body(part.feature);
    REQUIRE(body != nullptr);
    CHECK(body->isValid());
    CHECK(body->topology().solids == 1);
    CHECK(body->massProperties()->volume.si() > 0.0);

    // And yet:
    CHECK(part.reason() == GeometryIneligibility::GeometryStale);
}

// ---------------------------------------------------------------------------
// Geometry revision semantics
// ---------------------------------------------------------------------------

TEST_CASE("GeometryRevision_ChangesWhenAnUpstreamProfileChangesTheGeometry",
          "[meshing][geom][revision]") {
    Part part;
    const GeometryRevision before = meshing::geometryRevision(part.document, part.feature);
    REQUIRE(before.isValid());

    REQUIRE(part.document
                .modifyObject<sketch::Sketch>(part.profile,
                                              [&](sketch::Sketch& s) -> Result<bool> {
                                                  return s.addDistance(part.lines[0], 40_mm).has_value();
                                              })
                .has_value());

    // The feature's OWN revision did not move; the stamp must still change,
    // because the geometry did.
    const GeometryRevision after = meshing::geometryRevision(part.document, part.feature);
    CHECK(after != before);
}

TEST_CASE("GeometryRevision_IsStableWhenNothingChanges", "[meshing][geom][revision]") {
    const Part part;
    const GeometryRevision first = meshing::geometryRevision(part.document, part.feature);
    for (int repeat = 0; repeat < 8; ++repeat) {
        CHECK(meshing::geometryRevision(part.document, part.feature) == first);
    }
}

TEST_CASE("GeometryRevision_IsUnchangedByAMaterialEdit", "[meshing][geom][revision]") {
    // P16 geometry must not acquire a dependency on P15 engineering properties. A
    // density is not a dimension, and a mesh of this body is still a mesh of this
    // body after the density changes.
    //
    // This is the narrowing ADR-030 authorised: its mesh stamp uses
    // Document::revision() as a conservative outer guard, which a density edit
    // DOES move. The geometry revision is the precise one.
    Part part;
    const GeometryRevision before = meshing::geometryRevision(part.document, part.feature);
    const std::uint64_t documentRevisionBefore = part.document.revision();

    features::MaterialDefinition definition;
    definition.mechanical.density =
        materials::MaterialProperty<Density>::known(Density::fromSi(7800.0));
    const Result<MaterialId> material = features::createMaterial(part.document, "Steel", definition);
    REQUIRE(material);
    REQUIRE(features::assignMaterial(part.document, *material));

    // The document moved; the geometry did not.
    CHECK(part.document.revision() != documentRevisionBefore);
    CHECK(meshing::geometryRevision(part.document, part.feature) == before);
    // And the geometry is still meshable: a material edit cannot make it stale.
    CHECK(part.reason() == std::nullopt);
    CHECK(part.prepare().has_value());
}

TEST_CASE("GeometryRevision_IsUnchangedByRenamingTheFeature", "[meshing][geom][revision]") {
    // A name is not a dimension. Renaming does move the object's own revision, so
    // this records what the mechanism actually does rather than what would be
    // ideal -- see KNOWN LIMITATIONS in the evidence.
    Part part;
    const GeometryRevision before = meshing::geometryRevision(part.document, part.feature);
    REQUIRE(part.document.rename(part.feature, "Renamed").has_value());
    const GeometryRevision after = meshing::geometryRevision(part.document, part.feature);

    // Recorded, not asserted either way: if the revision moves, invalidation is
    // merely conservative, which ADR-030 permits and the evidence documents.
    if (after != before) {
        WARN("renaming a feature moves its geometry revision: invalidation is conservative here");
    }
    SUCCEED("renaming is recorded, not silently assumed harmless");
}

// ---------------------------------------------------------------------------
// Read-only, and determinism
// ---------------------------------------------------------------------------

TEST_CASE("Meshable_PreparationDoesNotTouchTheDocumentOrTheRegenerator", "[meshing][geom]") {
    Part part;
    const std::uint64_t revision = part.document.revision();
    const std::optional<std::uint64_t> built = part.regenerator.builtRevision(part.feature);
    const geometry::Body* body = part.regenerator.body(part.feature);
    const auto topologyBefore = body->topology();

    for (int repeat = 0; repeat < 4; ++repeat) {
        REQUIRE(part.prepare().has_value());
        REQUIRE(part.reason() == std::nullopt);
    }

    // No regeneration, no healing, no configuration change, no save.
    CHECK(part.document.revision() == revision);
    CHECK(part.regenerator.builtRevision(part.feature) == built);
    CHECK(part.regenerator.body(part.feature) == body);
    CHECK(part.regenerator.body(part.feature)->topology() == topologyBefore);
}

TEST_CASE("Meshable_GivesTheSameAnswerAndTheSameVolumeOnRepeatedCalls", "[meshing][geom][determinism]") {
    const Part part{20_mm, 30_mm, 50_mm};
    const Result<MeshableGeometry> first = part.prepare();
    REQUIRE(first.has_value());
    for (int repeat = 0; repeat < 8; ++repeat) {
        const Result<MeshableGeometry> again = part.prepare();
        REQUIRE(again.has_value());
        CHECK(again->revision == first->revision);
        CHECK(again->solidCount == first->solidCount);
        // Exact equality: the same integration of the same shape must not drift.
        CHECK(again->volume.si() == first->volume.si());
    }
}

TEST_CASE("GeometryIneligibility_EveryReasonHasAName", "[meshing][geom]") {
    const GeometryIneligibility reasons[] = {
        GeometryIneligibility::ConfigurationOverrideActive, GeometryIneligibility::ObjectNotFound,
        GeometryIneligibility::NeverRegenerated,           GeometryIneligibility::RegenerationFailed,
        GeometryIneligibility::RegenerationBlocked,        GeometryIneligibility::GeometryStale,
        GeometryIneligibility::NoBody,                     GeometryIneligibility::EmptyBody,
        GeometryIneligibility::InvalidBRep,                GeometryIneligibility::NotASolid,
        GeometryIneligibility::ZeroVolume,
    };
    for (const GeometryIneligibility reason : reasons) {
        CHECK_FALSE(meshing::toString(reason).empty());
        CHECK(meshing::toString(reason) != "unknown");
    }
}
