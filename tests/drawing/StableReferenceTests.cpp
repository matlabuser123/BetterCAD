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
#include <bettercad/drawing/Resolution.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/drawing/Views.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/FeatureCommands.hpp>
#include <bettercad/features/Datums.hpp>
#include <cmath>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
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
using drawing::DimensionDefinition;
using drawing::DimensionTarget;
using drawing::DimensionType;
using drawing::DrawingScale;
using drawing::Resolution;
using drawing::ResolutionState;
using drawing::SheetDefinition;
using drawing::StandardView;
using drawing::ViewDefinition;
using drawing::ViewSubject;

// P14-STREF-001: what a drawing references, and what happens when it moves.
//
// THE ONE RULE. A drawing references SEMANTIC MODEL IDENTITY -- a feature and
// the role of a face in it, a datum, a component occurrence -- and never a
// transient position: no kernel face index, no vector offset, no traversal
// order, no address, no "the nearest one".
//
// THE THREE STATES, and the distinction the whole milestone turns on:
//
//     Resolved     the target is there and the reference found it
//     Unresolved   the reference is well formed and its target is not there
//                  NOW. The intent is kept, and it resolves again if the
//                  target returns
//     Invalid      the reference itself is incoherent
//
// RECOVERY IS REQUIRED; REBINDING IS FORBIDDEN. They look the same from the
// outside -- a reference that was Unresolved is Resolved again -- and they are
// opposites. Recovery is the SAME target coming back. Rebinding is a
// DIFFERENT target being adopted because it resembles the old one. Every
// no-rebind fixture below is built so that a rebinding implementation would
// succeed: there is always an identical survivor sitting there to be grabbed.
namespace {

constexpr double kMm = 1e-9;

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
    void regenerate() {
        assembly::registerHandlers(regenerator, nullptr, nullptr);
        REQUIRE(regenerator.regenerateAll(document).has_value());
    }
    /// Regenerates without requiring success: used where the edit under test
    /// is meant to break something.
    void regenerateAllowingFailure() {
        assembly::registerHandlers(regenerator, nullptr, nullptr);
        (void)regenerator.regenerateAll(document);
    }
};

SheetId addSheet(Model& m, DrawingScale scale = DrawingScale{1, 1}) {
    return require(drawing::createSheet(
        m.document, "Sheet1",
        SheetDefinition{.format = drawing::SheetFormat::A3,
                        .orientation = drawing::SheetOrientation::Landscape,
                        .margins = drawing::SheetMargins{10_mm, 10_mm, 10_mm, 10_mm},
                        .scale = scale}));
}

/// A 100 x 60 x 40 block, with its four profile entities kept so a side face
/// can be named by the entity that sweeps it.
struct Block {
    ObjectId sketch{};
    ObjectId pad{};
    std::array<EntityId, 4> lines{};
    /// Kept so a parametric edit can move the profile itself, which is what
    /// moves a SIDE face without changing which entity sweeps it.
    std::array<EntityId, 4> corners{};
};

Block addBlock(Model& m, const std::string& name, Length width = 100_mm,
               Length depth = 60_mm, Length height = 40_mm) {
    Block block;
    auto sketch = std::make_unique<sketch::Sketch>(name + "Profile", Frame3D::xy());
    block.corners = {
        require(sketch->addPoint(Point2D{0_mm, 0_mm})),
        require(sketch->addPoint(Point2D{width, 0_mm})),
        require(sketch->addPoint(Point2D{width, depth})),
        require(sketch->addPoint(Point2D{0_mm, depth})),
    };
    for (std::size_t i = 0; i < 4; ++i) {
        block.lines[i] =
            require(sketch->addLine(block.corners[i], block.corners[(i + 1) % 4]));
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
                          .face = FaceSelector{.role = FaceRole::Side,
                                               .entity = block.lines[entity]}};
}

PlaneReference capOf(const Block& block, FaceRole role) {
    return PlaneReference{.object = block.pad, .face = FaceSelector{.role = role}};
}

