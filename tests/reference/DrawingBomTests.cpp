#include "reference/DrawingTestSupport.hpp"

#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Configurations.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/drawing/Views.hpp>
#include <bettercad/features/Validation.hpp>
#include <bettercad/io/DocumentFile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::test;
using namespace bettercad::test::drawref;
using Catch::Matchers::ContainsSubstring;

// RM-DWG-07 and RM-DWG-08 -- the bill of materials, the balloons that cite
// it, and what both do when the configuration changes.
namespace {

/// The quantities of a BOM, in item-number order. Item numbers run over
/// ascending part ObjectId and are recomputed every time (ADR-022), so the
/// order is derivable from the order the builder creates its parts in.
[[nodiscard]] std::vector<std::size_t> quantities(const drawing::BillOfMaterials& bom) {
    std::vector<std::size_t> counts;
    counts.reserve(bom.rows.size());
    for (const drawing::BomRow& row : bom.rows) {
        counts.push_back(row.quantity());
    }
    return counts;
}

[[nodiscard]] std::vector<std::string> names(const drawing::BillOfMaterials& bom) {
    std::vector<std::string> rows;
    rows.reserve(bom.rows.size());
    for (const drawing::BomRow& row : bom.rows) {
        rows.push_back(row.name);
    }
    return rows;
}

} // namespace

// --- RM-DWG-07: the bill of materials ------------------------------------------

TEST_CASE("DrawingReference_BoltedStackListsThreeRowsOfOneFourAndTwo",
          "[reference][drawing][rm-dwg-07][bom]") {
    // Bracket x1, Bolt x4, Spacer x2. Three rows, seven occurrences, and the
    // item numbers in the order the parts were created. Every number here is
    // read off the builder's description rather than off the BOM.
    auto m = reference::buildDrawnBoltedStackReferenceModel();
    INFO(why(m));
    REQUIRE(m.has_value());
    const ViewId front = m->front;
    Drawn model{std::move(m->document)};
    INFO(model.why());
    REQUIRE(model.succeeded());

    CHECK(assembly::activeComponents(model.document()).size() ==
          reference::DrawnBoltedStackModel::kOccurrences);

    const drawing::BillOfMaterials bom = bomOf(model, front);
    REQUIRE(bom.rows.size() == 3);
    CHECK(quantities(bom) == std::vector<std::size_t>{1, 4, 2});
    CHECK(names(bom) == std::vector<std::string>{"Bracket", "Bolt", "Spacer"});
    CHECK(bom.rows[0].item == 1);
    CHECK(bom.rows[1].item == 2);
    CHECK(bom.rows[2].item == 3);
    CHECK(bom.totalOccurrences() == reference::DrawnBoltedStackModel::kOccurrences);

    // A row keeps the occurrences it grouped, not just how many, and no
    // occurrence appears in two rows.
    std::vector<ComponentId> grouped;
    for (const drawing::BomRow& row : bom.rows) {
        grouped.insert(grouped.end(), row.occurrences.begin(), row.occurrences.end());
    }
    std::ranges::sort(grouped);
    CHECK(std::ranges::adjacent_find(grouped) == grouped.end());
    CHECK(grouped.size() == reference::DrawnBoltedStackModel::kOccurrences);
}

