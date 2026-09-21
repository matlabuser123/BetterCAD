#include "reference/AssemblyTestSupport.hpp"

#include <bettercad/core/document/MateReference.hpp>
#include <bettercad/io/DocumentFile.hpp>

#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <numbers>
#include <set>
#include <string>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using namespace bettercad::test::asmref;
using assembly::SolveStatus;
using reference::AssemblyReferenceModelKind;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P13-REFMOD-001: the production assembly reference suite.
//
// WHERE THE EXPECTED NUMBERS COME FROM. Every count below was derived by
// hand, from the mate equation counts that P13-SOLVE-001 and P13-MATE-002
// qualified, before any of these models was run:
//
//     Fixed          grounds a component: its 6 unknowns leave the problem
//     Distance       1    Parallel      2    Coincident   3
//     Perpendicular  1    Planar        3    Concentric   4
//     Angle          1    Cylindrical   4    Revolute     5    Slider  5
//
//     unknowns   = 6 x (active components - grounded ones)
//     equations  = the sum over active mates
//     DOF        = unknowns - rank, which for these models is
//                  unknowns - equations because no mate is redundant
//
// The last clause is a claim, not an assumption: every model below asserts
// `redundant` is empty, so if a mate ever became implied by the others the
// arithmetic would stop matching and the test would say so rather than
// quietly agreeing.
//
// NOTHING HERE READS AN EXPECTED VALUE OUT OF BETTERCAD. Positions are
// arithmetic on the dimensions the builders were given; directions are
// cosines of the angles the placements were given, computed here.
namespace {

constexpr std::array<double, 3> kUpZ{0.0, 0.0, 1.0};
constexpr std::array<double, 3> kAlongX{1.0, 0.0, 0.0};

/// What each model is expected to be, derived on paper. Kept beside the
/// catalogue rather than inside it: a builder that carried its own answer
/// would be marking its own homework.
struct Expected {
    AssemblyReferenceModelKind kind;
    std::string_view label;
    std::size_t components;
    std::size_t mates;
    std::size_t unknowns;
    std::size_t equations;
    std::size_t dof;
    SolveStatus status;
};

constexpr std::array kExpected{
    // RM-A  2 components, both grounded. No unknowns at all, so no equations
    //       are needed and none are left over.
    Expected{AssemblyReferenceModelKind::GroundedPair, "RM-A", 2, 2, 0, 0, 0, SolveStatus::FullyConstrained},
    // RM-B  3 components, 1 grounded -> 12 unknowns. Two arms x (parallel 2 +
    //       distance 1 + distance 1 + distance 1 + perpendicular 1) = 12.
    Expected{AssemblyReferenceModelKind::ConstrainedStack, "RM-B", 3, 11, 12, 12, 0,
             SolveStatus::FullyConstrained},
    // RM-C  2 components, 1 grounded -> 6 unknowns. Concentric = 4. The two
    //       left are the slide along the axis and the turn about it.
    Expected{AssemblyReferenceModelKind::ShaftInBore, "RM-C", 2, 2, 6, 4, 2, SolveStatus::UnderConstrained},
    // RM-D  5 components, 1 grounded -> 24 unknowns. Revolute 5 + slider 5 +
    //       cylindrical 4 + planar 3 = 17, so 7 freedoms: 1 + 1 + 2 + 3.
    Expected{AssemblyReferenceModelKind::JointSet, "RM-D", 5, 5, 24, 17, 7, SolveStatus::UnderConstrained},
    // RM-E  4 components, 1 grounded -> 18 unknowns. Three located bodies at
    //       6 equations each = 18.
    Expected{AssemblyReferenceModelKind::ConfiguredFrame, "RM-E", 4, 16, 18, 18, 0,
             SolveStatus::FullyConstrained},
    // RM-F  2 components, 1 grounded -> 6 unknowns. Coincident 3 + distance 1
    //       + distance 1 + perpendicular 1 = 6.
    Expected{AssemblyReferenceModelKind::DrivenCover, "RM-F", 2, 5, 6, 6, 0, SolveStatus::FullyConstrained},
    // RM-G  8 components, 1 grounded -> 42 unknowns. Cover 6 + cylindrical 4
    //       + rear shaft 6 + four feet at 6 = 40. The 2 left are the output
    //       shaft's slide and turn.
    Expected{AssemblyReferenceModelKind::Machine, "RM-G", 8, 31, 42, 40, 2, SolveStatus::UnderConstrained},
    // RM-H  2 components, 1 grounded -> 6 unknowns. Two distance mates = 2,
    //       and they contradict each other, so the solve is INCONSISTENT and
    //       publishes nothing. The DOF it reports is of the system it could
    //       not satisfy.
    Expected{AssemblyReferenceModelKind::FaultCases, "RM-H", 2, 3, 6, 2, 5, SolveStatus::Inconsistent},
};

[[nodiscard]] const Expected& expectedFor(AssemblyReferenceModelKind kind) {
    for (const Expected& e : kExpected) {
        if (e.kind == kind) {
            return e;
        }
    }
    FAIL("no expectation for that model");
    return kExpected[0];
}

} // namespace

