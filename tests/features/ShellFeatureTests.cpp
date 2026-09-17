#include "TestHelpers.hpp"
#include "core/geometry/GeometryTestSupport.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/ShellModels.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/FaceReferences.hpp>
#include <bettercad/features/FeatureCommands.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/features/SplitFeature.hpp>
#include <bettercad/features/Validation.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::ConcaveShellModel;
using bettercad::test::ExpectedShell;
using bettercad::test::FaceKindModel;
using bettercad::test::kPi;
using bettercad::test::nameOf;
using bettercad::test::requireReport;
using bettercad::test::ShelledBlockModel;
using bettercad::test::TurnedShellModel;
using bettercad::test::Vec3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using geometry::ShellSide;

// P12-FEAT-003: shell features. Expected volumes, centres, bounds and face
// areas are written out from the parameters in support/ShellModels.hpp.

namespace {

// Planes and cylinders are integrated to rounding (P12-DATUM-001).
constexpr double kRel = bettercad::test::kRelTight;
constexpr double kTolCentreMm = bettercad::test::kPositionToleranceMm;
constexpr double kTolBounds = 1e-7; // exact bounds (P3)

Vec3 mm(const Point3D& p) {
    return {p.x.in(units::mm), p.y.in(units::mm), p.z.in(units::mm)};
}

void checkVec(const Vec3& actual, const Vec3& expected, double tolerance) {
    CHECK_THAT(actual[0], WithinAbs(expected[0], tolerance));
    CHECK_THAT(actual[1], WithinAbs(expected[1], tolerance));
    CHECK_THAT(actual[2], WithinAbs(expected[2], tolerance));
}

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

RegenerationReport regenerate(Regenerator& regenerator, Document& doc) {
    RegenerationReport report = requireReport(regenerator, doc);
    INFO((report.errors.empty() ? std::string{}
                                : std::format("{}: {}", report.errors.begin()->first,
                                              report.errors.begin()->second.message)));
    REQUIRE(report.succeeded());
    return report;
}

const geometry::Body& bodyOf(const Regenerator& regenerator, ObjectId id) {
    const geometry::Body* body = regenerator.body(id);
    REQUIRE(body != nullptr);
    return *body;
}

double volumeOf(const Regenerator& regenerator, ObjectId id) {
    const auto props = bodyOf(regenerator, id).massProperties();
    REQUIRE(props.has_value());
    return props->volume.in(units::mm3);
}

/// A shell's body against its expected volume, centre and bounds: one valid
/// solid.
void checkShell(const Regenerator& regenerator, ObjectId id, const ExpectedShell& expected) {
    const geometry::Body& body = bodyOf(regenerator, id);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    const auto props = body.massProperties();
    REQUIRE(props.has_value());
    CHECK_THAT(props->volume.in(units::mm3), WithinRel(expected.volume(), kRel));
    checkVec(mm(props->centerOfMass), expected.centre(), kTolCentreMm);
    const auto box = body.boundingBox();
    REQUIRE(box.has_value());
    checkVec(mm(box->min), expected.lower, kTolBounds);
    checkVec(mm(box->max), expected.upper, kTolBounds);
}

/// The total area of the faces of @p id's body that carry @p name.
double namedArea(const Regenerator& regenerator, ObjectId id, const FaceName& name) {
    const auto faces = geometry::findNamedFaces(bodyOf(regenerator, id), name);
    REQUIRE(faces.has_value());
    double area = 0.0;
    for (const geometry::FaceInfo& face : *faces) {
        area += face.area.in(units::mm2);
    }
    return area;
}

/// How many faces of @p id's body carry no name.
std::size_t unnamedFaces(const Regenerator& regenerator, ObjectId id) {
    const auto faces = geometry::listFaces(bodyOf(regenerator, id));
    REQUIRE(faces.has_value());
    return static_cast<std::size_t>(
        std::ranges::count_if(*faces, [](const geometry::FaceInfo& face) { return face.names.empty(); }));
}

/// Every body volume of a document, bit for bit.
std::vector<std::uint64_t> volumes(const Regenerator& regenerator, const Document& doc) {
    std::vector<std::uint64_t> result;
    for (const DocumentObject& object : doc.objects()) {
        if (regenerator.body(object.id()) != nullptr) {
            result.push_back(bits(volumeOf(regenerator, object.id())));
        }
    }
    return result;
}

void setValue(CommandHistory& history, Document& doc, ParameterId parameter, Length value) {
    REQUIRE(history
                .execute(doc, std::make_unique<ModifyParameterCommand>(
                                  parameter, ParameterChanges{.value = DimensionedValue::of(value)}))
                .has_value());
}

void modifyShell(CommandHistory& history, Document& doc, ObjectId shell,
                 const std::function<void(ShellDefinition&)>& change) {
    ShellDefinition d = doc.findObjectAs<ShellFeature>(shell)->definition();
    change(d);
    REQUIRE(history.execute(doc, std::make_unique<ModifyShellCommand>(FaceKindModel::featureOf(shell), d))
                .has_value());
}

/// Regenerates: @p item fails with @p code, keeping no body; returns its message.
std::string failureOf(const RegenerationReport& report, const Regenerator& regenerator, ObjectId item,
                      ErrorCode code) {
    CHECK(std::ranges::find(report.failed, item) != report.failed.end());
    REQUIRE(report.errors.contains(item));
    CHECK(report.errors.at(item).code == code);
    CHECK(regenerator.body(item) == nullptr);
    return report.errors.at(item).message;
}

/// The message of a structured "cannot build" failure (geometry::shellBody()).
std::string cannotBuild(std::string_view feature, double t, std::string_view side, std::string_view reason) {
    return std::format("{}: shell: the kernel cannot build walls {} mm thick {} on this body: {}; walls thicker "
                       "than half the body where it is thinnest, or an inward wall at least as thick as a round it "
                       "follows, cannot be built",
                       feature, t, side, reason);
}

FeatureId fid(ObjectId id) {
    return FaceKindModel::featureOf(id);
}

} // namespace

