#include "features/FeatureTestSupport.hpp"

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Mate.hpp>
#include <bettercad/assembly/Mates.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/MateReference.hpp>
#include <bettercad/features/Datums.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using assembly::MateDefinition;
using assembly::MateType;
using Catch::Matchers::ContainsSubstring;

// P13-MATE-001: the constraint model, implementing ADR-004.
//
// A mate is intent. Nothing here moves a component, and nothing here solves
// anything: that is P13-SOLVE-001. What these tests pin down is what a mate
// may say, what it may point at, and what it refuses.

namespace {

struct Assembly {
    Document document{"Assembly"};
    ObjectId sketch{};
    ObjectId part{};
    ComponentId a{};
    ComponentId b{};
};

ObjectId addPart(Document& document, const std::string& name, ObjectId sketch) {
    auto extrude = features::ExtrudeFeature::create(
        name, {.profile = SketchId::fromValue(sketch.value()), .depth = 10_mm});
    REQUIRE(extrude.has_value());
    return require(document.addObject(std::move(*extrude)));
}

/// One part, placed twice. Two components of one part is the ordinary case a
/// mate relates, and it is also the case that proves a mate names instances
/// rather than geometry alone.
Assembly makeAssembly() {
    Assembly m;
    auto sketch = std::make_unique<sketch::Sketch>("BlockSketch", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
    m.sketch = require(m.document.addObject(std::move(sketch)));
    m.part = addPart(m.document, "Block", m.sketch);
    m.a = require(assembly::createComponent(m.document, "Block1", {.part = m.part}));
    m.b = require(assembly::createComponent(m.document, "Block2", {.part = m.part}));
    return m;
}

/// The model's XY plane, on a component. A principal plane names no object,
/// so it is the simplest legal target there is.
MateTarget plane(ComponentId component) {
    return planeTarget(component, PlaneReference{});
}

/// The model's Z axis, on a component.
MateTarget axis(ComponentId component) {
    return axisTarget(component, AxisReference{});
}

/// A face of @p feature, on a component.
MateTarget face(ComponentId component, ObjectId feature) {
    return faceTarget(component, FaceName{.feature = feature, .face = {.role = FaceRole::EndCap}});
}

MateId add(Document& document, const std::string& name, const MateDefinition& definition) {
    auto id = assembly::createMate(document, name, definition);
    if (!id) {
        FAIL(id.error().message);
    }
    return *id;
}

} // namespace

TEST_CASE("Mate_IdentifiesTheConstraintAndNotTheComponentsItRelates", "[assembly][mate][p13]") {
    Assembly m = makeAssembly();
    const auto before = m.document.objectCount();

    const MateId first = add(m.document, "Face1", {.type = MateType::Coincident,
                                                   .a = plane(m.a), .b = plane(m.b)});
    const MateId second = add(m.document, "Face2", {.type = MateType::Parallel,
                                                    .a = axis(m.a), .b = axis(m.b)});

    CHECK(first.isValid());
    CHECK(first != second);
    CHECK(m.document.objectCount() == before + 2);
    const assembly::Mate* mate = assembly::findMate(m.document, first);
    REQUIRE(mate != nullptr);
    CHECK(mate->typeName() == "mate");
    CHECK(mate->mateId() == first);
    // A mate is a document object like any other, and its ID is its own: it
    // is not either component's.
    CHECK(ObjectId{first} != ObjectId{m.a});
    CHECK(ObjectId{first} != ObjectId{m.b});
    CHECK(assembly::mates(m.document) == std::vector<MateId>{first, second});
}

TEST_CASE("Mate_SupportsExactlyTheSevenBasicKinds", "[assembly][mate][p13]") {
    // Seven kinds, each constructible, each with a distinct name. A kind
    // that cannot be stated exactly is not in the model.
    Assembly m = makeAssembly();
    const std::vector<std::pair<MateType, std::string_view>> kinds{
        {MateType::Fixed, "fixed"},           {MateType::Coincident, "coincident"},
        {MateType::Concentric, "concentric"}, {MateType::Parallel, "parallel"},
        {MateType::Perpendicular, "perpendicular"}, {MateType::Distance, "distance"},
        {MateType::Angle, "angle"},
    };
    CHECK(kinds.size() == 7);

    std::set<std::string_view> names;
    for (const auto& [type, name] : kinds) {
        CHECK(assembly::toString(type) == name);
        names.insert(name);

        MateDefinition definition{.type = type};
        if (type == MateType::Fixed) {
            definition.component = m.a;
        } else if (type == MateType::Concentric) {
            definition.a = axis(m.a);
            definition.b = axis(m.b);
        } else {
            definition.a = plane(m.a);
            definition.b = plane(m.b);
        }
        if (type == MateType::Distance) {
            definition.distance = 25_mm;
        }
        if (type == MateType::Angle) {
            definition.angle = 90_deg;
        }
        INFO("kind: " << name);
        CHECK(validate(definition).has_value());
    }
    CHECK(names.size() == 7);
}

TEST_CASE("Mate_FixedHoldsOneComponentAndNamesNoGeometry", "[assembly][mate][p13]") {
    // Fixed is the datum the rest solve against, which is why a component
    // needs no separate grounded flag.
    Assembly m = makeAssembly();
    const MateId id = add(m.document, "Ground", {.type = MateType::Fixed, .component = m.a});

    const assembly::Mate* mate = assembly::findMate(m.document, id);
    REQUIRE(mate != nullptr);
    CHECK(mate->definition().component == m.a);
    CHECK_FALSE(mate->definition().a.has_value());
    CHECK(mate->dependencies() == std::vector<ObjectId>{ObjectId{m.a}});

    SECTION("it must name a component") {
        auto bad = validate(MateDefinition{.type = MateType::Fixed});
        REQUIRE_FALSE(bad.has_value());
        CHECK_THAT(bad.error().message, ContainsSubstring("must name the component"));
    }
    SECTION("and it may not also name geometry") {
        auto bad = validate(MateDefinition{.type = MateType::Fixed, .component = m.a, .a = plane(m.a)});
        REQUIRE_FALSE(bad.has_value());
        CHECK_THAT(bad.error().message, ContainsSubstring("names no geometry"));
    }
    SECTION("a component it does not have") {
        auto bad = assembly::createMate(m.document, "Ghost",
                                        {.type = MateType::Fixed, .component = ComponentId::fromValue(9999)});
        REQUIRE_FALSE(bad.has_value());
        CHECK(bad.error().code == ErrorCode::NotFound);
    }
}

TEST_CASE("Mate_RelatesTwoPlanesOrTwoAxesAndRefusesAMixture", "[assembly][mate][p13]") {
    // "A plane parallel to an axis" and "a plane whose normal is parallel to
    // an axis" are opposite statements, and nothing in the model says which
    // was meant, so the mixture is refused rather than guessed at.
    Assembly m = makeAssembly();

    for (const MateType type : {MateType::Coincident, MateType::Parallel, MateType::Perpendicular}) {
        INFO("kind: " << assembly::toString(type));
        CHECK(validate(MateDefinition{.type = type, .a = plane(m.a), .b = plane(m.b)}).has_value());
        CHECK(validate(MateDefinition{.type = type, .a = axis(m.a), .b = axis(m.b)}).has_value());
        CHECK(validate(MateDefinition{.type = type, .a = face(m.a, m.part), .b = plane(m.b)}).has_value());

        auto mixed = validate(MateDefinition{.type = type, .a = plane(m.a), .b = axis(m.b)});
        REQUIRE_FALSE(mixed.has_value());
        CHECK_THAT(mixed.error().message, ContainsSubstring("two planes or two axes"));
    }
}

TEST_CASE("Mate_ConcentricRelatesTwoAxesAndNothingElse", "[assembly][mate][p13]") {
    // ADR-004: a hole's bore is not nameable, so concentricity is expressed
    // against the datum axis published for it. That makes axis-to-axis the
    // only form, and saying so is better than accepting a face and failing
    // later.
    Assembly m = makeAssembly();
    CHECK(validate(MateDefinition{.type = MateType::Concentric, .a = axis(m.a), .b = axis(m.b)}).has_value());

    for (const MateTarget& other : {plane(m.b), face(m.b, m.part)}) {
        auto bad = validate(MateDefinition{.type = MateType::Concentric, .a = axis(m.a), .b = other});
        REQUIRE_FALSE(bad.has_value());
        CHECK_THAT(bad.error().message, ContainsSubstring("two axes"));
    }
}

TEST_CASE("Mate_DistanceTakesALengthAndSaysWhatItsSignMeans", "[assembly][mate][p13]") {
    Assembly m = makeAssembly();

    SECTION("a length between planes, signed along the first normal") {
        CHECK(validate(MateDefinition{.type = MateType::Distance, .a = plane(m.a), .b = plane(m.b), .distance = 25_mm})
                  .has_value());
        // Negative is meaningful between planes: the other side.
        CHECK(validate(MateDefinition{.type = MateType::Distance, .a = plane(m.a), .b = plane(m.b), .distance = -25_mm})
                  .has_value());
    }
    SECTION("between axes it is a separation, which has no side") {
        CHECK(validate(MateDefinition{.type = MateType::Distance, .a = axis(m.a), .b = axis(m.b), .distance = 25_mm})
                  .has_value());
        auto negative =
            validate(MateDefinition{.type = MateType::Distance, .a = axis(m.a), .b = axis(m.b), .distance = -25_mm});
        REQUIRE_FALSE(negative.has_value());
        CHECK_THAT(negative.error().message, ContainsSubstring("cannot be negative"));
    }
    SECTION("it must have one") {
        auto bad = validate(MateDefinition{.type = MateType::Distance, .a = plane(m.a), .b = plane(m.b)});
        REQUIRE_FALSE(bad.has_value());
        CHECK_THAT(bad.error().message, ContainsSubstring("must have a distance"));
    }
    SECTION("and it must be finite") {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        for (const double value : {nan, inf, -inf}) {
            auto bad = validate(MateDefinition{.type = MateType::Distance, .a = plane(m.a), .b = plane(m.b),
                                 .distance = Length::fromSi(value)});
            REQUIRE_FALSE(bad.has_value());
            CHECK_THAT(bad.error().message, ContainsSubstring("finite"));
        }
    }
}

TEST_CASE("Mate_AngleTakesAnAngleInRangeAndRefusesToNormaliseIt", "[assembly][mate][p13]") {
    Assembly m = makeAssembly();
    const auto angled = [&](Angle value) {
        return validate(MateDefinition{.type = MateType::Angle, .a = plane(m.a), .b = plane(m.b), .angle = value});
    };

    CHECK(angled(0_deg).has_value());
    CHECK(angled(90_deg).has_value());
    CHECK(angled(180_deg).has_value());

    SECTION("outside 0 to 180 degrees it is refused, not wrapped") {
        // Silently turning 190 degrees into 170 would be the model deciding
        // what the engineer meant.
        for (const Angle value : {190_deg, 360_deg, -1_deg}) {
            auto bad = angled(value);
            REQUIRE_FALSE(bad.has_value());
            CHECK_THAT(bad.error().message, ContainsSubstring("0 to 180 degrees"));
        }
    }
    SECTION("it must be finite") {
        auto bad = angled(Angle::fromSi(std::numeric_limits<double>::quiet_NaN()));
        REQUIRE_FALSE(bad.has_value());
        CHECK_THAT(bad.error().message, ContainsSubstring("finite"));
    }
    SECTION("it must have one") {
        auto bad = validate(MateDefinition{.type = MateType::Angle, .a = plane(m.a), .b = plane(m.b)});
        REQUIRE_FALSE(bad.has_value());
        CHECK_THAT(bad.error().message, ContainsSubstring("must have an angle"));
    }
}

TEST_CASE("Mate_RefusesAValueOnAKindThatTakesNone", "[assembly][mate][p13]") {
    // Units are part of correctness, and so is their absence: a parallel
    // mate with a distance is a mate whose author meant something else.
    Assembly m = makeAssembly();
    for (const MateType type : {MateType::Coincident, MateType::Parallel, MateType::Perpendicular}) {
        INFO("kind: " << assembly::toString(type));
        auto withDistance = validate(MateDefinition{.type = type, .a = plane(m.a), .b = plane(m.b), .distance = 5_mm});
        REQUIRE_FALSE(withDistance.has_value());
        CHECK_THAT(withDistance.error().message, ContainsSubstring("takes no distance"));

        auto withAngle = validate(MateDefinition{.type = type, .a = plane(m.a), .b = plane(m.b), .angle = 30_deg});
        REQUIRE_FALSE(withAngle.has_value());
        CHECK_THAT(withAngle.error().message, ContainsSubstring("takes no angle"));
    }
    // And the two that do take one take only their own.
    CHECK_FALSE(validate(MateDefinition{.type = MateType::Distance, .a = plane(m.a), .b = plane(m.b),
                          .distance = 5_mm, .angle = 30_deg})
                    .has_value());
    CHECK_FALSE(validate(MateDefinition{.type = MateType::Angle, .a = plane(m.a), .b = plane(m.b),
                          .distance = 5_mm, .angle = 30_deg})
                    .has_value());
}

TEST_CASE("Mate_RefusesAMalformedTarget", "[assembly][mate][p13]") {
    Assembly m = makeAssembly();

    SECTION("a target with no component") {
        MateTarget target = plane(m.a);
        target.component = ComponentId{};
        auto bad = validate(target);
        REQUIRE_FALSE(bad.has_value());
        CHECK_THAT(bad.error().message, ContainsSubstring("must name the component"));
    }
    SECTION("a target naming two pieces of geometry") {
        MateTarget target = plane(m.a);
        target.axis = AxisReference{};
        auto bad = validate(target);
        REQUIRE_FALSE(bad.has_value());
        CHECK_THAT(bad.error().message, ContainsSubstring("exactly one piece of geometry"));
    }
    SECTION("a target whose kind does not match the geometry it carries") {
        MateTarget target = plane(m.a);
        target.kind = MateTargetKind::Axis;
        auto bad = validate(target);
        REQUIRE_FALSE(bad.has_value());
        CHECK_THAT(bad.error().message, ContainsSubstring("must name an axis"));
    }
    SECTION("a face target with no feature") {
        MateTarget target = faceTarget(m.a, FaceName{});
        auto bad = validate(target);
        REQUIRE_FALSE(bad.has_value());
        CHECK_THAT(bad.error().message, ContainsSubstring("feature that generates it"));
    }
}

TEST_CASE("Mate_RelatesTwoDifferentComponentsAndNotGeometryToItself", "[assembly][mate][p13]") {
    Assembly m = makeAssembly();

    SECTION("the same target twice") {
        auto bad = validate(MateDefinition{.type = MateType::Coincident, .a = plane(m.a), .b = plane(m.a)});
        REQUIRE_FALSE(bad.has_value());
        CHECK_THAT(bad.error().message, ContainsSubstring("to itself"));
    }
    SECTION("two pieces of geometry on one component") {
        // A component is rigid, so this says nothing and can only
        // over-constrain a solve.
        auto bad = validate(MateDefinition{.type = MateType::Coincident, .a = plane(m.a), .b = face(m.a, m.part)});
        REQUIRE_FALSE(bad.has_value());
        CHECK_THAT(bad.error().message, ContainsSubstring("two different components"));
    }
}

TEST_CASE("Mate_TargetsMustBeComponentsOfThisDocument", "[assembly][mate][p13]") {
    Assembly m = makeAssembly();

    SECTION("a component that does not exist") {
        auto bad = assembly::createMate(m.document, "Ghost",
                                        {.type = MateType::Coincident,
                                         .a = plane(ComponentId::fromValue(9999)), .b = plane(m.b)});
        REQUIRE_FALSE(bad.has_value());
        CHECK(bad.error().code == ErrorCode::NotFound);
        CHECK_THAT(bad.error().message, ContainsSubstring("not a component of this document"));
    }
    SECTION("an object that is not a component") {
        auto bad = assembly::createMate(m.document, "OnAPart",
                                        {.type = MateType::Coincident,
                                         .a = plane(ComponentId::fromValue(m.part.value())), .b = plane(m.b)});
        REQUIRE_FALSE(bad.has_value());
        CHECK(bad.error().code == ErrorCode::NotFound);
    }
}

TEST_CASE("Mate_AFaceTargetMustNameTheComponentsOwnPart", "[assembly][mate][p13]") {
    // A face is named by the feature that generated it, so a face of another
    // part's feature is a modelling mistake, not a resolution failure. ADR-004
    // requires the two to stay distinct, and this is the first of them.
    Assembly m = makeAssembly();
    const ObjectId otherPart = addPart(m.document, "Other", m.sketch);
    const ComponentId onOther = require(assembly::createComponent(m.document, "Other1", {.part = otherPart}));

    // A face of its own part is fine.
    CHECK(assembly::createMate(m.document, "Good",
                               {.type = MateType::Coincident, .a = face(m.a, m.part), .b = plane(onOther)})
              .has_value());

    // A face of the other component's part, used on this one, is not.
    auto bad = assembly::createMate(m.document, "Wrong",
                                    {.type = MateType::Coincident,
                                     .a = face(m.a, otherPart), .b = plane(onOther)});
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().code == ErrorCode::InvalidArgument);
    CHECK_THAT(bad.error().message, ContainsSubstring("is not part of"));
}

