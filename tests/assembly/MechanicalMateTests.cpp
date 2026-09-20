#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Mate.hpp>
#include <bettercad/assembly/Mates.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/MateReference.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using assembly::MateDefinition;
using assembly::MateType;
using Catch::Matchers::ContainsSubstring;

// P13-MATE-002: the four mechanical mates as engineering intent -- what they
// may name, what they refuse, and what survives a file.
//
// The model half of the milestone. Nothing here solves anything; that is
// MechanicalSolveTests.cpp.

namespace {

struct Rig {
    Document document{"Assembly"};
    ObjectId sketch{};
    ObjectId part{};
    ComponentId a{};
    ComponentId b{};
};

Rig makeRig() {
    Rig rig;
    auto sketch = std::make_unique<sketch::Sketch>("BlockSketch", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
    rig.sketch = require(rig.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Block", {.profile = SketchId::fromValue(rig.sketch.value()), .depth = 10_mm});
    REQUIRE(extrude.has_value());
    rig.part = require(rig.document.addObject(std::move(*extrude)));
    rig.a = require(assembly::createComponent(rig.document, "Block1", {.part = rig.part}));
    rig.b = require(assembly::createComponent(rig.document, "Block2", {.part = rig.part}));
    return rig;
}

MateTarget plane(ComponentId component) { return planeTarget(component, PlaneReference{}); }
MateTarget axis(ComponentId component, PrincipalAxis which = PrincipalAxis::Z) {
    return axisTarget(component, AxisReference{.axis = which});
}
MateTarget face(ComponentId component, ObjectId feature) {
    return faceTarget(component, FaceName{.feature = feature, .face = {.role = FaceRole::EndCap}});
}

MateDefinition slider(ComponentId a, ComponentId b) {
    return {.type = MateType::Slider,
            .a = axis(a),
            .b = axis(b),
            .a2 = axis(a, PrincipalAxis::X),
            .b2 = axis(b, PrincipalAxis::X)};
}

constexpr std::array<MateType, 4> kMechanical{MateType::Revolute, MateType::Slider, MateType::Cylindrical,
                                              MateType::Planar};

} // namespace

TEST_CASE("MechanicalMate_NamesEachKind", "[assembly][mate][mechanical][p13]") {
    CHECK(assembly::toString(MateType::Revolute) == "revolute");
    CHECK(assembly::toString(MateType::Slider) == "slider");
    CHECK(assembly::toString(MateType::Cylindrical) == "cylindrical");
    CHECK(assembly::toString(MateType::Planar) == "planar");
}

// --- What each kind may relate ------------------------------------------------------------------

TEST_CASE("MechanicalMate_TheAxisJointsRelateTwoAxes", "[assembly][mate][mechanical][p13]") {
    Rig rig = makeRig();
    // A hinge, a slide and a sleeve are all built on a pair of axes. Giving
    // one a plane is refused rather than reinterpreted: a plane and an axis
    // do not say the same thing about a direction.
    for (const MateType type : {MateType::Revolute, MateType::Cylindrical}) {
        INFO("mate: " << assembly::toString(type));
        CHECK(validate(MateDefinition{.type = type, .a = axis(rig.a), .b = axis(rig.b)}).has_value());
        auto planes = validate(MateDefinition{.type = type, .a = plane(rig.a), .b = plane(rig.b)});
        REQUIRE_FALSE(planes.has_value());
        CHECK_THAT(planes.error().message, ContainsSubstring("relates two axes"));
        auto mixed = validate(MateDefinition{.type = type, .a = axis(rig.a), .b = plane(rig.b)});
        CHECK_FALSE(mixed.has_value());
    }
    auto slide = validate(slider(rig.a, rig.b));
    CHECK(slide.has_value());
    MateDefinition planarSlide = slider(rig.a, rig.b);
    planarSlide.a = plane(rig.a);
    planarSlide.b = plane(rig.b);
    CHECK_FALSE(validate(planarSlide).has_value());
}

TEST_CASE("MechanicalMate_ThePlanarJointRelatesTwoPlanes", "[assembly][mate][mechanical][p13]") {
    Rig rig = makeRig();
    CHECK(validate(MateDefinition{.type = MateType::Planar, .a = plane(rig.a), .b = plane(rig.b)}).has_value());
    // A face is a plane, so it qualifies -- the same rule the basic mates use.
    CHECK(validate(MateDefinition{.type = MateType::Planar, .a = face(rig.a, rig.part), .b = plane(rig.b)})
              .has_value());
    auto axes = validate(MateDefinition{.type = MateType::Planar, .a = axis(rig.a), .b = axis(rig.b)});
    REQUIRE_FALSE(axes.has_value());
    CHECK_THAT(axes.error().message, ContainsSubstring("relates two planes"));
}

TEST_CASE("MechanicalMate_RelatesTwoDifferentComponents", "[assembly][mate][mechanical][p13]") {
    Rig rig = makeRig();
    for (const MateType type : kMechanical) {
        INFO("mate: " << assembly::toString(type));
        MateDefinition d{.type = type, .a = axis(rig.a), .b = axis(rig.a, PrincipalAxis::X)};
        if (type == MateType::Planar) {
            d.a = plane(rig.a);
            d.b = planeTarget(rig.a, PlaneReference{.plane = PrincipalPlane::YZ});
        } else if (type == MateType::Slider) {
            // Distinct geometry, one component: "relates geometry to itself"
            // is a different refusal and would mask the one under test.
            d = slider(rig.a, rig.a);
            d.b = axis(rig.a, PrincipalAxis::Y);
        }
        auto result = validate(d);
        REQUIRE_FALSE(result.has_value());
        CHECK_THAT(result.error().message, ContainsSubstring("two different components"));
    }
}

TEST_CASE("MechanicalMate_TakesNoDistanceOrAngleOfItsOwn", "[assembly][mate][mechanical][p13]") {
    // A joint is named for the freedom it leaves, not for a value. Where a
    // position along that freedom is wanted, it comes from a Distance or an
    // Angle mate beside it -- which is tested in MechanicalSolveTests.
    Rig rig = makeRig();
    for (const MateType type : kMechanical) {
        INFO("mate: " << assembly::toString(type));
        MateDefinition d{.type = type, .a = axis(rig.a), .b = axis(rig.b)};
        if (type == MateType::Planar) {
            d.a = plane(rig.a);
            d.b = plane(rig.b);
        } else if (type == MateType::Slider) {
            d = slider(rig.a, rig.b);
        }
        MateDefinition withDistance = d;
        withDistance.distance = 10_mm;
        CHECK_FALSE(validate(withDistance).has_value());
        MateDefinition withAngle = d;
        withAngle.angle = 30_deg;
        CHECK_FALSE(validate(withAngle).has_value());
        MateDefinition withComponent = d;
        withComponent.component = rig.a;
        CHECK_FALSE(validate(withComponent).has_value());
    }
}

// --- The roll reference -------------------------------------------------------------------------

TEST_CASE("Slider_MustNameARollReferenceOnEachComponent", "[assembly][mate][mechanical][p13]") {
    Rig rig = makeRig();
    MateDefinition bare{.type = MateType::Slider, .a = axis(rig.a), .b = axis(rig.b)};
    auto missing = validate(bare);
    REQUIRE_FALSE(missing.has_value());
    CHECK_THAT(missing.error().message, ContainsSubstring("roll reference"));

    // One of the two is not enough: the roll is a relationship.
    MateDefinition half = bare;
    half.a2 = axis(rig.a, PrincipalAxis::X);
    CHECK_FALSE(validate(half).has_value());
}

TEST_CASE("Slider_RollReferencesMustSitOnTheirOwnComponents", "[assembly][mate][mechanical][p13]") {
    // The roll fixes one component's turn against the other's, so a
    // reference on the wrong side describes nothing. Swapped here, which is
    // the mistake a caller would actually make.
    Rig rig = makeRig();
    MateDefinition swapped = slider(rig.a, rig.b);
    swapped.a2 = axis(rig.b, PrincipalAxis::X);
    swapped.b2 = axis(rig.a, PrincipalAxis::X);
    auto result = validate(swapped);
    REQUIRE_FALSE(result.has_value());
    CHECK_THAT(result.error().message, ContainsSubstring("same components"));
}

TEST_CASE("Slider_RollReferenceMustDifferFromItsAxis", "[assembly][mate][mechanical][p13]") {
    // Rolling a direction against itself says nothing about the roll.
    Rig rig = makeRig();
    MateDefinition sameAsAxis = slider(rig.a, rig.b);
    sameAsAxis.a2 = axis(rig.a);
    auto result = validate(sameAsAxis);
    REQUIRE_FALSE(result.has_value());
    CHECK_THAT(result.error().message, ContainsSubstring("differ from its axis"));
}

TEST_CASE("MechanicalMate_OnlyASliderCarriesARollReference", "[assembly][mate][mechanical][p13]") {
    // A field a kind ignores is a field that means nothing, which is what
    // this validation exists to prevent. Checked for every other kind, the
    // basic ones included.
    Rig rig = makeRig();
    for (const MateType type : {MateType::Revolute, MateType::Cylindrical, MateType::Planar, MateType::Coincident,
                                MateType::Concentric, MateType::Parallel, MateType::Perpendicular}) {
        INFO("mate: " << assembly::toString(type));
        MateDefinition d{.type = type, .a = axis(rig.a), .b = axis(rig.b)};
        if (type == MateType::Planar || type == MateType::Coincident || type == MateType::Parallel ||
            type == MateType::Perpendicular) {
            d.a = plane(rig.a);
            d.b = plane(rig.b);
        }
        d.a2 = axis(rig.a, PrincipalAxis::X);
        d.b2 = axis(rig.b, PrincipalAxis::X);
        auto result = validate(d);
        REQUIRE_FALSE(result.has_value());
        CHECK_THAT(result.error().message, ContainsSubstring("takes no roll reference"));
    }
}

TEST_CASE("Slider_RollReferenceNeedsADirection", "[assembly][mate][mechanical][p13]") {
    Rig rig = makeRig();
    MateDefinition d = slider(rig.a, rig.b);
    // Every target kind carries a direction today, so this checks the rule
    // holds rather than that some kind fails it: a malformed target does.
    MateTarget broken = axis(rig.a, PrincipalAxis::X);
    broken.axis.reset();
    d.a2 = broken;
    CHECK_FALSE(validate(d).has_value());
}

// --- The document ------------------------------------------------------------------------------

TEST_CASE("MechanicalMate_DependsOnItsRollReferencesToo", "[assembly][mate][mechanical][p13]") {
    // A slide that did not depend on its roll references would not be
    // dirtied when the geometry they name changed.
    Rig rig = makeRig();
    const MateId id = require(assembly::createMate(rig.document, "Slide", slider(rig.a, rig.b)));
    const std::vector<ObjectId> dependencies = assembly::findMate(rig.document, id)->dependencies();
    CHECK(std::ranges::find(dependencies, ObjectId{rig.a}) != dependencies.end());
    CHECK(std::ranges::find(dependencies, ObjectId{rig.b}) != dependencies.end());
    // Both components appear, and each appears once: the axis and the roll
    // reference name the same component.
    CHECK(dependencies.size() == 2);
}

TEST_CASE("MechanicalMate_IsRefusedWhenItNamesAComponentThatIsNotThere", "[assembly][mate][mechanical][p13]") {
    Rig rig = makeRig();
    MateDefinition d = slider(rig.a, rig.b);
    d.b2 = axis(ComponentId::fromValue(9999), PrincipalAxis::X);
    // The roll reference must be on b's component, so a stray id fails that
    // rule before the document is ever consulted.
    CHECK_FALSE(validate(d).has_value());
    CHECK_FALSE(assembly::createMate(rig.document, "Slide", d).has_value());
}

// --- Persistence --------------------------------------------------------------------------------

TEST_CASE("MechanicalMateFile_KeepsEveryKindAndItsTargets", "[assembly][mate][mechanical][p13][io]") {
    TempDir dir;
    Rig rig = makeRig();
    std::vector<MateId> ids;
    ids.push_back(require(assembly::createMate(
        rig.document, "Hinge", {.type = MateType::Revolute, .a = axis(rig.a), .b = axis(rig.b)})));
    ids.push_back(require(assembly::createMate(rig.document, "Slide", slider(rig.a, rig.b))));
    ids.push_back(require(assembly::createMate(
        rig.document, "Sleeve", {.type = MateType::Cylindrical, .a = axis(rig.a), .b = axis(rig.b)})));
    ids.push_back(require(assembly::createMate(
        rig.document, "Face", {.type = MateType::Planar, .a = plane(rig.a), .b = plane(rig.b)})));
    REQUIRE(ids.size() == 4);

    const auto path = dir.path() / "joints.bcad";
    REQUIRE(io::saveDocument(rig.document, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());

    CHECK(assembly::mates(*loaded) == ids);
    for (const MateId id : ids) {
        const assembly::Mate* before = assembly::findMate(rig.document, id);
        const assembly::Mate* after = assembly::findMate(*loaded, id);
        REQUIRE(before != nullptr);
        REQUIRE(after != nullptr);
        INFO("mate: " << after->name());
        CHECK(after->definition() == before->definition());
        CHECK(after->dependencies() == before->dependencies());
        CHECK(after->contentEquals(*before));
    }

    // Writing what was read gives the same bytes.
    const auto again = dir.path() / "joints-again.bcad";
    REQUIRE(io::saveDocument(*loaded, again).has_value());
    CHECK(readFile(again) == readFile(path));
}

TEST_CASE("MechanicalMateFile_WritesTheRollReferenceOnlyForASlider", "[assembly][mate][mechanical][p13][io]") {
    // What validation requires is what is written, so a file cannot describe
    // a mate the model would refuse.
    TempDir dir;
    Rig rig = makeRig();
    require(assembly::createMate(rig.document, "Hinge",
                                 {.type = MateType::Revolute, .a = axis(rig.a), .b = axis(rig.b)}));
    require(assembly::createMate(rig.document, "Slide", slider(rig.a, rig.b)));

    const auto path = dir.path() / "joints.bcad";
    REQUIRE(io::saveDocument(rig.document, path).has_value());
    const std::string text = readFile(path);

    // Counted rather than searched for: "a2" belongs to exactly one of the
    // two mates, and a whole-file test for its absence would prove nothing.
    std::size_t rolls = 0;
    for (std::size_t at = text.find("\"a2\""); at != std::string::npos; at = text.find("\"a2\"", at + 4)) {
        ++rolls;
    }
    CHECK(rolls == 1);
    CHECK_THAT(text, ContainsSubstring("\"revolute\""));
    CHECK_THAT(text, ContainsSubstring("\"slider\""));
}

TEST_CASE("MechanicalMateFile_RefusesASliderWithNoRollReference", "[assembly][mate][mechanical][p13][io]") {
    // A hand-edited file that drops the roll reference must be refused on
    // load, not loaded into a slider that silently behaves as a sleeve.
    TempDir dir;
    Rig rig = makeRig();
    require(assembly::createMate(rig.document, "Slide", slider(rig.a, rig.b)));
    const auto path = dir.path() / "joints.bcad";
    REQUIRE(io::saveDocument(rig.document, path).has_value());

    std::string text = readFile(path);
    const std::size_t at = text.find("\"a2\"");
    REQUIRE(at != std::string::npos);
    // Rename the key so the reader no longer sees a roll reference.
    text.replace(at, 4, "\"zz\"");
    const auto edited = dir.path() / "edited.bcad";
    writeFile(edited, text);

    auto loaded = io::loadDocument(edited);
    CHECK_FALSE(loaded.has_value());
}