TEST_CASE("ShellFeature_DefinitionsAreValidated", "[features][shell][p12]") {
    const FeatureId target = FeatureId::fromValue(1);
    const FaceName top = nameOf(ObjectId::fromValue(1), FaceRole::EndCap);
    const auto message = [](const ShellDefinition& d) {
        const auto created = ShellFeature::create("S", d);
        REQUIRE_FALSE(created.has_value());
        CHECK(created.error().code == ErrorCode::InvalidArgument);
        return created.error().message;
    };
    REQUIRE(ShellFeature::create("S", {.target = target, .openFaces = {top}, .thickness = 2_mm}).has_value());
    CHECK(message({.openFaces = {top}, .thickness = 2_mm}) == "a shell needs a target feature");
    CHECK(message({.target = target, .thickness = 2_mm}) ==
          "a shell needs one or more open faces (a closed hollow is not built)");
    CHECK(message({.target = target, .openFaces = {top, top}, .thickness = 2_mm}) ==
          "open face 2 repeats an earlier open face");
    CHECK(message({.target = target,
                   .openFaces = {nameOf(ObjectId::fromValue(1), FaceRole::EndCap, EntityId::fromValue(3))},
                   .thickness = 2_mm}) == "open face 1: an end cap is not named by an entity");
    CHECK(message({.target = target, .openFaces = {top}}) ==
          "the shell thickness must be positive and finite, got 0 mm");
    CHECK(message({.target = target,
                   .openFaces = {top},
                   .thickness = Length::fromSi(std::numeric_limits<double>::quiet_NaN())}) ==
          "the shell thickness must be positive and finite, got nan mm");
    CHECK(message({.target = target, .openFaces = {top}, .thicknessParameter = ParameterId{}}) ==
          "the thickness parameter ID must be valid");
    CHECK(message({.target = target, .openFaces = {top}, .thickness = 2_mm, .side = static_cast<ShellSide>(5)}) ==
          "a shell's walls lie inward or outward");
    // A driven thickness needs no literal one.
    REQUIRE(ShellFeature::create("S", {.target = target,
                                       .openFaces = {top},
                                       .thicknessParameter = ParameterId::fromValue(9)})
                .has_value());

    // Dependencies: the target, the parameter, the open faces' features and
    // the features that copied them, each once, in that order.
    FaceName copied = nameOf(ObjectId::fromValue(4), FaceRole::Side, EntityId::fromValue(2));
    copied.face.copies = {FaceCopy{ObjectId::fromValue(6), 2}, FaceCopy{ObjectId::fromValue(7), 1}};
    const auto shell = ShellFeature::create("S", {.target = FeatureId::fromValue(7),
                                                  .openFaces = {top, copied, nameOf(ObjectId::fromValue(4),
                                                                                    FaceRole::StartCap)},
                                                  .thicknessParameter = ParameterId::fromValue(9)});
    REQUIRE(shell.has_value());
    CHECK((*shell)->dependencies() == std::vector<ObjectId>{ObjectId::fromValue(7), ObjectId::fromValue(9),
                                                            ObjectId::fromValue(1), ObjectId::fromValue(4),
                                                            ObjectId::fromValue(6)});
    CHECK((*shell)->consumedFeatures() == std::vector<FeatureId>{FeatureId::fromValue(7)});
    CHECK((*shell)->typeName() == "shell");
    // An unchanged definition is not an edit.
    auto edited = ShellFeature::create("S", {.target = target, .openFaces = {top}, .thickness = 2_mm});
    REQUIRE(edited.has_value());
    CHECK((*edited)->setDefinition({.target = target, .openFaces = {top}, .thickness = 2_mm}) == false);
    CHECK((*edited)->setDefinition({.target = target, .openFaces = {top}, .thickness = 3_mm}) == true);
}