// --- The suite as a whole ------------------------------------------------------

TEST_CASE("AssemblyReference_EveryModelBuildsAndMatchesItsDerivedShape", "[reference][assembly][p13]") {
    // The catalogue and the hand-derived table must cover exactly the same
    // models: a model added without an expectation would otherwise be
    // exercised by nothing.
    REQUIRE(reference::kAssemblyReferenceModels.size() == kExpected.size());

    for (const reference::AssemblyReferenceModelInfo& info : reference::kAssemblyReferenceModels) {
        INFO("model " << info.label << " " << info.name);
        const Expected& want = expectedFor(info.kind);
        CHECK(info.label == want.label);

        Assembled built{build(info.kind)};
        // Every model regenerates, including the one that cannot be solved:
        // a contradictory assembly is a solver outcome, not a broken model.
        CHECK(built.regenerated());
        INFO(built.problems());

        CHECK(assembly::components(built.document()).size() == want.components);
        CHECK(assembly::mates(built.document()).size() == want.mates);
        // Nothing is suppressed in any model's committed state.
        CHECK(assembly::activeComponents(built.document()).size() == want.components);
        CHECK(assembly::activeMates(built.document()).size() == want.mates);

        const auto solved = built.solve();
        REQUIRE(solved.has_value());
        INFO(solved->message);
        CHECK(solved->status == want.status);
        CHECK(solved->unknowns == want.unknowns);
        CHECK(solved->equations == want.equations);
        CHECK(solved->degreesOfFreedom == want.dof);
        // The arithmetic above only holds while no mate is implied by the
        // others, so that is asserted rather than assumed.
        CHECK(solved->redundant.empty());

        if (info.solves) {
            CHECK(solved->solved());
            CHECK(solved->conflicting.empty());
            CHECK(solved->maxResidual.si() < 1e-9);
            // A solved assembly publishes a transform for every component,
            // grounded ones included.
            CHECK(built.pass().transforms == want.components);
        } else {
            CHECK_FALSE(solved->solved());
            CHECK_FALSE(solved->conflicting.empty());
            // ADR-005: a failed solve publishes nothing rather than a partial
            // answer somebody might render.
            CHECK(built.pass().transforms == 0);
        }
    }
}

TEST_CASE("AssemblyReference_EveryModelHasItsOwnIdentityAndFiles", "[reference][assembly][p13]") {
    std::set<std::string> names;
    std::set<std::string> stems;
    std::set<std::string> ids;
    for (const reference::AssemblyReferenceModelInfo& info : reference::kAssemblyReferenceModels) {
        INFO("model " << info.label);
        Document document = build(info.kind);
        CHECK(document.name() == info.name);
        CHECK(names.insert(std::string{info.name}).second);
        CHECK(stems.insert(std::string{info.fileStem}).second);
        // A fixed document ID is what makes a saved model byte-reproducible;
        // two models sharing one would make two files claim one identity.
        CHECK(ids.insert(std::format("{}", document.id())).second);
    }
}

// --- RM-A: the baseline ---------------------------------------------------------

TEST_CASE("AssemblyReferenceRmA_HoldsBothPlatesExactlyWhereTheyWerePut", "[reference][assembly][p13]") {
    auto model = reference::buildGroundedPairReferenceModel();
    REQUIRE(model.has_value());
    Assembled built{std::move(model->document)};
    REQUIRE(built.regenerated());

    // Both grounded, so the solved transforms ARE the placements, and the
    // placements are 0 and 40 mm up. Nothing to derive; that is the point of
    // having this model first.
    const RigidTransform3D* base = built.transform(model->base);
    const RigidTransform3D* cover = built.transform(model->cover);
    REQUIRE(base != nullptr);
    REQUIRE(cover != nullptr);
    checkPosition(*base, {0.0, 0.0, 0.0});
    checkPosition(*cover, {0.0, 0.0, reference::GroundedPairModel::kCoverHeightMm});
    checkDirection(axisMm(*cover), kUpZ);
    checkDirection(rollMm(*cover), kAlongX);
}

// --- RM-B: fully constrained, and the geometry is where it should be ------------

