#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Configurations.hpp>
#include <bettercad/assembly/Resolution.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/drawing/Annotations.hpp>
#include <bettercad/drawing/Bom.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/drawing/Views.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using drawing::AnnotationDefinition;
using drawing::AnnotationTarget;
using drawing::AnnotationType;
using drawing::BillOfMaterials;
using drawing::BomRow;
using drawing::BomTableStyle;
using drawing::DrawingScale;
using drawing::SceneItems;
using drawing::SceneText;
using drawing::SheetDefinition;
using drawing::StandardView;
using drawing::ViewDefinition;
using drawing::ViewSubject;

// P14-BOM-001: the bill of materials, and the balloons that point into it.
//
// WHERE THE EXPECTED NUMBERS COME FROM. Every fixture is hand-countable and
// written out: three of part A, two of part B, one of part C is three rows of
// quantities 3, 2 and 1, and that is the whole of the expected value. Nothing
// is read back from the code under test.
//
// WHAT IS BEING GUARDED. A BOM is DERIVED (ADR-022): no quantity, row or item
// number is stored anywhere, so the failure to hunt for is not "the table
// looks wrong" but "the table looks right and disagrees with the assembly".
// Several tests below therefore change the model and re-ask, rather than
// checking a number once.
namespace {

constexpr double kMm = 1e-9;

struct Assembly {
    Document document{"Machine"};
    SheetId sheet{};
    ViewId view{};
    features::Regenerator regenerator;

    drawing::BodyLookup bodies() {
        const features::Regenerator* r = &regenerator;
        return [r](ObjectId object) { return r->body(object); };
    }
    drawing::TransformLookup transforms() {
        const features::Regenerator* r = &regenerator;
        return [r](ComponentId c) { return r->transform(c); };
    }
    void regenerate() {
        assembly::registerHandlers(regenerator, nullptr, nullptr);
        REQUIRE(regenerator.regenerateAll(document).has_value());
    }
};

/// A distinct part definition: a @p size cube, extruded from its own sketch.
/// Two calls with the same size give two DIFFERENT parts that are
/// geometrically identical, which is exactly the case grouping must not
/// collapse.
ObjectId addPart(Assembly& a, const std::string& name, Length size = 20_mm) {
    auto sketch = std::make_unique<sketch::Sketch>(name + "Sketch", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, size, size);
    const ObjectId sketchId = require(a.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        name, {.profile = SketchId::fromValue(sketchId.value()), .depth = size});
    REQUIRE(extrude.has_value());
    return require(a.document.addObject(std::move(*extrude)));
}

Assembly makeAssembly(DrawingScale scale = DrawingScale{1, 1}) {
    Assembly a;
    a.sheet = require(drawing::createSheet(
        a.document, "Sheet1",
        SheetDefinition{.format = drawing::SheetFormat::A3,
                        .orientation = drawing::SheetOrientation::Landscape,
                        .margins = drawing::SheetMargins{10_mm, 10_mm, 10_mm, 10_mm},
                        .scale = scale}));
    a.view = require(drawing::createView(
        a.document, "MainView",
        ViewDefinition{.sheet = a.sheet,
                       .subject = ViewSubject::Assembly,
                       .orientation = StandardView::Front,
                       .placement = Point2D{200_mm, 150_mm}}));
    return a;
}

/// One occurrence of @p part, spaced along x so nothing overlaps.
ComponentId place(Assembly& a, const std::string& name, ObjectId part, Length x) {
    return require(assembly::createComponent(
        a.document, name,
        {.part = part, .placement = ComponentPlacement{.translation = {x, 0_mm, 0_mm}}}));
}

BillOfMaterials bomOf(Assembly& a) {
    auto bom = drawing::billOfMaterials(a.document, a.view);
    REQUIRE(bom.has_value());
    return std::move(*bom);
}

/// The row a part has, by its identity.
const BomRow* rowFor(const BillOfMaterials& bom, ObjectId part) {
    const auto found = std::ranges::find_if(
        bom.rows, [&](const BomRow& row) { return row.part.object == part; });
    return found == bom.rows.end() ? nullptr : &*found;
}

AnnotationId balloon(Assembly& a, const std::string& name, ComponentId occurrence,
                     Point2D placement = {60_mm, 60_mm}) {
    auto made = drawing::createAnnotation(
        a.document, name,
        AnnotationDefinition{.view = a.view,
                             .type = AnnotationType::Balloon,
                             .target = AnnotationTarget{.object = ObjectId{occurrence}},
                             .placement = placement});
    const std::string why = made.has_value() ? std::string{} : made.error().message;
    INFO(why);
    REQUIRE(made.has_value());
    return *made;
}

AnnotationId bomTable(Assembly& a, const std::string& name,
                      Point2D placement = {250_mm, 250_mm},
                      BomTableStyle style = BomTableStyle{}) {
    auto made = drawing::createAnnotation(a.document, name,
                                          AnnotationDefinition{.view = a.view,
                                                               .type = AnnotationType::BomTable,
                                                               .placement = placement,
                                                               .table = style});
    const std::string why = made.has_value() ? std::string{} : made.error().message;
    INFO(why);
    REQUIRE(made.has_value());
    return *made;
}

SceneItems drawn(Assembly& a, AnnotationId id) {
    auto items = drawing::draw(a.document, id, a.bodies(), a.transforms());
    const std::string why = items.has_value() ? std::string{} : items.error().message;
    INFO(why);
    REQUIRE(items.has_value());
    return std::move(*items);
}

/// The number a balloon shows.
std::string balloonText(Assembly& a, AnnotationId id) {
    const SceneItems items = drawn(a, id);
    REQUIRE(items.texts.size() == 1);
    return items.texts.front().text;
}

} // namespace

// --- Fixture A: three rows, quantities 3, 2, 1 ---------------------------------------------

TEST_CASE("Bom_CountsEveryActiveOccurrenceOfEveryPart", "[drawing][bom][p14]") {
    // Part A x3, Part B x2, Part C x1. Three rows; quantities 3, 2, 1.
    Assembly a = makeAssembly();
    const ObjectId partA = addPart(a, "PartA");
    const ObjectId partB = addPart(a, "PartB");
    const ObjectId partC = addPart(a, "PartC");
    (void)place(a, "A1", partA, 0_mm);
    (void)place(a, "A2", partA, 40_mm);
    (void)place(a, "A3", partA, 80_mm);
    (void)place(a, "B1", partB, 120_mm);
    (void)place(a, "B2", partB, 160_mm);
    (void)place(a, "C1", partC, 200_mm);
    a.regenerate();

    const BillOfMaterials bom = bomOf(a);
    REQUIRE(bom.rows.size() == 3);
    CHECK(rowFor(bom, partA)->quantity() == 3);
    CHECK(rowFor(bom, partB)->quantity() == 2);
    CHECK(rowFor(bom, partC)->quantity() == 1);

    // Nothing lost and nothing counted twice: the rows between them hold
    // exactly the occurrences the view draws.
    CHECK(bom.totalOccurrences() == 6);
    CHECK(drawing::drawnOccurrences(a.document, a.view)->size() == 6);

    // Every row names its part, and the name is the part object's.
    CHECK(rowFor(bom, partA)->name == "PartA");
    CHECK(rowFor(bom, partC)->name == "PartC");
}