TEST_CASE("ShellFeature_BlockFollowsItsWallAndHeight", "[features][shell][regeneration][p12][acceptance]") {
    ShelledBlockModel m;
    Regenerator regenerator;
    CommandHistory history;
    const auto check = [&](double h, double t) {
        CAPTURE(h, t);
        checkShell(regenerator, m.cup, ShelledBlockModel::cupShape(h, t));
        checkShell(regenerator, m.tube, ShelledBlockModel::tubeShape(h, t));
        checkShell(regenerator, m.casing, ShelledBlockModel::casingShape(h, t));
        // The rims keep the caps' names; the sides keep theirs, whole.
        const FaceName top = nameOf(m.block, FaceRole::EndCap);
        const FaceName bottom = nameOf(m.block, FaceRole::StartCap);
        CHECK_THAT(namedArea(regenerator, m.cup, top), WithinRel(ShelledBlockModel::innerRim(t), kRel));
        CHECK_THAT(namedArea(regenerator, m.cup, bottom), WithinRel(6000.0, kRel));
        CHECK_THAT(namedArea(regenerator, m.tube, top), WithinRel(ShelledBlockModel::innerRim(t), kRel));
        CHECK_THAT(namedArea(regenerator, m.tube, bottom), WithinRel(ShelledBlockModel::innerRim(t), kRel));
        CHECK_THAT(namedArea(regenerator, m.casing, top), WithinRel(ShelledBlockModel::outerRim(t), kRel));
        CHECK_THAT(namedArea(regenerator, m.casing, bottom), WithinRel(6000.0, kRel));
        for (const ObjectId shell : {m.cup, m.tube, m.casing}) {
            for (std::size_t i = 0; i < m.lines.size(); ++i) {
                CHECK_THAT(namedArea(regenerator, shell, nameOf(m.block, FaceRole::Side, m.lines[i])),
                           WithinRel((i % 2 == 0 ? 100.0 : 60.0) * h, kRel));
            }
        }
        // The walls are not named: five each for the cup and the casing, four
        // for the tube.
        CHECK(unnamedFaces(regenerator, m.cup) == 5);
        CHECK(unnamedFaces(regenerator, m.tube) == 4);
        CHECK(unnamedFaces(regenerator, m.casing) == 5);
    };
    regenerate(regenerator, m.doc);
    check(40.0, 5.0);
    const auto first = volumes(regenerator, m.doc);
    // The shells consume the block.
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.cup, m.tube, m.casing});
    const DocumentGraph graph = buildDependencyGraph(m.doc);
    const auto deps = graph.graph.dependenciesOf(m.cup);
    CHECK(std::ranges::find(deps, m.block) != deps.end());
    CHECK(std::ranges::find(deps, ObjectId{m.wall}) != deps.end());

    // A thinner wall regenerates only the shells.
    setValue(history, m.doc, m.wall, 2_mm);
    const RegenerationReport thinner = regenerate(regenerator, m.doc);
    for (const ObjectId id : {m.cup, m.tube, m.casing}) {
        CHECK(std::ranges::find(thinner.regenerated, id) != thinner.regenerated.end());
    }
    for (const ObjectId id : {m.blockSketch, m.block}) {
        CHECK(std::ranges::find(thinner.regenerated, id) == thinner.regenerated.end());
    }
    check(40.0, 2.0);
    // A taller block moves the end cap; the shells open it where it now is.
    setValue(history, m.doc, m.height, 60_mm);
    regenerate(regenerator, m.doc);
    check(60.0, 2.0);
    // A wall that is not round.
    setValue(history, m.doc, m.wall, 2.5_mm);
    regenerate(regenerator, m.doc);
    check(60.0, 2.5);

    REQUIRE(history.undo(m.doc).has_value());
    REQUIRE(history.undo(m.doc).has_value());
    REQUIRE(history.undo(m.doc).has_value());
    regenerate(regenerator, m.doc);
    check(40.0, 5.0);
    CHECK(volumes(regenerator, m.doc) == first);
    // A fresh regeneration of a copy gives the same bodies, bit for bit.
    Document copy = m.doc.clone();
    Regenerator again;
    regenerate(again, copy);
    CHECK(volumes(again, copy) == first);
}

