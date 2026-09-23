#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Configurations.hpp>
#include <bettercad/assembly/Resolution.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/drawing/Annotations.hpp>
#include <bettercad/drawing/Bom.hpp>
#include <bettercad/drawing/Dimensions.hpp>
#include <bettercad/drawing/Regeneration.hpp>
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
#include <array>
#include <memory>
#include <string>
#include <utility>
#include <format>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using drawing::AnnotationDefinition;
using drawing::AnnotationTarget;
using drawing::AnnotationType;
using drawing::DimensionDefinition;
using drawing::DimensionTarget;
using drawing::DimensionType;
using drawing::DrawingScale;
using drawing::SheetDefinition;
using drawing::StandardView;
using drawing::ViewDefinition;
using drawing::ViewSubject;
using features::NodeState;

// P14-REGEN-001: drawings in the dependency graph.
//
// THE ONE RULE. A model edit reaches the drawing state it affects, and never
// leaves state that is one edit out of date being presented as current.
//
// HOW THIS CODEBASE MEETS IT, which decides what there is to test. A drawing
// stores no derived state at all (ADR-011, ADR-014): projection, dimension
// values, BOM rows and annotation items are computed on every call. So
// "invalidate the cache" is not a thing that can be got wrong here, because
// there is no cache -- and the tests below prove that by construction rather
// than assume it (StaleGeometry..., ...IsComputedNotStored).
//
// What CAN be got wrong, and was, is the other half: an object with no
// RegenerationHandler is silently marked UpToDate and never validated. Every
// drawing object was in that state before this milestone, which is the exact
// defect P13-REGEN-001 fixed for mates. The first test below is the before/
// after of precisely that, run in one process.
//
// WHAT A HANDLER CHECKS (ADR-023): what the object NAMES, never what it
// DRAWS. It has no choice -- the assembly solve is a FINAL pass, so during
// the object phase the transforms a balloon would need are the previous
// pass's, and using them would be the stale read this milestone forbids.
namespace {

constexpr double kMm = 1e-9;

[[nodiscard]] bool contains(const std::vector<ObjectId>& list, ObjectId id) {
    return std::ranges::find(list, id) != list.end();
}

struct Model {
    Document document{"Machine"};
    features::Regenerator regenerator;

    drawing::BodyLookup bodies() {
        const features::Regenerator* r = &regenerator;
        return [r](ObjectId object) { return r->body(object); };
    }
    drawing::TransformLookup transforms() {
        const features::Regenerator* r = &regenerator;
        return [r](ComponentId c) { return r->transform(c); };
    }

    /// Registers BOTH modules' handlers, which is what a document carrying an
    /// assembly drawing needs: the assembly's to resolve the components, the
    /// drawing's to resolve what the sheet names.
    void registerAll() {
        assembly::registerHandlers(regenerator, nullptr, nullptr);
        drawing::registerHandlers(regenerator);
    }
    features::RegenerationReport regenerate() {
        registerAll();
        auto report = regenerator.regenerateAll(document);
        REQUIRE(report.has_value());
        return *report;
    }
    /// Regenerates without requiring the report to be clean: used where the
    /// edit under test is meant to break something.
    features::RegenerationReport regenerateAllowingFailure() {
        registerAll();
        auto report = regenerator.regenerateAll(document);
        REQUIRE(report.has_value()); // a per-object failure is still a report
        return *report;
    }
    /// An INCREMENTAL pass: what regenerate() is for a user who edited one
    /// thing. regenerateAll() forgets everything, so it can never show that
    /// only the affected objects were rebuilt.
    features::RegenerationReport pass() {
        registerAll();
        auto report = regenerator.regenerate(document);
        REQUIRE(report.has_value());
        return *report;
    }

    /// Every failure and block in @p report, for INFO when an expectation
    /// about a state is wrong: "which object, and why" beats "3 == 2".
    std::string explain(const features::RegenerationReport& report) const {
        const auto nameOf = [this](ObjectId id) {
            const auto name = document.nameOf(id);
            return name ? std::string{*name} : std::string{"?"};
        };
        std::string text = "report:";
        for (const ObjectId id : report.failed) {
            const auto found = report.errors.find(id);
            text += std::format(" [FAILED {} {}: {}]", id.value(), nameOf(id),
                                found == report.errors.end() ? std::string{} : found->second.message);
        }
        for (const ObjectId id : report.blocked) {
            text += std::format(" [BLOCKED {} {}]", id.value(), nameOf(id));
        }
        for (const auto& [id, error] : report.errors) {
            if (!contains(report.failed, id)) {
                text += std::format(" [ERROR {} {}: {}]", id.value(), nameOf(id), error.message);
            }
        }
        return text;
    }
};

SheetId addSheet(Model& m, const std::string& name = "Sheet1") {
    return require(drawing::createSheet(
        m.document, name,
        SheetDefinition{.format = drawing::SheetFormat::A3,
                        .orientation = drawing::SheetOrientation::Landscape,
                        .margins = drawing::SheetMargins{10_mm, 10_mm, 10_mm, 10_mm},
                        .scale = DrawingScale{1, 1}}));
}

/// A 100 x 60 x 40 block whose four profile entities are kept, so a side face
/// can be named by the entity that sweeps it (P12-STREF-001).
struct Block {
    ObjectId sketch{};
    ObjectId pad{};
    std::array<EntityId, 4> lines{};
    std::array<EntityId, 4> corners{};
};

Block addBlock(Model& m, const std::string& name, Length width = 100_mm, Length depth = 60_mm,
               Length height = 40_mm) {
    Block block;
    auto sketch = std::make_unique<sketch::Sketch>(name + "Profile", Frame3D::xy());
    block.corners = {
        require(sketch->addPoint(Point2D{0_mm, 0_mm})),
        require(sketch->addPoint(Point2D{width, 0_mm})),
        require(sketch->addPoint(Point2D{width, depth})),
        require(sketch->addPoint(Point2D{0_mm, depth})),
    };
    for (std::size_t i = 0; i < 4; ++i) {
        block.lines[i] = require(sketch->addLine(block.corners[i], block.corners[(i + 1) % 4]));
    }
    block.sketch = require(m.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        name, {.profile = SketchId::fromValue(block.sketch.value()), .depth = height});
    REQUIRE(extrude.has_value());
    block.pad = require(m.document.addObject(std::move(*extrude)));
    return block;
}

PlaneReference sideOf(const Block& block, std::size_t entity) {
    return PlaneReference{.object = block.pad,
                          .face = FaceSelector{.role = FaceRole::Side, .entity = block.lines[entity]}};
}

ViewId addView(Model& m, SheetId sheet, const std::string& name, ObjectId source) {
    return require(drawing::createView(m.document, name,
                                       ViewDefinition{.sheet = sheet,
                                                      .source = ObjectReference{source},
                                                      .orientation = StandardView::Front,
                                                      .placement = Point2D{200_mm, 150_mm}}));
}

DimensionId addWidth(Model& m, ViewId view, const std::string& name, const Block& block) {
    return require(drawing::createDimension(
        m.document, name,
        DimensionDefinition{.view = view,
                            .type = DimensionType::Linear,
                            .from = DimensionTarget{.plane = sideOf(block, 3)},
                            .to = DimensionTarget{.plane = sideOf(block, 1)},
                            .format = drawing::DimensionFormat{.decimals = 2}}));
}

/// Widens the block's profile to @p width, which moves the two faces the width
/// dimension names WITHOUT changing which entity sweeps them.
void widenTo(Model& m, const Block& block, Length width) {
    REQUIRE(m.document
                .modifyObject<sketch::Sketch>(
                    block.sketch,
                    [&](sketch::Sketch& s) {
                        REQUIRE(s.setPointPosition(block.corners[1], Point2D{width, 0_mm}).has_value());
                        REQUIRE(s.setPointPosition(block.corners[2], Point2D{width, 60_mm}).has_value());
                        return true;
                    })
                .has_value());
}

/// Removes the side face swept by `lines[1]` WITHOUT breaking the model: the
/// rectangular profile becomes a triangle, so the extrude still builds a solid
/// and the ONLY thing that changed for the drawing is that one named face has
/// stopped existing.
///
/// That distinction is the whole point of the fixture. Deleting one line would
/// leave the profile open, the extrude would fail, and every drawing object
/// downstream would be BLOCKED -- which the dependency graph already handled
/// before this milestone, and so proves nothing about handlers.
EntityId cutCornerOff(Model& m, const Block& block) {
    EntityId diagonal{};
    REQUIRE(m.document
                .modifyObject<sketch::Sketch>(
                    block.sketch,
                    [&](sketch::Sketch& s) {
                        REQUIRE(s.removeEntity(block.lines[1]).has_value());
                        REQUIRE(s.removeEntity(block.lines[2]).has_value());
                        diagonal = require(s.addLine(block.corners[1], block.corners[3]));
                        return true;
                    })
                .has_value());
    return diagonal;
}

double widthOf(Model& m, DimensionId id) {
    auto measured = drawing::measure(m.document, id, m.bodies(), m.transforms());
    REQUIRE(measured.has_value());
    REQUIRE(measured->length.has_value());
    return measured->length->in(units::mm);
}

} // namespace