TEST_CASE("Bom_QuantityFollowsTheNumberOfInstances", "[drawing][bom][p14]") {
    // 1, then 2, then 10 -- asserted at each step rather than once at the end,
    // so a quantity that came from the number of PARTS rather than the number
    // of OCCURRENCES would show up immediately.
    Assembly a = makeAssembly();
    const ObjectId part = addPart(a, "Bolt");
    for (int i = 1; i <= 10; ++i) {
        (void)place(a, std::format("Bolt{}", i), part, Length::fromSi(0.04 * i));
        a.regenerate();
        const BillOfMaterials bom = bomOf(a);
        INFO(i);
        REQUIRE(bom.rows.size() == 1); // one part, however many instances
        CHECK(bom.rows.front().quantity() == static_cast<std::size_t>(i));
        CHECK(bom.rows.front().occurrences.size() == static_cast<std::size_t>(i));
    }
}

// --- Fixture C: identical geometry, distinct definitions ------------------------------------

TEST_CASE("Bom_TwoSeparatelyDefinedPartsAreTwoRowsEvenWhenIdentical",
          "[drawing][bom][p14]") {
    // The test that catches grouping by geometry, by name or by a shape hash.
    // Both parts are 20 mm cubes built the same way; only their identities
    // differ, and identity is what grouping uses.
    Assembly a = makeAssembly();
    const ObjectId first = addPart(a, "Spacer", 20_mm);
    const ObjectId second = addPart(a, "Packer", 20_mm);
    (void)place(a, "S1", first, 0_mm);
    (void)place(a, "P1", second, 40_mm);
    a.regenerate();

    const BillOfMaterials bom = bomOf(a);
    CHECK(bom.rows.size() == 2);
    REQUIRE(rowFor(bom, first) != nullptr);
    REQUIRE(rowFor(bom, second) != nullptr);
    CHECK(rowFor(bom, first)->quantity() == 1);
    CHECK(rowFor(bom, second)->quantity() == 1);
    CHECK(rowFor(bom, first)->item != rowFor(bom, second)->item);

    // And their bodies really are the same shape, so the test is about
    // identity and not about the parts being different.
    const geometry::Body* one = a.regenerator.body(first);
    const geometry::Body* two = a.regenerator.body(second);
    REQUIRE(one != nullptr);
    REQUIRE(two != nullptr);
    auto oneBox = one->boundingBox();
    auto twoBox = two->boundingBox();
    REQUIRE(oneBox.has_value());
    REQUIRE(twoBox.has_value());
    CHECK_THAT(oneBox->sizeX().in(units::mm), WithinAbs(twoBox->sizeX().in(units::mm), kMm));
    CHECK_THAT(oneBox->sizeZ().in(units::mm), WithinAbs(twoBox->sizeZ().in(units::mm), kMm));
}

TEST_CASE("Bom_TwoPartsWithTheSameNameAreStillTwoRows", "[drawing][bom][p14]") {
    // Names are not identity. Two document objects cannot share a name here,
    // so the near case is a part RENAMED to what another was called -- and
    // the rows must not merge, because nothing about the assembly changed.
    Assembly a = makeAssembly();
    const ObjectId first = addPart(a, "Bracket");
    const ObjectId second = addPart(a, "Support");
    (void)place(a, "B1", first, 0_mm);
    (void)place(a, "S1", second, 40_mm);
    a.regenerate();
    REQUIRE(bomOf(a).rows.size() == 2);

    // Rename one; the count is unchanged and the printed name follows.
    REQUIRE(a.document.rename(second, "Bracket2").has_value());
    const BillOfMaterials bom = bomOf(a);
    CHECK(bom.rows.size() == 2);
    CHECK(rowFor(bom, second)->name == "Bracket2"); // derived, not copied
}

// --- Occurrence provenance -------------------------------------------------------------------

TEST_CASE("Bom_ARowKeepsTheOccurrencesItGrouped", "[drawing][bom][p14]") {
    // A quantity-only row would be anonymous count data, and a balloon could
    // not get back from it to the instance it labels.
    Assembly a = makeAssembly();
    const ObjectId part = addPart(a, "Bolt");
    const ComponentId one = place(a, "C1", part, 0_mm);
    const ComponentId two = place(a, "C2", part, 40_mm);
    const ComponentId three = place(a, "C3", part, 80_mm);
    a.regenerate();

    const BillOfMaterials bom = bomOf(a);
    REQUIRE(bom.rows.size() == 1);
    const BomRow& row = bom.rows.front();
    CHECK(row.quantity() == 3);
    CHECK(row.occurrences == std::vector<ComponentId>{one, two, three});
    // Ascending ComponentId, so two runs give one order.
    CHECK(std::ranges::is_sorted(row.occurrences,
                                 [](ComponentId l, ComponentId r) { return l.value() < r.value(); }));
}

// --- Item numbering --------------------------------------------------------------------------

TEST_CASE("Bom_ItemNumbersAreUniqueAndContiguousFromOne", "[drawing][bom][p14]") {
    Assembly a = makeAssembly();
    const ObjectId first = addPart(a, "One");
    const ObjectId second = addPart(a, "Two");
    const ObjectId third = addPart(a, "Three");
    (void)place(a, "A", first, 0_mm);
    (void)place(a, "B", second, 40_mm);
    (void)place(a, "C", third, 80_mm);
    a.regenerate();

    const BillOfMaterials bom = bomOf(a);
    REQUIRE(bom.rows.size() == 3);
    std::vector<int> items;
    for (const BomRow& row : bom.rows) {
        items.push_back(row.item);
    }
    CHECK(items == std::vector<int>{1, 2, 3});
    // In ascending part identity, which is the stated ordering contract.
    CHECK(bom.rows[0].part.object.value() < bom.rows[1].part.object.value());
    CHECK(bom.rows[1].part.object.value() < bom.rows[2].part.object.value());
}

TEST_CASE("Bom_NumberingDoesNotDependOnTheOrderComponentsWereCreatedIn",
          "[drawing][bom][p14][determinism]") {
    // Two documents with the same parts and the same instances, built in
    // OPPOSITE orders. Same rows, same quantities, same item numbers -- which
    // is only true because the order is the part's identity and not anything
    // about traversal.
    const auto build = [](bool reversed) {
        Assembly a = makeAssembly();
        const ObjectId first = addPart(a, "Alpha");
        const ObjectId second = addPart(a, "Beta");
        if (reversed) {
            (void)place(a, "B1", second, 0_mm);
            (void)place(a, "A2", first, 40_mm);
            (void)place(a, "B2", second, 80_mm);
            (void)place(a, "A1", first, 120_mm);
        } else {
            (void)place(a, "A1", first, 0_mm);
            (void)place(a, "A2", first, 40_mm);
            (void)place(a, "B1", second, 80_mm);
            (void)place(a, "B2", second, 120_mm);
        }
        a.regenerate();
        auto bom = drawing::billOfMaterials(a.document, a.view);
        REQUIRE(bom.has_value());
        std::vector<std::pair<int, std::size_t>> summary;
        for (const BomRow& row : bom->rows) {
            summary.emplace_back(row.item, row.quantity());
        }
        return summary;
    };
    CHECK(build(false) == build(true));
    CHECK(build(false) == std::vector<std::pair<int, std::size_t>>{{1, 2}, {2, 2}});
}