TEST_CASE("DrawingReference_BoltedStackBalloonsNameOccurrencesNotParts",
          "[reference][drawing][rm-dwg-07][bom][stref]") {
    // TWO BALLOONS ON TWO IDENTICAL BOLTS. They must show the same item
    // number -- they label the same part -- and land in two different places,
    // because they label two different instances of it. A balloon that had
    // collapsed onto the part would draw both arrows at one bolt and read
    // entirely correctly.
    auto m = reference::buildDrawnBoltedStackReferenceModel();
    INFO(why(m));
    REQUIRE(m.has_value());
    const ViewId front = m->front;
    const AnnotationId bracketBalloon = m->bracketBalloon;
    const AnnotationId boltA = m->boltBalloonA;
    const AnnotationId boltB = m->boltBalloonB;
    const AnnotationId spacerBalloon = m->spacerBalloon;
    const ComponentId firstBolt = m->bolts[0];
    const ComponentId lastBolt = m->bolts[3];
    Drawn model{std::move(m->document)};
    REQUIRE(model.succeeded());

    CHECK(annotationTextOf(model, bracketBalloon) == "1");
    CHECK(annotationTextOf(model, boltA) == "2");
    CHECK(annotationTextOf(model, boltB) == "2");
    CHECK(annotationTextOf(model, spacerBalloon) == "3");

    // The item number is a function of the OCCURRENCE, resolved through the
    // row its part groups into -- asked here of both bolts independently.
    auto firstItem = drawing::itemNumberOf(model.document(), front, firstBolt);
    auto lastItem = drawing::itemNumberOf(model.document(), front, lastBolt);
    INFO(why(firstItem) << why(lastItem));
    REQUIRE(firstItem.has_value());
    REQUIRE(lastItem.has_value());
    CHECK(*firstItem == 2);
    CHECK(*lastItem == 2);

    // Different places: the arrow tips are the ends of the leaders.
    const auto tipOf = [&](AnnotationId id) {
        auto items = drawing::draw(model.document(), id, model.bodies(), model.transforms());
        INFO(why(items));
        REQUIRE(items.has_value());
        REQUIRE_FALSE(items->lines.empty());
        const drawing::SceneLine& leader = items->lines.front();
        REQUIRE_FALSE(leader.points.empty());
        return leader.points.back();
    };
    const Point2D a = tipOf(boltA);
    const Point2D b = tipOf(boltB);
    CHECK(std::hypot((a.x - b.x).si(), (a.y - b.y).si()) > 0.01);
}

TEST_CASE("DrawingReference_BoltedStackQuantityFallsAndReturnsWithSuppression",
          "[reference][drawing][rm-dwg-07][bom][recovery]") {
    // Bolt x4 -> suppress one -> Qty 3 -> restore -> Qty 4. And the balloon
    // on the suppressed bolt must become UNRESOLVED rather than moving to one
    // of the three that are left: they are identical instances of one part,
    // which is exactly the case a rebinding implementation would survive.
    auto m = reference::buildDrawnBoltedStackReferenceModel();
    INFO(why(m));
    REQUIRE(m.has_value());
    const ViewId front = m->front;
    const AnnotationId boltA = m->boltBalloonA;
    const AnnotationId boltB = m->boltBalloonB;
    const ComponentId firstBolt = m->bolts[0];
    Drawn model{std::move(m->document)};
    REQUIRE(model.succeeded());
    REQUIRE(quantities(bomOf(model, front)) == std::vector<std::size_t>{1, 4, 2});

    suppressBase(model, firstBolt, true);
    CHECK(quantities(bomOf(model, front)) == std::vector<std::size_t>{1, 3, 2});

    // The balloon on the bolt that went: unresolved, and NOT quietly moved.
    const drawing::Resolution gone = resolutionOf(model, boltA);
    INFO(gone.diagnostic);
    CHECK(gone.state == drawing::ResolutionState::Unresolved);
    auto orphan = drawing::itemNumberOf(model.document(), front, firstBolt);
    CHECK_FALSE(orphan.has_value());
    // The other bolt's balloon is untouched, and still says 2.
    CHECK(resolutionOf(model, boltB).resolved());
    CHECK(annotationTextOf(model, boltB) == "2");

    suppressBase(model, firstBolt, false);
    INFO(model.why());
    REQUIRE(model.succeeded());
    CHECK(quantities(bomOf(model, front)) == std::vector<std::size_t>{1, 4, 2});
    CHECK(resolutionOf(model, boltA).resolved());
    CHECK(annotationTextOf(model, boltA) == "2");
}

TEST_CASE("DrawingReference_BoltedStackTableSaysWhatTheBomSays",
          "[reference][drawing][rm-dwg-07][bom]") {
    // The table draws the BOM rather than a copy of it: every part name and
    // every quantity in the bill has to appear on the sheet.
    auto m = reference::buildDrawnBoltedStackReferenceModel();
    REQUIRE(m.has_value());
    const ViewId front = m->front;
    Drawn model{std::move(m->document)};
    REQUIRE(model.succeeded());

    const drawing::BillOfMaterials bom = bomOf(model, front);
    const drawing::DrawingScene scene = sceneOf(model, drawing::sheets(model.document()).front());
    const std::vector<std::string> texts = sceneTexts(scene);
    for (const drawing::BomRow& row : bom.rows) {
        INFO("row " << row.item << " " << row.name);
        CHECK(std::ranges::find(texts, row.name) != texts.end());
        CHECK(std::ranges::find(texts, std::to_string(row.quantity())) != texts.end());
        CHECK(std::ranges::find(texts, std::to_string(row.item)) != texts.end());
    }
}