TEST_CASE("AssemblyReferenceRmB_PutsBothArmsOnTheDeckAndLeavesNothingFree", "[reference][assembly][p13]") {
    using M = reference::ConstrainedStackModel;
    auto model = reference::buildConstrainedStackReferenceModel();
    REQUIRE(model.has_value());
    Assembled built{std::move(model->document)};
    REQUIRE(built.regenerated());

    // Neither arm was PLACED where its mates put it, so these positions are
    // the solve's work, not the placement's.
    const RigidTransform3D* left = built.transform(model->armLeft);
    const RigidTransform3D* right = built.transform(model->armRight);
    REQUIRE(left != nullptr);
    REQUIRE(right != nullptr);
    checkPosition(*left, {M::kLeftXMm, M::kArmYMm, M::kDeckMm});
    checkPosition(*right, {M::kRightXMm, M::kArmYMm, M::kDeckMm});
    // Both square to the deck: the perpendicular mate removed the turn each
    // was placed with.
    checkDirection(axisMm(*left), kUpZ);
    checkDirection(rollMm(*left), kAlongX);
    checkDirection(axisMm(*right), kUpZ);
    checkDirection(rollMm(*right), kAlongX);

    // Both arms sit ON the deck rather than beside it: the deck is 100 x 80
    // and the arm is 40 x 30, so an arm at (20, 25) spans 20..60 by 25..55.
    CHECK(M::kLeftXMm + 40.0 <= 100.0);
    CHECK(M::kRightXMm + 40.0 <= 100.0);
    CHECK(M::kArmYMm + 30.0 <= 80.0);
}

// --- RM-C: the freedoms a concentric mate is meant to leave ---------------------

TEST_CASE("AssemblyReferenceRmC_RemovesTheOffAxisOffsetAndKeepsTheOtherTwo",
          "[reference][assembly][p13]") {
    using M = reference::ShaftInBoreModel;
    auto model = reference::buildShaftInBoreReferenceModel();
    REQUIRE(model.has_value());
    Assembled built{std::move(model->document)};
    REQUIRE(built.regenerated());

    const RigidTransform3D* shaft = built.transform(model->shaft);
    REQUIRE(shaft != nullptr);
    // On the axis: the 18 mm and -7 mm are gone...
    // ...and the 25 mm along it and the 40 degree turn are untouched. A
    // concentric mate that quietly behaved like a revolute or a fixed mate
    // would fail here on the two values it had no business changing.
    checkPosition(*shaft, {0.0, 0.0, M::kAlongZMm});
    checkDirection(axisMm(*shaft), kUpZ);
    checkDirection(rollMm(*shaft), spin(M::kSpinDeg));
}

// --- RM-D: each mechanical joint keeps its own freedoms -------------------------

TEST_CASE("AssemblyReferenceRmD_EachJointKeepsExactlyItsOwnFreedoms", "[reference][assembly][p13]") {
    using M = reference::JointSetModel;
    auto model = reference::buildJointSetReferenceModel();
    REQUIRE(model.has_value());
    Assembled built{std::move(model->document)};
    REQUIRE(built.regenerated());

    const RigidTransform3D* hinge = built.transform(model->hinge);
    const RigidTransform3D* shoe = built.transform(model->shoe);
    const RigidTransform3D* sleeve = built.transform(model->sleeve);
    const RigidTransform3D* pad = built.transform(model->pad);
    REQUIRE(hinge != nullptr);
    REQUIRE(shoe != nullptr);
    REQUIRE(sleeve != nullptr);
    REQUIRE(pad != nullptr);

    // Every one of the four was placed kStrayMm off its joint, and every one
    // must have removed it. That is the shared half of the assertion.
    SECTION("all four remove the displacement none of them allows") {
        CHECK_THAT(positionMm(*hinge)[0], WithinAbs(0.0, kPlaceMm));
        CHECK_THAT(positionMm(*shoe)[0], WithinAbs(0.0, kPlaceMm));
        CHECK_THAT(positionMm(*sleeve)[0], WithinAbs(0.0, kPlaceMm));
        checkDirection(axisMm(*hinge), kUpZ);
        checkDirection(axisMm(*shoe), kUpZ);
        checkDirection(axisMm(*sleeve), kUpZ);
        checkDirection(axisMm(*pad), kUpZ);
    }

    // And the differing half: which freedom each one kept is what makes it
    // that joint rather than another with the same DOF count.
    SECTION("the hinge keeps its turn and nothing else") {
        checkPosition(*hinge, {0.0, 0.0, 0.0});
        checkDirection(rollMm(*hinge), spin(M::kHingeSpinDeg));
    }
    SECTION("the slider keeps its slide and loses its turn") {
        // The mirror image of the hinge, and the case that proves the two are
        // not one joint under two names.
        checkPosition(*shoe, {0.0, 0.0, M::kShoeAlongMm});
        checkDirection(rollMm(*shoe), kAlongX);
    }
    SECTION("the sleeve keeps both") {
        checkPosition(*sleeve, {0.0, 0.0, M::kSleeveAlongMm});
        checkDirection(rollMm(*sleeve), spin(M::kSleeveSpinDeg));
    }
    SECTION("the face keeps its two slides and its spin, and loses the gap") {
        checkPosition(*pad, {M::kPadAcrossXMm, M::kPadAcrossYMm, 0.0});
        checkDirection(rollMm(*pad), spin(M::kPadSpinDeg));
    }
}