// --- The defect this milestone closes -----------------------------------------------------------

TEST_CASE("DrawingRegeneration_WithoutAHandlerABrokenDimensionRegeneratesAsUpToDate",
          "[drawing][regen][p14]") {
    // THE BEFORE AND AFTER, in one process, on one document, so the claim is
    // measured rather than asserted from the commit history.
    //
    // ADR-014 said this in as many words: "an object with no handler is
    // silently marked UpToDate and never validated". Every sheet, view,
    // dimension and annotation was in that state until this milestone.
    const auto build = [](Model& m) {
        const SheetId sheet = addSheet(m);
        const Block block = addBlock(m, "Block");
        m.regenerate();
        const ViewId view = addView(m, sheet, "Front", block.pad);
        const DimensionId width = addWidth(m, view, "Width", block);
        // Break the dimension WITHOUT breaking the graph: the extrude still
        // exists and still builds, so nothing is missing and nothing upstream
        // fails. Only the FACE the dimension names stops existing, and a face
        // name is not a dependency edge -- which is precisely why a handler is
        // the only thing that can notice.
        cutCornerOff(m, block);
        return width;
    };

    // WITHOUT the drawing handlers: the old behaviour, still reachable
    // because registration is the caller's.
    {
        Model m;
        const DimensionId width = build(m);
        features::Regenerator bare;
        auto report = bare.regenerateAll(m.document);
        REQUIRE(report.has_value());
        const auto state = bare.state(ObjectId{width});
        REQUIRE(state.has_value());
        CHECK(*state == NodeState::UpToDate); // never looked at
        CHECK(bare.error(ObjectId{width}) == nullptr);
        CHECK_FALSE(contains(report->failed, ObjectId{width}));
    }

    // WITH them: the same document, the same edit, reported.
    {
        Model m;
        const DimensionId width = build(m);
        const auto report = m.regenerateAllowingFailure();
        INFO(m.explain(report));
        const auto state = m.regenerator.state(ObjectId{width});
        REQUIRE(state.has_value());
        CHECK(*state == NodeState::Failed);
        CHECK(contains(report.failed, ObjectId{width}));
        CHECK_FALSE(report.succeeded());
        const Error* error = m.regenerator.error(ObjectId{width});
        REQUIRE(error != nullptr);
        // The diagnostic names the object, and says what could not be found.
        CHECK_THAT(error->message, ContainsSubstring("Width"));
        CHECK_THAT(error->message, ContainsSubstring("cannot be measured"));
    }
}

TEST_CASE("DrawingRegeneration_EveryDrawingKindIsValidatedRatherThanAssumed",
          "[drawing][regen][p14]") {
    // All four kinds take part: none is left in the unhandled branch.
    Model m;
    const SheetId sheet = addSheet(m);
    const Block block = addBlock(m, "Block");
    m.regenerate();
    const ViewId view = addView(m, sheet, "Front", block.pad);
    const DimensionId width = addWidth(m, view, "Width", block);
    const AnnotationId note = require(drawing::createAnnotation(
        m.document, "Note",
        AnnotationDefinition{
            .view = view, .type = AnnotationType::Note, .text = "BREAK SHARP EDGES",
            .placement = Point2D{40_mm, 40_mm}}));

    const auto report = m.regenerate();
    for (const ObjectId id : {ObjectId{sheet}, ObjectId{view}, ObjectId{width}, ObjectId{note}}) {
        INFO("object " << id.value());
        const auto state = m.regenerator.state(id);
        REQUIRE(state.has_value());
        CHECK(*state == NodeState::Regenerated);
        CHECK(contains(report.regenerated, id));
        // A drawing object owns NO geometry: a handler that published a body
        // would be a cache, and ADR-014 forbids one.
        CHECK(m.regenerator.body(id) == nullptr);
    }
    CHECK(report.succeeded());
}

// --- Dirty propagation --------------------------------------------------------------------------