TEST_CASE("Mate_DependsOnItsComponentsAndTheGeometryItNames", "[assembly][mate][p13]") {
    // ADR-004: a mate's dependencies include the objects its references name,
    // which is what makes it rebuild when the datum it uses moves.
    Assembly m = makeAssembly();
    const MateId id = add(m.document, "Face",
                          {.type = MateType::Coincident, .a = face(m.a, m.part), .b = plane(m.b)});

    const assembly::Mate* mate = assembly::findMate(m.document, id);
    REQUIRE(mate != nullptr);
    const std::vector<ObjectId> dependencies = mate->dependencies();
    CHECK(std::ranges::find(dependencies, ObjectId{m.a}) != dependencies.end());
    CHECK(std::ranges::find(dependencies, ObjectId{m.b}) != dependencies.end());
    CHECK(std::ranges::find(dependencies, m.part) != dependencies.end());

    const DocumentGraph graph = buildDependencyGraph(m.document);
    CHECK(graph.missing.empty());
    CHECK(graph.graph.dependentsOf(ObjectId{m.a}).contains(ObjectId{id}));
    CHECK(graph.graph.dependentsOf(m.part).contains(ObjectId{id}));

    SECTION("a fixed mate depends only on the component it holds") {
        const MateId fixed = add(m.document, "Ground", {.type = MateType::Fixed, .component = m.a});
        CHECK(assembly::findMate(m.document, fixed)->dependencies() == std::vector<ObjectId>{ObjectId{m.a}});
    }
}