// --- RM-E: configurations, and what each one is meant to change ----------------

TEST_CASE("AssemblyReferenceRmE_EachConfigurationChangesExactlyOneThing", "[reference][assembly][p13]") {
    using M = reference::ConfiguredFrameModel;
    auto model = reference::buildConfiguredFrameReferenceModel();
    REQUIRE(model.has_value());
    const ConfigurationId full = model->full;
    const ConfigurationId noBrace = model->noBrace;
    const ConfigurationId loose = model->loose;
    const ComponentId brace = model->brace;
    Document document = std::move(model->document);

    // Derived by hand, before running any of them:
    //
    //   Full     4 components, 1 grounded -> 18 unknowns; 16 mates, 18
    //            equations (three located bodies at six) -> 0 DOF
    //   NoBrace  the brace goes, and its five mates go inactive with it:
    //            3 components -> 12 unknowns, 11 mates, 12 equations -> 0 DOF
    //   Loose    one mate suppressed: 4 components, 15 mates, 17 equations
    //            -> 1 DOF, and the freedom is the brace sliding along X
    struct Case {
        ConfigurationId configuration;
        std::string_view name;
        std::size_t activeComponents;
        std::size_t activeMates;
        std::size_t unknowns;
        std::size_t equations;
        std::size_t dof;
        SolveStatus status;
    };
    const std::array<Case, 3> cases{{
        {full, "Full", 4, 16, 18, 18, 0, SolveStatus::FullyConstrained},
        {noBrace, "NoBrace", 3, 11, 12, 12, 0, SolveStatus::FullyConstrained},
        {loose, "Loose", 4, 15, 18, 17, 1, SolveStatus::UnderConstrained},
    }};

    for (const Case& probe : cases) {
        INFO("configuration " << probe.name);
        REQUIRE(document.setActiveConfiguration(probe.configuration).has_value());
        Assembled built{document.clone()};
        REQUIRE(built.regenerated());
        CHECK(assembly::activeComponents(built.document()).size() == probe.activeComponents);
        CHECK(assembly::activeMates(built.document()).size() == probe.activeMates);
        const auto solved = built.solve();
        REQUIRE(solved.has_value());
        INFO(solved->message);
        CHECK(solved->status == probe.status);
        CHECK(solved->unknowns == probe.unknowns);
        CHECK(solved->equations == probe.equations);
        CHECK(solved->degreesOfFreedom == probe.dof);
        CHECK(solved->redundant.empty());
        // The brace is only absent in NoBrace, and suppression must reach the
        // component list rather than merely the solve.
        const bool bracePresent = probe.configuration != noBrace;
        CHECK(assembly::isComponentSuppressed(built.document(), brace) == !bracePresent);
        CHECK((built.transform(brace) != nullptr) == bracePresent);
        // The posts do not move between configurations: taking the brace off
        // must not disturb what the brace was not holding.
        const RigidTransform3D* left = built.transform(componentNamed(built.document(), "PostLeft"));
        REQUIRE(left != nullptr);
        checkPosition(*left, {M::kPostLeftXMm, M::kPostYMm, M::kDeckMm});
    }
}