TEST_CASE("DrawingRegeneration_AModelEditReachesOnlyTheDrawingObjectsThatNameIt",
          "[drawing][regen][p14]") {
    // "Regenerate only affected drawing state where feasible": an edit to one
    // block must not rebuild the sheet that draws the other.
    Model m;
    const SheetId sheet = addSheet(m);
    const Block edited = addBlock(m, "Edited");
    const Block untouched = addBlock(m, "Untouched");
    m.regenerate();
    const ViewId editedView = addView(m, sheet, "EditedFront", edited.pad);
    const ViewId untouchedView = addView(m, sheet, "UntouchedFront", untouched.pad);
    const DimensionId editedWidth = addWidth(m, editedView, "EditedWidth", edited);
    const DimensionId untouchedWidth = addWidth(m, untouchedView, "UntouchedWidth", untouched);
    m.regenerate();

    // A settled document: a pass with no edit rebuilds nothing at all.
    const auto quiet = m.pass();
    CHECK(quiet.regenerated.empty());
    CHECK(quiet.succeeded());

    widenTo(m, edited, 120_mm);
    const auto after = m.pass();

    CHECK(contains(after.regenerated, ObjectId{editedWidth}));
    CHECK(contains(after.regenerated, ObjectId{editedView}));
    CHECK_FALSE(contains(after.regenerated, ObjectId{untouchedWidth}));
    CHECK_FALSE(contains(after.regenerated, ObjectId{untouchedView}));
    // The sheet names nothing, so no model edit reaches it.
    CHECK_FALSE(contains(after.regenerated, ObjectId{sheet}));
    CHECK(after.succeeded());
}

TEST_CASE("DrawingRegeneration_ADimensionFollowsTheModelItNames", "[drawing][regen][p14]") {
    Model m;
    const SheetId sheet = addSheet(m);
    const Block block = addBlock(m, "Block");
    m.regenerate();
    const ViewId view = addView(m, sheet, "Front", block.pad);
    const DimensionId width = addWidth(m, view, "Width", block);
    m.regenerate();
    CHECK_THAT(widthOf(m, width), WithinAbs(100.0, kMm));

    widenTo(m, block, 137.5_mm);
    m.regenerate();
    CHECK_THAT(widthOf(m, width), WithinAbs(137.5, kMm));

    // And the stored intent is exactly what it was: the dimension still names
    // the same two faces by the same two entities. Nothing was rebound to
    // make the new number appear.
    const DimensionDefinition& d = drawing::findDimension(m.document, width)->definition();
    CHECK(d.from.plane->face->entity == block.lines[3]);
    CHECK(d.to.plane->face->entity == block.lines[1]);
}

TEST_CASE("DrawingRegeneration_AViewRedrawsAtTheModelsNewSize", "[drawing][regen][p14]") {
    Model m;
    const SheetId sheet = addSheet(m);
    const Block block = addBlock(m, "Block");
    m.regenerate();
    const ViewId view = addView(m, sheet, "Front", block.pad);
    m.regenerate();

    const auto before = drawing::projectedGeometry(m.document, view, m.bodies(), m.transforms());
    REQUIRE(before.has_value());
    const double wide = (before->bounds.max.x - before->bounds.min.x).in(units::mm);
    CHECK_THAT(wide, WithinAbs(100.0, kMm));

    widenTo(m, block, 180_mm);
    m.regenerate();

    const auto after = drawing::projectedGeometry(m.document, view, m.bodies(), m.transforms());
    REQUIRE(after.has_value());
    CHECK_THAT((after->bounds.max.x - after->bounds.min.x).in(units::mm), WithinAbs(180.0, kMm));
}

// --- Stale geometry is impossible ---------------------------------------------------------------

TEST_CASE("DrawingRegeneration_ViewGeometryIsComputedOnEveryCallAndNeverStored",
          "[drawing][regen][p14][stale]") {
    // The invariant ADR-014 rests on, asserted directly rather than trusted:
    // the drawing follows the model with NO regeneration pass between the
    // edit and the question. If a projection were stored anywhere, this would
    // return the old width.
    Model m;
    const SheetId sheet = addSheet(m);
    const Block block = addBlock(m, "Block");
    m.regenerate();
    const ViewId view = addView(m, sheet, "Front", block.pad);
    const DimensionId width = addWidth(m, view, "Width", block);
    m.regenerate();
    REQUIRE_THAT(widthOf(m, width), WithinAbs(100.0, kMm));

    // A DRAWING-ONLY edit, with NO regeneration pass after it. Nothing in the
    // model moved, so a cached projection would still be "valid" by any
    // invalidation rule keyed on the model -- and would still be wrong.
    const auto before = drawing::projectedGeometry(m.document, view, m.bodies(), m.transforms());
    REQUIRE(before.has_value());
    const double drawnWide = (before->bounds.max.x - before->bounds.min.x).in(units::mm);
    CHECK_THAT(drawnWide, WithinAbs(100.0, kMm));

    auto halved = drawing::findView(m.document, view)->definition();
    halved.scale = DrawingScale{1, 2};
    REQUIRE(drawing::setViewDefinition(m.document, view, halved).has_value());

    const auto after = drawing::projectedGeometry(m.document, view, m.bodies(), m.transforms());
    REQUIRE(after.has_value());
    CHECK_THAT((after->bounds.max.x - after->bounds.min.x).in(units::mm), WithinAbs(50.0, kMm));

    // And a MODEL edit that has not been regenerated yet reports what the
    // body says, which is the honest answer rather than a remembered one:
    // the sketch has moved, the solid has not.
    widenTo(m, block, 150_mm);
    CHECK_THAT(widthOf(m, width), WithinAbs(100.0, kMm));

    m.regenerate();
    CHECK_THAT(widthOf(m, width), WithinAbs(150.0, kMm));
}

TEST_CASE("DrawingRegeneration_AFailedRegenerationCannotBeDrawnWithTheOldGeometry",
          "[drawing][regen][p14][stale]") {
    // THE GATE: "stale geometry impossible". A drawing that was good, then a
    // model change that breaks it, then the same questions again -- and the
    // answer must be a refusal, never the last good answer.
    Model m;
    const SheetId sheet = addSheet(m);
    const Block block = addBlock(m, "Block");
    m.regenerate();
    const ViewId view = addView(m, sheet, "Front", block.pad);
    const DimensionId width = addWidth(m, view, "Width", block);
    m.regenerate();

    const auto good = drawing::projectedGeometry(m.document, view, m.bodies(), m.transforms());
    REQUIRE(good.has_value());
    REQUIRE_THAT(widthOf(m, width), WithinAbs(100.0, kMm));

    // Remove what the view draws. The view's source IS a dependency edge, so
    // the graph itself reports this one.
    REQUIRE(m.document.removeObject(block.pad).has_value());
    const auto report = m.regenerateAllowingFailure();
    CHECK_FALSE(report.succeeded());

    // Neither the view nor the dimension will now produce anything.
    const auto stale = drawing::projectedGeometry(m.document, view, m.bodies(), m.transforms());
    CHECK_FALSE(stale.has_value());
    const auto measured = drawing::measure(m.document, width, m.bodies(), m.transforms());
    CHECK_FALSE(measured.has_value());
    // And no body was left behind under the view's id to be drawn from.
    CHECK(m.regenerator.body(ObjectId{view}) == nullptr);
    CHECK(m.regenerator.body(block.pad) == nullptr);
}