TEST_CASE("Bom_NumbersAreCompactSoARemovedRowClosesTheGap", "[drawing][bom][p14]") {
    // The renumber policy, asserted rather than left accidental (ADR-022):
    // nothing is stored, so a number cannot be retained, and the BOM is a
    // function of the assembly as it is now.
    Assembly a = makeAssembly();
    const ObjectId first = addPart(a, "One");
    const ObjectId second = addPart(a, "Two");
    const ObjectId third = addPart(a, "Three");
    (void)place(a, "A", first, 0_mm);
    const ComponentId middle = place(a, "B", second, 40_mm);
    (void)place(a, "C", third, 80_mm);
    a.regenerate();
    REQUIRE(rowFor(bomOf(a), third)->item == 3);

    REQUIRE(assembly::removeComponent(a.document, middle).has_value());
    a.regenerate();

    const BillOfMaterials bom = bomOf(a);
    REQUIRE(bom.rows.size() == 2);
    CHECK(rowFor(bom, second) == nullptr);   // its last occurrence went
    CHECK(rowFor(bom, first)->item == 1);
    CHECK(rowFor(bom, third)->item == 2);    // compact: 3 became 2
}

// --- Suppression and configuration -------------------------------------------------------------

TEST_CASE("Bom_ASuppressedOccurrenceDecrementsTheQuantityExactly",
          "[drawing][bom][p14]") {
    // Four, then suppress one: three. Not "about three" and not a row with a
    // stale four in it.
    Assembly a = makeAssembly();
    const ObjectId part = addPart(a, "Bolt");
    (void)place(a, "C1", part, 0_mm);
    (void)place(a, "C2", part, 40_mm);
    (void)place(a, "C3", part, 80_mm);
    const ComponentId fourth = place(a, "C4", part, 120_mm);
    const ConfigurationId reduced = require(a.document.createConfiguration("Reduced"));
    REQUIRE(assembly::suppressComponent(a.document, reduced, fourth, true).has_value());
    a.regenerate();
    REQUIRE(bomOf(a).rows.front().quantity() == 4);

    REQUIRE(a.document.setActiveConfiguration(reduced).has_value());
    a.regenerate();
    const BillOfMaterials bom = bomOf(a);
    REQUIRE(bom.rows.size() == 1);
    CHECK(bom.rows.front().quantity() == 3);
    // And the suppressed one is not merely uncounted -- it is not listed.
    CHECK(std::ranges::find(bom.rows.front().occurrences, fourth) ==
          bom.rows.front().occurrences.end());
}

TEST_CASE("Bom_SuppressingTheLastOccurrenceRemovesTheRow", "[drawing][bom][p14]") {
    // A row with quantity zero would be a line on a drawing telling somebody
    // to buy none of something.
    Assembly a = makeAssembly();
    const ObjectId kept = addPart(a, "Kept");
    const ObjectId going = addPart(a, "Going");
    (void)place(a, "K1", kept, 0_mm);
    const ComponentId only = place(a, "G1", going, 40_mm);
    const ConfigurationId reduced = require(a.document.createConfiguration("Reduced"));
    REQUIRE(assembly::suppressComponent(a.document, reduced, only, true).has_value());
    REQUIRE(a.document.setActiveConfiguration(reduced).has_value());
    a.regenerate();

    const BillOfMaterials bom = bomOf(a);
    CHECK(bom.rows.size() == 1);
    CHECK(rowFor(bom, going) == nullptr);
    CHECK(rowFor(bom, kept)->item == 1);
    for (const BomRow& row : bom.rows) {
        CHECK(row.quantity() >= 1); // no zero-quantity row can exist
    }
}

TEST_CASE("Bom_FollowsTheActiveConfigurationAndComesBack", "[drawing][bom][p14]") {
    // Config A: X x2, Y x1.  Config B: X x1, Y suppressed, Z x3.
    Assembly a = makeAssembly();
    const ObjectId x = addPart(a, "PartX");
    const ObjectId y = addPart(a, "PartY");
    const ObjectId z = addPart(a, "PartZ");
    (void)place(a, "X1", x, 0_mm);
    const ComponentId x2 = place(a, "X2", x, 40_mm);
    const ComponentId y1 = place(a, "Y1", y, 80_mm);
    const ComponentId z1 = place(a, "Z1", z, 120_mm);
    const ComponentId z2 = place(a, "Z2", z, 160_mm);
    const ComponentId z3 = place(a, "Z3", z, 200_mm);

    const ConfigurationId configA = require(a.document.createConfiguration("A"));
    const ConfigurationId configB = require(a.document.createConfiguration("B"));
    for (const ComponentId absent : {z1, z2, z3}) {
        REQUIRE(assembly::suppressComponent(a.document, configA, absent, true).has_value());
    }
    for (const ComponentId absent : {x2, y1}) {
        REQUIRE(assembly::suppressComponent(a.document, configB, absent, true).has_value());
    }

    REQUIRE(a.document.setActiveConfiguration(configA).has_value());
    a.regenerate();
    const BillOfMaterials inA = bomOf(a);
    CHECK(inA.rows.size() == 2);
    CHECK(rowFor(inA, x)->quantity() == 2);
    CHECK(rowFor(inA, y)->quantity() == 1);
    CHECK(rowFor(inA, z) == nullptr);

    REQUIRE(a.document.setActiveConfiguration(configB).has_value());
    a.regenerate();
    const BillOfMaterials inB = bomOf(a);
    CHECK(inB.rows.size() == 2);
    CHECK(rowFor(inB, x)->quantity() == 1);
    CHECK(rowFor(inB, y) == nullptr);
    CHECK(rowFor(inB, z)->quantity() == 3);
    // Z is now item 2, where in A there was no Z at all.
    CHECK(rowFor(inB, z)->item == 2);

    REQUIRE(a.document.setActiveConfiguration(configA).has_value());
    a.regenerate();
    CHECK(bomOf(a) == inA); // the whole bill, restored
}

// --- Balloons --------------------------------------------------------------------------------

TEST_CASE("Balloon_ShowsTheItemNumberOfTheOccurrenceItPointsAt",
          "[drawing][bom][p14][balloon]") {
    Assembly a = makeAssembly();
    const ObjectId first = addPart(a, "One");
    const ObjectId second = addPart(a, "Two");
    const ComponentId a1 = place(a, "A1", first, 0_mm);
    const ComponentId b1 = place(a, "B1", second, 40_mm);
    a.regenerate();

    const BillOfMaterials bom = bomOf(a);
    const AnnotationId onFirst = balloon(a, "BalloonA", a1, {60_mm, 60_mm});
    const AnnotationId onSecond = balloon(a, "BalloonB", b1, {90_mm, 60_mm});

    CHECK(balloonText(a, onFirst) == std::to_string(rowFor(bom, first)->item));
    CHECK(balloonText(a, onSecond) == std::to_string(rowFor(bom, second)->item));
    CHECK(balloonText(a, onFirst) != balloonText(a, onSecond));
}