TEST_CASE("ShellFeature_TurnedAndCurvedBodies", "[features][shell][regeneration][p12][acceptance]") {
    TurnedShellModel m;
    Regenerator regenerator;
    CommandHistory history;
    for (const double t : {3.0, 5.0}) {
        CAPTURE(t);
        if (t != 3.0) {
            setValue(history, m.doc, m.wall, t * units::mm);
        }
        regenerate(regenerator, m.doc);
        checkShell(regenerator, m.canCup, TurnedShellModel::canCupShape(t));
        checkShell(regenerator, m.canCase, TurnedShellModel::canCaseShape(t));
        checkShell(regenerator, m.shaftBore, TurnedShellModel::boreShape(t));
        checkShell(regenerator, m.shaftSleeve, TurnedShellModel::sleeveShape(t));
        // Rims: the can's top and the shaft's small end.
        const FaceName canTop = nameOf(m.can, FaceRole::EndCap);
        CHECK_THAT(namedArea(regenerator, m.canCup, canTop), WithinRel(kPi * (400.0 - (20 - t) * (20 - t)), kRel));
        CHECK_THAT(namedArea(regenerator, m.canCase, canTop), WithinRel(kPi * ((20 + t) * (20 + t) - 400.0), kRel));
        const FaceName smallEnd = nameOf(m.shaft, FaceRole::Side, m.shaftLines[4]);
        CHECK_THAT(namedArea(regenerator, m.shaftBore, smallEnd),
                   WithinRel(kPi * (225.0 - (15 - t) * (15 - t)), kRel));
        CHECK_THAT(namedArea(regenerator, m.shaftSleeve, smallEnd),
                   WithinRel(kPi * ((15 + t) * (15 + t) - 225.0), kRel));
        // The big end stays whole, and so do the cylinders.
        for (const ObjectId shell : {m.shaftBore, m.shaftSleeve}) {
            CHECK_THAT(namedArea(regenerator, shell, nameOf(m.shaft, FaceRole::Side, m.shaftLines[0])),
                       WithinRel(900.0 * kPi, kRel));
            CHECK_THAT(namedArea(regenerator, shell, nameOf(m.shaft, FaceRole::Side, m.shaftLines[1])),
                       WithinRel(2.0 * kPi * 30.0 * 20.0, kRel));
            CHECK_THAT(namedArea(regenerator, shell, nameOf(m.shaft, FaceRole::Side, m.shaftLines[3])),
                       WithinRel(2.0 * kPi * 15.0 * 40.0, kRel));
        }
    }
    // At 3 mm the kernel probe's values: 10602 pi and 13302 pi.
    REQUIRE(history.undo(m.doc).has_value());
    regenerate(regenerator, m.doc);
    CHECK_THAT(volumeOf(regenerator, m.shaftBore), WithinRel(10602.0 * kPi, kRel));
    CHECK_THAT(volumeOf(regenerator, m.shaftSleeve), WithinRel(13302.0 * kPi, kRel));
}

TEST_CASE("ShellFeature_ConcaveDrilledAndRoundedBodies", "[features][shell][regeneration][p12][acceptance]") {
    ConcaveShellModel m;
    Regenerator regenerator;
    CommandHistory history;
    const auto check = [&](double h, double t, double r) {
        CAPTURE(h, t, r);
        checkShell(regenerator, m.ellCup, ConcaveShellModel::ellCupShape(t));
        checkShell(regenerator, m.ellCase, ConcaveShellModel::ellCaseShape(t));
        checkShell(regenerator, m.drillCup, ConcaveShellModel::drillCupShape(h, t));
        checkShell(regenerator, m.drillCase, ConcaveShellModel::drillCaseShape(h, t));
        checkShell(regenerator, m.roundCup, ConcaveShellModel::roundCupShape(h, t, r));
        checkShell(regenerator, m.roundCase, ConcaveShellModel::roundCaseShape(h, t, r));
        // Rims.
        const double ellCavity = (100 - 2 * t) * (30 - 2 * t) + (40 - 2 * t) * (80 - 2 * t) -
                                 (40 - 2 * t) * (30 - 2 * t);
        CHECK_THAT(namedArea(regenerator, m.ellCup, nameOf(m.ell, FaceRole::EndCap)),
                   WithinRel(5000.0 - ellCavity, kRel));
        CHECK_THAT(namedArea(regenerator, m.drillCup, nameOf(m.plate, FaceRole::EndCap)),
                   WithinRel(ConcaveShellModel::drillRim(t), kRel));
        const double padTop = 6000.0 - (4.0 - kPi) * r * r;
        const double padMouth = (100 - 2 * t) * (60 - 2 * t) - (4.0 - kPi) * (r - t) * (r - t);
        CHECK_THAT(namedArea(regenerator, m.roundCup, nameOf(m.pad, FaceRole::EndCap)),
                   WithinRel(padTop - padMouth, kRel));
    };
    regenerate(regenerator, m.doc);
    check(40.0, 5.0, 10.0);
    // The kernel probe's values at these sizes.
    CHECK_THAT(volumeOf(regenerator, m.ellCup), WithinRel(101500.0, kRel));
    CHECK_THAT(volumeOf(regenerator, m.ellCase), WithinRel(129500.0, kRel));
    CHECK_THAT(volumeOf(regenerator, m.drillCup), WithinRel(82500.0 + 3875.0 * kPi, kRel));
    CHECK_THAT(volumeOf(regenerator, m.drillCase), WithinRel(106500.0 + 2875.0 * kPi, kRel));
    const auto first = volumes(regenerator, m.doc);

    setValue(history, m.doc, m.wall, 3_mm);
    setValue(history, m.doc, m.height, 55_mm);
    setValue(history, m.doc, m.round, 8_mm);
    regenerate(regenerator, m.doc);
    check(55.0, 3.0, 8.0);
    for (int i = 0; i < 3; ++i) {
        REQUIRE(history.undo(m.doc).has_value());
    }
    regenerate(regenerator, m.doc);
    CHECK(volumes(regenerator, m.doc) == first);
}