// --- Unresolved references ----------------------------------------------------------------------

TEST_CASE("DrawingRegeneration_AViewWhoseSourceIsRemovedFailsAndBlocksItsDimensions",
          "[drawing][regen][p14]") {
    Model m;
    const SheetId sheet = addSheet(m);
    const Block block = addBlock(m, "Block");
    m.regenerate();
    const ViewId view = addView(m, sheet, "Front", block.pad);
    const DimensionId width = addWidth(m, view, "Width", block);
    m.regenerate();

    REQUIRE(m.document.removeObject(block.pad).has_value());
    const auto report = m.regenerateAllowingFailure();

    CHECK(*m.regenerator.state(ObjectId{view}) == NodeState::Failed);
    // The dimension depends on the view, so it is BLOCKED rather than failed:
    // it is not asked a question whose answer could only be wrong.
    CHECK(*m.regenerator.state(ObjectId{width}) == NodeState::Blocked);
    CHECK(contains(report.blocked, ObjectId{width}));
    // The sheet is untouched by either: it names nothing.
    CHECK(*m.regenerator.state(ObjectId{sheet}) != NodeState::Failed);
    CHECK(*m.regenerator.state(ObjectId{sheet}) != NodeState::Blocked);
}

TEST_CASE("DrawingRegeneration_TheFailureCodeSaysUnresolvedOrInvalid", "[drawing][regen][p14]") {
    // P14-STREF-001's three states survive into Regenerator::error(), because
    // the handler forwards the resolver's own code rather than flattening
    // every failure to one. Only an Unresolved one is worth waiting for.
    Model m;
    const SheetId sheet = addSheet(m);
    const Block block = addBlock(m, "Block");
    m.regenerate();
    const ViewId view = addView(m, sheet, "Front", block.pad);

    // UNRESOLVED: the face is not there now. An extrude HAS side faces; this
    // one's sweeping entity was deleted.
    const DimensionId width = addWidth(m, view, "Width", block);
    // INVALID: a role an extrude cannot produce in any configuration.
    const DimensionId impossible = require(drawing::createDimension(
        m.document, "Impossible",
        DimensionDefinition{
            .view = view,
            .type = DimensionType::Linear,
            .from = DimensionTarget{.plane = PlaneReference{
                        .object = block.pad, .face = FaceSelector{.role = FaceRole::HoleBottom}}},
            .to = DimensionTarget{.plane = sideOf(block, 1)},
            .format = drawing::DimensionFormat{.decimals = 2}}));
    cutCornerOff(m, block);
    m.regenerateAllowingFailure();

    const Error* gone = m.regenerator.error(ObjectId{width});
    REQUIRE(gone != nullptr);
    CHECK(gone->code == ErrorCode::NotFound);

    const Error* wrong = m.regenerator.error(ObjectId{impossible});
    REQUIRE(wrong != nullptr);
    CHECK(wrong->code == ErrorCode::InvalidArgument);
}

TEST_CASE("DrawingRegeneration_ADrawingObjectThatFailedIsRetriedAndRecovers",
          "[drawing][regen][p14]") {
    // A Failed node is a source of dirtiness, so the next pass asks again --
    // which is what makes Unresolved a waiting state rather than a dead one.
    Model m;
    const SheetId sheet = addSheet(m);
    const Block block = addBlock(m, "Block");
    m.regenerate();
    const ViewId view = addView(m, sheet, "Front", block.pad);
    const DimensionId width = addWidth(m, view, "Width", block);
    m.regenerate();
    REQUIRE(*m.regenerator.state(ObjectId{width}) == NodeState::Regenerated);

    // Break it, incrementally.
    const EntityId diagonal = cutCornerOff(m, block);
    INFO(m.explain(m.pass()));
    REQUIRE(*m.regenerator.state(ObjectId{width}) == NodeState::Failed);
    // The stored intent is untouched by the failure.
    CHECK(drawing::findDimension(m.document, width)->definition().to.plane->face->entity ==
          block.lines[1]);

    // Put the rectangle back, so the face is in exactly the place and shape it
    // was -- but swept by a NEW entity. A rebinding implementation would take
    // it; this one must not.
    EntityId replacement{};
    REQUIRE(m.document
                .modifyObject<sketch::Sketch>(
                    block.sketch,
                    [&](sketch::Sketch& s) {
                        REQUIRE(s.removeEntity(diagonal).has_value());
                        replacement = require(s.addLine(block.corners[1], block.corners[2]));
                        REQUIRE(s.addLine(block.corners[2], block.corners[3]).has_value());
                        return true;
                    })
                .has_value());
    m.pass();
    CHECK(replacement != block.lines[1]);
    // Still failed: recovery means the SAME entity coming back, and a
    // look-alike in the same place is not it (P14-STREF-001).
    CHECK(*m.regenerator.state(ObjectId{width}) == NodeState::Failed);

    // Now point the dimension at what is actually there. It recovers.
    auto repaired = drawing::findDimension(m.document, width)->definition();
    repaired.to = DimensionTarget{.plane = PlaneReference{
                                      .object = block.pad,
                                      .face = FaceSelector{.role = FaceRole::Side,
                                                           .entity = replacement}}};
    REQUIRE(drawing::setDimensionDefinition(m.document, width, repaired).has_value());
    m.pass();
    CHECK(*m.regenerator.state(ObjectId{width}) == NodeState::Regenerated);
    CHECK(m.regenerator.error(ObjectId{width}) == nullptr);
}

// --- Failure atomicity --------------------------------------------------------------------------