TEST_CASE("AssemblyReferenceRmE_ReturnsToWhereItStartedAfterASwitchingRound", "[reference][assembly][p13]") {
    // A -> B -> C -> A, and A must be bit for bit what it was. A
    // configuration that left a trace would show up as a moved component
    // after a round trip through the other two.
    auto model = reference::buildConfiguredFrameReferenceModel();
    REQUIRE(model.has_value());
    Document document = std::move(model->document);

    const auto snapshot = [&]() {
        Assembled built{document.clone()};
        REQUIRE(built.regenerated());
        std::map<std::string, std::array<double, 3>> places;
        for (const ComponentId id : assembly::activeComponents(built.document())) {
            const assembly::Component* component = assembly::findComponent(built.document(), id);
            const RigidTransform3D* motion = built.transform(id);
            REQUIRE(component != nullptr);
            REQUIRE(motion != nullptr);
            places.emplace(component->name(), positionMm(*motion));
        }
        return places;
    };

    REQUIRE(document.setActiveConfiguration(model->full).has_value());
    const auto first = snapshot();
    REQUIRE(document.setActiveConfiguration(model->noBrace).has_value());
    (void)snapshot();
    REQUIRE(document.setActiveConfiguration(model->loose).has_value());
    (void)snapshot();
    REQUIRE(document.setActiveConfiguration(model->full).has_value());
    const auto back = snapshot();

    REQUIRE(back.size() == first.size());
    for (const auto& [name, place] : first) {
        INFO("component " << name);
        REQUIRE(back.contains(name));
        for (std::size_t i = 0; i < 3; ++i) {
            // Exactly, not nearly: the same intent solved twice is the same
            // arithmetic, and P13-PERSIST-001 already holds transforms to bit
            // equality across a file.
            CHECK(back.at(name)[i] == place[i]);
        }
    }
}

// --- RM-F: the stable reference, driven ----------------------------------------

TEST_CASE("AssemblyReferenceRmF_TheLidFollowsTheBodyWhenTheBodyGrows", "[reference][assembly][p13]") {
    using M = reference::DrivenCoverModel;
    auto model = reference::buildDrivenCoverReferenceModel();
    REQUIRE(model.has_value());
    const ParameterId height = model->bodyHeight;
    const ComponentId lid = model->lid;
    Document document = std::move(model->document);

    // The lid is seated on a face NAMED through the feature that makes it, so
    // its height is not a number in the mate -- it is wherever that face is.
    const auto lidHeight = [&]() {
        Assembled built{document.clone()};
        REQUIRE(built.regenerated());
        const RigidTransform3D* motion = built.transform(lid);
        REQUIRE(motion != nullptr);
        // Squared up and flat, whatever the height.
        checkDirection(axisMm(*motion), kUpZ);
        checkPosition(*motion, {0.0, 0.0, positionMm(*motion)[2]});
        return positionMm(*motion)[2];
    };

    CHECK_THAT(lidHeight(), WithinAbs(M::kBodyHeightMm, kPlaceMm));

    // Raise the body: the lid must rise to the new face, not stay at 30.
    REQUIRE(document.setParameterValue(height, M::kRaisedHeightMm * units::mm).has_value());
    CHECK_THAT(lidHeight(), WithinAbs(M::kRaisedHeightMm, kPlaceMm));

    // And back. A reference that had bound to a position rather than to the
    // feature would have stopped following by now.
    REQUIRE(document.setParameterValue(height, M::kBodyHeightMm * units::mm).has_value());
    CHECK_THAT(lidHeight(), WithinAbs(M::kBodyHeightMm, kPlaceMm));
}

TEST_CASE("AssemblyReferenceRmF_SaysSoWhenTheFaceItNamesIsGone", "[reference][assembly][p13]") {
    // The controlled disappearance. Removing the feature that owns the named
    // face must produce an explicit failure -- never a silent rebinding to
    // whatever plane happens to be nearby, which is the failure ADR-004
    // exists to prevent.
    auto model = reference::buildDrivenCoverReferenceModel();
    REQUIRE(model.has_value());
    Document document = std::move(model->document);
    const ObjectId body = model->bodyPart;

    REQUIRE(document.removeObject(body).has_value());
    Assembled built{std::move(document)};
    CHECK_FALSE(built.regenerated());
    CHECK_FALSE(built.problems().empty());
    // No transform is published for anything, so nothing downstream can
    // render or export a lid sitting on a body that is not there.
    CHECK(built.pass().transforms == 0);
}

// --- RM-G: the production-scale one --------------------------------------------