TEST_CASE("Mate_WhoseTargetIsDeletedBecomesUnresolvedAndIsNeverRebound",
          "[assembly][mate][p13][regeneration]") {
    Assembly m = makeAssembly();
    const MateId id = add(m.document, "Face", {.type = MateType::Coincident,
                                               .a = plane(m.a), .b = plane(m.b)});
    CHECK(assembly::unresolvedMates(m.document).empty());
    features::Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.document).succeeded());

    REQUIRE(assembly::removeComponent(m.document, m.b).has_value());

    const auto unresolved = assembly::unresolvedMates(m.document);
    REQUIRE(unresolved.size() == 1);
    CHECK(unresolved.front().mate == id);
    CHECK(unresolved.front().missing == std::vector<ObjectId>{ObjectId{m.b}});

    // The graph reports the same absence, so the mate is blocked rather than
    // looking satisfied.
    const DocumentGraph graph = buildDependencyGraph(m.document);
    CHECK_FALSE(graph.missing.empty());
    CHECK_FALSE(requireReport(regenerator, m.document).succeeded());

    // And the mate still names what it always named: adding another
    // component does not capture it.
    const ComponentId replacement = require(assembly::createComponent(m.document, "Block3", {.part = m.part}));
    CHECK(replacement != m.b);
    CHECK(assembly::findMate(m.document, id)->definition().b->component == m.b);
    CHECK(assembly::unresolvedMates(m.document).size() == 1);
}