TEST_CASE("DrawingRegeneration_AFailedDrawingRegenerationCommitsNothing",
          "[drawing][regen][p14][atomic]") {
    // A drawing handler is a pure READ of the document. So a failed drawing
    // regeneration cannot leave a half-applied edit behind -- not because it
    // is careful, but because it never writes (ADR-023).
    Model m;
    const SheetId sheet = addSheet(m);
    const Block block = addBlock(m, "Block");
    m.regenerate();
    const ViewId view = addView(m, sheet, "Front", block.pad);
    const DimensionId width = addWidth(m, view, "Width", block);
    m.regenerate();

    cutCornerOff(m, block);
    m.pass(); // the sketch and extrude rebuild; the dimension fails

    const DimensionDefinition before = drawing::findDimension(m.document, width)->definition();
    const auto revision = m.document.revisionOf(ObjectId{width});
    const std::size_t objects = m.document.objectCount();
    REQUIRE(*m.regenerator.state(ObjectId{width}) == NodeState::Failed);

    // Three more failing passes change nothing about the document.
    for (int i = 0; i < 3; ++i) {
        m.pass();
        CHECK(*m.regenerator.state(ObjectId{width}) == NodeState::Failed);
        CHECK(drawing::findDimension(m.document, width)->definition() == before);
        CHECK(m.document.revisionOf(ObjectId{width}) == revision);
        CHECK(m.document.objectCount() == objects);
    }
}

// --- Configuration --------------------------------------------------------------------------------

namespace {

struct Assembly {
    Model model;
    SheetId sheet{};
    ViewId view{};
    ObjectId part{};
    ComponentId first{};
    ComponentId second{};
};

/// One part placed twice, drawn as an assembly view.
Assembly twoOfOnePart() {
    Assembly a;
    a.sheet = addSheet(a.model);
    const Block block = addBlock(a.model, "Bracket", 40_mm, 40_mm, 40_mm);
    a.part = block.pad;
    a.first = require(assembly::createComponent(
        a.model.document, "First",
        {.part = a.part, .placement = ComponentPlacement{.translation = {0_mm, 0_mm, 0_mm}}}));
    a.second = require(assembly::createComponent(
        a.model.document, "Second",
        {.part = a.part, .placement = ComponentPlacement{.translation = {80_mm, 0_mm, 0_mm}}}));
    a.view = require(drawing::createView(a.model.document, "MainView",
                                         ViewDefinition{.sheet = a.sheet,
                                                        .subject = ViewSubject::Assembly,
                                                        .orientation = StandardView::Front,
                                                        .placement = Point2D{200_mm, 150_mm}}));
    a.model.regenerate();
    return a;
}

AnnotationId addBalloon(Assembly& a, const std::string& name, ComponentId occurrence, Point2D at) {
    return require(drawing::createAnnotation(
        a.model.document, name,
        AnnotationDefinition{.view = a.view,
                             .type = AnnotationType::Balloon,
                             .target = AnnotationTarget{.object = ObjectId{occurrence}},
                             .placement = at}));
}

} // namespace

TEST_CASE("DrawingRegeneration_ABalloonOnASuppressedOccurrenceFailsAndComesBack",
          "[drawing][regen][p14][config]") {
    // A balloon is IN FORCE even when its target is not, which is why this
    // fails where a SUPPRESSED MATE is silent (ADR-023): the sheet would
    // otherwise print one balloon short, with nothing said.
    Assembly a = twoOfOnePart();
    const AnnotationId balloon = addBalloon(a, "B2", a.second, {120_mm, 60_mm});
    const ConfigurationId reduced = require(a.model.document.createConfiguration("Reduced"));
    REQUIRE(assembly::suppressComponent(a.model.document, reduced, a.second, true).has_value());
    a.model.regenerate();
    REQUIRE(*a.model.regenerator.state(ObjectId{balloon}) == NodeState::Regenerated);

    REQUIRE(a.model.document.setActiveConfiguration(reduced).has_value());
    const auto report = a.model.regenerateAllowingFailure();
    CHECK(*a.model.regenerator.state(ObjectId{balloon}) == NodeState::Failed);
    CHECK_FALSE(report.succeeded());
    const Error* error = a.model.regenerator.error(ObjectId{balloon});
    REQUIRE(error != nullptr);
    // Unresolved, not Invalid: the intent is kept and is waiting.
    CHECK((error->code == ErrorCode::NotFound || error->code == ErrorCode::FailedPrecondition));
    CHECK(drawing::findAnnotation(a.model.document, balloon)->definition().target.object ==
          ObjectId{a.second});

    // The sibling is identical, still active, and is NOT adopted.
    REQUIRE(drawing::drawnOccurrences(a.model.document, a.view)->size() == 1);

    REQUIRE(a.model.document.setActiveConfiguration(std::nullopt).has_value());
    a.model.regenerate();
    CHECK(*a.model.regenerator.state(ObjectId{balloon}) == NodeState::Regenerated);
    CHECK(drawing::findAnnotation(a.model.document, balloon)->definition().target.object ==
          ObjectId{a.second});
}

TEST_CASE("DrawingRegeneration_AConfigurationChangesWhatTheAssemblyViewDrawsAndTheBomSays",
          "[drawing][regen][p14][config]") {
    Assembly a = twoOfOnePart();
    const AnnotationId table = require(drawing::createAnnotation(
        a.model.document, "Bom",
        AnnotationDefinition{.view = a.view,
                             .type = AnnotationType::BomTable,
                             .placement = Point2D{240_mm, 200_mm}}));
    a.model.regenerate();
    REQUIRE(drawing::billOfMaterials(a.model.document, a.view)->totalOccurrences() == 2);

    const ConfigurationId reduced = require(a.model.document.createConfiguration("Reduced"));
    REQUIRE(assembly::suppressComponent(a.model.document, reduced, a.second, true).has_value());
    REQUIRE(a.model.document.setActiveConfiguration(reduced).has_value());
    a.model.regenerate();

    // The BOM follows the configuration, with no invalidation step: it is
    // recomputed from activeComponents() on every call.
    INFO(a.model.explain(a.model.regenerate()));
    const auto bom = drawing::billOfMaterials(a.model.document, a.view);
    REQUIRE(bom.has_value());
    CHECK(bom->totalOccurrences() == 1);
    CHECK(bom->rows.size() == 1);
    CHECK(bom->rows.front().quantity() == 1);
    CHECK(drawing::drawnOccurrences(a.model.document, a.view)->size() == 1);
    // The table itself still regenerates: what it names is the view's
    // assembly, and that still resolves.
    CHECK(*a.model.regenerator.state(ObjectId{table}) == NodeState::Regenerated);
}