TEST_CASE("ShellFeature_FailuresAreStructuredAndAtomic", "[features][shell][p12]") {
    SECTION("walls too thick for the whole body") {
        ShelledBlockModel m;
        Regenerator regenerator;
        regenerate(regenerator, m.doc);
        const auto before = volumes(regenerator, m.doc);
        CommandHistory history;
        // 35 mm walls overlap across the 60 mm width: the kernel returns the
        // block unchanged and says it succeeded.
        setValue(history, m.doc, m.wall, 35_mm);
        RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(failureOf(report, regenerator, m.cup, ErrorCode::FailedPrecondition) ==
              cannotBuild("Cup", 35.0, "inward",
                          "its result is not a shell of the body: 5 of the 5 remaining faces have no wall"));
        CHECK_THAT(failureOf(report, regenerator, m.tube, ErrorCode::FailedPrecondition),
                   StartsWith("Tube: shell: the kernel cannot build walls 35 mm thick inward on this body: "));
        // Outward there is room.
        CHECK(regenerator.body(m.casing) != nullptr);
        checkShell(regenerator, m.casing, ShelledBlockModel::casingShape(40.0, 35.0));
        // 30 mm closes the cavity: the kernel returns an invalid solid.
        setValue(history, m.doc, m.wall, 30_mm);
        report = requireReport(regenerator, m.doc);
        CHECK(failureOf(report, regenerator, m.cup, ErrorCode::FailedPrecondition) ==
              cannotBuild("Cup", 30.0, "inward", "its result is not a shell of the body: it is not a valid solid"));
        // Undo restores the bodies, bit for bit.
        REQUIRE(history.undo(m.doc).has_value());
        REQUIRE(history.undo(m.doc).has_value());
        regenerate(regenerator, m.doc);
        CHECK(volumes(regenerator, m.doc) == before);
    }
    SECTION("walls too thick where the body is thin") {
        // Two 40 x 40 blocks joined by a bridge 8 wide, 30 high. 3 mm walls
        // leave a 2 mm passage through the bridge; 5 mm walls would meet in
        // it, and the kernel cannot build them.
        FaceKindModel m("0e5c3a91-7d24-4b68-8f1a-2b9d6c4e7f35", "Dumbbell");
        const ParameterId wall = m.doc.createParameter("wall", 3_mm, units::mm).value();
        const ObjectId sketch =
            m.doc
                .addObject(bettercad::test::fixedPolygon("DumbbellSketch", Frame3D::xy(),
                                                         {{0, 0}, {40, 0}, {40, 16}, {60, 16}, {60, 0}, {100, 0},
                                                          {100, 40}, {60, 40}, {60, 24}, {40, 24}, {40, 40}, {0, 40}}))
                .value();
        const ObjectId dumbbell = m.addBoss("Dumbbell", sketch, 30.0);
        const ObjectId hollow = m.add(ShellFeature::create(
            "Hollow", {.target = fid(dumbbell), .openFaces = {nameOf(dumbbell, FaceRole::EndCap)},
                       .thicknessParameter = wall}));
        Regenerator regenerator;
        regenerate(regenerator, m.doc);
        using bettercad::test::boxPart;
        checkShell(regenerator, hollow,
                   {{boxPart({0, 0, 0}, {40, 40, 30}), boxPart({60, 0, 0}, {100, 40, 30}),
                     boxPart({40, 16, 0}, {60, 24, 30}), boxPart({3, 3, 3}, {37, 37, 30}, -1.0),
                     boxPart({63, 3, 3}, {97, 37, 30}, -1.0), boxPart({37, 19, 3}, {63, 21, 30}, -1.0)},
                    {0, 0, 0},
                    {100, 40, 30}});
        CHECK_THAT(volumeOf(regenerator, hollow), WithinRel(100800.0 - 63828.0, kRel));
        CommandHistory history;
        setValue(history, m.doc, wall, 5_mm);
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(failureOf(report, regenerator, hollow, ErrorCode::FailedPrecondition) ==
              cannotBuild("Hollow", 5.0, "inward", "kernel: unknown error"));
        CHECK(report.failed == std::vector<ObjectId>{hollow});
        CHECK(regenerator.body(dumbbell) != nullptr);
    }
    SECTION("rounds no larger than an inward wall") {
        ConcaveShellModel m;
        Regenerator regenerator;
        CommandHistory history;
        setValue(history, m.doc, m.round, 3_mm);
        RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(failureOf(report, regenerator, m.roundCup, ErrorCode::FailedPrecondition) ==
              cannotBuild("RoundCup", 5.0, "inward", "kernel: unknown error"));
        CHECK(report.failed == std::vector<ObjectId>{m.roundCup});
        // Outward the round grows.
        checkShell(regenerator, m.roundCase, ConcaveShellModel::roundCaseShape(40.0, 5.0, 3.0));
        // A round as large as the wall would vanish: the kernel throws.
        setValue(history, m.doc, m.round, 5_mm);
        report = requireReport(regenerator, m.doc);
        CHECK_THAT(failureOf(report, regenerator, m.roundCup, ErrorCode::FailedPrecondition),
                   StartsWith("RoundCup: shell: the kernel cannot build walls 5 mm thick inward on this body: "
                              "kernel: "));
        checkShell(regenerator, m.roundCase, ConcaveShellModel::roundCaseShape(40.0, 5.0, 5.0));
    }
    SECTION("open faces that the target's body does not have") {
        FaceKindModel m("6b1f8d24-9e37-4a50-b2c6-d4e8a0f3c719", "Missing");
        const ObjectId blockSketch = m.addFixedRectangle("BlockSketch", 0.0, 0.0, 100.0, 60.0);
        const ObjectId block = m.addBoss("Block", blockSketch, 40.0);
        const ObjectId bossSketch = m.addFixedRectangle("BossSketch", 200.0, 0.0, 20.0, 20.0);
        const ObjectId boss = m.addBoss("Boss", bossSketch, 10.0);
        // Base: the block below z = 10, without its top. Halves: the block cut
        // across at x = 50, both parts kept.
        const ObjectId level = m.add(DatumPlane::create(
            "Level", {.kind = DatumPlaneKind::Offset, .base = {.plane = PrincipalPlane::XY}, .offset = 10_mm}));
        const ObjectId base = m.add(SplitFeature::create(
            "Base", {.target = fid(block), .plane = {.object = level}, .keep = geometry::SplitKeep::Back}));
        const ObjectId middle = m.add(DatumPlane::create(
            "Middle", {.kind = DatumPlaneKind::Offset, .base = {.plane = PrincipalPlane::YZ}, .offset = 50_mm}));
        const ObjectId halves = m.add(SplitFeature::create(
            "Halves", {.target = fid(block), .plane = {.object = middle}, .keep = geometry::SplitKeep::Both}));
        const ObjectId wrongBody = m.add(ShellFeature::create(
            "WrongBody", {.target = fid(block), .openFaces = {nameOf(boss, FaceRole::EndCap)}, .thickness = 2_mm}));
        const ObjectId cutAway = m.add(ShellFeature::create(
            "CutAway", {.target = fid(base), .openFaces = {nameOf(block, FaceRole::EndCap)}, .thickness = 2_mm}));
        const ObjectId unnamed = m.add(ShellFeature::create(
            "Unnamed", {.target = fid(base), .openFaces = {nameOf(base, FaceRole::EndCap)}, .thickness = 2_mm}));
        const ObjectId onSketch = m.add(ShellFeature::create(
            "OnSketch", {.target = fid(block),
                         .openFaces = {nameOf(block, FaceRole::EndCap), nameOf(blockSketch, FaceRole::EndCap)},
                         .thickness = 2_mm}));
        const ObjectId twoSolids = m.add(ShellFeature::create(
            "TwoSolids", {.target = fid(halves), .openFaces = {nameOf(block, FaceRole::EndCap)}, .thickness = 2_mm}));
        Regenerator regenerator;
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(failureOf(report, regenerator, wrongBody, ErrorCode::NotFound) ==
              std::format("WrongBody: open face 1, the end cap of Boss ({}), is not a face of the body of Block ({})",
                          boss, block));
        CHECK(failureOf(report, regenerator, cutAway, ErrorCode::NotFound) ==
              std::format("CutAway: open face 1, the end cap of Block ({}), is not a face of the body of Base ({})",
                          block, base));
        CHECK(failureOf(report, regenerator, unnamed, ErrorCode::InvalidArgument) ==
              std::format("Unnamed: open face 1: Base ({}) is a split, whose faces are not named (extrudes, "
                          "revolves, sweeps, lofts, holes, chamfers and ribs name theirs)",
                          base));
        CHECK(failureOf(report, regenerator, onSketch, ErrorCode::InvalidArgument) ==
              std::format("OnSketch: open face 2: BlockSketch ({}) is a sketch, not a feature, and has no faces",
                          blockSketch));
        CHECK(failureOf(report, regenerator, twoSolids, ErrorCode::FailedPrecondition) ==
              "TwoSolids: shell: a shell hollows one solid; the body has 2");
        // The rest is built.
        CHECK(regenerator.body(base) != nullptr);
        CHECK(regenerator.body(halves) != nullptr);

        // Validation says why for the names it can check without geometry.
        const ValidationReport validation = validateDocument(m.doc);
        CHECK_FALSE(validation.valid());
        const auto has = [&](const std::string& text) {
            return std::ranges::any_of(validation.issues, [&](const ValidationIssue& issue) {
                return issue.message == text;
            });
        };
        CHECK(has(std::format("Unnamed ({}): open face 1 is the end cap of Base ({}): Base ({}) is a split, whose "
                              "faces are not named (extrudes, revolves, sweeps, lofts, holes, chamfers and ribs name "
                              "theirs)",
                              unnamed, base, base)));
        CHECK(has(std::format("OnSketch ({}): open face 2 is the end cap of BlockSketch ({}): BlockSketch ({}) is a "
                              "sketch, not a feature, and has no faces",
                              onSketch, blockSketch, blockSketch)));
        // Open faces depend on the features that name them.
        const DocumentGraph graph = buildDependencyGraph(m.doc);
        const auto deps = graph.graph.dependenciesOf(wrongBody);
        CHECK(std::ranges::find(deps, boss) != deps.end());
    }
    SECTION("thicknesses of the wrong kind or value") {
        ShelledBlockModel m;
        const ParameterId tilt = m.doc.createParameter("tilt", 10_deg, units::deg).value();
        CommandHistory history;
        modifyShell(history, m.doc, m.cup, [&](ShellDefinition& d) { d.thicknessParameter = tilt; });
        Regenerator regenerator;
        RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(failureOf(report, regenerator, m.cup, ErrorCode::DimensionMismatch).starts_with("Cup: "));
        const ValidationReport validation = validateDocument(m.doc);
        CHECK(std::ranges::any_of(validation.issues, [&](const ValidationIssue& issue) {
            return issue.message == std::format("Cup ({}): the thickness is driven by tilt ({}), which is an angle, "
                                                "not a length",
                                                m.cup, ObjectId{tilt});
        }));
        REQUIRE(history.undo(m.doc).has_value());
        setValue(history, m.doc, m.wall, -(2_mm));
        report = requireReport(regenerator, m.doc);
        const std::array<std::pair<ObjectId, std::string_view>, 3> shells{
            {{m.cup, "Cup"}, {m.tube, "Tube"}, {m.casing, "Casing"}}};
        for (const auto& [id, name] : shells) {
            CHECK(failureOf(report, regenerator, id, ErrorCode::InvalidArgument) ==
                  std::format("{}: shell: the shell thickness must be positive and finite, got -2 mm", name));
        }
    }
    SECTION("shells are not repeated, their faces are not named, and they need a body") {
        ShelledBlockModel m;
        const ObjectId row = m.add(LinearPatternFeature::create(
            "Row", {.source = fid(m.cup), .first = {.direction = {1.0, 0.0, 0.0}, .count = 2, .spacing = 150_mm}}));
        auto onCup = std::make_unique<sketch::Sketch>("OnCup");
        REQUIRE(onCup->setAttachment(FaceKindModel::faceOf(m.cup, {.role = FaceRole::EndCap})).has_value());
        const ObjectId onCupId = m.doc.addObject(std::move(onCup)).value();
        const ObjectId noBody = m.add(ShellFeature::create(
            "NoBody", {.target = fid(m.blockSketch), .openFaces = {nameOf(m.block, FaceRole::EndCap)},
                       .thickness = 2_mm}));
        Regenerator regenerator;
        const RegenerationReport report = requireReport(regenerator, m.doc);
        REQUIRE(report.errors.contains(row));
        CHECK_THAT(report.errors.at(row).message, ContainsSubstring("a linear pattern cannot repeat a shell"));
        REQUIRE(report.errors.contains(onCupId));
        CHECK(report.errors.at(onCupId).message ==
              std::format("OnCup ({}): Cup ({}) is a shell, whose faces are not named (extrudes, revolves, sweeps, "
                          "lofts, holes, chamfers and ribs name theirs)",
                          onCupId, m.cup));
        CHECK(failureOf(report, regenerator, noBody, ErrorCode::FailedPrecondition) ==
              "NoBody: a shell needs the body of its target feature");
        const ValidationReport validation = validateDocument(m.doc);
        CHECK(std::ranges::any_of(validation.issues, [&](const ValidationIssue& issue) {
            return issue.message == std::format("NoBody ({}): the target is BlockSketch ({}), which is a sketch, not "
                                                "a feature with a body",
                                                noBody, m.blockSketch);
        }));
    }
}