TEST_CASE("Mate_FailedCreationChangesNothing", "[assembly][mate][p13]") {
    Assembly m = makeAssembly();
    const auto objectsBefore = m.document.objectCount();
    const auto idBefore = m.document.lastAllocatedId();
    const auto revisionBefore = m.document.revision();

    // Malformed, unknown component, wrong kinds, out-of-range value.
    CHECK_FALSE(assembly::createMate(m.document, "A", {.type = MateType::Concentric,
                                                       .a = plane(m.a), .b = plane(m.b)}).has_value());
    CHECK_FALSE(assembly::createMate(m.document, "B", {.type = MateType::Angle, .a = plane(m.a),
                                                       .b = plane(m.b), .angle = 270_deg}).has_value());
    CHECK_FALSE(assembly::createMate(m.document, "C", {.type = MateType::Coincident,
                                                       .a = plane(ComponentId::fromValue(9999)),
                                                       .b = plane(m.b)}).has_value());

    CHECK(m.document.objectCount() == objectsBefore);
    CHECK(m.document.lastAllocatedId() == idBefore);
    CHECK(m.document.revision() == revisionBefore);

    const MateId good = add(m.document, "Good", {.type = MateType::Coincident,
                                                 .a = plane(m.a), .b = plane(m.b)});
    CHECK(good.value() == idBefore + 1);
}