// --- RM-DWG-08: two configurations ---------------------------------------------

TEST_CASE("DrawingReference_GuardedFrameDrawsADifferentAssemblyInEachConfiguration",
          "[reference][drawing][rm-dwg-08][config]") {
    // Guarded: frame, guard, four bolts. Open: the guard and two bolts gone.
    // The BOM changes, and so does the item number of the bolt -- from 3 to
    // 2 -- because item numbers are compact and are recomputed over the parts
    // that are actually there (ADR-022). A stored number could not do that.
    auto m = reference::buildDrawnGuardedFrameReferenceModel();
    INFO(why(m));
    REQUIRE(m.has_value());
    const ViewId front = m->front;
    const ConfigurationId guarded = m->guarded;
    const ConfigurationId open = m->open;
    Drawn model{std::move(m->document)};
    INFO(model.why());
    REQUIRE(model.succeeded());

    activate(model, guarded);
    INFO(model.why());
    REQUIRE(model.succeeded());
    const drawing::BillOfMaterials full = bomOf(model, front);
    CHECK(quantities(full) == std::vector<std::size_t>{1, 1, 4});
    CHECK(names(full) == std::vector<std::string>{"Frame", "Guard", "Bolt"});
    CHECK(full.totalOccurrences() == reference::DrawnGuardedFrameModel::kGuardedOccurrences);
    CHECK(full.rows[2].item == 3);

    // `Open` takes the guard out from under its own balloon, so the drawing
    // reports that balloon rather than drawing one of the bolts instead. The
    // BILL is still computed -- it is a question about the assembly, not
    // about the annotation -- and it is the reduced one.
    activate(model, open);
    const drawing::BillOfMaterials reduced = bomOf(model, front);
    CHECK(quantities(reduced) == std::vector<std::size_t>{1, 2});
    CHECK(names(reduced) == std::vector<std::string>{"Frame", "Bolt"});
    CHECK(reduced.totalOccurrences() == reference::DrawnGuardedFrameModel::kOpenOccurrences);
    // The bolt is item 2 now, not item 3 with a gap where the guard was.
    CHECK(reduced.rows[1].item == 2);
}

TEST_CASE("DrawingReference_GuardedFrameBalloonGoesUnresolvedAndComesBack",
          "[reference][drawing][rm-dwg-08][config][stref][recovery]") {
    // A -> B -> A, and the restoration must be EXACT.
    //
    // In `Open` the guard is not drawn, so its balloon has nothing to label.
    // BetterCAD's answer is the loud one (ADR-014): the reference becomes
    // Unresolved, regeneration REPORTS THAT ANNOTATION by name, and the sheet
    // refuses to draw. What must not happen -- and is what this case is for --
    // is that the balloon quietly moves to the frame or to one of the bolts.
    //
    // Coming back to `Guarded` must then give the identical sheet, item by
    // item: no history-dependent drift.
    auto m = reference::buildDrawnGuardedFrameReferenceModel();
    INFO(why(m));
    REQUIRE(m.has_value());
    const AnnotationId guardBalloon = m->guardBalloon;
    const AnnotationId frameBalloon = m->frameBalloon;
    const ComponentId guard = m->guard;
    const ConfigurationId guarded = m->guarded;
    const ConfigurationId open = m->open;
    Drawn model{std::move(m->document)};
    REQUIRE(model.succeeded());

    activate(model, guarded);
    REQUIRE(model.succeeded());
    const SheetId sheet = drawing::sheets(model.document()).front();
    const drawing::DrawingScene a = sceneOf(model, sheet);
    CHECK(resolutionOf(model, guardBalloon).resolved());
    CHECK(annotationTextOf(model, guardBalloon) == "2");
    CHECK(annotationTextOf(model, frameBalloon) == "1");

    activate(model, open);
    // Reported, by name, as the object that failed -- not swallowed, and not
    // some other object blamed for it.
    CHECK_FALSE(model.succeeded());
    CHECK(model.failedOn(ObjectId::fromValue(guardBalloon.value())));
    CHECK_THAT(model.errorOn(ObjectId::fromValue(guardBalloon.value())),
               ContainsSubstring("GuardBalloon"));
    const drawing::Resolution gone = resolutionOf(model, guardBalloon);
    INFO(gone.diagnostic);
    CHECK(gone.state == drawing::ResolutionState::Unresolved);

    // NOT REBOUND. The balloon still names the guard, and the guard is still
    // the component it always named: the intent is intact and only its
    // resolution changed. A rebinding implementation would have had two
    // identical-looking alternatives here -- the frame and four bolts -- and
    // this is what would catch it taking one.
    const drawing::Annotation* still = drawing::findAnnotation(model.document(), guardBalloon);
    REQUIRE(still != nullptr);
    REQUIRE(still->definition().target.object.has_value());
    CHECK(*still->definition().target.object == ObjectId::fromValue(guard.value()));

    // The frame's balloon is untouched and still says 1.
    CHECK(resolutionOf(model, frameBalloon).resolved());
    CHECK(annotationTextOf(model, frameBalloon) == "1");

    activate(model, guarded);
    INFO(model.why());
    REQUIRE(model.succeeded());
    const drawing::DrawingScene again = sceneOf(model, sheet);
    CHECK(again.items.lines == a.items.lines);
    CHECK(again.items.arcs == a.items.arcs);
    CHECK(again.items.texts == a.items.texts);
    CHECK(resolutionOf(model, guardBalloon).resolved());
    CHECK(annotationTextOf(model, guardBalloon) == "2");
}