TEST_CASE("ShellFeature_RegeneratesDeterministically", "[features][shell][regeneration][p12]") {
    const auto namesOf = [](const Regenerator& r, ObjectId id) {
        const auto faces = geometry::listFaces(bodyOf(r, id));
        REQUIRE(faces.has_value());
        std::vector<std::vector<FaceName>> names;
        for (const geometry::FaceInfo& face : *faces) {
            names.push_back(face.names);
        }
        return names;
    };
    ConcaveShellModel a;
    ConcaveShellModel b;
    Regenerator ra;
    Regenerator rb;
    regenerate(ra, a.doc);
    regenerate(rb, b.doc);
    CHECK(volumes(ra, a.doc) == volumes(rb, b.doc));
    CHECK(equivalent(a.doc, b.doc));
    for (const ObjectId id : {a.ellCup, a.ellCase, a.drillCup, a.drillCase, a.roundCup, a.roundCase}) {
        CAPTURE(id);
        CHECK(namesOf(ra, id) == namesOf(rb, id));
        CHECK(bodyOf(ra, id).topology() == bodyOf(rb, id).topology());
    }
    // A second pass rebuilds nothing; a full rebuild gives the same bits.
    const auto before = volumes(ra, a.doc);
    CHECK(requireReport(ra, a.doc).regenerated.empty());
    auto again = ra.regenerateAll(a.doc);
    REQUIRE(again.has_value());
    REQUIRE(again->succeeded());
    CHECK(volumes(ra, a.doc) == before);
}