TEST_CASE("DrawingRegeneration_AnAssemblyViewWithNothingActiveFails",
          "[drawing][regen][p14][config]") {
    // An assembly view has NO dependency edge to any component -- what it
    // draws comes from the active configuration at draw time (ADR-021) -- so
    // the graph cannot report this and the handler must.
    Assembly a = twoOfOnePart();
    const ConfigurationId empty = require(a.model.document.createConfiguration("Empty"));
    REQUIRE(assembly::suppressComponent(a.model.document, empty, a.first, true).has_value());
    REQUIRE(assembly::suppressComponent(a.model.document, empty, a.second, true).has_value());
    REQUIRE(a.model.document.setActiveConfiguration(empty).has_value());

    const auto report = a.model.regenerateAllowingFailure();
    CHECK(*a.model.regenerator.state(ObjectId{a.view}) == NodeState::Failed);
    CHECK_FALSE(report.succeeded());
    const Error* error = a.model.regenerator.error(ObjectId{a.view});
    REQUIRE(error != nullptr);
    CHECK_THAT(error->message, ContainsSubstring("no active components"));

    REQUIRE(a.model.document.setActiveConfiguration(std::nullopt).has_value());
    a.model.regenerate();
    CHECK(*a.model.regenerator.state(ObjectId{a.view}) == NodeState::Regenerated);
}

// --- Determinism ----------------------------------------------------------------------------------

TEST_CASE("DrawingRegeneration_RepeatedPassesGiveTheSameReportAndTheSameDrawing",
          "[drawing][regen][p14][determinism]") {
    Assembly a = twoOfOnePart();
    const AnnotationId balloon = addBalloon(a, "B1", a.first, {40_mm, 60_mm});
    CHECK(*drawing::itemNumberOf(a.model.document, a.view, a.first) == 1);
    (void)balloon;
    const DimensionId width = require(drawing::createDimension(
        a.model.document, "Width",
        DimensionDefinition{.view = a.view,
                            .type = DimensionType::Linear,
                            .from = DimensionTarget{.plane = PlaneReference{
                                        .object = a.part,
                                        .face = FaceSelector{.role = FaceRole::StartCap}}},
                            .to = DimensionTarget{.plane = PlaneReference{
                                      .object = a.part,
                                      .face = FaceSelector{.role = FaceRole::EndCap}}},
                            .format = drawing::DimensionFormat{.decimals = 3}}));

    std::vector<std::string> shapes;
    std::vector<std::size_t> lineCounts;
    for (int run = 0; run < 6; ++run) {
        // regenerateAll() forgets every previous result, so each run rebuilds
        // from nothing: agreement is the implementation's, not a cache's.
        const auto report = a.model.regenerate();
        INFO(a.model.explain(report));
        REQUIRE(report.succeeded());
        const auto drawn =
            drawing::projectedGeometry(a.model.document, a.view, a.model.bodies(), a.model.transforms());
        REQUIRE(drawn.has_value());
        lineCounts.push_back(drawn->edges.size());
        // Every annotation the sheet draws, summarised: what is written and
        // how much is drawn. A balloon's number comes from the BOM, so this
        // covers the derived table as well as the items.
        const auto items =
            drawing::drawAnnotations(a.model.document, a.view, a.model.bodies(), a.model.transforms());
        std::string why;
        if (!items) {
            why = items.error().message;
        }
        INFO(why);
        REQUIRE(items.has_value());
        std::string summary = std::format("{}/{}/", items->lines.size(), items->texts.size());
        for (const auto& drawnText : items->texts) {
            summary += drawnText.text + ",";
        }
        const auto measured =
            drawing::measure(a.model.document, width, a.model.bodies(), a.model.transforms());
        REQUIRE(measured.has_value());
        shapes.push_back(summary + "|" + measured->text);
    }
    CHECK(std::ranges::adjacent_find(shapes, std::not_equal_to<>{}) == shapes.end());
    CHECK(std::ranges::adjacent_find(lineCounts, std::not_equal_to<>{}) == lineCounts.end());
}


TEST_CASE("DrawingRegeneration_AnAssemblyViewDeclaresNoEdgeToAnAbsentSource",
          "[drawing][regen][p14]") {
    // A DEFECT THIS MILESTONE FOUND AND FIXED, and the regression that holds
    // it closed.
    //
    // An assembly view names no source (ADR-021), but localTarget() hands back
    // a reference's object whether or not it is valid -- so
    // View::dependencies() pushed ObjectId{0}, and the dependency graph's
    // missing-reference check failed EVERY assembly view with "references
    // object:0, which does not exist". It has done so since P14-ASM-001.
    //
    // Nothing caught it because no drawing test asserted succeeded(): the
    // fixtures required regenerateAll() to RETURN a report, which it does even
    // when objects in it failed.
    Assembly a = twoOfOnePart();
    const drawing::View* view = drawing::findView(a.model.document, a.view);
    REQUIRE(view != nullptr);
    REQUIRE(view->definition().subject == ViewSubject::Assembly);

    for (const ObjectId id : view->dependencies()) {
        INFO("declared dependency " << id.value());
        CHECK(id.isValid());
        CHECK(a.model.document.findObject(id) != nullptr);
    }

    // With NO drawing handlers at all: this is the graph's own check, so the
    // fix has to hold without anything this milestone registers.
    features::Regenerator bare;
    assembly::registerHandlers(bare, nullptr, nullptr);
    auto report = bare.regenerateAll(a.model.document);
    REQUIRE(report.has_value());
    INFO(a.model.explain(*report));
    CHECK_FALSE(contains(report->failed, ObjectId{a.view}));
    CHECK(report->succeeded());
}

TEST_CASE("DrawingRegeneration_AViewOfAnExternalPartFailsRatherThanPassingSilently",
          "[drawing][regen][p14]") {
    // checkView() lets an external source through on purpose -- the owning
    // document may not be available, and opening it behind the caller's back
    // is the implicit filesystem access ADR-003 forbids. It contributes no
    // dependency edge either, so the graph reports nothing missing.
    //
    // View::dependencies() says such a view "is failed by its handler
    // instead". This is that handler, and this is the test that it is.
    Model m;
    const SheetId sheet = addSheet(m);
    const Block block = addBlock(m, "Block");
    m.regenerate();
    const ViewId external = require(drawing::createView(
        m.document, "FromElsewhere",
        ViewDefinition{.sheet = sheet,
                       .source = ObjectReference{DocumentId::fromValue(Uuid::generateV4()),
                                                ObjectId::fromValue(7), "parts/block.bcad"},
                       .orientation = StandardView::Front,
                       .placement = Point2D{200_mm, 150_mm}}));

    // Without the drawing handlers it is silent, as it has always been.
    {
        features::Regenerator bare;
        auto report = bare.regenerateAll(m.document);
        REQUIRE(report.has_value());
        CHECK(*bare.state(ObjectId{external}) == NodeState::UpToDate);
        CHECK(report->succeeded());
    }

    const auto report = m.regenerateAllowingFailure();
    INFO(m.explain(report));
    CHECK(*m.regenerator.state(ObjectId{external}) == NodeState::Failed);
    const Error* error = m.regenerator.error(ObjectId{external});
    REQUIRE(error != nullptr);
    CHECK_THAT(error->message, ContainsSubstring("another document"));
    // The block's own view is untouched by it.
    const ViewId local = addView(m, sheet, "Front", block.pad);
    m.regenerateAllowingFailure();
    CHECK(*m.regenerator.state(ObjectId{local}) == NodeState::Regenerated);
}