TEST_CASE("DrawingReference_GuardedFrameSheetRefusesToDrawAnUnresolvedBalloon",
          "[reference][drawing][rm-dwg-08][config]") {
    // A sheet that quietly dropped the annotation it could not resolve would
    // look complete and would not be. In `Open` the guard's balloon cannot be
    // drawn, so the sheet must FAIL rather than produce a drawing that is
    // silently missing an item -- and must say which annotation.
    auto m = reference::buildDrawnGuardedFrameReferenceModel();
    REQUIRE(m.has_value());
    const ConfigurationId open = m->open;
    Drawn model{std::move(m->document)};
    REQUIRE(model.succeeded());
    const SheetId sheet = drawing::sheets(model.document()).front();

    activate(model, open);
    auto scene = model.scene(sheet);
    REQUIRE_FALSE(scene.has_value());
    CHECK_THAT(scene.error().message, ContainsSubstring("GuardBalloon"));
}

TEST_CASE("DrawingReference_GuardedFrameConfigurationSurvivesSaveAndLoad",
          "[reference][drawing][rm-dwg-08][config][io]") {
    // The configuration is intent and is persisted; which one is active is
    // too. Saved in `Open` and loaded, the drawing must come back in `Open`:
    // the same reduced BOM, and the same balloon still unresolved and still
    // naming the guard. A round trip that healed it would have rebound it.
    TempDir dir;
    auto m = reference::buildDrawnGuardedFrameReferenceModel();
    REQUIRE(m.has_value());
    const ViewId front = m->front;
    const AnnotationId guardBalloon = m->guardBalloon;
    const ComponentId guard = m->guard;
    const ConfigurationId open = m->open;
    Document document = std::move(m->document);
    REQUIRE(document.setActiveConfiguration(open).has_value());

    const auto path = dir.path() / "guarded_open.bcad";
    REQUIRE(io::saveDocument(document, path).has_value());
    auto loaded = io::loadDocument(path);
    INFO(why(loaded));
    REQUIRE(loaded.has_value());

    Drawn model{std::move(*loaded)};
    CHECK(model.document().activeConfiguration() == open);
    CHECK(quantities(bomOf(model, front)) == std::vector<std::size_t>{1, 2});
    CHECK(resolutionOf(model, guardBalloon).state == drawing::ResolutionState::Unresolved);
    const drawing::Annotation* still = drawing::findAnnotation(model.document(), guardBalloon);
    REQUIRE(still != nullptr);
    REQUIRE(still->definition().target.object.has_value());
    CHECK(*still->definition().target.object == ObjectId::fromValue(guard.value()));

    // And it heals when the configuration comes back, in the loaded document
    // exactly as in the built one.
    activate(model, std::nullopt);
    INFO(model.why());
    REQUIRE(model.succeeded());
    CHECK(resolutionOf(model, guardBalloon).resolved());
}