TEST_CASE("ShellFeature_CreationAndEditsAreUndoable", "[features][shell][commands][p12]") {
    ShelledBlockModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    const Document before = m.doc.clone();
    CommandHistory history;
    // Open at the top and at the front (the side the line y = 0 sweeps),
    // 1 mm inward: the cavity reaches the front.
    const ShellDefinition open{
        .target = fid(m.block),
        .openFaces = {nameOf(m.block, FaceRole::EndCap), nameOf(m.block, FaceRole::Side, m.lines[0])},
        .thickness = 1_mm};
    REQUIRE(history.execute(m.doc, std::make_unique<CreateShellCommand>("Open", open)).has_value());
    const auto opened = m.doc.findByName("Open");
    REQUIRE(opened.has_value());
    regenerate(regenerator, m.doc);
    CHECK_THAT(volumeOf(regenerator, *opened), WithinRel(240000.0 - 98.0 * 59.0 * 39.0, kRel));
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.cup, m.tube, m.casing, *opened});

    // Outward, the outside grows everywhere but at the openings.
    modifyShell(history, m.doc, *opened, [](ShellDefinition& d) { d.side = ShellSide::Outward; });
    regenerate(regenerator, m.doc);
    CHECK_THAT(volumeOf(regenerator, *opened), WithinRel(102.0 * 61.0 * 41.0 - 240000.0, kRel));
    // An invalid edit is refused and changes nothing.
    const Document edited = m.doc.clone();
    ShellDefinition closed = m.doc.findObjectAs<ShellFeature>(*opened)->definition();
    closed.openFaces.clear();
    const auto refused = history.execute(m.doc, std::make_unique<ModifyShellCommand>(fid(*opened), closed));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message == "a shell needs one or more open faces (a closed hollow is not built)");
    CHECK(equivalent(edited, m.doc));

    REQUIRE(history.undo(m.doc).has_value());
    regenerate(regenerator, m.doc);
    CHECK_THAT(volumeOf(regenerator, *opened), WithinRel(240000.0 - 98.0 * 59.0 * 39.0, kRel));
    REQUIRE(history.undo(m.doc).has_value());
    CHECK(equivalent(before, m.doc));
    REQUIRE(history.redo(m.doc).has_value());
    REQUIRE(history.redo(m.doc).has_value());
    regenerate(regenerator, m.doc);
    CHECK_THAT(volumeOf(regenerator, *m.doc.findByName("Open")), WithinRel(102.0 * 61.0 * 41.0 - 240000.0, kRel));
}