TEST_CASE("DrawingRegeneration_ABalloonIsCheckedForBeingInForceNotJustForResolving",
          "[drawing][regen][p14][config]") {
    // Why the annotation prefix asks a second question (ADR-023).
    //
    // An occurrence resolves through its PART, and a configuration does not
    // touch the part -- so the anchor is found perfectly well for a component
    // that this configuration suppresses. draw() catches it one step later, by
    // finding no solved transform, and that step is out of a handler's reach.
    // Asking activeComponents() gets the same answer from the configuration
    // rather than from the solver.
    Assembly a = twoOfOnePart();
    const AnnotationId balloon = addBalloon(a, "B2", a.second, {120_mm, 60_mm});
    const ConfigurationId reduced = require(a.model.document.createConfiguration("Reduced"));
    REQUIRE(assembly::suppressComponent(a.model.document, reduced, a.second, true).has_value());
    REQUIRE(a.model.document.setActiveConfiguration(reduced).has_value());
    a.model.regenerateAllowingFailure();

    // The part still builds, and the component object is still there: nothing
    // a reference could notice has gone.
    CHECK(a.model.regenerator.body(a.part) != nullptr);
    CHECK(a.model.document.findObject(ObjectId{a.second}) != nullptr);
    // And the balloon is reported anyway.
    CHECK(*a.model.regenerator.state(ObjectId{balloon}) == NodeState::Failed);
    const Error* error = a.model.regenerator.error(ObjectId{balloon});
    REQUIRE(error != nullptr);
    CHECK_THAT(error->message, ContainsSubstring("active configuration"));
    // The full resolver agrees, by the other route.
    const auto drawn =
        drawing::draw(a.model.document, balloon, a.model.bodies(), a.model.transforms());
    CHECK_FALSE(drawn.has_value());
}

TEST_CASE("DrawingRegeneration_AddingAnOccurrenceMovesTheBomAndTheBalloonNumbers",
          "[drawing][regen][p14]") {
    // "Update annotations/BOM where required", driven by a MODEL change
    // rather than a configuration one: a third occurrence of a second part is
    // added, and the derived table and the numbers it feeds follow with no
    // invalidation step, because neither is stored (ADR-022).
    Assembly a = twoOfOnePart();
    const AnnotationId balloon = addBalloon(a, "B1", a.first, {40_mm, 60_mm});
    a.model.regenerate();

    auto bom = drawing::billOfMaterials(a.model.document, a.view);
    REQUIRE(bom.has_value());
    REQUIRE(bom->rows.size() == 1);
    CHECK(bom->rows.front().quantity() == 2);
    CHECK(*drawing::itemNumberOf(a.model.document, a.view, a.first) == 1);

    // A SECOND part, placed once. It sorts after the bracket by part id, so
    // the bracket keeps item 1 and the new part takes item 2.
    const Block cover = addBlock(a.model, "Cover", 20_mm, 20_mm, 5_mm);
    const ComponentId third = require(assembly::createComponent(
        a.model.document, "Cover1",
        {.part = cover.pad, .placement = ComponentPlacement{.translation = {0_mm, 0_mm, 60_mm}}}));
    const auto report = a.model.regenerate();
    INFO(a.model.explain(report));
    CHECK(report.succeeded());

    bom = drawing::billOfMaterials(a.model.document, a.view);
    REQUIRE(bom.has_value());
    CHECK(bom->rows.size() == 2);
    CHECK(bom->totalOccurrences() == 3);
    CHECK(*drawing::itemNumberOf(a.model.document, a.view, a.first) == 1);
    CHECK(*drawing::itemNumberOf(a.model.document, a.view, third) == 2);
    // The balloon still names the occurrence it always named, and still
    // regenerates.
    CHECK(drawing::findAnnotation(a.model.document, balloon)->definition().target.object ==
          ObjectId{a.first});
    CHECK(*a.model.regenerator.state(ObjectId{balloon}) == NodeState::Regenerated);
    CHECK(drawing::drawnOccurrences(a.model.document, a.view)->size() == 3);
}

TEST_CASE("DrawingRegeneration_UndoingTheDeletionOfWhatAViewDrawsRestoresIt",
          "[drawing][regen][p14]") {
    // Undo is removeObject followed by insertObject, which puts the object
    // back under its ORIGINAL ObjectId -- so this is recovery, not rebinding:
    // the same target returns rather than a look-alike being adopted.
    Model m;
    const SheetId sheet = addSheet(m);
    const Block block = addBlock(m, "Block");
    m.regenerate();
    const ViewId view = addView(m, sheet, "Front", block.pad);
    const DimensionId width = addWidth(m, view, "Width", block);
    m.regenerate();
    REQUIRE_THAT(widthOf(m, width), WithinAbs(100.0, kMm));

    auto removed = m.document.removeObject(block.pad);
    REQUIRE(removed.has_value());
    m.regenerateAllowingFailure();
    CHECK(*m.regenerator.state(ObjectId{view}) == NodeState::Failed);
    CHECK(*m.regenerator.state(ObjectId{width}) == NodeState::Blocked);
    // Nothing is drawn in the meantime -- not the last good projection.
    CHECK_FALSE(drawing::projectedGeometry(m.document, view, m.bodies(), m.transforms()).has_value());

    REQUIRE(m.document.insertObject(std::move(*removed)).has_value());
    const auto report = m.regenerate();
    INFO(m.explain(report));
    CHECK(report.succeeded());
    CHECK(*m.regenerator.state(ObjectId{view}) == NodeState::Regenerated);
    CHECK(*m.regenerator.state(ObjectId{width}) == NodeState::Regenerated);
    CHECK_THAT(widthOf(m, width), WithinAbs(100.0, kMm));
}

// --- Adversarial: the prefix must not drift from the whole ---------------------------------------