TEST_CASE("Balloon_EveryOccurrenceOfOnePartShowsTheSameNumber",
          "[drawing][bom][p14][balloon]") {
    // Fixture D. Four occurrences of one part group into one row, so all four
    // balloons show that row's number -- while each still targets its own
    // instance.
    Assembly a = makeAssembly();
    const ObjectId part = addPart(a, "Bolt");
    const ComponentId c1 = place(a, "C1", part, 0_mm);
    const ComponentId c2 = place(a, "C2", part, 40_mm);
    const ComponentId c3 = place(a, "C3", part, 80_mm);
    const ComponentId c4 = place(a, "C4", part, 120_mm);
    a.regenerate();
    REQUIRE(bomOf(a).rows.front().quantity() == 4);

    const AnnotationId first = balloon(a, "B1", c1, {40_mm, 60_mm});
    const AnnotationId last = balloon(a, "B4", c4, {160_mm, 60_mm});
    CHECK(balloonText(a, first) == "1");
    CHECK(balloonText(a, last) == "1");

    // Same number, DIFFERENT targets -- the balloons are not the same object
    // and do not point at the same place.
    const AnnotationDefinition& one = drawing::findAnnotation(a.document, first)->definition();
    const AnnotationDefinition& four = drawing::findAnnotation(a.document, last)->definition();
    CHECK(one.target.object == ObjectId{c1});
    CHECK(four.target.object == ObjectId{c4});
    CHECK_FALSE(one.target.object == four.target.object);

    // And their leaders end in different places, because they label different
    // instances.
    const SceneItems firstItems = drawn(a, first);
    const SceneItems lastItems = drawn(a, last);
    CHECK_FALSE(firstItems.lines.back().points.front() == lastItems.lines.back().points.front());
    (void)c2;
    (void)c3;
}

TEST_CASE("Balloon_FollowsTheNumberWhenTheAssemblyChanges",
          "[drawing][bom][p14][balloon]") {
    // The number is a function of the occurrence, so it cannot go stale: add
    // a part whose identity sorts before this one and the item number moves,
    // with no edit to the balloon.
    Assembly a = makeAssembly();
    const ObjectId later = addPart(a, "Later");
    const ComponentId target = place(a, "L1", later, 0_mm);
    a.regenerate();
    const AnnotationId label = balloon(a, "Balloon", target);
    CHECK(balloonText(a, label) == "1");

    // A part created AFTER this one has a higher ObjectId, so it sorts after
    // and the first row keeps item 1.
    const ObjectId newer = addPart(a, "Newer");
    (void)place(a, "N1", newer, 40_mm);
    a.regenerate();
    CHECK(balloonText(a, label) == "1");
    CHECK(rowFor(bomOf(a), newer)->item == 2);

    // Removing the row that sorts first moves this one up.
    const BillOfMaterials before = bomOf(a);
    REQUIRE(before.rows.front().part.object == later);
    REQUIRE(assembly::removeComponent(a.document, target).has_value());
    a.regenerate();
    CHECK(rowFor(bomOf(a), newer)->item == 1); // 2 became 1
}

TEST_CASE("Balloon_OnASuppressedOccurrenceIsUnresolvedAndNeverRebinds",
          "[drawing][bom][p14][balloon]") {
    // The hard one. Two identical occurrences of one part; balloon the
    // second; suppress it. A balloon that fell back to "an occurrence of the
    // same part" would carry on showing a number as if nothing happened, and
    // would be labelling a component that is not there.
    Assembly a = makeAssembly();
    const ObjectId part = addPart(a, "Bolt");
    (void)place(a, "C1", part, 0_mm);
    const ComponentId second = place(a, "C2", part, 40_mm);
    const ConfigurationId reduced = require(a.document.createConfiguration("Reduced"));
    REQUIRE(assembly::suppressComponent(a.document, reduced, second, true).has_value());
    a.regenerate();

    const AnnotationId label = balloon(a, "Balloon", second);
    CHECK(balloonText(a, label) == "1");

    REQUIRE(a.document.setActiveConfiguration(reduced).has_value());
    a.regenerate();

    // The row still exists -- the other occurrence is still there -- so a
    // rebinding balloon would happily show "1".
    REQUIRE(bomOf(a).rows.size() == 1);
    REQUIRE(bomOf(a).rows.front().quantity() == 1);

    const auto drawnNow = drawing::draw(a.document, label, a.bodies(), a.transforms());
    REQUIRE_FALSE(drawnNow.has_value());
    CHECK_THAT(drawnNow.error().message, ContainsSubstring("Balloon"));

    // Bring it back and the same balloon resolves again, unchanged.
    REQUIRE(a.document.setActiveConfiguration(std::nullopt).has_value());
    a.regenerate();
    CHECK(balloonText(a, label) == "1");
}

TEST_CASE("Balloon_MustNameAnOccurrence", "[drawing][bom][p14][balloon]") {
    Assembly a = makeAssembly();
    const ObjectId part = addPart(a, "Bolt");
    (void)place(a, "C1", part, 0_mm);
    a.regenerate();

    // A plane is not an instance and has no item number.
    const auto onPlane = drawing::createAnnotation(
        a.document, "Bad",
        AnnotationDefinition{.view = a.view,
                             .type = AnnotationType::Balloon,
                             .target = AnnotationTarget{.plane = PlaneReference{}},
                             .placement = {60_mm, 60_mm}});
    REQUIRE_FALSE(onPlane.has_value());
    CHECK_THAT(onPlane.error().message, ContainsSubstring("must name one"));

    // An object that is not a component cannot be ballooned either.
    const auto onPart = drawing::createAnnotation(
        a.document, "Bad",
        AnnotationDefinition{.view = a.view,
                             .type = AnnotationType::Balloon,
                             .target = AnnotationTarget{.object = part},
                             .placement = {60_mm, 60_mm}});
    REQUIRE(onPart.has_value()); // it names an object, so it is well formed...
    const auto drawnBad = drawing::draw(a.document, *onPart, a.bodies(), a.transforms());
    REQUIRE_FALSE(drawnBad.has_value()); // ...but the part is not an occurrence
    CHECK_THAT(drawnBad.error().message, ContainsSubstring("not a hole or a component"));
}

TEST_CASE("Balloon_IsPaperSizedAtEveryScale", "[drawing][bom][p14][balloon]") {
    // P14-ANNO-001's invariant: what it points at moves with the scale, the
    // balloon does not.
    double firstDiameter = 0.0;
    Point2D firstTarget{};
    for (const DrawingScale scale :
         {DrawingScale{1, 1}, DrawingScale{1, 2}, DrawingScale{2, 1}, DrawingScale{1, 5}}) {
        INFO("scale " << scale.label());
        Assembly a = makeAssembly(scale);
        const ObjectId part = addPart(a, "Bolt");
        const ComponentId only = place(a, "C1", part, 0_mm);
        (void)place(a, "C2", part, 200_mm);
        a.regenerate();
        const SceneItems items = drawn(a, balloon(a, "Balloon", only));

        double minX = 1e9, maxX = -1e9;
        for (const Point2D& p : items.lines.front().points) {
            minX = std::min(minX, p.x.in(units::mm));
            maxX = std::max(maxX, p.x.in(units::mm));
        }
        CHECK_THAT(items.texts.front().height.in(units::mm), WithinAbs(3.5, kMm));
        const Point2D target = items.lines.back().points.front();
        if (firstDiameter == 0.0) {
            firstDiameter = maxX - minX;
            firstTarget = target;
        } else {
            CHECK_THAT(maxX - minX, WithinAbs(firstDiameter, kMm)); // same size
            CHECK(target != firstTarget);                           // different place
        }
    }
}