TEST_CASE("Mate_EditingIsCheckedLikeCreation", "[assembly][mate][p13]") {
    Assembly m = makeAssembly();
    const MateDefinition original{.type = MateType::Coincident, .a = plane(m.a), .b = plane(m.b)};
    const MateId id = add(m.document, "Face", original);
    const auto revision = m.document.revision();

    SECTION("a valid edit goes through") {
        auto changed = assembly::setMateDefinition(
            m.document, id, {.type = MateType::Distance, .a = plane(m.a), .b = plane(m.b), .distance = 12_mm});
        REQUIRE(changed.has_value());
        CHECK(*changed);
        CHECK(assembly::findMate(m.document, id)->definition().distance == 12_mm);
    }
    SECTION("an invalid one is refused and changes nothing") {
        auto refused = assembly::setMateDefinition(
            m.document, id, {.type = MateType::Concentric, .a = plane(m.a), .b = plane(m.b)});
        REQUIRE_FALSE(refused.has_value());
        CHECK(assembly::findMate(m.document, id)->definition() == original);
        CHECK(m.document.revision() == revision);
    }
    SECTION("setting the same definition changes nothing") {
        auto changed = assembly::setMateDefinition(m.document, id, original);
        REQUIRE(changed.has_value());
        CHECK_FALSE(*changed);
        CHECK(m.document.revision() == revision);
    }
    SECTION("a mate that does not exist") {
        auto missing = assembly::setMateDefinition(m.document, MateId::fromValue(9999), original);
        REQUIRE_FALSE(missing.has_value());
        CHECK(missing.error().code == ErrorCode::NotFound);
    }
}

