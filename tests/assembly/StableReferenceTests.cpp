#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Configurations.hpp>
#include <bettercad/assembly/Mate.hpp>
#include <bettercad/assembly/Mates.hpp>
#include <bettercad/assembly/Placement.hpp>
#include <bettercad/assembly/Resolution.hpp>
#include <bettercad/assembly/Solver.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/MateReference.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using assembly::MateDefinition;
using assembly::MateTargetSide;
using assembly::MateType;
using assembly::UnresolvedMateTarget;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

// P13-STREF-001: a reference survives the model changing under it, or it is
// honestly broken. Never quietly attached to something else.
//
// The failure this file exists to catch is a mate that still solves, on the
// WRONG face. Nothing reports that one: the solve succeeds, the assembly
// looks plausible, and the parts are in the wrong places. So an assertion
// that a reference "still resolves" is worth almost nothing here. Every case
// below asserts WHICH geometry it resolved to.
//
// The expected geometry is derived by hand and never from BetterCAD's own
// resolver. A rectangle on the XY plane extruded to depth d has its end cap
// at z = d with the normal facing out of the material, +Z. That is the oracle
// throughout, and it is arithmetic rather than a second call into the code
// under test.

namespace {

constexpr double kTolerance = 1e-9;

struct Rig {
    Document document{"Assembly"};
    features::Regenerator regenerator{};
    ObjectId sketch{};
    ObjectId part{};
    ComponentId a{};
    ComponentId b{};