TEST_CASE("AssemblyReferenceRmG_BuildsTheWholeMachineWhereItBelongs", "[reference][assembly][p13]") {
    using M = reference::MachineModel;
    auto model = reference::buildMachineReferenceModel();
    REQUIRE(model.has_value());
    const std::array<ComponentId, 4> feet = model->feet;
    Assembled built{std::move(model->document)};
    REQUIRE(built.regenerated());

    // The cover sits on the housing's top face, by name.
    const RigidTransform3D* cover = built.transform(model->cover);
    REQUIRE(cover != nullptr);
    checkPosition(*cover, {0.0, 0.0, M::kHousingHeightMm});

    // The rear shaft is located outright; the front one is on the centreline
    // with its slide and turn intact, which is what a cylindrical joint
    // leaves and what an output shaft needs.
    const RigidTransform3D* rear = built.transform(model->shaftRear);
    const RigidTransform3D* front = built.transform(model->shaftFront);
    REQUIRE(rear != nullptr);
    REQUIRE(front != nullptr);
    checkPosition(*rear, {M::kShaftRearXMm, M::kShaftYMm, 0.0});
    CHECK_THAT(positionMm(*front)[0], WithinAbs(0.0, kPlaceMm));
    CHECK_THAT(positionMm(*front)[1], WithinAbs(0.0, kPlaceMm));
    checkDirection(axisMm(*front), kUpZ);

    // Four feet, four different places, all 10 mm below the housing: an
    // assembly that collapsed its repeated instances would put them in one.
    std::set<std::pair<long long, long long>> corners;
    for (const ComponentId foot : feet) {
        const RigidTransform3D* motion = built.transform(foot);
        REQUIRE(motion != nullptr);
        const std::array<double, 3> place = positionMm(*motion);
        CHECK_THAT(place[2], WithinAbs(-10.0, kPlaceMm));
        corners.emplace(std::llround(place[0]), std::llround(place[1]));
    }
    CHECK(corners.size() == M::kFootCount);
}

TEST_CASE("AssemblyReferenceRmG_StripsToABareHousingAndBackAgain", "[reference][assembly][p13]") {
    auto model = reference::buildMachineReferenceModel();
    REQUIRE(model.has_value());
    const ConfigurationId assembled = model->assembled;
    const ConfigurationId bare = model->bare;
    Document document = std::move(model->document);

    // Derived: Bare suppresses the cover and the four feet, so five
    // components go and their mates with them -- 4 for the cover seat and 5
    // per foot. 8 - 5 = 3 components; 31 - 4 - 20 = 7 mates; 2 free bodies
    // -> 12 unknowns; cylindrical 4 + rear shaft 6 = 10 equations -> 2 DOF.
    REQUIRE(document.setActiveConfiguration(bare).has_value());
    {
        Assembled built{document.clone()};
        REQUIRE(built.regenerated());
        CHECK(assembly::activeComponents(built.document()).size() == 3);
        CHECK(assembly::activeMates(built.document()).size() == 7);
        const auto solved = built.solve();
        REQUIRE(solved.has_value());
        CHECK(solved->status == SolveStatus::UnderConstrained);
        CHECK(solved->unknowns == 12);
        CHECK(solved->equations == 10);
        CHECK(solved->degreesOfFreedom == 2);
        CHECK(solved->redundant.empty());
        CHECK(built.transform(model->cover) == nullptr);
    }

    REQUIRE(document.setActiveConfiguration(assembled).has_value());
    {
        Assembled built{document.clone()};
        REQUIRE(built.regenerated());
        CHECK(assembly::activeComponents(built.document()).size() == 8);
        CHECK(built.transform(model->cover) != nullptr);
    }
}

// --- RM-H: the assembly that cannot be solved, and its rescue -------------------

TEST_CASE("AssemblyReferenceRmH_ReportsTheContradictionAndPublishesNothing",
          "[reference][assembly][p13]") {
    using M = reference::FaultCasesModel;
    auto model = reference::buildFaultCasesReferenceModel();
    REQUIRE(model.has_value());
    const MateId near = model->near;
    const MateId far = model->far;
    Document document = std::move(model->document);

    {
        Assembled built{document.clone()};
        // The model itself is sound -- it regenerates. It is the SOLVE that
        // cannot be satisfied, and the two must stay distinguishable.
        CHECK(built.regenerated());
        const auto solved = built.solve();
        REQUIRE(solved.has_value());
        CHECK(solved->status == SolveStatus::Inconsistent);
        CHECK_FALSE(solved->solved());
        // Both distance mates are named as conflicting, and the message says
        // how far apart they are: 90 - 10 split between them is 40 mm.
        CHECK(solved->conflicting.size() == 2);
        CHECK(std::ranges::find(solved->conflicting, near) != solved->conflicting.end());
        CHECK(std::ranges::find(solved->conflicting, far) != solved->conflicting.end());
        CHECK_THAT(solved->maxResidual.si() * 1000.0, WithinAbs((M::kFarMm - M::kNearMm) / 2.0, 1e-6));
        CHECK_THAT(solved->message, ContainsSubstring("cannot all be satisfied"));
        // Nothing derived survives a failed solve.
        CHECK(built.pass().transforms == 0);
        CHECK(built.transform(model->floater) == nullptr);
    }

    // Releasing the second distance makes the same assembly solve, and the
    // floater lands at the distance the surviving mate asks for.
    REQUIRE(document.setActiveConfiguration(model->healthy).has_value());
    {
        Assembled built{document.clone()};
        REQUIRE(built.regenerated());
        CHECK(assembly::activeMates(built.document()).size() == 2);
        const auto solved = built.solve();
        REQUIRE(solved.has_value());
        CHECK(solved->status == SolveStatus::UnderConstrained);
        CHECK(solved->solved());
        CHECK(solved->conflicting.empty());
        CHECK(solved->equations == 1);
        CHECK(solved->degreesOfFreedom == 5);
        const RigidTransform3D* floater = built.transform(model->floater);
        REQUIRE(floater != nullptr);
        CHECK_THAT(positionMm(*floater)[2], WithinAbs(M::kNearMm, kPlaceMm));
    }
}