TEST_CASE("Mate_IsRemovedWithoutTouchingWhatItRelated", "[assembly][mate][p13]") {
    Assembly m = makeAssembly();
    const MateId id = add(m.document, "Face", {.type = MateType::Coincident,
                                               .a = plane(m.a), .b = plane(m.b)});
    CHECK(assembly::matesOf(m.document, m.a) == std::vector<MateId>{id});

    REQUIRE(assembly::removeMate(m.document, id).has_value());

    CHECK(assembly::findMate(m.document, id) == nullptr);
    CHECK(assembly::mates(m.document).empty());
    CHECK(assembly::matesOf(m.document, m.a).empty());
    // The components are untouched.
    CHECK(assembly::findComponent(m.document, m.a) != nullptr);
    CHECK(assembly::findComponent(m.document, m.b) != nullptr);

    auto again = assembly::removeMate(m.document, id);
    REQUIRE_FALSE(again.has_value());
    CHECK(again.error().code == ErrorCode::NotFound);
}

TEST_CASE("Mate_IsDeterministic", "[assembly][mate][p13][determinism]") {
    const auto build = [] {
        Assembly m = makeAssembly();
        std::vector<MateId> ids;
        ids.push_back(require(assembly::createMate(m.document, "Ground",
                                                   {.type = MateType::Fixed, .component = m.a})));
        ids.push_back(require(assembly::createMate(
            m.document, "Face", {.type = MateType::Distance, .a = plane(m.a), .b = plane(m.b),
                                 .distance = 25_mm})));
        return std::pair{std::move(m), ids};
    };

    auto [first, firstIds] = build();
    auto [second, secondIds] = build();
    CHECK(firstIds == secondIds);
    CHECK(assembly::mates(first.document) == assembly::mates(second.document));
    for (const MateId id : firstIds) {
        CHECK(assembly::findMate(first.document, id)->definition() ==
              assembly::findMate(second.document, id)->definition());
        // Dependency order is the order the mate names things, not hash order.
        CHECK(assembly::findMate(first.document, id)->dependencies() ==
              assembly::findMate(second.document, id)->dependencies());
    }
}