// --- The table -------------------------------------------------------------------------------

TEST_CASE("BomTable_DrawsAHeaderAndOneRowPerPart", "[drawing][bom][p14][table]") {
    Assembly a = makeAssembly();
    const ObjectId first = addPart(a, "Bracket");
    const ObjectId second = addPart(a, "Bolt");
    (void)place(a, "A1", first, 0_mm);
    (void)place(a, "B1", second, 40_mm);
    (void)place(a, "B2", second, 80_mm);
    a.regenerate();

    const SceneItems items = drawn(a, bomTable(a, "Bom"));

    // Three cells a line, and three lines: the header and two rows.
    REQUIRE(items.texts.size() == 9);
    CHECK(items.texts[0].text == "ITEM");
    CHECK(items.texts[1].text == "PART");
    CHECK(items.texts[2].text == "QTY");
    CHECK(items.texts[3].text == "1");
    CHECK(items.texts[4].text == "Bracket");
    CHECK(items.texts[5].text == "1");
    CHECK(items.texts[6].text == "2");
    CHECK(items.texts[7].text == "Bolt");
    CHECK(items.texts[8].text == "2");

    // The boundary, two rules between the three lines, and two between the
    // three columns.
    CHECK(items.lines.size() == 1 + 2 + 2);
    CHECK(validate(items).has_value());
}

TEST_CASE("BomTable_IsPaperSizedAndDoesNotFollowTheViewScale",
          "[drawing][bom][p14][table]") {
    // A BOM table is a sheet annotation. A 1:2 view does not halve it, for
    // the same reason it does not halve a note.
    double firstWidth = 0.0;
    double firstHeight = 0.0;
    for (const DrawingScale scale : {DrawingScale{1, 1}, DrawingScale{1, 2}, DrawingScale{5, 1}}) {
        INFO("scale " << scale.label());
        Assembly a = makeAssembly(scale);
        const ObjectId part = addPart(a, "Bolt");
        (void)place(a, "C1", part, 0_mm);
        a.regenerate();
        const SceneItems items = drawn(a, bomTable(a, "Bom"));

        double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
        for (const Point2D& p : items.lines.front().points) {
            minX = std::min(minX, p.x.in(units::mm));
            maxX = std::max(maxX, p.x.in(units::mm));
            minY = std::min(minY, p.y.in(units::mm));
            maxY = std::max(maxY, p.y.in(units::mm));
        }
        // Default style: 15 + 60 + 15 = 90 mm across, two rows of 8 mm.
        CHECK_THAT(maxX - minX, WithinAbs(90.0, kMm));
        CHECK_THAT(maxY - minY, WithinAbs(16.0, kMm));
        if (firstWidth == 0.0) {
            firstWidth = maxX - minX;
            firstHeight = maxY - minY;
        } else {
            CHECK_THAT(maxX - minX, WithinAbs(firstWidth, kMm));
            CHECK_THAT(maxY - minY, WithinAbs(firstHeight, kMm));
        }
    }
}

TEST_CASE("BomTable_GrowsDownwardFromWhereItWasPut", "[drawing][bom][p14][table]") {
    // The placement is the top-left corner, so adding a part lengthens the
    // table away from where the engineer put it rather than moving it.
    Assembly a = makeAssembly();
    const ObjectId first = addPart(a, "One");
    (void)place(a, "A1", first, 0_mm);
    a.regenerate();
    const AnnotationId table = bomTable(a, "Bom", {250_mm, 250_mm});

    const auto topLeftOf = [&]() {
        const SceneItems items = drawn(a, table);
        return items.lines.front().points.front();
    };
    const Point2D before = topLeftOf();
    CHECK_THAT(before.x.in(units::mm), WithinAbs(250.0, kMm));
    CHECK_THAT(before.y.in(units::mm), WithinAbs(250.0, kMm));

    const ObjectId second = addPart(a, "Two");
    (void)place(a, "B1", second, 40_mm);
    a.regenerate();
    CHECK(topLeftOf() == before); // it did not move

    // And it got one row longer: three lines now instead of two.
    const SceneItems items = drawn(a, table);
    CHECK(items.texts.size() == 9); // header + two rows, three cells each
}

TEST_CASE("BomTable_WithoutAHeaderDrawsOnlyRows", "[drawing][bom][p14][table]") {
    Assembly a = makeAssembly();
    const ObjectId part = addPart(a, "Bolt");
    (void)place(a, "C1", part, 0_mm);
    a.regenerate();
    const SceneItems items =
        drawn(a, bomTable(a, "Bom", {250_mm, 250_mm}, BomTableStyle{.header = false}));
    REQUIRE(items.texts.size() == 3);
    CHECK(items.texts[0].text == "1");
    CHECK(items.texts[1].text == "Bolt");
    CHECK(items.texts[2].text == "1");
}

TEST_CASE("BomTable_OfAnEmptyAssemblyIsRefused", "[drawing][bom][p14][table]") {
    Assembly a = makeAssembly();
    a.regenerate();
    const AnnotationId table = bomTable(a, "Bom");
    const auto items = drawing::draw(a.document, table, a.bodies(), a.transforms());
    REQUIRE_FALSE(items.has_value());
    CHECK_THAT(items.error().message, ContainsSubstring("no active components"));
}

TEST_CASE("Bom_OfAViewThatDrawsOneObjectIsRefused", "[drawing][bom][p14]") {
    Assembly a = makeAssembly();
    const ObjectId part = addPart(a, "Bolt");
    const ComponentId only = place(a, "C1", part, 0_mm);
    a.regenerate();

    const ViewId objectView = require(drawing::createView(
        a.document, "JustOne",
        ViewDefinition{.sheet = a.sheet,
                       .source = ObjectReference{ObjectId{only}},
                       .orientation = StandardView::Front,
                       .placement = Point2D{100_mm, 100_mm}}));
    const auto bom = drawing::billOfMaterials(a.document, objectView);
    REQUIRE_FALSE(bom.has_value());
    CHECK_THAT(bom.error().message, ContainsSubstring("draws one object"));
}

// --- Persistence ------------------------------------------------------------------------------