// --- The coverage matrix --------------------------------------------------------

namespace {

/// What a model actually exercises, read out of the model itself rather than
/// asserted about it. The point of deriving it is that a model which stopped
/// covering something would change this, not just a comment.
struct Coverage {
    bool basicMates = false;
    bool mechanicalMates = false;
    bool faceReferences = false;
    bool repeatedInstances = false;
    bool severalParts = false;
    bool parameterDrivenPlacement = false;
    bool configurations = false;
    bool suppression = false;
    bool underConstrained = false;
    bool fullyConstrained = false;
    bool inconsistent = false;

    friend bool operator==(const Coverage&, const Coverage&) = default;
};

[[nodiscard]] Coverage coverageOf(AssemblyReferenceModelKind kind) {
    Assembled built{build(kind)};
    REQUIRE(built.regenerated());
    const Document& document = built.document();
    Coverage found;

    std::set<ObjectId> parts;
    std::map<ObjectId, std::size_t> uses;
    for (const ComponentId id : assembly::components(document)) {
        const assembly::Component* component = assembly::findComponent(document, id);
        REQUIRE(component != nullptr);
        const ObjectId part = component->definition().part.object;
        parts.insert(part);
        ++uses[part];
        const ComponentPlacement& placement = component->definition().placement;
        const auto bound = [](const auto& slots) {
            return std::ranges::any_of(slots, [](const auto& slot) { return slot.has_value(); });
        };
        if (bound(placement.translationParameters) || bound(placement.rotationParameters)) {
            found.parameterDrivenPlacement = true;
        }
    }
    found.severalParts = parts.size() > 1;
    found.repeatedInstances = std::ranges::any_of(uses, [](const auto& e) { return e.second > 1; });

    for (const MateId id : assembly::mates(document)) {
        const assembly::Mate* mate = assembly::findMate(document, id);
        REQUIRE(mate != nullptr);
        const assembly::MateDefinition& d = mate->definition();
        switch (d.type) {
        case assembly::MateType::Revolute:
        case assembly::MateType::Slider:
        case assembly::MateType::Cylindrical:
        case assembly::MateType::Planar:
            found.mechanicalMates = true;
            break;
        default:
            found.basicMates = true;
            break;
        }
        for (const std::optional<MateTarget>& target : {d.a, d.b, d.a2, d.b2}) {
            if (target && target->kind == MateTargetKind::Face) {
                found.faceReferences = true;
            }
        }
    }

    found.configurations = !document.configurations().empty();
    for (const auto& configuration : document.configurations().all()) {
        Document copy = document.clone();
        REQUIRE(copy.setActiveConfiguration(configuration.id()).has_value());
        if (assembly::activeComponents(copy).size() != assembly::components(copy).size() ||
            assembly::activeMates(copy).size() != assembly::mates(copy).size()) {
            found.suppression = true;
        }
    }

    const auto solved = built.solve();
    REQUIRE(solved.has_value());
    found.underConstrained = solved->status == SolveStatus::UnderConstrained;
    found.fullyConstrained = solved->status == SolveStatus::FullyConstrained;
    found.inconsistent = solved->status == SolveStatus::Inconsistent;
    return found;
}

} // namespace