TEST_CASE("Mate_DependsOnTheDatumItUsesAndFollowsItWhenItMoves", "[assembly][mate][p13][regeneration]") {
    // ADR-004 states this as the reason a mate's dependencies include the
    // objects its references name. Every other test here targets a principal
    // plane, which names no object at all, so this is the case that actually
    // measures it.
    Assembly m = makeAssembly();
    auto datum = features::DatumPlane::create("MatingFace",
                                              {.kind = features::DatumPlaneKind::Offset,
                                               .base = PlaneReference{.plane = PrincipalPlane::XY},
                                               .offset = 5_mm});
    REQUIRE(datum.has_value());
    const ObjectId datumId = require(m.document.addObject(std::move(*datum)));

    const MateId id = add(m.document, "ToDatum",
                          {.type = MateType::Coincident,
                           .a = planeTarget(m.a, PlaneReference{.object = datumId}),
                           .b = plane(m.b)});

    // The edge exists, which is what makes the mate rebuild with the datum.
    const assembly::Mate* mate = assembly::findMate(m.document, id);
    REQUIRE(mate != nullptr);
    const std::vector<ObjectId> dependencies = mate->dependencies();
    CHECK(std::ranges::find(dependencies, datumId) != dependencies.end());

    const DocumentGraph graph = buildDependencyGraph(m.document);
    CHECK(graph.missing.empty());
    CHECK(graph.graph.dependentsOf(datumId).contains(ObjectId{id}));

    SECTION("moving the datum dirties the mate rather than leaving it stale") {
        const auto before = m.document.revision();
        REQUIRE(m.document
                    .modifyObject<features::DatumPlane>(
                        datumId,
                        [](features::DatumPlane& plane) {
                            auto definition = plane.definition();
                            definition.offset = 20_mm;
                            return plane.setDefinition(definition).value_or(false);
                        })
                    .has_value());
        CHECK(m.document.revision() != before);
        // The mate still names the same datum: it followed it, it was not
        // rewritten.
        CHECK(assembly::findMate(m.document, id)->definition().a->plane->object == datumId);
    }
    SECTION("deleting the datum leaves the mate unresolved, never rebound") {
        REQUIRE(m.document.removeObject(datumId).has_value());

        const auto unresolved = assembly::unresolvedMates(m.document);
        REQUIRE(unresolved.size() == 1);
        CHECK(unresolved.front().mate == id);
        CHECK(unresolved.front().missing == std::vector<ObjectId>{datumId});
        CHECK_FALSE(buildDependencyGraph(m.document).missing.empty());
        // Still naming the datum that is gone, not some other plane.
        CHECK(assembly::findMate(m.document, id)->definition().a->plane->object == datumId);
    }
}