TEST_CASE("Bom_RoundTripsAsIntentAndRecomputesEverythingElse",
          "[drawing][bom][p14][persistence]") {
    Assembly a = makeAssembly();
    const ObjectId first = addPart(a, "Bracket");
    const ObjectId second = addPart(a, "Bolt");
    (void)place(a, "A1", first, 0_mm);
    const ComponentId b1 = place(a, "B1", second, 40_mm);
    (void)place(a, "B2", second, 80_mm);
    a.regenerate();

    const AnnotationId table = bomTable(a, "Bom", {250_mm, 250_mm},
                                        BomTableStyle{.rowHeight = 9_mm, .header = false});
    const AnnotationId label = balloon(a, "Balloon", b1);
    const BillOfMaterials before = bomOf(a);
    const SceneItems tableBefore = drawn(a, table);

    const TempDir directory;
    const std::filesystem::path path = directory.path() / "bom.bcad";
    REQUIRE(io::saveDocument(a.document, path).has_value());

    // What is stored is where the table sits and how it looks, and which
    // occurrence the balloon points at. NOT a row, a quantity or a number.
    const std::string text = readFile(path);
    CHECK_THAT(text, ContainsSubstring(R"("type": "bom_table")"));
    CHECK_THAT(text, ContainsSubstring(R"("type": "balloon")"));
    CHECK_THAT(text, !ContainsSubstring("\"quantity\":"));
    CHECK_THAT(text, !ContainsSubstring("\"item\":"));
    CHECK_THAT(text, !ContainsSubstring("\"rows\":"));

    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    CHECK(drawing::findAnnotation(*loaded, table)->definition() ==
          drawing::findAnnotation(a.document, table)->definition());
    CHECK(drawing::findAnnotation(*loaded, label)->definition() ==
          drawing::findAnnotation(a.document, label)->definition());

    features::Regenerator again;
    assembly::registerHandlers(again, nullptr, nullptr);
    REQUIRE(again.regenerateAll(*loaded).has_value());
    const features::Regenerator* r = &again;
    const drawing::BodyLookup loadedBodies = [r](ObjectId object) { return r->body(object); };
    const drawing::TransformLookup loadedTransforms = [r](ComponentId c) {
        return r->transform(c);
    };

    auto reloadedBom = drawing::billOfMaterials(*loaded, a.view);
    REQUIRE(reloadedBom.has_value());
    CHECK(*reloadedBom == before);

    auto reloadedTable = drawing::draw(*loaded, table, loadedBodies, loadedTransforms);
    REQUIRE(reloadedTable.has_value());
    CHECK(reloadedTable->texts == tableBefore.texts);
    CHECK(reloadedTable->lines == tableBefore.lines);
}

TEST_CASE("Bom_ASavedQuantityCannotOverrideTheAssembly",
          "[drawing][bom][p14][persistence]") {
    // The regression the whole design exists for: save while the quantity is
    // 4, change the model to 3, reload, and require 3. There is nowhere in
    // the file for the 4 to have been kept, and this proves it.
    Assembly a = makeAssembly();
    const ObjectId part = addPart(a, "Bolt");
    (void)place(a, "C1", part, 0_mm);
    (void)place(a, "C2", part, 40_mm);
    (void)place(a, "C3", part, 80_mm);
    const ComponentId fourth = place(a, "C4", part, 120_mm);
    a.regenerate();
    (void)bomTable(a, "Bom");
    REQUIRE(bomOf(a).rows.front().quantity() == 4);

    const TempDir directory;
    const std::filesystem::path path = directory.path() / "four.bcad";
    REQUIRE(io::saveDocument(a.document, path).has_value());

    // Change the model AFTER saving, then load that older file and remove the
    // component in the loaded document -- the file never carried a count, so
    // the BOM follows whatever the assembly says now.
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    REQUIRE(assembly::removeComponent(*loaded, fourth).has_value());
    features::Regenerator again;
    assembly::registerHandlers(again, nullptr, nullptr);
    REQUIRE(again.regenerateAll(*loaded).has_value());

    auto after = drawing::billOfMaterials(*loaded, a.view);
    REQUIRE(after.has_value());
    REQUIRE(after->rows.size() == 1);
    CHECK(after->rows.front().quantity() == 3);
}

TEST_CASE("Bom_MalformedFilesAreRefused", "[drawing][bom][p14][persistence]") {
    Assembly a = makeAssembly();
    const ObjectId part = addPart(a, "Bolt");
    const ComponentId only = place(a, "C1", part, 0_mm);
    a.regenerate();
    (void)bomTable(a, "Bom");
    (void)balloon(a, "Balloon", only);

    const TempDir directory;
    const std::filesystem::path good = directory.path() / "good.bcad";
    REQUIRE(io::saveDocument(a.document, good).has_value());
    const std::string original = readFile(good);

    const auto refuse = [&](const std::string& from, const std::string& to,
                            std::string_view expected) {
        INFO(from << " -> " << to);
        std::string text = original;
        const auto at = text.find(from);
        REQUIRE(at != std::string::npos);
        text.replace(at, from.size(), to);
        const std::filesystem::path path = directory.path() / "bad.bcad";
        writeFile(path, text);
        const auto loaded = io::loadDocument(path);
        REQUIRE_FALSE(loaded.has_value());
        CHECK_THAT(loaded.error().message, ContainsSubstring(std::string{expected}));
    };

    refuse(R"("type": "bom_table")", R"("type": "parts_list")", "unknown annotation type");
    refuse(R"("row_height": 0.008)", R"("row_height": 0.0)", "greater than zero");
    refuse(R"("row_height": 0.008)", R"("row_height": -0.008)", "greater than zero");
    refuse(R"("part_width": 0.06)", R"("part_width": "wide")", "expected a number");

    // A balloon whose occurrence is not there at all. The file LOADS, and
    // that is the established contract rather than an oversight: every
    // drawing object in this codebase is read definition by definition,
    // because the object a reference names may not have been read yet, so a
    // reference is resolved when it is USED and not when it is parsed. What
    // must not happen is a number appearing anyway -- the balloon is
    // UNRESOLVED, and says so.
    {
        std::string text = original;
        const std::string key = std::format("\"object\": {}", ObjectId{only}.value());
        const auto at = text.find(key);
        REQUIRE(at != std::string::npos);
        text.replace(at, key.size(), R"("object": 99999)");
        const std::filesystem::path path = directory.path() / "ghost.bcad";
        writeFile(path, text);
        auto loaded = io::loadDocument(path);
        REQUIRE(loaded.has_value());

        features::Regenerator again;
        assembly::registerHandlers(again, nullptr, nullptr);
        REQUIRE(again.regenerateAll(*loaded).has_value());
        const features::Regenerator* r = &again;
        const drawing::BodyLookup ghostBodies = [r](ObjectId object) { return r->body(object); };
        const drawing::TransformLookup ghostTransforms = [r](ComponentId c) {
            return r->transform(c);
        };
        bool sawUnresolvedBalloon = false;
        for (const AnnotationId id : drawing::annotations(*loaded)) {
            const drawing::Annotation* annotation = drawing::findAnnotation(*loaded, id);
            if (annotation->definition().type != AnnotationType::Balloon) {
                continue;
            }
            const auto items = drawing::draw(*loaded, id, ghostBodies, ghostTransforms);
            REQUIRE_FALSE(items.has_value());
            CHECK_THAT(items.error().message, ContainsSubstring("does not exist"));
            sawUnresolvedBalloon = true;
        }
        CHECK(sawUnresolvedBalloon);
    }
}

// --- Determinism ------------------------------------------------------------------------------