TEST_CASE("AssemblyReference_TheSuiteCoversEveryP13CapabilityItClaimsTo", "[reference][assembly][p13]") {
    // The matrix, derived from the models and checked against what each one
    // is FOR. A model that quietly stopped exercising a capability -- a mate
    // retyped, a configuration dropped -- changes a row here.
    struct Row {
        AssemblyReferenceModelKind kind;
        std::string_view label;
        Coverage expected;
    };
    // basic, mechanical, face-ref, repeated, several-parts,
    // parameter-driven placement, configs, suppression,
    // under, fully, inconsistent
    const std::array<Row, 8> matrix{{
        {AssemblyReferenceModelKind::GroundedPair, "RM-A",
         {true, false, false, false, true, false, false, false, false, true, false}},
        {AssemblyReferenceModelKind::ConstrainedStack, "RM-B",
         {true, false, false, true, true, false, false, false, false, true, false}},
        {AssemblyReferenceModelKind::ShaftInBore, "RM-C",
         {true, false, false, false, true, true, false, false, true, false, false}},
        {AssemblyReferenceModelKind::JointSet, "RM-D",
         {true, true, false, false, true, false, false, false, true, false, false}},
        {AssemblyReferenceModelKind::ConfiguredFrame, "RM-E",
         {true, false, false, true, true, false, true, true, false, true, false}},
        {AssemblyReferenceModelKind::DrivenCover, "RM-F",
         {true, false, true, false, true, false, false, false, false, true, false}},
        {AssemblyReferenceModelKind::Machine, "RM-G",
         {true, true, true, true, true, false, true, true, true, false, false}},
        {AssemblyReferenceModelKind::FaultCases, "RM-H",
         {true, false, false, true, false, false, true, true, false, false, true}},
    }};

    Coverage suite;
    for (const Row& row : matrix) {
        INFO("model " << row.label);
        const Coverage actual = coverageOf(row.kind);
        CHECK(actual.basicMates == row.expected.basicMates);
        CHECK(actual.mechanicalMates == row.expected.mechanicalMates);
        CHECK(actual.faceReferences == row.expected.faceReferences);
        CHECK(actual.repeatedInstances == row.expected.repeatedInstances);
        CHECK(actual.severalParts == row.expected.severalParts);
        CHECK(actual.parameterDrivenPlacement == row.expected.parameterDrivenPlacement);
        CHECK(actual.configurations == row.expected.configurations);
        CHECK(actual.suppression == row.expected.suppression);
        CHECK(actual.underConstrained == row.expected.underConstrained);
        CHECK(actual.fullyConstrained == row.expected.fullyConstrained);
        CHECK(actual.inconsistent == row.expected.inconsistent);

        suite.basicMates = suite.basicMates || actual.basicMates;
        suite.mechanicalMates = suite.mechanicalMates || actual.mechanicalMates;
        suite.faceReferences = suite.faceReferences || actual.faceReferences;
        suite.repeatedInstances = suite.repeatedInstances || actual.repeatedInstances;
        suite.severalParts = suite.severalParts || actual.severalParts;
        suite.parameterDrivenPlacement = suite.parameterDrivenPlacement || actual.parameterDrivenPlacement;
        suite.configurations = suite.configurations || actual.configurations;
        suite.suppression = suite.suppression || actual.suppression;
        suite.underConstrained = suite.underConstrained || actual.underConstrained;
        suite.fullyConstrained = suite.fullyConstrained || actual.fullyConstrained;
        suite.inconsistent = suite.inconsistent || actual.inconsistent;
    }

    // The gap check: every column has at least one model in it. This is what
    // makes the matrix worth having rather than decorative -- a P13
    // capability with no reference model behind it fails here.
    CHECK(suite == Coverage{true, true, true, true, true, true, true, true, true, true, true});
}

TEST_CASE("AssemblyReference_AllFourMechanicalMatesAppearInTheSuite", "[reference][assembly][p13]") {
    // The matrix records that mechanical mates are covered; this records that
    // ALL FOUR are, which a boolean column cannot say.
    std::set<assembly::MateType> seen;
    for (const reference::AssemblyReferenceModelInfo& info : reference::kAssemblyReferenceModels) {
        Document document = build(info.kind);
        for (const MateId id : assembly::mates(document)) {
            const assembly::Mate* mate = assembly::findMate(document, id);
            REQUIRE(mate != nullptr);
            seen.insert(mate->definition().type);
        }
    }
    CHECK(seen.contains(assembly::MateType::Revolute));
    CHECK(seen.contains(assembly::MateType::Slider));
    CHECK(seen.contains(assembly::MateType::Cylindrical));
    CHECK(seen.contains(assembly::MateType::Planar));
    // And the basic kinds the suite relies on to locate anything.
    CHECK(seen.contains(assembly::MateType::Fixed));
    CHECK(seen.contains(assembly::MateType::Coincident));
    CHECK(seen.contains(assembly::MateType::Concentric));
    CHECK(seen.contains(assembly::MateType::Parallel));
    CHECK(seen.contains(assembly::MateType::Perpendicular));
    CHECK(seen.contains(assembly::MateType::Distance));
}