TEST_CASE("DrawingRegeneration_ThePrefixAndTheFullResolverAgreeOnEveryBrokenReference",
          "[drawing][regen][p14][adversarial]") {
    // The one way this design fails quietly: resolveDimensionTargets() and
    // resolveAnnotationTarget() are hand-written prefixes of measure() and
    // draw(), so nothing but this test stops them drifting apart. A handler
    // that passed where the drawing fails would report a good document and
    // print a bad sheet -- which is the silence the milestone closes, wearing
    // a different hat.
    //
    // Agreement is asserted in BOTH directions, over every kind of breakage
    // reachable here, with the same lookups the handler is given.
    Model m;
    const SheetId sheet = addSheet(m);
    const Block block = addBlock(m, "Block");
    m.regenerate();
    const ViewId view = addView(m, sheet, "Front", block.pad);

    const DimensionId good = addWidth(m, view, "Good", block);
    const DimensionId goesAway = require(drawing::createDimension(
        m.document, "GoesAway",
        DimensionDefinition{.view = view,
                            .type = DimensionType::Linear,
                            .from = DimensionTarget{.plane = sideOf(block, 3)},
                            .to = DimensionTarget{.plane = sideOf(block, 2)},
                            .format = drawing::DimensionFormat{.decimals = 2}}));
    const DimensionId incoherent = require(drawing::createDimension(
        m.document, "Incoherent",
        DimensionDefinition{
            .view = view,
            .type = DimensionType::Linear,
            .from = DimensionTarget{.plane = PlaneReference{
                        .object = block.pad, .face = FaceSelector{.role = FaceRole::HoleBottom}}},
            .to = DimensionTarget{.plane = sideOf(block, 3)},
            .format = drawing::DimensionFormat{.decimals = 2}}));

    const auto agreeDimension = [&](DimensionId id, const char* what) {
        INFO(what);
        const auto prefix = drawing::resolveDimensionTargets(m.document, id, m.bodies());
        const auto whole = drawing::measure(m.document, id, m.bodies(), m.transforms());
        // A prefix may pass where the whole fails -- it asks strictly less --
        // but it must NEVER fail where the whole passes.
        if (whole.has_value()) {
            CHECK(prefix.has_value());
        }
        if (!prefix.has_value()) {
            CHECK_FALSE(whole.has_value());
            CHECK(prefix.error().code == whole.error().code);
        }
    };

    cutCornerOff(m, block);
    m.regenerateAllowingFailure();
    agreeDimension(good, "a side face whose sweeping entity was removed");
    agreeDimension(goesAway, "the second removed face");
    agreeDimension(incoherent, "a role an extrude cannot produce");

    // And the same for an annotation, across the kinds that name something.
    Assembly a = twoOfOnePart();
    const AnnotationId balloon = addBalloon(a, "B2", a.second, {120_mm, 60_mm});
    const AnnotationId table = require(drawing::createAnnotation(
        a.model.document, "Bom",
        AnnotationDefinition{.view = a.view,
                             .type = AnnotationType::BomTable,
                             .placement = Point2D{240_mm, 200_mm}}));
    const ConfigurationId reduced = require(a.model.document.createConfiguration("Reduced"));
    REQUIRE(assembly::suppressComponent(a.model.document, reduced, a.second, true).has_value());

    const auto agreeAnnotation = [&](AnnotationId id, const char* what) {
        INFO(what);
        const auto prefix = drawing::resolveAnnotationTarget(a.model.document, id, a.model.bodies());
        const auto whole =
            drawing::draw(a.model.document, id, a.model.bodies(), a.model.transforms());
        if (whole.has_value()) {
            CHECK(prefix.has_value());
        }
        if (!prefix.has_value()) {
            CHECK_FALSE(whole.has_value());
        }
    };
    a.model.regenerate();
    agreeAnnotation(balloon, "a balloon, everything active");
    agreeAnnotation(table, "a BOM table, everything active");

    REQUIRE(a.model.document.setActiveConfiguration(reduced).has_value());
    a.model.regenerateAllowingFailure();
    agreeAnnotation(balloon, "a balloon whose occurrence this configuration suppresses");
    agreeAnnotation(table, "a BOM table with one occurrence left");
}

TEST_CASE("DrawingRegeneration_AnAssemblyDrawingSettlesInsteadOfRebuildingEveryPass",
          "[drawing][regen][p14]") {
    // The other half of the ObjectId{0} defect. A view that the graph reported
    // as having a missing reference was marked changed on EVERY pass, so it
    // was never up to date and never could be. "Regenerate only affected
    // drawing state" is not a thing a document can do while one of its objects
    // is permanently dirty.
    Assembly a = twoOfOnePart();
    const AnnotationId balloon = addBalloon(a, "B1", a.first, {40_mm, 60_mm});
    a.model.regenerate();
    const auto first = a.model.pass();
    INFO(a.model.explain(first));
    CHECK(first.succeeded());

    const auto quiet = a.model.pass();
    INFO(a.model.explain(quiet));
    CHECK(quiet.succeeded());
    CHECK_FALSE(contains(quiet.regenerated, ObjectId{a.view}));
    CHECK_FALSE(contains(quiet.regenerated, ObjectId{balloon}));
    CHECK(quiet.changed.empty());
}

// --- Persistence ------------------------------------------------------------------------------------

TEST_CASE("DrawingRegeneration_ADrawingRegeneratesTheSameWayAfterSaveAndLoad",
          "[drawing][regen][p14][persistence]") {
    TempDir dir;
    const auto path = dir.path() / "sheet.bcad";

    std::string measuredBefore;
    {
        Model m;
        const SheetId sheet = addSheet(m);
        const Block block = addBlock(m, "Block");
        m.regenerate();
        const ViewId view = addView(m, sheet, "Front", block.pad);
        const DimensionId width = addWidth(m, view, "Width", block);
        widenTo(m, block, 123.25_mm);
        m.regenerate();
        const auto measured = drawing::measure(m.document, width, m.bodies(), m.transforms());
        REQUIRE(measured.has_value());
        measuredBefore = measured->text;
        REQUIRE(io::saveDocument(m.document, path).has_value());
    }

    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    Model m;
    m.document = std::move(*loaded);
    const auto report = m.regenerate();
    CHECK(report.succeeded());

    const auto ids = drawing::dimensions(m.document);
    REQUIRE(ids.size() == 1);
    const auto measured = drawing::measure(m.document, ids.front(), m.bodies(), m.transforms());
    REQUIRE(measured.has_value());
    CHECK(measured->text == measuredBefore);
    CHECK(*m.regenerator.state(ObjectId{ids.front()}) == NodeState::Regenerated);
}