TEST_CASE("Bom_ComputesIdenticallyEveryTime", "[drawing][bom][p14][determinism]") {
    Assembly a = makeAssembly();
    const ObjectId first = addPart(a, "Alpha");
    const ObjectId second = addPart(a, "Beta");
    const ObjectId third = addPart(a, "Gamma");
    (void)place(a, "A1", first, 0_mm);
    (void)place(a, "A2", first, 40_mm);
    const ComponentId b1 = place(a, "B1", second, 80_mm);
    (void)place(a, "C1", third, 120_mm);
    a.regenerate();

    const AnnotationId table = bomTable(a, "Bom");
    const AnnotationId label = balloon(a, "Balloon", b1);
    const BillOfMaterials firstBom = bomOf(a);
    const SceneItems firstTable = drawn(a, table);
    const std::string firstNumber = balloonText(a, label);

    for (int i = 0; i < 6; ++i) {
        INFO(i);
        CHECK(bomOf(a) == firstBom);
        const SceneItems again = drawn(a, table);
        CHECK(again.texts == firstTable.texts);
        CHECK(again.lines == firstTable.lines);
        CHECK(balloonText(a, label) == firstNumber);
    }
}


// --- Failure paths ---------------------------------------------------------------------------

TEST_CASE("Balloon_OnADeletedOccurrenceIsUnresolvedAndNamesNoOtherInstance",
          "[drawing][bom][p14][balloon]") {
    // Suppression is not the only way an occurrence can go. Deleting one must
    // leave its balloon unresolved rather than sliding onto the survivor --
    // which is the failure that would be invisible, because the survivor is
    // an identical part with the same item number.
    Assembly a = makeAssembly();
    const ObjectId part = addPart(a, "Bolt");
    const ComponentId survivor = place(a, "C1", part, 0_mm);
    const ComponentId going = place(a, "C2", part, 40_mm);
    a.regenerate();
    const AnnotationId onGoing = balloon(a, "BalloonGoing", going, {60_mm, 60_mm});
    const AnnotationId onSurvivor = balloon(a, "BalloonKept", survivor, {20_mm, 60_mm});
    CHECK(balloonText(a, onGoing) == "1");

    REQUIRE(assembly::removeComponent(a.document, going).has_value());
    a.regenerate();

    // The row is still there, still item 1, so a balloon that fell back to
    // "any occurrence of this part" would carry on showing 1.
    REQUIRE(bomOf(a).rows.size() == 1);
    CHECK(bomOf(a).rows.front().quantity() == 1);
    CHECK(balloonText(a, onSurvivor) == "1"); // the one that still exists is fine

    const auto drawnNow = drawing::draw(a.document, onGoing, a.bodies(), a.transforms());
    REQUIRE_FALSE(drawnNow.has_value());
    CHECK_THAT(drawnNow.error().message, ContainsSubstring("does not exist"));
}

TEST_CASE("Bom_ListsThePartsEvenWhenTheAssemblyDidNotSolve", "[drawing][bom][p14]") {
    // A deliberate split, stated rather than stumbled into: a bill of
    // materials says WHAT is in the assembly, and that does not depend on
    // WHERE the solver put anything. So a BOM survives a failed solve and is
    // not stale -- it is the current active set.
    //
    // A balloon is the opposite: it needs a transform to put its leader on
    // the instance, so it fails. Both are asserted here together, because the
    // pair is the contract.
    Assembly a = makeAssembly();
    const ObjectId part = addPart(a, "Bolt");
    const ComponentId one = place(a, "C1", part, 0_mm);
    (void)place(a, "C2", part, 40_mm);
    a.regenerate();
    const AnnotationId label = balloon(a, "Balloon", one);
    const AnnotationId table = bomTable(a, "Bom");
    REQUIRE(bomOf(a).rows.front().quantity() == 2);

    // A transform lookup that has lost every occurrence: what an assembly
    // that did not solve looks like to a drawing.
    const drawing::TransformLookup unsolved = [](ComponentId) -> const RigidTransform3D* {
        return nullptr;
    };

    // The BOM is unchanged, because what is in the assembly is unchanged.
    const BillOfMaterials stillThere = bomOf(a);
    CHECK(stillThere.rows.front().quantity() == 2);
    const auto tableItems = drawing::draw(a.document, table, a.bodies(), unsolved);
    REQUIRE(tableItems.has_value());
    CHECK(tableItems->texts.size() == 6); // header + one row

    // The balloon cannot be placed, and says so rather than guessing.
    const auto balloonItems = drawing::draw(a.document, label, a.bodies(), unsolved);
    REQUIRE_FALSE(balloonItems.has_value());
    CHECK_THAT(balloonItems.error().message, ContainsSubstring("did not solve"));
}

TEST_CASE("Bom_RefusesAnOccurrenceWhosePartIsGone", "[drawing][bom][p14]") {
    // A component placing a part the document no longer has cannot be put in
    // a row: there is nothing to group it by and nothing to call it. It is a
    // diagnostic, not a blank line in the table.
    Assembly a = makeAssembly();
    const ObjectId part = addPart(a, "Bolt");
    (void)place(a, "C1", part, 0_mm);
    a.regenerate();
    REQUIRE(bomOf(a).rows.size() == 1);

    REQUIRE(a.document.removeObject(part).has_value());
    const auto bom = drawing::billOfMaterials(a.document, a.view);
    REQUIRE_FALSE(bom.has_value());
    CHECK(errorCode(bom) == ErrorCode::NotFound);
    CHECK_THAT(bom.error().message, ContainsSubstring("not an object of this document"));
}

TEST_CASE("Bom_ATableAndABalloonAreCheckedWhenTheyAreMade", "[drawing][bom][p14]") {
    Assembly a = makeAssembly();
    const ObjectId part = addPart(a, "Bolt");
    (void)place(a, "C1", part, 0_mm);
    a.regenerate();

    // A table points at nothing, like a note.
    const auto pointingTable = drawing::createAnnotation(
        a.document, "Bad",
        AnnotationDefinition{.view = a.view,
                             .type = AnnotationType::BomTable,
                             .target = AnnotationTarget{.plane = PlaneReference{}},
                             .placement = {250_mm, 250_mm}});
    REQUIRE_FALSE(pointingTable.has_value());
    CHECK_THAT(pointingTable.error().message, ContainsSubstring("points at nothing"));
    // And it says which KIND was made. The message used to say "a note"
    // whatever you had built, which sends the reader looking in the wrong
    // place.
    CHECK_THAT(pointingTable.error().message, ContainsSubstring("a bom_table annotation"));

    // Neither kind writes words of its own: both are read from the assembly.
    for (const AnnotationType kind : {AnnotationType::BomTable, AnnotationType::Balloon}) {
        INFO(drawing::toString(kind));
        AnnotationDefinition definition{.view = a.view,
                                        .type = kind,
                                        .text = "12",
                                        .placement = {250_mm, 250_mm}};
        if (kind == AnnotationType::Balloon) {
            definition.target = AnnotationTarget{.object = ObjectId::fromValue(1)};
        }
        const auto withWords = drawing::createAnnotation(a.document, "Bad", definition);
        REQUIRE_FALSE(withWords.has_value());
        CHECK_THAT(withWords.error().message, ContainsSubstring("takes its words from the model"));
    }

    // A table's sizes must be real lengths.
    const auto zeroRow = drawing::createAnnotation(
        a.document, "Bad",
        AnnotationDefinition{.view = a.view,
                             .type = AnnotationType::BomTable,
                             .placement = {250_mm, 250_mm},
                             .table = BomTableStyle{.rowHeight = 0_mm}});
    REQUIRE_FALSE(zeroRow.has_value());
    CHECK_THAT(zeroRow.error().message, ContainsSubstring("greater than zero"));
}