    /// The bodies of the current regeneration, which a face target needs to
    /// resolve against.
    [[nodiscard]] features::BodyLookup bodies() const {
        return [this](ObjectId id) { return regenerator.body(id); };
    }
    void regenerate() { requireReport(regenerator, document); }
};

/// One part -- a 40 x 30 rectangle extruded @p depth -- placed twice.
Rig makeRig(Length depth = 10_mm) {
    Rig rig;
    assembly::registerHandlers(rig.regenerator);
    auto sketch = std::make_unique<sketch::Sketch>("BlockSketch", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
    rig.sketch = require(rig.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Block", {.profile = SketchId::fromValue(rig.sketch.value()), .depth = depth});
    REQUIRE(extrude.has_value());
    rig.part = require(rig.document.addObject(std::move(*extrude)));
    rig.a = require(assembly::createComponent(rig.document, "First", {.part = rig.part}));
    rig.b = require(assembly::createComponent(rig.document, "Second",
                                              {.part = rig.part, .placement = {.translation = {0_mm, 0_mm, 60_mm}}}));
    rig.regenerate();
    return rig;
}

MateTarget endCap(ComponentId component, ObjectId feature) {
    return faceTarget(component, FaceName{.feature = feature, .face = {.role = FaceRole::EndCap}});
}

MateId add(Rig& rig, const std::string& name, const MateDefinition& definition) {
    return require(assembly::createMate(rig.document, name, definition));
}

/// The geometry a target resolves to, which must exist.
assembly::MateTargetGeometry resolved(const Rig& rig, const MateTarget& target) {
    auto geometry = assembly::resolveMateTarget(rig.document, target, rig.bodies());
    if (!geometry) {
        FAIL(geometry.error().message);
    }
    return *geometry;
}

/// Hand-derived: the end cap of a block extruded @p depth sits at z = depth
/// and faces +Z, out of the material.
void checkEndCapAt(const assembly::MateTargetGeometry& geometry, double depthMetres) {
    CHECK(geometry.planar);
    CHECK_THAT(geometry.origin.z.si(), WithinAbs(depthMetres, kTolerance));
    CHECK_THAT(geometry.direction.x(), WithinAbs(0.0, kTolerance));
    CHECK_THAT(geometry.direction.y(), WithinAbs(0.0, kTolerance));
    CHECK_THAT(geometry.direction.z(), WithinAbs(1.0, kTolerance));
}

} // namespace

// --- The oracle itself ----------------------------------------------------------------------------

TEST_CASE("StableReference_TheEndCapIsWhereTheGeometrySaysItIs", "[assembly][stref][p13]") {
    // Establishes the hand-derived oracle the rest of the file leans on,
    // against two depths, so that a later "it moved to 25 mm" means something.
    Rig ten = makeRig(10_mm);
    checkEndCapAt(resolved(ten, endCap(ten.a, ten.part)), 0.010);

    Rig twentyFive = makeRig(25_mm);
    checkEndCapAt(resolved(twentyFive, endCap(twentyFive.a, twentyFive.part)), 0.025);
}

// --- Regeneration ---------------------------------------------------------------------------------

TEST_CASE("StableReference_AFaceTargetFollowsItsFeatureThroughARegeneration", "[assembly][stref][p13]") {
    // The heart of the milestone. A face is named by the feature that makes
    // it and the role it plays there, never by a position in a topology
    // array, so changing the depth must move the face the reference resolves
    // to -- not break the reference, and not leave it pointing at the old
    // plane.
    Rig rig = makeRig(10_mm);
    const MateTarget target = endCap(rig.a, rig.part);
    add(rig, "Touch", {.type = MateType::Coincident, .a = target, .b = endCap(rig.b, rig.part)});
    checkEndCapAt(resolved(rig, target), 0.010);

    const auto* extrude = rig.document.findObjectAs<features::ExtrudeFeature>(rig.part);
    REQUIRE(extrude != nullptr);
    auto definition = extrude->definition();
    definition.depth = 25_mm;
    REQUIRE(rig.document
                .modifyObject<features::ExtrudeFeature>(
                    rig.part, [&](features::ExtrudeFeature& f) { return f.setDefinition(definition); })
                .has_value());
    rig.regenerate();

    // The reference itself did not change -- only what it resolves to.
    const assembly::Mate* mate = assembly::findMate(rig.document, assembly::mates(rig.document).front());
    REQUIRE(mate != nullptr);
    CHECK(mate->definition().a == target);
    checkEndCapAt(resolved(rig, target), 0.025);
    CHECK(assembly::unresolvedMateTargets(rig.document, rig.bodies()).empty());
}

// --- The target disappearing ------------------------------------------------------------------------

TEST_CASE("StableReference_AFaceTargetBecomesUnresolvedWhenItsFeatureIsGone", "[assembly][stref][p13]") {
    // Unresolved is a state the document reports, not a refusal to open --
    // the same contract unresolvedComponents() already gives for parts.
    Rig rig = makeRig();
    const MateId touch =
        add(rig, "Touch", {.type = MateType::Coincident, .a = endCap(rig.a, rig.part), .b = endCap(rig.b, rig.part)});
    REQUIRE(assembly::unresolvedMateTargets(rig.document, rig.bodies()).empty());

    // Remove the feature the faces are named from. The components go too --
    // they place that part -- but the mate stays, naming geometry that is no
    // longer there.
    REQUIRE(rig.document.removeObject(rig.part).has_value());

    const std::vector<UnresolvedMateTarget> unresolved =
        assembly::unresolvedMateTargets(rig.document, rig.bodies());
    REQUIRE(unresolved.size() == 2);
    CHECK(unresolved[0] == UnresolvedMateTarget{.mate = touch, .side = MateTargetSide::A,
                                                .reason = ErrorCode::NotFound});
    CHECK(unresolved[1].side == MateTargetSide::B);
    CHECK(assembly::toString(unresolved[0].side) == "a");
    CHECK(assembly::toString(unresolved[1].side) == "b");
}

TEST_CASE("StableReference_NeverRebindsToASimilarFaceThatTookItsPlace", "[assembly][stref][p13]") {
    // The adversarial case the whole model exists to defeat. A second part
    // with an end cap of its own is added, at the very plane the original
    // occupied. The reference must stay broken rather than attach to the
    // stranger that fits.
    Rig rig = makeRig(10_mm);
    const MateTarget original = endCap(rig.a, rig.part);
    const MateId touch =
        add(rig, "Touch", {.type = MateType::Coincident, .a = original, .b = endCap(rig.b, rig.part)});
    checkEndCapAt(resolved(rig, original), 0.010);

    // A second block of identical shape, so its end cap is at the same place.
    auto sketch = std::make_unique<sketch::Sketch>("OtherSketch", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
    const ObjectId otherSketch = require(rig.document.addObject(std::move(sketch)));
    auto other = features::ExtrudeFeature::create(
        "Other", {.profile = SketchId::fromValue(otherSketch.value()), .depth = 10_mm});
    REQUIRE(other.has_value());
    const ObjectId otherPart = require(rig.document.addObject(std::move(*other)));
    rig.regenerate();
    // Geometrically indistinguishable from the original.
    checkEndCapAt(resolved(rig, endCap(rig.a, otherPart)), 0.010);

    // Now take the original away.
    REQUIRE(rig.document.removeObject(rig.part).has_value());

    const std::vector<UnresolvedMateTarget> unresolved =
        assembly::unresolvedMateTargets(rig.document, rig.bodies());
    REQUIRE_FALSE(unresolved.empty());
    CHECK(unresolved.front().mate == touch);
    // The mate still names the feature that is gone. It did not quietly
    // acquire the identical face next door.
    const assembly::Mate* mate = assembly::findMate(rig.document, touch);
    REQUIRE(mate != nullptr);
    CHECK(mate->definition().a->face->feature == rig.part);
    CHECK(mate->definition().a->face->feature != otherPart);
}

TEST_CASE("StableReference_RecoversWhenTheIntendedTargetReturns", "[assembly][stref][p13]") {
    // Unresolved is not a death. Restoring the feature under its own ID must
    // restore the reference -- to the same geometry, with the reference
    // itself never having changed.
    Rig rig = makeRig(10_mm);
    const MateTarget target = endCap(rig.a, rig.part);
    const MateId touch =
        add(rig, "Touch", {.type = MateType::Coincident, .a = target, .b = endCap(rig.b, rig.part)});
    const MateDefinition before = assembly::findMate(rig.document, touch)->definition();
    checkEndCapAt(resolved(rig, target), 0.010);

    auto removed = rig.document.removeObject(rig.part);
    REQUIRE(removed.has_value());
    rig.regenerate();
    CHECK_FALSE(assembly::unresolvedMateTargets(rig.document, rig.bodies()).empty());
    // The canonical reference did not change while it was broken.
    CHECK(assembly::findMate(rig.document, touch)->definition() == before);

    // insertObject(), not restoreObject(): the removed object kept its ID, so
    // this is the undo path rather than the loading one. Coming back under
    // its own ID is precisely what makes the reference recover.
    REQUIRE(rig.document.insertObject(std::move(*removed)).has_value());
    rig.regenerate();

    CHECK(assembly::unresolvedMateTargets(rig.document, rig.bodies()).empty());
    checkEndCapAt(resolved(rig, target), 0.010);
    CHECK(assembly::findMate(rig.document, touch)->definition() == before);
}

// --- Configuration switching ------------------------------------------------------------------------

TEST_CASE("StableReference_SurvivesAConfigurationSwitchThereAndBack", "[assembly][stref][p13]") {
    // P13-CONF-001 made this possible: a target can sit on a component that
    // is in one build and not another. Switching must not change what the
    // reference means, and must not retarget it to the component that is
    // still present.
    Rig rig = makeRig(10_mm);
    const MateTarget target = endCap(rig.b, rig.part);
    const MateId touch =
        add(rig, "Touch", {.type = MateType::Coincident, .a = endCap(rig.a, rig.part), .b = target});
    const MateDefinition before = assembly::findMate(rig.document, touch)->definition();
    const assembly::MateTargetGeometry base = resolved(rig, target);
    checkEndCapAt(base, 0.010);

    const ConfigurationId lean = require(rig.document.createConfiguration("Lean"));
    REQUIRE(assembly::suppressComponent(rig.document, lean, rig.b, true).has_value());
    REQUIRE(rig.document.setActiveConfiguration(lean).has_value());

    // While its component is out of the build the mate is INACTIVE, which is
    // not the same as unresolved: nothing about the model is broken.
    CHECK_FALSE(assembly::isMateActive(rig.document, touch));
    CHECK(assembly::unresolvedMateTargets(rig.document, rig.bodies()).empty());
    // The reference is untouched, and still names the component it always did.
    CHECK(assembly::findMate(rig.document, touch)->definition() == before);
    CHECK(assembly::findMate(rig.document, touch)->definition().b->component == rig.b);
    CHECK(assembly::findMate(rig.document, touch)->definition().b->component != rig.a);

    REQUIRE(rig.document.setActiveConfiguration(std::nullopt).has_value());
    CHECK(assembly::isMateActive(rig.document, touch));
    CHECK(assembly::findMate(rig.document, touch)->definition() == before);
    // And it resolves to exactly the geometry it did before, not merely to
    // something.
    CHECK(resolved(rig, target) == base);
}

// --- Ordering ----------------------------------------------------------------------------------------

TEST_CASE("StableReference_IsNotDisturbedByTheOrderComponentsWereAdded", "[assembly][stref][p13]") {
    // Identity is the component's ID, not its position in any list. Two
    // documents built with the components added in opposite orders must give
    // each reference the same meaning.
    const auto build = [](bool firstFirst) {
        Rig rig;
        assembly::registerHandlers(rig.regenerator);
        auto sketch = std::make_unique<sketch::Sketch>("BlockSketch", Frame3D::xy());
        addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
        rig.sketch = require(rig.document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Block", {.profile = SketchId::fromValue(rig.sketch.value()), .depth = 10_mm});
        REQUIRE(extrude.has_value());
        rig.part = require(rig.document.addObject(std::move(*extrude)));
        if (firstFirst) {
            rig.a = require(assembly::createComponent(rig.document, "First", {.part = rig.part}));
            rig.b = require(assembly::createComponent(rig.document, "Second", {.part = rig.part}));
        } else {
            rig.b = require(assembly::createComponent(rig.document, "Second", {.part = rig.part}));
            rig.a = require(assembly::createComponent(rig.document, "First", {.part = rig.part}));
        }
        rig.regenerate();
        return rig;
    };
    Rig forward = build(true);
    Rig backward = build(false);

    // The IDs differ between the two documents -- they were allocated in a
    // different order -- and that is exactly the point: each reference means
    // the component it names, not the first or the second one.
    CHECK(forward.a != backward.a);
    CHECK(resolved(forward, endCap(forward.a, forward.part)) ==
          resolved(backward, endCap(backward.a, backward.part)));
    CHECK(assembly::findComponent(forward.document, forward.a)->name() ==
          assembly::findComponent(backward.document, backward.a)->name());
}

TEST_CASE("StableReference_TwoComponentsOfOnePartStayDistinguishable", "[assembly][stref][p13]") {
    // Two instances of one part name the same geometry, and must still be
    // different targets -- which is the whole reason a mate needs a component
    // and not just a reference.
    Rig rig = makeRig(10_mm);
    const MateTarget first = endCap(rig.a, rig.part);
    const MateTarget second = endCap(rig.b, rig.part);
    CHECK(first != second);
    CHECK(first.face == second.face);
    CHECK(first.component != second.component);
    // The geometry is resolved in the part's own space, so it is the same for
    // both; what tells them apart is the component each is placed by.
    CHECK(resolved(rig, first) == resolved(rig, second));
    CHECK(require(assembly::placementOf(rig.document, rig.b)).apply(Point3D{}).z.si() == 0.060);
    CHECK(require(assembly::placementOf(rig.document, rig.a)).apply(Point3D{}).z.si() == 0.0);
}

// --- Persistence ---------------------------------------------------------------------------------------

TEST_CASE("StableReference_SurvivesSaveAndLoad", "[assembly][stref][p13][io]") {
    TempDir dir;
    Rig rig = makeRig(10_mm);
    const MateTarget target = endCap(rig.a, rig.part);
    const MateId touch =
        add(rig, "Touch", {.type = MateType::Coincident, .a = target, .b = endCap(rig.b, rig.part)});
    const MateDefinition before = assembly::findMate(rig.document, touch)->definition();

    const auto path = dir.path() / "stable.bcad";
    REQUIRE(io::saveDocument(rig.document, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());

    // Canonical identity compared exactly, not "a mate came back".
    const assembly::Mate* after = assembly::findMate(*loaded, touch);
    REQUIRE(after != nullptr);
    CHECK(after->definition() == before);
    CHECK(after->definition().a->face->feature == rig.part);
    CHECK(after->definition().a->component == rig.a);

    // And it resolves to the same hand-derived geometry in the loaded
    // document, which "the bytes matched" would not prove.
    features::Regenerator regenerator;
    assembly::registerHandlers(regenerator);
    requireReport(regenerator, *loaded);
    const features::BodyLookup bodies = [&](ObjectId id) { return regenerator.body(id); };
    auto geometry = assembly::resolveMateTarget(*loaded, after->definition().a.value(), bodies);
    REQUIRE(geometry.has_value());
    checkEndCapAt(*geometry, 0.010);
}

TEST_CASE("StableReference_AnUnresolvedTargetSurvivesSaveAndLoadAndThenRecovers",
          "[assembly][stref][p13][io]") {
    // A broken reference must persist as broken -- not be dropped on save, not
    // be repaired on load -- and must still recover when its target returns.
    TempDir dir;
    Rig rig = makeRig(10_mm);
    const MateId touch = add(rig, "Touch",
                             {.type = MateType::Coincident,
                              .a = endCap(rig.a, rig.part),
                              .b = endCap(rig.b, rig.part)});
    const MateDefinition before = assembly::findMate(rig.document, touch)->definition();
    auto removed = rig.document.removeObject(rig.part);
    REQUIRE(removed.has_value());

    const auto path = dir.path() / "broken.bcad";
    REQUIRE(io::saveDocument(rig.document, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());

    // Still broken, and still naming exactly what it always named.
    const assembly::Mate* after = assembly::findMate(*loaded, touch);
    REQUIRE(after != nullptr);
    CHECK(after->definition() == before);
    features::Regenerator regenerator;
    assembly::registerHandlers(regenerator);
    (void)regenerator.regenerate(*loaded);
    const features::BodyLookup bodies = [&](ObjectId id) { return regenerator.body(id); };
    CHECK_FALSE(assembly::unresolvedMateTargets(*loaded, bodies).empty());

    // Bring the part back under its own ID, and the reference works again.
    REQUIRE(loaded->insertObject(std::move(*removed)).has_value());
    requireReport(regenerator, *loaded);
    const features::BodyLookup after_ = [&](ObjectId id) { return regenerator.body(id); };
    CHECK(assembly::unresolvedMateTargets(*loaded, after_).empty());
    auto geometry = assembly::resolveMateTarget(*loaded, after->definition().a.value(), after_);
    REQUIRE(geometry.has_value());
    checkEndCapAt(*geometry, 0.010);
}

// --- Cross-document ------------------------------------------------------------------------------------

TEST_CASE("StableReference_AForeignObjectIdNeverBindsToTheLocalObjectOfThatNumber",
          "[assembly][stref][p13]") {
    // Document A object 42 and document B object 42 are different objects.
    // A reference carrying B's identity must not bind to A's object because
    // the number matches -- and the check is which object it did NOT find,
    // not merely that something failed.
    Rig rig = makeRig(10_mm);
    const DocumentId foreign = DocumentId::fromValue(Uuid::generateV4());
    const ObjectReference external{foreign, rig.part};

    auto part = assembly::resolvePart(rig.document, external, nullptr);
    REQUIRE_FALSE(part.has_value());
    CHECK(part.error().code == ErrorCode::NotFound);
    // The local object of that number is right there and resolvable, which is
    // what makes the negative result meaningful.
    auto internal = assembly::resolvePart(rig.document, ObjectReference{rig.part}, nullptr);
    CHECK(internal.has_value());
    // Identity is the document and the object, never the locator.
    CHECK_FALSE(sameTarget(external, ObjectReference{rig.part}));
    CHECK(sameTarget(ObjectReference{foreign, rig.part, "somewhere/else.bcad"}, external));
}

// --- Determinism and failure paths ----------------------------------------------------------------------

TEST_CASE("StableReference_ResolutionIsDeterministic", "[assembly][stref][p13][determinism]") {
    Rig rig = makeRig(10_mm);
    const MateTarget target = endCap(rig.a, rig.part);
    add(rig, "Touch", {.type = MateType::Coincident, .a = target, .b = endCap(rig.b, rig.part)});

    const assembly::MateTargetGeometry first = resolved(rig, target);
    for (int pass = 0; pass < 8; ++pass) {
        INFO("pass " << pass);
        // Equality on the geometry is exact, so this is bitwise agreement
        // between passes, not agreement to a tolerance.
        CHECK(resolved(rig, target) == first);
        CHECK(assembly::unresolvedMateTargets(rig.document, rig.bodies()).empty());
    }
    // And re-regenerating does not move it either.
    rig.regenerate();
    CHECK(resolved(rig, target) == first);
}

TEST_CASE("StableReference_AFailedResolutionChangesNothing", "[assembly][stref][p13]") {
    Rig rig = makeRig(10_mm);
    const MateId touch = add(rig, "Touch",
                             {.type = MateType::Coincident,
                              .a = endCap(rig.a, rig.part),
                              .b = endCap(rig.b, rig.part)});
    const MateDefinition before = assembly::findMate(rig.document, touch)->definition();
    const auto revisionBefore = rig.document.revision();

    // A target on a component that is not there; and a face of a feature that
    // is not there. Both fail, and neither writes anything.
    MateTarget strangerComponent = endCap(ComponentId::fromValue(9999), rig.part);
    CHECK_FALSE(assembly::resolveMateTarget(rig.document, strangerComponent, rig.bodies()).has_value());
    MateTarget strangerFeature = endCap(rig.a, ObjectId::fromValue(9999));
    CHECK_FALSE(assembly::resolveMateTarget(rig.document, strangerFeature, rig.bodies()).has_value());
    // Without bodies a face cannot be resolved at all, which is a failure and
    // not a guess.
    CHECK_FALSE(assembly::resolveMateTarget(rig.document, endCap(rig.a, rig.part), {}).has_value());

    CHECK(assembly::findMate(rig.document, touch)->definition() == before);
    CHECK(rig.document.revision() == revisionBefore);
    // And a valid resolution still succeeds afterwards.
    checkEndCapAt(resolved(rig, endCap(rig.a, rig.part)), 0.010);
}

TEST_CASE("StableReference_ASuppressedComponentIsNotAnUnresolvedOne", "[assembly][stref][p13]") {
    // The distinction that would be easiest to lose: inactive is a statement
    // about this build, unresolved is a statement about the model. A report
    // that conflated them would tell an engineer their assembly was broken
    // every time they switched to a leaner build.
    Rig rig = makeRig(10_mm);
    const MateId touch = add(rig, "Touch",
                             {.type = MateType::Coincident,
                              .a = endCap(rig.a, rig.part),
                              .b = endCap(rig.b, rig.part)});
    const ConfigurationId lean = require(rig.document.createConfiguration("Lean"));
    REQUIRE(assembly::suppressComponent(rig.document, lean, rig.b, true).has_value());
    REQUIRE(rig.document.setActiveConfiguration(lean).has_value());

    CHECK_FALSE(assembly::isMateActive(rig.document, touch));
    CHECK(assembly::unresolvedMateTargets(rig.document, rig.bodies()).empty());

    // Whereas a genuinely missing feature is reported in either build.
    REQUIRE(rig.document.removeObject(rig.part).has_value());
    REQUIRE(rig.document.setActiveConfiguration(std::nullopt).has_value());
    CHECK_FALSE(assembly::unresolvedMateTargets(rig.document, rig.bodies()).empty());
}

TEST_CASE("StableReference_TheSolveAndTheReportNeverDisagree", "[assembly][stref][p13]") {
    // One resolution path, two callers. If unresolvedMateTargets() says the
    // model is sound, a solve must not then fail to resolve something -- and
    // if it names a broken target, the solve must fail too.
    Rig rig = makeRig(10_mm);
    add(rig, "Ground", {.type = MateType::Fixed, .component = rig.a});
    add(rig, "Touch",
        {.type = MateType::Coincident, .a = endCap(rig.a, rig.part), .b = endCap(rig.b, rig.part)});

    CHECK(assembly::unresolvedMateTargets(rig.document, rig.bodies()).empty());
    auto solvedOk = assembly::solve(rig.document, {}, rig.bodies());
    CHECK(solvedOk.has_value());

    REQUIRE(rig.document.removeObject(rig.part).has_value());
    CHECK_FALSE(assembly::unresolvedMateTargets(rig.document, rig.bodies()).empty());
    auto solveBroken = assembly::solve(rig.document, {}, rig.bodies());
    CHECK_FALSE(solveBroken.has_value());
}