ViewId addView(Model& m, SheetId sheet, const std::string& name, ObjectId source,
               StandardView orientation = StandardView::Front) {
    return require(drawing::createView(m.document, name,
                                       ViewDefinition{.sheet = sheet,
                                                      .source = ObjectReference{source},
                                                      .orientation = orientation,
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

Resolution stateOf(Model& m, DimensionId id) {
    return drawing::dimensionResolution(m.document, id, m.bodies(), m.transforms());
}
Resolution stateOf(Model& m, AnnotationId id) {
    return drawing::annotationResolution(m.document, id, m.bodies(), m.transforms());
}
Resolution stateOf(Model& m, ViewId id) {
    return drawing::viewResolution(m.document, id, m.bodies(), m.transforms());
}

} // namespace

// --- The resolution contract ------------------------------------------------------------------

TEST_CASE("Reference_StatesAreResolvedUnresolvedOrInvalid", "[drawing][stref][p14]") {
    CHECK(drawing::toString(ResolutionState::Resolved) == "resolved");
    CHECK(drawing::toString(ResolutionState::Unresolved) == "unresolved");
    CHECK(drawing::toString(ResolutionState::Invalid) == "invalid");

    Model m;
    const SheetId sheet = addSheet(m);
    const Block block = addBlock(m, "Block");
    m.regenerate();
    const ViewId view = addView(m, sheet, "Front", block.pad);
    const DimensionId width = addWidth(m, view, "Width", block);

    // Resolved carries no diagnostic: there is nothing to explain.
    const Resolution good = stateOf(m, width);
    CHECK(good.state == ResolutionState::Resolved);
    CHECK(good.resolved());
    CHECK(good.diagnostic.empty());

    // INVALID: a role an extrude can never produce. This is not "the face is
    // not there just now" -- an extrude has no hole bottom in any
    // configuration, so the reference is incoherent rather than waiting.
    const DimensionId impossible = require(drawing::createDimension(
        m.document, "Impossible",
        DimensionDefinition{.view = view,
                            .type = DimensionType::Linear,
                            .from = DimensionTarget{.plane = capOf(block, FaceRole::StartCap)},
                            .to = DimensionTarget{.plane = PlaneReference{
                                .object = block.pad,
                                .face = FaceSelector{.role = FaceRole::HoleBottom}}}}));
    const Resolution incoherent = stateOf(m, impossible);
    CHECK(incoherent.state == ResolutionState::Invalid);
    CHECK_FALSE(incoherent.resolved());
    CHECK_FALSE(incoherent.diagnostic.empty()); // it says why, in the resolver's own words

    // UNRESOLVED: a reference that was right and whose target is not here
    // now. The difference matters, because only this one is worth keeping and
    // only this one can recover.
    const Block other = addBlock(m, "Other", 20_mm, 20_mm, 20_mm);
    m.regenerate();
    const DimensionId onOther = addWidth(m, view, "OnOther", other);
    REQUIRE(stateOf(m, onOther).resolved());
    REQUIRE(m.document.removeObject(other.pad).has_value());
    m.regenerateAllowingFailure();
    const Resolution absent = stateOf(m, onOther);
    CHECK(absent.state == ResolutionState::Unresolved);
    CHECK_FALSE(absent.diagnostic.empty());
}

// --- View -> model ----------------------------------------------------------------------------

TEST_CASE("Reference_AViewKeepsItsSourceThroughRegenerationAndUnrelatedEdits",
          "[drawing][stref][p14]") {
    Model m;
    const SheetId sheet = addSheet(m);
    const Block block = addBlock(m, "Block");
    m.regenerate();
    const ViewId view = addView(m, sheet, "Front", block.pad);
    REQUIRE(stateOf(m, view).resolved());
    const ObjectReference before = *drawing::effectiveSource(m.document, view);

    // Regenerate, then add and remove unrelated objects. None of it is the
    // view's business, and none of it may change what the view draws.
    m.regenerate();
    const Block other = addBlock(m, "Other", 10_mm, 10_mm, 10_mm);
    m.regenerate();
    REQUIRE(m.document.removeObject(other.pad).has_value());
    REQUIRE(m.document.removeObject(other.sketch).has_value());
    m.regenerate();

    CHECK(sameTarget(*drawing::effectiveSource(m.document, view), before));
    CHECK(drawing::effectiveSource(m.document, view)->object == block.pad);
    CHECK(stateOf(m, view).resolved());
}

TEST_CASE("Reference_AViewWhoseSourceIsGoneIsUnresolvedAndTakesNoOtherSource",
          "[drawing][stref][p14]") {
    // Two blocks of identical size. The view names the first; the first goes.
    // A view that looked for "something like what I drew" would find the
    // second sitting there.
    Model m;
    const SheetId sheet = addSheet(m);
    const Block first = addBlock(m, "First");
    const Block second = addBlock(m, "Second");
    m.regenerate();
    const ViewId view = addView(m, sheet, "Front", first.pad);
    REQUIRE(stateOf(m, view).resolved());

    REQUIRE(m.document.removeObject(first.pad).has_value());
    m.regenerateAllowingFailure();

    const Resolution after = stateOf(m, view);
    CHECK(after.state == ResolutionState::Unresolved);
    // The reference intent is KEPT: it still names what it always named.
    CHECK(drawing::effectiveSource(m.document, view)->object == first.pad);
    CHECK_FALSE(drawing::effectiveSource(m.document, view)->object == second.pad);
}

TEST_CASE("Reference_ADerivedViewKeepsItsParentAndTakesNoOther",
          "[drawing][stref][p14]") {
    Model m;
    const SheetId sheet = addSheet(m);
    const Block block = addBlock(m, "Block");
    m.regenerate();
    const ViewId parent = addView(m, sheet, "Front", block.pad);
    const ViewId spare = addView(m, sheet, "Spare", block.pad, StandardView::Top);
    const ViewId child = require(drawing::createView(
        m.document, "FromTheRight",
        ViewDefinition{.kind = drawing::ViewKind::Projected,
                       .sheet = sheet,
                       .parent = parent,
                       .direction = drawing::ProjectedDirection::Right,
                       .spacing = 150_mm}));
    REQUIRE(stateOf(m, child).resolved());

    // The contract here is STRONGER than "becomes unresolved": a view cannot
    // be removed while another is projected from it, so a child's reference
    // to its parent cannot be orphaned in the first place.
    const auto refused = drawing::removeView(m.document, parent);
    REQUIRE_FALSE(refused.has_value());
    CHECK(errorCode(refused) == ErrorCode::FailedPrecondition);
    CHECK_THAT(refused.error().message, ContainsSubstring("is projected from it"));

    // Everything is where it was: the removal changed nothing, and the child
    // was certainly not re-parented onto the other view sitting there.
    CHECK(drawing::findView(m.document, parent) != nullptr);
    CHECK(drawing::findView(m.document, child)->definition().parent == parent);
    CHECK_FALSE(drawing::findView(m.document, child)->definition().parent == spare);
    CHECK(stateOf(m, child).resolved());

    // Removed in the right order -- child first -- it goes without complaint,
    // so the refusal above is about the dependency and not about the view.
    REQUIRE(drawing::removeView(m.document, child).has_value());
    CHECK(drawing::removeView(m.document, parent).has_value());
}

// --- Dimension -> geometry --------------------------------------------------------------------

TEST_CASE("Reference_ADimensionFollowsItsFaceThroughAParametricEdit",
          "[drawing][stref][p14]") {
    // The face is named by the profile entity that sweeps it, so widening the
    // block moves the face and the dimension follows it -- the same face, a
    // new number.
    Model m;
    const SheetId sheet = addSheet(m);
    const Block block = addBlock(m, "Block");
    m.regenerate();
    const ViewId view = addView(m, sheet, "Front", block.pad);
    const DimensionId width = addWidth(m, view, "Width", block);
    CHECK(drawing::measure(m.document, width, m.bodies())->text == "100.00");

    // Move the profile's right-hand edge out to 120. The face keeps the
    // entity that sweeps it, so the reference is untouched and the number
    // changes.
    REQUIRE(m.document
                .modifyObject<sketch::Sketch>(
                    block.sketch,
                    [&](sketch::Sketch& s) {
                        REQUIRE(s.setPointPosition(block.corners[1], Point2D{120_mm, 0_mm})
                                    .has_value());
                        REQUIRE(s.setPointPosition(block.corners[2], Point2D{120_mm, 60_mm})
                                    .has_value());
                        return true;
                    })
                .has_value());
    m.regenerate();

    const Resolution after = stateOf(m, width);
    CHECK(after.resolved());
    CHECK(drawing::measure(m.document, width, m.bodies())->text == "120.00");
    // The stored intent did not change: the same entity names the same face.
    CHECK(drawing::findDimension(m.document, width)->definition().to.plane->face->entity ==
          block.lines[1]);
}

TEST_CASE("Reference_ADimensionNeverMovesToAnIdenticalFaceOfAnotherFeature",
          "[drawing][stref][p14][rebind]") {
    // THE HARD GATE. Two blocks, identical in every dimension, side by side.
    // A dimension measures the first block's two side faces. The first block
    // is removed. There is an identical pair of faces still in the document,
    // at the same size, of the same role, swept by entities of a sketch built
    // the same way -- everything a resemblance-matching implementation could
    // want.
    Model m;
    const SheetId sheet = addSheet(m);
    const Block first = addBlock(m, "First");
    const Block second = addBlock(m, "Second");
    m.regenerate();
    const ViewId view = addView(m, sheet, "Front", second.pad);
    const DimensionId width = addWidth(m, view, "Width", first);
    REQUIRE(stateOf(m, width).resolved());

    REQUIRE(m.document.removeObject(first.pad).has_value());
    m.regenerateAllowingFailure();

    const Resolution after = stateOf(m, width);
    CHECK(after.state == ResolutionState::Unresolved);
    // The survivor is still there and still measurable, which is what makes
    // the line above a statement about rebinding rather than about the model.
    const DimensionId onSurvivor = addWidth(m, view, "Survivor", second);
    CHECK(stateOf(m, onSurvivor).resolved());
    // And the broken one still names what it always named.
    CHECK(drawing::findDimension(m.document, width)->definition().from.plane->object == first.pad);
}

// --- Annotation -> model ----------------------------------------------------------------------

TEST_CASE("Reference_AHoleCalloutFollowsItsHoleThroughADiameterChange",
          "[drawing][stref][p14]") {
    Model m;
    const SheetId sheet = addSheet(m);
    const Block block = addBlock(m, "Plate", 100_mm, 60_mm, 20_mm);
    auto hole = features::HoleFeature::create(
        "Hole", {.target = FeatureId::fromValue(block.pad.value()),
                 .face = geometry::planeSignature(Point3D{0_mm, 0_mm, 20_mm}, Direction3D::unitZ()),
                 .center = Point2D{50_mm, 30_mm},
                 .diameter = 10_mm});
    REQUIRE(hole.has_value());
    const ObjectId holeId = require(m.document.addObject(std::move(*hole)));
    m.regenerate();

    const ViewId view = addView(m, sheet, "Top", holeId, StandardView::Top);
    const AnnotationId callout = require(drawing::createAnnotation(
        m.document, "Callout",
        AnnotationDefinition{.view = view,
                             .type = AnnotationType::HoleCallout,
                             .target = AnnotationTarget{.object = holeId},
                             .placement = Point2D{300_mm, 200_mm}}));
    CHECK(*drawing::annotationText(m.document, callout, m.bodies()) == "Ø10 THRU");

    // Ø10 -> Ø12. Same target identity, new text.
    const auto* feature = m.document.findObjectAs<features::HoleFeature>(holeId);
    REQUIRE(feature != nullptr);
    auto definition = feature->definition();
    definition.diameter = 12_mm;
    REQUIRE(m.document
                .modifyObject<features::HoleFeature>(
                    holeId, [&](features::HoleFeature& h) { return h.setDefinition(definition); })
                .has_value());
    m.regenerate();

    CHECK(stateOf(m, callout).resolved());
    CHECK(*drawing::annotationText(m.document, callout, m.bodies()) == "Ø12 THRU");
    CHECK(drawing::findAnnotation(m.document, callout)->definition().target.object == holeId);
}

TEST_CASE("Reference_ACalloutNeverMovesToAnIdenticalHole",
          "[drawing][stref][p14][rebind]") {
    // Two identical holes in one plate. The callout names the first; the
    // first goes. The second is the same diameter, the same depth, in the
    // same face.
    Model m;
    const SheetId sheet = addSheet(m);
    const Block block = addBlock(m, "Plate", 100_mm, 60_mm, 20_mm);
    const auto top = geometry::planeSignature(Point3D{0_mm, 0_mm, 20_mm}, Direction3D::unitZ());
    auto first = features::HoleFeature::create(
        "HoleA", {.target = FeatureId::fromValue(block.pad.value()),
                  .face = top,
                  .center = Point2D{20_mm, 30_mm},
                  .diameter = 10_mm});
    REQUIRE(first.has_value());
    const ObjectId a = require(m.document.addObject(std::move(*first)));
    auto second = features::HoleFeature::create(
        "HoleB", {.target = FeatureId::fromValue(a.value()),
                  .face = top,
                  .center = Point2D{80_mm, 30_mm},
                  .diameter = 10_mm});
    REQUIRE(second.has_value());
    const ObjectId b = require(m.document.addObject(std::move(*second)));
    m.regenerate();

    const ViewId view = addView(m, sheet, "Top", b, StandardView::Top);
    const AnnotationId callout = require(drawing::createAnnotation(
        m.document, "Callout",
        AnnotationDefinition{.view = view,
                             .type = AnnotationType::HoleCallout,
                             .target = AnnotationTarget{.object = a},
                             .placement = Point2D{300_mm, 200_mm}}));
    REQUIRE(stateOf(m, callout).resolved());

    // Re-point B at the plate so A can go, leaving an identical survivor.
    const auto* holeB = m.document.findObjectAs<features::HoleFeature>(b);
    REQUIRE(holeB != nullptr);
    auto definition = holeB->definition();
    definition.target = FeatureId::fromValue(block.pad.value());
    REQUIRE(m.document
                .modifyObject<features::HoleFeature>(
                    b, [&](features::HoleFeature& h) { return h.setDefinition(definition); })
                .has_value());
    REQUIRE(m.document.removeObject(a).has_value());
    m.regenerate();

    // The survivor is there and drawable...
    REQUIRE(m.document.findObjectAs<features::HoleFeature>(b) != nullptr);
    // ...and the callout is unresolved rather than labelling it.
    CHECK(stateOf(m, callout).state == ResolutionState::Unresolved);
    CHECK(drawing::findAnnotation(m.document, callout)->definition().target.object == a);
}

// --- Balloon -> occurrence --------------------------------------------------------------------

namespace {

struct Assembly {
    Model model;
    SheetId sheet{};
    ViewId view{};
    ObjectId part{};
    ComponentId first{};
    ComponentId second{};
};

/// One part, placed twice. The two occurrences are identical in every way a
/// resemblance test could see.
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
    a.view = require(drawing::createView(
        a.model.document, "MainView",
        ViewDefinition{.sheet = a.sheet,
                       .subject = ViewSubject::Assembly,
                       .orientation = StandardView::Front,
                       .placement = Point2D{200_mm, 150_mm}}));
    a.model.regenerate();
    return a;
}

AnnotationId addBalloon(Assembly& a, const std::string& name, ComponentId occurrence,
                        Point2D at) {
    return require(drawing::createAnnotation(
        a.model.document, name,
        AnnotationDefinition{.view = a.view,
                             .type = AnnotationType::Balloon,
                             .target = AnnotationTarget{.object = ObjectId{occurrence}},
                             .placement = at}));
}

} // namespace

TEST_CASE("Reference_BalloonsOnTwoInstancesOfOnePartAreTwoReferences",
          "[drawing][stref][p14]") {
    Assembly a = twoOfOnePart();
    const AnnotationId onFirst = addBalloon(a, "B1", a.first, {40_mm, 60_mm});
    const AnnotationId onSecond = addBalloon(a, "B2", a.second, {120_mm, 60_mm});

    // The same BOM row and item number...
    CHECK(*drawing::itemNumberOf(a.model.document, a.view, a.first) ==
          *drawing::itemNumberOf(a.model.document, a.view, a.second));
    // ...and two different stored targets. A balloon that had degraded to the
    // PART would have one target here, not two.
    const AnnotationDefinition& one = drawing::findAnnotation(a.model.document, onFirst)->definition();
    const AnnotationDefinition& two = drawing::findAnnotation(a.model.document, onSecond)->definition();
    CHECK(one.target.object == ObjectId{a.first});
    CHECK(two.target.object == ObjectId{a.second});
    CHECK_FALSE(one.target.object == two.target.object);
    CHECK_FALSE(one.target.object == a.part);
    CHECK(stateOf(a.model, onFirst).resolved());
    CHECK(stateOf(a.model, onSecond).resolved());
}

TEST_CASE("Reference_ABalloonRecoversTheSameOccurrenceAndNeverTheOther",
          "[drawing][stref][p14][rebind]") {
    // Resolved -> Unresolved -> Resolved, with the SAME occurrence at the end
    // and an identical sibling available throughout for a rebinding
    // implementation to take instead.
    Assembly a = twoOfOnePart();
    const AnnotationId balloon = addBalloon(a, "B2", a.second, {120_mm, 60_mm});
    const ConfigurationId reduced = require(a.model.document.createConfiguration("Reduced"));
    REQUIRE(assembly::suppressComponent(a.model.document, reduced, a.second, true).has_value());

    CHECK(stateOf(a.model, balloon).state == ResolutionState::Resolved);

    REQUIRE(a.model.document.setActiveConfiguration(reduced).has_value());
    a.model.regenerate();
    CHECK(stateOf(a.model, balloon).state == ResolutionState::Unresolved);
    // The sibling is still active, still the same part, still item 1.
    REQUIRE(drawing::drawnOccurrences(a.model.document, a.view)->size() == 1);
    CHECK(*drawing::itemNumberOf(a.model.document, a.view, a.first) == 1);
    // The reference intent is untouched.
    CHECK(drawing::findAnnotation(a.model.document, balloon)->definition().target.object ==
          ObjectId{a.second});

    REQUIRE(a.model.document.setActiveConfiguration(std::nullopt).has_value());
    a.model.regenerate();
    CHECK(stateOf(a.model, balloon).state == ResolutionState::Resolved);
    CHECK(drawing::findAnnotation(a.model.document, balloon)->definition().target.object ==
          ObjectId{a.second});
}

// --- Recovery vs rebinding, stated as a pair ---------------------------------------------------

TEST_CASE("Reference_RecoveryRestoresTheSameTargetAndRebindingIsRefused",
          "[drawing][stref][p14][rebind]") {
    // The two halves of the distinction, in one fixture, so the difference is
    // impossible to read as one behaviour.
    Assembly a = twoOfOnePart();
    const AnnotationId balloon = addBalloon(a, "B2", a.second, {120_mm, 60_mm});
    const ConfigurationId reduced = require(a.model.document.createConfiguration("Reduced"));
    REQUIRE(assembly::suppressComponent(a.model.document, reduced, a.second, true).has_value());

    // RECOVERY: the same target goes and comes back.
    REQUIRE(a.model.document.setActiveConfiguration(reduced).has_value());
    a.model.regenerate();
    REQUIRE(stateOf(a.model, balloon).state == ResolutionState::Unresolved);
    REQUIRE(a.model.document.setActiveConfiguration(std::nullopt).has_value());
    a.model.regenerate();
    CHECK(stateOf(a.model, balloon).state == ResolutionState::Resolved);

    // REBINDING: the target goes for good, and an identical one remains.
    REQUIRE(assembly::removeComponent(a.model.document, a.second).has_value());
    a.model.regenerate();
    CHECK(stateOf(a.model, balloon).state == ResolutionState::Unresolved);
    CHECK(drawing::drawnOccurrences(a.model.document, a.view)->size() == 1); // the sibling is there
    CHECK(drawing::findAnnotation(a.model.document, balloon)->definition().target.object ==
          ObjectId{a.second}); // and the reference did not move to it
}

// --- Configuration switching -------------------------------------------------------------------

TEST_CASE("Reference_ADimensionRecoversWhenItsTargetReturns",
          "[drawing][stref][p14]") {
    // A DIMENSION's target is a feature, and P13's configuration suppression
    // applies to components and mates rather than features -- so the way a
    // dimension's target legitimately goes and comes back is the undo path:
    // removeObject hands the object back, and insertObject puts it in again
    // WITH ITS ORIGINAL ID, which is what undo does.
    //
    // The point of the test is the identity: the reference is not repaired,
    // rewritten or recreated. The same target returns and the same reference
    // finds it.
    Model m;
    const SheetId sheet = addSheet(m);
    const Block block = addBlock(m, "Block");
    m.regenerate();
    const ViewId view = addView(m, sheet, "Front", block.pad);
    const DimensionId width = addWidth(m, view, "Width", block);

    const auto valueNow = [&]() {
        return drawing::measure(m.document, width, m.bodies(), m.transforms());
    };
    REQUIRE(valueNow().has_value());
    const std::string before = valueNow()->text;
    const DimensionDefinition intent = drawing::findDimension(m.document, width)->definition();

    auto removed = m.document.removeObject(block.pad);
    REQUIRE(removed.has_value());
    m.regenerateAllowingFailure();

    // No value at all -- not the old one still presented as current.
    CHECK_FALSE(valueNow().has_value());
    CHECK(stateOf(m, width).state == ResolutionState::Unresolved);
    // And the intent is untouched while it waits.
    CHECK(drawing::findDimension(m.document, width)->definition() == intent);

    REQUIRE(m.document.insertObject(std::move(*removed)).has_value());
    m.regenerate();

    CHECK(stateOf(m, width).resolved());
    REQUIRE(valueNow().has_value());
    CHECK(valueNow()->text == before);
    CHECK(drawing::findDimension(m.document, width)->definition() == intent);
}

// --- Save and load ------------------------------------------------------------------------------

TEST_CASE("Reference_EveryReferenceClassRoundTripsAsItself",
          "[drawing][stref][p14][persistence]") {
    Assembly a = twoOfOnePart();
    const AnnotationId balloon = addBalloon(a, "B1", a.first, {40_mm, 60_mm});
    // A dimension names a FEATURE's faces. (A component is not a feature and
    // has none of its own: an assembly dimension measures the part it places,
    // which is the part this occurrence places.)
    const DimensionId across = require(drawing::createDimension(
        a.model.document, "Across",
        DimensionDefinition{.view = a.view,
                            .type = DimensionType::Linear,
                            .from = DimensionTarget{.plane = PlaneReference{
                                .object = a.part,
                                .face = FaceSelector{.role = FaceRole::StartCap}}},
                            .to = DimensionTarget{.plane = PlaneReference{
                                .object = a.part,
                                .face = FaceSelector{.role = FaceRole::EndCap}}}}));
    const ViewId child = require(drawing::createView(
        a.model.document, "FromTheRight",
        ViewDefinition{.kind = drawing::ViewKind::Projected,
                       .sheet = a.sheet,
                       .parent = a.view,
                       .direction = drawing::ProjectedDirection::Right,
                       .spacing = 150_mm}));

    const TempDir directory;
    const std::filesystem::path path = directory.path() / "references.bcad";
    REQUIRE(io::saveDocument(a.model.document, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());

    // deserialize(serialize(intent)) == intent, for every class.
    CHECK(drawing::findView(*loaded, a.view)->definition() ==
          drawing::findView(a.model.document, a.view)->definition());
    CHECK(drawing::findView(*loaded, child)->definition() ==
          drawing::findView(a.model.document, child)->definition());
    CHECK(drawing::findDimension(*loaded, across)->definition() ==
          drawing::findDimension(a.model.document, across)->definition());
    CHECK(drawing::findAnnotation(*loaded, balloon)->definition() ==
          drawing::findAnnotation(a.model.document, balloon)->definition());

    // And they resolve the same way after a fresh regeneration.
    features::Regenerator again;
    assembly::registerHandlers(again, nullptr, nullptr);
    REQUIRE(again.regenerateAll(*loaded).has_value());
    const features::Regenerator* r = &again;
    const drawing::BodyLookup bodies = [r](ObjectId object) { return r->body(object); };
    const drawing::TransformLookup transforms = [r](ComponentId c) { return r->transform(c); };

    CHECK(drawing::dimensionResolution(*loaded, across, bodies, transforms).resolved());
    CHECK(drawing::annotationResolution(*loaded, balloon, bodies, transforms).resolved());
    CHECK(drawing::viewResolution(*loaded, child, bodies, transforms).resolved());
}

TEST_CASE("Reference_NoPersistedReferenceCarriesAnIndexIntoTheKernel",
          "[drawing][stref][p14][persistence]") {
    // The schema audit. A document carrying every drawing reference class is
    // saved and read as text: nothing in it may name a kernel face, an edge
    // ordinal, an address or a position in a traversal.
    Assembly a = twoOfOnePart();
    (void)addBalloon(a, "B1", a.first, {40_mm, 60_mm});
    const Block plate = addBlock(a.model, "Plate", 100_mm, 60_mm, 20_mm);
    a.model.regenerate();
    const ViewId partView = addView(a.model, a.sheet, "PlateView", plate.pad);
    (void)addWidth(a.model, partView, "Width", plate);
    (void)require(drawing::createAnnotation(
        a.model.document, "Datum",
        AnnotationDefinition{.view = partView,
                             .type = AnnotationType::Datum,
                             .target = AnnotationTarget{.plane = sideOf(plate, 0)},
                             .text = "A",
                             .placement = Point2D{60_mm, 40_mm}}));

    const TempDir directory;
    const std::filesystem::path path = directory.path() / "schema.bcad";
    REQUIRE(io::saveDocument(a.model.document, path).has_value());
    const std::string text = readFile(path);

    for (const std::string_view forbidden :
         {"face_index", "faceIndex", "edge_index", "edgeIndex", "shape_index", "shapeIndex",
          "topology", "ordinal", "pointer", "address", "handle", "traversal"}) {
        INFO(forbidden);
        CHECK_THAT(text, !ContainsSubstring(std::string{forbidden}));
    }

    // What IS there: semantic identity. A side face names the sketch entity
    // that sweeps it, and a balloon names an object.
    CHECK_THAT(text, ContainsSubstring(R"("role": "side")"));
    CHECK_THAT(text, ContainsSubstring(R"("entity":)"));
    CHECK_THAT(text, ContainsSubstring(R"("type": "balloon")"));

    // THIS FIXTURE HAS NO CHAMFER, AND THAT IS THE POINT OF THE TEST BELOW.
    // The companion covers the one reference class this one cannot: a chamfer
    // face. See Reference_NoChamferReferenceIsStoredAsAPositionInTheFile.
    CHECK_THAT(text, !ContainsSubstring(R"("role": "chamfer")"));
}


// --- Determinism ---------------------------------------------------------------------------------

TEST_CASE("Reference_ResolvesToTheSameStateAndTargetEveryTime",
          "[drawing][stref][p14][determinism]") {
    Assembly a = twoOfOnePart();
    const AnnotationId balloon = addBalloon(a, "B1", a.first, {40_mm, 60_mm});
    const Block plate = addBlock(a.model, "Plate", 100_mm, 60_mm, 20_mm);
    a.model.regenerate();
    const ViewId partView = addView(a.model, a.sheet, "PlateView", plate.pad);
    const DimensionId width = addWidth(a.model, partView, "Width", plate);
    // An incoherent reference: a role an extrude cannot produce. Its STATE
    // and its DIAGNOSTIC must be as stable as a resolved one's.
    const DimensionId broken = require(drawing::createDimension(
        a.model.document, "Broken",
        DimensionDefinition{.view = partView,
                            .type = DimensionType::Linear,
                            .from = DimensionTarget{.plane = capOf(plate, FaceRole::StartCap)},
                            .to = DimensionTarget{.plane = PlaneReference{
                                .object = plate.pad,
                                .face = FaceSelector{.role = FaceRole::HoleBottom}}}}));

    const Resolution firstBalloon = stateOf(a.model, balloon);
    const Resolution firstWidth = stateOf(a.model, width);
    const Resolution firstBroken = stateOf(a.model, broken);
    REQUIRE(firstBroken.state == ResolutionState::Invalid);

    for (int i = 0; i < 6; ++i) {
        INFO(i);
        // The state, the target and the DIAGNOSTIC are all identical: a
        // message that varied would mean the resolver took a different path.
        CHECK(stateOf(a.model, balloon) == firstBalloon);
        CHECK(stateOf(a.model, width) == firstWidth);
        CHECK(stateOf(a.model, broken) == firstBroken);
        CHECK(*drawing::itemNumberOf(a.model.document, a.view, a.first) == 1);
    }
}


// --- The audit's one finding: a chamfer face is named by LIST POSITION -------------------------

namespace {

/// Space, tab, CR and LF: the whitespace a JSON writer may put between a
/// key and its value.
constexpr std::string_view kSpace = " \t\r\n";

/// The diagnostic of a failed result, or "" when it succeeded. Safe to call
/// either way, so an INFO() never has to guess.
template <typename T>
std::string whyNot(const Result<T>& result) {
    return result.has_value() ? std::string{} : result.error().message;
}

/// A block with two chamfered edges at opposite sides of its top face, and the
/// chamfer's selections in a known order.
///
/// The two edges are 60 mm apart along Y, so the face a reference resolves to
/// says which selection it found without any tolerance argument: the front
/// chamfer's face lies toward y = 0 and the back one's toward y = 60.
struct Chamfered {
    ObjectId feature{};
    ChamferEdgeId front{};
    ChamferEdgeId back{};
};

geometry::EdgeSignature topEdgeAt(double yMm) {
    return geometry::lineSignature(Point3D{0_mm, yMm * units::mm, 40_mm}, Direction3D::unitX());
}

Chamfered addChamfer(Model& m, const Block& block) {
    auto chamfer = features::ChamferFeature::create(
        "Edges", {.target = FeatureId::fromValue(block.pad.value()),
                  .edges = { features::ChamferEdge{topEdgeAt(0.0)}, features::ChamferEdge{topEdgeAt(60.0)}},
                  .distance = 5_mm});
    REQUIRE(chamfer.has_value());
    // The ids the feature allocated, read back rather than assumed to be 1
    // and 2: what the test asserts is that a reference to an id keeps meaning
    // one selection, not that ids happen to start at 1.
    const auto& edges = (*chamfer)->definition().edges;
    REQUIRE(edges.size() == 2);
    Chamfered result{.feature = {}, .front = edges[0].id, .back = edges[1].id};
    REQUIRE(result.front.isValid());
    REQUIRE(result.back.isValid());
    REQUIRE(result.front != result.back);
    result.feature = require(m.document.addObject(std::move(*chamfer)));
    m.regenerate();
    return result;
}

PlaneReference chamferFace(const Chamfered& c, ChamferEdgeId edge) {
    return PlaneReference{.object = c.feature,
                          .face = FaceSelector{.role = FaceRole::Chamfer, .edge = edge}};
}

features::ChamferDefinition definitionOf(Model& m, ObjectId feature) {
    const auto* found = m.document.findObjectAs<features::ChamferFeature>(feature);
    REQUIRE(found != nullptr);
    return found->definition();
}

/// Applies @p definition through the production API and regenerates.
Result<bool> reshape(Model& m, ObjectId feature, const features::ChamferDefinition& definition) {
    auto changed = m.document.modifyObject<features::ChamferFeature>(
        feature, [&](features::ChamferFeature& c) { return c.setDefinition(definition); });
    if (changed) {
        m.regenerate();
    }
    return changed;
}

/// Where the face a reference names sits along Y, in mm.
double faceYMm(Model& m, const PlaneReference& reference) {
    auto plane = features::resolvePlane(m.document, reference, m.bodies());
    INFO(whyNot(plane));
    REQUIRE(plane.has_value());
    return plane->origin().y.in(units::mm);
}

} // namespace

TEST_CASE("Reference_AChamferFaceKeepsItsMeaningWhenTheEdgeListIsReordered",
          "[drawing][stref][p14][chamfer]") {
    // TEST A, AND THE REASON ADR-024 EXISTS. Before it, a chamfer face was
    // named {role = Chamfer, edge = N} where N was the POSITION of the
    // selection in ChamferDefinition::edges -- a vector of ordinary stored
    // intent that a user may reorder. Reordering it left every stored
    // reference resolving, to different material, with the solid unchanged
    // and the reference untouched. That is a silent rebind, and this test
    // measured it happening.
    //
    // Now the name is the selection's own id, and the id travels with the
    // selection, so a reorder moves the pairs and changes nothing.
    Model m;
    const Block block = addBlock(m, "Block", 100_mm, 60_mm, 40_mm);
    const Chamfered c = addChamfer(m, block);

    const double frontBefore = faceYMm(m, chamferFace(c, c.front));
    const double backBefore = faceYMm(m, chamferFace(c, c.back));
    // The two are genuinely different faces, so a reference that moved between
    // them could not hide inside a tolerance.
    CHECK(frontBefore < 30.0);
    CHECK(backBefore > 30.0);
    CHECK(std::abs(backBefore - frontBefore) > 30.0);

    // REORDER: the same two selections, the same distance, the opposite order.
    // The pairs move, so the ids move with the curves they identify.
    features::ChamferDefinition reordered = definitionOf(m, c.feature);
    std::swap(reordered.edges[0], reordered.edges[1]);
    REQUIRE(reshape(m, c.feature, reordered).has_value());
    REQUIRE(definitionOf(m, c.feature).edges[0].id == c.back);

    // THE ASSERTION. Each reference still names the face it named before.
    CHECK_THAT(faceYMm(m, chamferFace(c, c.front)), WithinAbs(frontBefore, kMm));
    CHECK_THAT(faceYMm(m, chamferFace(c, c.back)), WithinAbs(backBefore, kMm));

    // And the solid did not change either, which is what made the old
    // behaviour so easy to miss: nothing visible moved.
    auto side = features::resolvePlane(m.document, sideOf(block, 0), m.bodies());
    REQUIRE(side.has_value());
}

TEST_CASE("Reference_AChamferFaceIsUnmovedWhenAnotherEdgeIsInsertedBeforeIt",
          "[drawing][stref][p14][chamfer]") {
    // TEST B. Inserting a selection ahead of the referenced one shifts every
    // later POSITION by one. Under the old scheme that moved the reference;
    // under an id it cannot, and the new selection gets an identity of its own
    // rather than inheriting anybody's.
    Model m;
    const Block block = addBlock(m, "Block", 100_mm, 60_mm, 40_mm);
    const Chamfered c = addChamfer(m, block);
    const double backBefore = faceYMm(m, chamferFace(c, c.back));

    features::ChamferDefinition grown = definitionOf(m, c.feature);
    // A third edge, inserted FIRST: the back selection moves from index 1 to 2.
    // It is a BOTTOM edge -- the block is 100 x 60 x 40, so its top face has
    // edges at y = 0 and y = 60 and nothing between them. Chamfering a curve
    // that is not an edge of the body fails the whole feature, which would
    // test the wrong thing.
    grown.edges.insert(grown.edges.begin(),
                       features::ChamferEdge{geometry::lineSignature(
                           Point3D{0_mm, 0_mm, 0_mm}, Direction3D::unitX())});
    // It carries no identity of its own yet, which is what makes it new.
    CHECK_FALSE(grown.edges.front().id.isValid());
    REQUIRE(reshape(m, c.feature, grown).has_value());

    const features::ChamferDefinition after = definitionOf(m, c.feature);
    REQUIRE(after.edges.size() == 3);
    CHECK(after.edges[2].id == c.back);
    // The fresh selection was given an id that is nobody else's, and one that
    // was never handed out before.
    const ChamferEdgeId inserted = after.edges[0].id;
    CHECK(inserted.isValid());
    CHECK(inserted != c.front);
    CHECK(inserted != c.back);
    CHECK(inserted.value() > c.back.value());

    CHECK_THAT(faceYMm(m, chamferFace(c, c.back)), WithinAbs(backBefore, kMm));
}

TEST_CASE("Reference_AChamferFaceSurvivesTheDeletionOfAnUnrelatedEdge",
          "[drawing][stref][p14][chamfer]") {
    // TEST C. Deleting a selection the reference does not name leaves it
    // alone. Under the old scheme deleting an EARLIER one renumbered the rest.
    Model m;
    const Block block = addBlock(m, "Block", 100_mm, 60_mm, 40_mm);
    const Chamfered c = addChamfer(m, block);
    const double backBefore = faceYMm(m, chamferFace(c, c.back));

    features::ChamferDefinition shortened = definitionOf(m, c.feature);
    shortened.edges.erase(shortened.edges.begin()); // drop the FRONT selection
    REQUIRE(reshape(m, c.feature, shortened).has_value());

    const features::ChamferDefinition after = definitionOf(m, c.feature);
    REQUIRE(after.edges.size() == 1);
    CHECK(after.edges[0].id == c.back);
    CHECK_THAT(faceYMm(m, chamferFace(c, c.back)), WithinAbs(backBefore, kMm));

    // And the reference to the one that went is now unresolved, by name.
    const auto gone = features::resolvePlane(m.document, chamferFace(c, c.front), m.bodies());
    REQUIRE_FALSE(gone.has_value());
    CHECK(errorCode(gone) == ErrorCode::NotFound);
}

TEST_CASE("Reference_AChamferFaceWhoseEdgeIsDeletedBecomesUnresolved",
          "[drawing][stref][p14][chamfer]") {
    // TEST D. The referenced selection itself goes. The right answer is
    // Unresolved -- reported, not guessed at, and never the other chamfer face
    // that is still there.
    Model m;
    const Block block = addBlock(m, "Block", 100_mm, 60_mm, 40_mm);
    const Chamfered c = addChamfer(m, block);
    const double frontBefore = faceYMm(m, chamferFace(c, c.front));

    features::ChamferDefinition shortened = definitionOf(m, c.feature);
    shortened.edges.pop_back(); // drop the BACK selection, which is referenced
    REQUIRE(reshape(m, c.feature, shortened).has_value());

    const auto gone = features::resolvePlane(m.document, chamferFace(c, c.back), m.bodies());
    REQUIRE_FALSE(gone.has_value());
    CHECK(errorCode(gone) == ErrorCode::NotFound);
    // The survivor is untouched: the lost reference did not land on it.
    CHECK_THAT(faceYMm(m, chamferFace(c, c.front)), WithinAbs(frontBefore, kMm));
}

TEST_CASE("Reference_AChamferFaceDoesNotRebindToAnIdenticalReplacementEdge",
          "[drawing][stref][p14][chamfer]") {
    // TEST E, AND THE ONE THAT DECIDES THE DESIGN. Delete the referenced
    // selection and add one back with EXACTLY the same supporting curve. The
    // solid is identical to what it was, and the reference must still be
    // unresolved.
    //
    // This is why the identity is an allocated id and not the EdgeSignature.
    // A signature is content: the replacement's signature is byte-identical to
    // the original's, so a signature-named face would resolve and the user
    // would never learn that the thing they dimensioned was deleted. It is the
    // same rule a side face already follows -- it is named by the sketch
    // entity's id, so redrawing an identical line does not adopt its
    // references.
    Model m;
    const Block block = addBlock(m, "Block", 100_mm, 60_mm, 40_mm);
    const Chamfered c = addChamfer(m, block);
    const geometry::EdgeSignature backCurve = definitionOf(m, c.feature).edges[1].curve;

    features::ChamferDefinition without = definitionOf(m, c.feature);
    without.edges.pop_back();
    REQUIRE(reshape(m, c.feature, without).has_value());

    features::ChamferDefinition again = definitionOf(m, c.feature);
    again.edges.push_back(features::ChamferEdge{backCurve});
    REQUIRE(reshape(m, c.feature, again).has_value());

    const features::ChamferDefinition after = definitionOf(m, c.feature);
    REQUIRE(after.edges.size() == 2);
    // Same curve, different identity -- and an identity above every one used
    // before, because a retired id is never handed out again.
    CHECK(after.edges[1].curve == backCurve);
    CHECK(after.edges[1].id != c.back);
    CHECK(after.edges[1].id.value() > c.back.value());

    // THE ASSERTION. The old reference stays unresolved, though the geometry
    // it described is back.
    const auto stale = features::resolvePlane(m.document, chamferFace(c, c.back), m.bodies());
    REQUIRE_FALSE(stale.has_value());
    CHECK(errorCode(stale) == ErrorCode::NotFound);

    // The replacement is referenceable in its own right.
    CHECK(faceYMm(m, chamferFace(c, after.edges[1].id)) > 30.0);
}

TEST_CASE("Reference_AChamferEdgeIdIsRestorableButNotForgeable", "[drawing][stref][p14][chamfer]") {
    // WHERE THE LINE ACTUALLY FALLS, and it is not where it first appears to.
    //
    // The tempting rule is "an edit may only name ids the chamfer currently
    // has", which would stop a deleted selection being revived under its old
    // identity. It is too strong, and the reference models caught it: UNDO of a
    // deletion restores exactly that -- a selection with the id it used to have
    // -- and so does the target recovery this milestone requires. A rule that
    // forbade it would make undoing a chamfer edit impossible.
    //
    // So restoring a RETIRED id is allowed, and that is target recovery. What
    // is refused is an id this chamfer has never allocated: nothing can be
    // referring to it, and granting it would reserve a value a later selection
    // would be given too.
    //
    // Silent rebinding is prevented before this rule is reached: a new
    // selection carries no identity, because ChamferEdge does not convert from
    // a bare curve implicitly, so ordinary editing can only allocate a fresh
    // one. Test E covers that route.
    Model m;
    const Block block = addBlock(m, "Block", 100_mm, 60_mm, 40_mm);
    const Chamfered c = addChamfer(m, block);
    const double backBefore = faceYMm(m, chamferFace(c, c.back));

    features::ChamferDefinition without = definitionOf(m, c.feature);
    const features::ChamferEdge removed = without.edges.back();
    without.edges.pop_back();
    REQUIRE(reshape(m, c.feature, without).has_value());
    REQUIRE_FALSE(features::resolvePlane(m.document, chamferFace(c, c.back), m.bodies()).has_value());

    // FORGING is refused: an identity above everything this chamfer has ever
    // handed out.
    features::ChamferDefinition forged = definitionOf(m, c.feature);
    forged.edges.push_back(features::ChamferEdge{ChamferEdgeId::fromValue(4096), topEdgeAt(60.0)});
    const auto refused = m.document.modifyObject<features::ChamferFeature>(
        c.feature, [&](features::ChamferFeature& f) { return f.setDefinition(forged); });
    REQUIRE_FALSE(refused.has_value());
    CHECK(errorCode(refused) == ErrorCode::InvalidArgument);
    CHECK_THAT(whyNot(refused), ContainsSubstring("never had an edge"));
    // The refusal left the feature exactly as it was.
    REQUIRE(definitionOf(m, c.feature).edges.size() == 1);
    CHECK(definitionOf(m, c.feature).edges[0].id == c.front);

    // RESTORING the selection that was removed, exactly as it was, brings the
    // reference back to the same face. This is target recovery, and it is what
    // undo does.
    features::ChamferDefinition restored = definitionOf(m, c.feature);
    restored.edges.push_back(removed);
    REQUIRE(reshape(m, c.feature, restored).has_value());
    CHECK(definitionOf(m, c.feature).edges[1].id == c.back);
    CHECK_THAT(faceYMm(m, chamferFace(c, c.back)), WithinAbs(backBefore, kMm));
}

TEST_CASE("Reference_UndoingAChamferEditRestoresItsReferences", "[drawing][stref][p14][chamfer][undo]") {
    // The same thing through the published command, which is how a user
    // reaches it. Delete a selection, undo, and the reference that went
    // unresolved must resolve again to the face it always meant.
    Model m;
    const Block block = addBlock(m, "Block", 100_mm, 60_mm, 40_mm);
    const Chamfered c = addChamfer(m, block);
    const double backBefore = faceYMm(m, chamferFace(c, c.back));

    features::ChamferDefinition without = definitionOf(m, c.feature);
    without.edges.pop_back();

    CommandHistory history;
    REQUIRE(history
                .execute(m.document, std::make_unique<features::ModifyChamferCommand>(
                                         FeatureId::fromValue(c.feature.value()), without))
                .has_value());
    m.regenerate();
    REQUIRE_FALSE(features::resolvePlane(m.document, chamferFace(c, c.back), m.bodies()).has_value());

    REQUIRE(history.undo(m.document).has_value());
    m.regenerate();
    CHECK(definitionOf(m, c.feature).edges.size() == 2);
    CHECK(definitionOf(m, c.feature).edges[1].id == c.back);
    CHECK_THAT(faceYMm(m, chamferFace(c, c.back)), WithinAbs(backBefore, kMm));

    // And redo takes it away again, still by identity.
    REQUIRE(history.redo(m.document).has_value());
    m.regenerate();
    CHECK_FALSE(features::resolvePlane(m.document, chamferFace(c, c.back), m.bodies()).has_value());
}

TEST_CASE("Reference_AChamferFaceRoundTripsThroughSaveAndLoad", "[drawing][stref][p14][chamfer][io]") {
    // TEST F. The identity is persistent or it is nothing: a reference that
    // only holds within one session does not survive the thing drawings are
    // for.
    Model m;
    const Block block = addBlock(m, "Block", 100_mm, 60_mm, 40_mm);
    const Chamfered c = addChamfer(m, block);
    const double backBefore = faceYMm(m, chamferFace(c, c.back));

    const TempDir directory;
    const std::filesystem::path path = directory.path() / "chamfer.bcad";
    REQUIRE(io::saveDocument(m.document, path).has_value());

    // A separate parse, into a document that shares nothing with the first.
    Model loaded;
    auto read = io::loadDocument(path);
    INFO(whyNot(read));
    REQUIRE(read.has_value());
    loaded.document = std::move(*read);
    loaded.regenerate();

    const features::ChamferDefinition after = definitionOf(loaded, c.feature);
    REQUIRE(after.edges.size() == 2);
    CHECK(after.edges[0].id == c.front);
    CHECK(after.edges[1].id == c.back);
    CHECK_THAT(faceYMm(loaded, chamferFace(c, c.back)), WithinAbs(backBefore, kMm));
}

TEST_CASE("Reference_AReorderedChamferStillResolvesAfterSaveAndLoad",
          "[drawing][stref][p14][chamfer][io]") {
    // TEST G, and the one a position-based scheme cannot fake its way through.
    // A file records the list in its CURRENT order. If identity came from
    // position, reordering and then saving would write a file whose positions
    // disagree with the references stored in it, and the next load would
    // resolve them to the wrong faces -- with nothing in the file to show
    // anything had gone wrong.
    Model m;
    const Block block = addBlock(m, "Block", 100_mm, 60_mm, 40_mm);
    const Chamfered c = addChamfer(m, block);
    const double backBefore = faceYMm(m, chamferFace(c, c.back));

    features::ChamferDefinition reordered = definitionOf(m, c.feature);
    std::swap(reordered.edges[0], reordered.edges[1]);
    REQUIRE(reshape(m, c.feature, reordered).has_value());

    const TempDir directory;
    const std::filesystem::path path = directory.path() / "reordered.bcad";
    REQUIRE(io::saveDocument(m.document, path).has_value());

    Model loaded;
    auto read = io::loadDocument(path);
    INFO(whyNot(read));
    REQUIRE(read.has_value());
    loaded.document = std::move(*read);
    loaded.regenerate();

    // The saved order is the reordered one -- so the file really does exercise
    // the case -- and the reference still names the same face.
    const features::ChamferDefinition after = definitionOf(loaded, c.feature);
    REQUIRE(after.edges.size() == 2);
    CHECK(after.edges[0].id == c.back);
    CHECK(after.edges[1].id == c.front);
    CHECK_THAT(faceYMm(loaded, chamferFace(c, c.back)), WithinAbs(backBefore, kMm));
}

TEST_CASE("Reference_AChamferFaceResolvesToTheSamePlaceEveryTime",
          "[drawing][stref][p14][chamfer]") {
    // TEST H. Resolution is a pure question about stored state, so asking it
    // repeatedly must give one answer. Run across the three presets by the
    // qualification, and repeated here within one run so a resolver that
    // depended on iteration order or on a cache would show up.
    Model m;
    const Block block = addBlock(m, "Block", 100_mm, 60_mm, 40_mm);
    const Chamfered c = addChamfer(m, block);

    const double front = faceYMm(m, chamferFace(c, c.front));
    const double back = faceYMm(m, chamferFace(c, c.back));
    for (int pass = 0; pass < 8; ++pass) {
        INFO("pass " << pass);
        CHECK_THAT(faceYMm(m, chamferFace(c, c.front)), WithinAbs(front, 0.0));
        CHECK_THAT(faceYMm(m, chamferFace(c, c.back)), WithinAbs(back, 0.0));
    }
    // And across a regeneration, which rebuilds the faces from scratch.
    m.regenerate();
    CHECK_THAT(faceYMm(m, chamferFace(c, c.front)), WithinAbs(front, 0.0));
    CHECK_THAT(faceYMm(m, chamferFace(c, c.back)), WithinAbs(back, 0.0));
}

TEST_CASE("Reference_NoChamferReferenceIsStoredAsAPositionInTheFile",
          "[drawing][stref][p14][chamfer][persistence]") {
    // THE COMPANION THE AUDIT ASKED FOR, and the reason it is written this way.
    //
    // The schema audit above missed the positional chamfer reference for two
    // independent reasons, either of which was enough on its own:
    //
    //   its fixture builds no chamfer, so no chamfer reference was in the
    //   text it searched;
    //   the persisted key was "edge", which is not in its forbidden list --
    //   and could not simply be ADDED to it, because "edge" is a legitimate
    //   key elsewhere in the same file. A chamfer's own selection is
    //   {"id": n, "edge": <curve>} and a variable fillet's is
    //   {"edge": <curve>, "radii": [...]}. Banning the word would ban those.
    //
    // So this test does not ban a word. It checks the STRUCTURE two ways: the
    // shape a positional reference would have, and -- the part no string list
    // can fake -- what happens to the stored reference when the list is
    // reordered under it.
    Model m;
    const Block block = addBlock(m, "Block", 100_mm, 60_mm, 40_mm);
    const Chamfered c = addChamfer(m, block);
    const SheetId sheet = require(drawing::createSheet(
        m.document, "Sheet1",
        drawing::SheetDefinition{.format = drawing::SheetFormat::A3, .scale = {1, 1}}));
    const ViewId view = require(drawing::createView(
        m.document, "Front",
        drawing::ViewDefinition{.sheet = sheet,
                                .source = c.feature,
                                .orientation = drawing::StandardView::Front,
                                .placement = Point2D{120_mm, 150_mm}}));
    // A datum annotation on the SECOND chamfer face: a stored reference to a
    // chamfer face, which is the thing being audited.
    (void)require(drawing::createAnnotation(
        m.document, "Datum",
        drawing::AnnotationDefinition{.view = view,
                                      .type = drawing::AnnotationType::Datum,
                                      .target = drawing::AnnotationTarget{.plane = chamferFace(c, c.back)},
                                      .text = "A",
                                      .placement = Point2D{60_mm, 40_mm}}));
    m.regenerate();

    const TempDir directory;
    const auto save = [&](const std::string& name) {
        const std::filesystem::path path = directory.path() / name;
        REQUIRE(io::saveDocument(m.document, path).has_value());
        return readFile(path);
    };
    const std::string before = save("chamfer-before.bcad");

    // ONE: the reference is stored as an identity, and the file really does
    // contain the case.
    CHECK_THAT(before, ContainsSubstring(R"("role": "chamfer")"));
    CHECK_THAT(before, ContainsSubstring(std::format(R"("chamfer_edge": {})", c.back.value())));

    // TWO: no face selector anywhere carries a POSITION. A positional
    // reference is "edge" followed by a number; the legitimate uses of the key
    // -- a chamfer's own selection and a variable fillet's -- are always
    // "edge" followed by an object. So the structure is what is checked, not
    // the word, and the word is left free to mean what it means elsewhere.
    for (std::size_t at = before.find("\"edge\":"); at != std::string::npos;
         at = before.find("\"edge\":", at + 1)) {
        const std::size_t value = before.find_first_not_of(kSpace, at + 7);
        REQUIRE(value != std::string::npos);
        INFO("at byte " << at << ": " << before.substr(at, 40));
        CHECK(before[value] == '{');
    }

    // THREE, AND THE ONE A FORBIDDEN-WORD LIST COULD NEVER GIVE. Reorder the
    // selections and save again. If the stored reference were a position, the
    // file would now have to say something different to keep meaning the same
    // face -- and under the old scheme it did not, which is exactly how the
    // defect survived a save and a load. Here the reference is untouched while
    // the array it points into is written in the opposite order.
    features::ChamferDefinition reordered = definitionOf(m, c.feature);
    std::swap(reordered.edges[0], reordered.edges[1]);
    REQUIRE(reshape(m, c.feature, reordered).has_value());
    const std::string after = save("chamfer-after.bcad");

    CHECK_THAT(after, ContainsSubstring(std::format(R"("chamfer_edge": {})", c.back.value())));
    // The file's own selection order really did change, so the check above is
    // not passing because nothing moved.
    const auto firstEdgeId = [](const std::string& text) {
        const std::size_t chamfer = text.find(R"("type": "chamfer")");
        REQUIRE(chamfer != std::string::npos);
        const std::size_t id = text.find(R"("id": )", chamfer);
        REQUIRE(id != std::string::npos);
        return text.substr(id, 20);
    };
    CHECK(firstEdgeId(before) != firstEdgeId(after));
}