// --- annotationText() over the model-driven kinds ---------------------------------------------
//
// P14-REFMOD-001 found these. `isModelDriven()` admits three kinds -- a hole
// callout, a balloon and a BOM table -- because none of the three stores
// words of its own. `annotationText()` handled only the first, and answered
// for the other two by reporting that they were hole callouts pointing at
// something that is not a hole. A false statement about the document, on a
// public API, with no production caller yet to notice it.

TEST_CASE("Balloon_AnnotationTextIsTheNumberTheBalloonDraws",
          "[drawing][bom][p14][balloon]") {
    // The two routes to a balloon's number must give the same answer, because
    // there is only supposed to be one: the item number of the occurrence,
    // resolved through the bill of materials now (ADR-022).
    Assembly a = makeAssembly();
    const ObjectId part = addPart(a, "Bolt");
    const ComponentId first = place(a, "Bolt1", part, 0_mm);
    const ComponentId second = place(a, "Bolt2", part, 40_mm);
    a.regenerate();

    const AnnotationId onFirst = balloon(a, "BalloonA", first, {60_mm, 60_mm});
    const AnnotationId onSecond = balloon(a, "BalloonB", second, {90_mm, 60_mm});

    auto textA = drawing::annotationText(a.document, onFirst, a.bodies(), a.transforms());
    auto textB = drawing::annotationText(a.document, onSecond, a.bodies(), a.transforms());
    const std::string whyA = textA.has_value() ? std::string{} : textA.error().message;
    const std::string whyB = textB.has_value() ? std::string{} : textB.error().message;
    INFO(whyA);
    REQUIRE(textA.has_value());
    INFO(whyB);
    REQUIRE(textB.has_value());
    CHECK(*textA == balloonText(a, onFirst));
    CHECK(*textB == balloonText(a, onSecond));
    // Two occurrences of one part: one row, one number.
    CHECK(*textA == "1");
    CHECK(*textA == *textB);
}

TEST_CASE("Balloon_AnnotationTextFollowsTheAssemblyLikeTheDrawnNumberDoes",
          "[drawing][bom][p14][balloon]") {
    // Nothing is stored, so removing the part above a balloon's own row has
    // to move its number -- by both routes, together.
    Assembly a = makeAssembly();
    const ObjectId first = addPart(a, "Cover");
    const ObjectId second = addPart(a, "Pin");
    const ComponentId cover = place(a, "Cover1", first, 0_mm);
    const ComponentId pin = place(a, "Pin1", second, 40_mm);
    a.regenerate();
    const AnnotationId onPin = balloon(a, "PinBalloon", pin, {60_mm, 60_mm});

    auto before = drawing::annotationText(a.document, onPin, a.bodies(), a.transforms());
    REQUIRE(before.has_value());
    CHECK(*before == "2");
    CHECK(*before == balloonText(a, onPin));

    REQUIRE(assembly::removeComponent(a.document, cover).has_value());
    a.regenerate();
    auto after = drawing::annotationText(a.document, onPin, a.bodies(), a.transforms());
    REQUIRE(after.has_value());
    CHECK(*after == "1");
    CHECK(*after == balloonText(a, onPin));
}

TEST_CASE("Balloon_AnnotationTextOfAnOccurrenceThatIsNotDrawnFailsAsItself",
          "[drawing][bom][p14][balloon]") {
    // A balloon on an occurrence the view does not draw has no number. It
    // must say so as a BALLOON: the old message called it a hole callout,
    // which is a diagnostic an engineer cannot act on.
    Assembly a = makeAssembly();
    const ObjectId part = addPart(a, "Spacer");
    const ComponentId spacer = place(a, "Spacer1", part, 0_mm);
    a.regenerate();
    const AnnotationId onSpacer = balloon(a, "SpacerBalloon", spacer, {60_mm, 60_mm});

    const auto* component = a.document.findObjectAs<assembly::Component>(spacer);
    REQUIRE(component != nullptr);
    assembly::ComponentDefinition definition = component->definition();
    definition.suppressed = true;
    REQUIRE(assembly::setComponentDefinition(a.document, spacer, definition).has_value());
    a.regenerate();

    const auto text = drawing::annotationText(a.document, onSpacer, a.bodies(), a.transforms());
    REQUIRE_FALSE(text.has_value());
    CHECK_THAT(text.error().message, ContainsSubstring("SpacerBalloon"));
    CHECK_THAT(text.error().message, !ContainsSubstring("hole"));
}

TEST_CASE("BomTable_AnnotationTextSaysATableHasNoOneRunOfText",
          "[drawing][bom][p14]") {
    // A table's words are its ROWS, and there is no single string that is
    // "what the table says". Refusing is right; refusing while claiming the
    // table is a hole callout is not.
    Assembly a = makeAssembly();
    const ObjectId part = addPart(a, "Frame");
    (void)place(a, "Frame1", part, 0_mm);
    a.regenerate();
    const AnnotationId table = require(drawing::createAnnotation(
        a.document, "Table",
        AnnotationDefinition{
            .view = a.view, .type = AnnotationType::BomTable, .placement = {300_mm, 250_mm}}));

    const auto text = drawing::annotationText(a.document, table, a.bodies(), a.transforms());
    REQUIRE_FALSE(text.has_value());
    CHECK_THAT(text.error().message, ContainsSubstring("Table"));
    CHECK_THAT(text.error().message, ContainsSubstring("rows"));
    CHECK_THAT(text.error().message, !ContainsSubstring("hole"));
}

TEST_CASE("Annotation_EveryModelDrivenKindIsAnsweredByAnnotationText",
          "[drawing][bom][p14]") {
    // THE CONTRACT, as one case. `isModelDriven()` is public, and the natural
    // way to use it is "if the words are derived, ask what they are". Every
    // kind it admits must therefore be answered -- with its text, or with a
    // refusal that names that kind. None of them may be reported as some
    // other kind of annotation.
    Assembly a = makeAssembly();
    const ObjectId part = addPart(a, "Block");
    const ComponentId block = place(a, "Block1", part, 0_mm);
    a.regenerate();

    const AnnotationId item = balloon(a, "ItemBalloon", block, {60_mm, 60_mm});
    const AnnotationId table = require(drawing::createAnnotation(
        a.document, "ItemTable",
        AnnotationDefinition{
            .view = a.view, .type = AnnotationType::BomTable, .placement = {300_mm, 250_mm}}));

    for (const auto& [id, name, type] :
         std::vector<std::tuple<AnnotationId, std::string, AnnotationType>>{
             {item, "ItemBalloon", AnnotationType::Balloon},
             {table, "ItemTable", AnnotationType::BomTable}}) {
        INFO(name << " (" << drawing::toString(type) << ")");
        REQUIRE(drawing::isModelDriven(type));
        const auto text = drawing::annotationText(a.document, id, a.bodies(), a.transforms());
        if (text.has_value()) {
            CHECK_FALSE(text->empty());
        } else {
            CHECK_THAT(text.error().message, ContainsSubstring(name));
            CHECK_THAT(text.error().message, !ContainsSubstring("hole callout"));
        }
    }
}
