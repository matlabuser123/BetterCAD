#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Configurations.hpp>
#include <bettercad/assembly/Resolution.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/drawing/Annotations.hpp>
#include <bettercad/drawing/Bom.hpp>
#include <bettercad/drawing/Commands.hpp>
#include <bettercad/drawing/Dimensions.hpp>
#include <bettercad/drawing/Regeneration.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/drawing/Views.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <ranges>
#include <array>
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
using drawing::SheetDefinition;
using drawing::StandardView;
using drawing::ViewDefinition;
using drawing::ViewSubject;
using features::NodeState;

// P14-CMD-001: drawing edits as commands, and undo/redo that restores intent.
//
// THE ONE RULE. A command mutates CANONICAL INTENT; regeneration recomputes
// what is derived. Undo restores the exact prior intent -- not something that
// draws the same.
//
// HOW "EXACT" IS MEASURED HERE. Every state comparison below is the document's
// own serialization (`io::documentToJson`), compared as text. That is the
// right instrument and not a convenience: ADR-011 keeps derived state OUT of
// the file, so the JSON is precisely canonical intent and nothing else -- every
// ObjectId, every reference, every placement, every format setting, and no
// projection, measured value or BOM row. Two states that serialize identically
// are the same canonical state; two that draw the same but serialize
// differently are NOT, and this suite fails them.
//
// WHAT IS NEVER UNDO PAYLOAD. No command stores a measured value, a projection
// or an item number. The tests that matter most here are the ones that prove
// it by changing the MODEL between a command and its redo: a redone dimension
// must show the new number, and a redone balloon the new item number.
namespace {

constexpr double kMm = 1e-9;

/// The message of a failed Result, or nothing. A helper because
/// INFO(r ? "" : r.error().message) is ill-formed: INFO builds its text with
/// operator<<, and a conditional of two different types has no common one.
[[nodiscard]] std::string why(const Result<void>& result) {
    return result.has_value() ? std::string{} : result.error().message;
}

struct Drawing {
    Document document{"Machine"};
    CommandHistory history;
    features::Regenerator regenerator;

    drawing::BodyLookup bodies() {
        const features::Regenerator* r = &regenerator;
        return [r](ObjectId object) { return r->body(object); };
    }
    drawing::TransformLookup transforms() {
        const features::Regenerator* r = &regenerator;
        return [r](ComponentId c) { return r->transform(c); };
    }
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

    /// The document's canonical state, as its own serialization, with two
    /// pieces of BOOKKEEPING removed.
    ///
    /// Derived state is not in the file at all (ADR-011), so what is left is
    /// intent and only intent. What is taken out is not intent either:
    ///
    ///   last_allocated_id  the ID allocator's watermark. It MUST NOT roll
    ///                      back on undo -- if it did, a later create would
    ///                      hand out an ID a redo is still holding, and two
    ///                      objects would collide. Asserted separately, and
    ///                      in the right direction, by watermark().
    ///   the document UUID  generated per document, so it differs between
    ///                      fixtures by design. Only the determinism test
    ///                      compares across documents.
    ///
    /// Everything else stays: every ObjectId, every reference, every
    /// placement, every format setting.
    [[nodiscard]] std::string canonical() const {
        auto json = io::documentToJson(document);
        REQUIRE(json.has_value());
        std::string kept;
        for (const auto line : std::views::split(std::string_view{*json}, '\n')) {
            const std::string_view text{line.begin(), line.end()};
            if (text.find("\"last_allocated_id\"") != std::string_view::npos) {
                continue;
            }
            // The document's own id is a QUOTED uuid; an object's is a bare
            // number, so this cannot swallow one.
            if (text.find("\"id\": \"") != std::string_view::npos) {
                continue;
            }
            kept.append(text);
            kept.push_back('\n');
        }
        return kept;
    }

    /// The ID allocator's watermark, which only ever goes up.
    [[nodiscard]] std::uint64_t watermark() const { return document.lastAllocatedId(); }
};

/// Executes a command through the history and hands back a pointer to it, so
/// the ID it allocated can be read. The pointer stays valid while the command
/// sits in either stack: undo/redo move the owning unique_ptr, not the object.
template <typename C, typename... Args>
C* run(Drawing& d, Args&&... args) {
    auto command = std::make_unique<C>(std::forward<Args>(args)...);
    C* raw = command.get();
    auto executed = d.history.execute(d.document, std::move(command));
    INFO(why(executed));
    REQUIRE(executed.has_value());
    return raw;
}

/// Executes a command that is expected to FAIL, and returns its error.
template <typename C, typename... Args>
Error runExpectingFailure(Drawing& d, Args&&... args) {
    auto executed = d.history.execute(d.document, std::make_unique<C>(std::forward<Args>(args)...));
    REQUIRE_FALSE(executed.has_value());
    return executed.error();
}

SheetDefinition a3() {
    return SheetDefinition{.format = drawing::SheetFormat::A3,
                           .orientation = drawing::SheetOrientation::Landscape,
                           .margins = drawing::SheetMargins{10_mm, 10_mm, 10_mm, 10_mm},
                           .scale = DrawingScale{1, 1}};
}

struct Block {
    ObjectId sketch{};
    ObjectId pad{};
    std::array<EntityId, 4> lines{};
    std::array<EntityId, 4> corners{};
};

Block addBlock(Drawing& d, const std::string& name, Length width = 100_mm, Length depth = 60_mm,
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
    block.sketch = require(d.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        name, {.profile = SketchId::fromValue(block.sketch.value()), .depth = height});
    REQUIRE(extrude.has_value());
    block.pad = require(d.document.addObject(std::move(*extrude)));
    return block;
}

PlaneReference sideOf(const Block& block, std::size_t entity) {
    return PlaneReference{.object = block.pad,
                          .face = FaceSelector{.role = FaceRole::Side, .entity = block.lines[entity]}};
}

ViewDefinition baseView(SheetId sheet, ObjectId source, Point2D at = {200_mm, 150_mm}) {
    return ViewDefinition{.sheet = sheet,
                          .source = ObjectReference{source},
                          .orientation = StandardView::Front,
                          .placement = at};
}

DimensionDefinition widthDimension(ViewId view, const Block& block) {
    return DimensionDefinition{.view = view,
                               .type = DimensionType::Linear,
                               .from = DimensionTarget{.plane = sideOf(block, 3)},
                               .to = DimensionTarget{.plane = sideOf(block, 1)},
                               .format = drawing::DimensionFormat{.decimals = 2},
                               .placement = Point2D{200_mm, 100_mm}};
}

/// Widens the block's profile, which moves the faces a width dimension names
/// without changing which entity sweeps them.
void widenTo(Drawing& d, const Block& block, Length width) {
    REQUIRE(d.document
                .modifyObject<sketch::Sketch>(
                    block.sketch,
                    [&](sketch::Sketch& s) {
                        REQUIRE(s.setPointPosition(block.corners[1], Point2D{width, 0_mm}).has_value());
                        REQUIRE(s.setPointPosition(block.corners[2], Point2D{width, 60_mm}).has_value());
                        return true;
                    })
                .has_value());
}

/// A sheet, a block and a front view of it, regenerated.
struct Simple {
    Drawing d;
    SheetId sheet{};
    Block block{};
    ViewId view{};
};

Simple simple() {
    Simple s;
    s.sheet = run<drawing::CreateSheetCommand>(s.d, "Sheet1", a3())->sheetId();
    s.block = addBlock(s.d, "Block");
    s.d.regenerate();
    s.view = run<drawing::CreateViewCommand>(s.d, "Front", baseView(s.sheet, s.block.pad))->viewId();
    s.d.regenerate();
    return s;
}

} // namespace

// --- Undo and redo restore canonical state exactly ----------------------------------------------

TEST_CASE("Command_UndoAndRedoRestoreTheExactCanonicalStateForEveryDrawingKind",
          "[drawing][cmd][p14]") {
    // State0 -> command -> State1 -> undo -> State0 -> redo -> State1,
    // compared as the document's own serialization each time. Not "equivalent
    // enough": the same bytes.
    Simple s = simple();
    const Block& block = s.block;

    const auto check = [&](const std::string& what, auto makeCommand) {
        INFO(what);
        const std::string state0 = s.d.canonical();
        auto command = makeCommand();
        auto executed = s.d.history.execute(s.d.document, std::move(command));
        INFO(why(executed));
        REQUIRE(executed.has_value());
        const std::string state1 = s.d.canonical();
        CHECK(state1 != state0);

        REQUIRE(s.d.history.undo(s.d.document).has_value());
        CHECK(s.d.canonical() == state0);

        REQUIRE(s.d.history.redo(s.d.document).has_value());
        CHECK(s.d.canonical() == state1);
    };

    check("create a second sheet", [&] {
        return std::make_unique<drawing::CreateSheetCommand>("Sheet2", a3());
    });
    check("edit the sheet's definition", [&] {
        SheetDefinition wider = a3();
        wider.format = drawing::SheetFormat::A2;
        wider.scale = DrawingScale{1, 2};
        return std::make_unique<drawing::SetSheetDefinitionCommand>(s.sheet, wider);
    });
    check("move the view", [&] {
        return std::make_unique<drawing::MoveViewCommand>(s.view, Point2D{120_mm, 70_mm});
    });
    check("create a dimension", [&] {
        return std::make_unique<drawing::CreateDimensionCommand>("Width",
                                                                 widthDimension(s.view, block));
    });
    check("create a note", [&] {
        return std::make_unique<drawing::CreateAnnotationCommand>(
            "Note", AnnotationDefinition{.view = s.view,
                                         .type = AnnotationType::Note,
                                         .text = "BREAK SHARP EDGES",
                                         .placement = Point2D{40_mm, 40_mm}});
    });
}

TEST_CASE("Command_RedoRestoresTheSameObjectIdForEveryDrawingKind", "[drawing][cmd][p14]") {
    // "Do not create a semantically similar replacement with a fresh ID and
    // call it exact undo." Everything that names a drawing object names it by
    // ID, so a fresh one silently breaks every reference to it.
    Simple s = simple();
    auto* dimension = run<drawing::CreateDimensionCommand>(s.d, "Width",
                                                           widthDimension(s.view, s.block));
    auto* note = run<drawing::CreateAnnotationCommand>(
        s.d, "Note",
        AnnotationDefinition{.view = s.view,
                             .type = AnnotationType::Note,
                             .text = "NOTE",
                             .placement = Point2D{40_mm, 40_mm}});
    const DimensionId dimensionId = dimension->dimensionId();
    const AnnotationId annotationId = note->annotationId();

    // Undo both, then redo both.
    REQUIRE(s.d.history.undo(s.d.document).has_value());
    REQUIRE(s.d.history.undo(s.d.document).has_value());
    CHECK(drawing::findDimension(s.d.document, dimensionId) == nullptr);
    CHECK(drawing::findAnnotation(s.d.document, annotationId) == nullptr);

    REQUIRE(s.d.history.redo(s.d.document).has_value());
    REQUIRE(s.d.history.redo(s.d.document).has_value());
    CHECK(dimension->dimensionId() == dimensionId);
    CHECK(note->annotationId() == annotationId);
    REQUIRE(drawing::findDimension(s.d.document, dimensionId) != nullptr);
    REQUIRE(drawing::findAnnotation(s.d.document, annotationId) != nullptr);
    CHECK(drawing::findDimension(s.d.document, dimensionId)->name() == "Width");
    CHECK(drawing::findAnnotation(s.d.document, annotationId)->name() == "Note");
    // And the sheet and view, deleted and restored, keep theirs.
    const SheetId sheet = s.sheet;
    auto* deleted = run<drawing::DeleteDimensionCommand>(s.d, dimensionId);
    (void)deleted;
    REQUIRE(s.d.history.undo(s.d.document).has_value());
    REQUIRE(drawing::findDimension(s.d.document, dimensionId) != nullptr);
    CHECK(drawing::findSheet(s.d.document, sheet) != nullptr);
}

// --- Multi-level history ------------------------------------------------------------------------

TEST_CASE("Command_MultiLevelUndoAndRedoWalkEveryIntermediateStateExactly",
          "[drawing][cmd][p14]") {
    // Seven commands, every intermediate canonical state recorded, then the
    // whole history walked backwards and forwards. Single-command undo proves
    // much less: a command that restores its own edit but disturbs a
    // neighbour's passes that and fails this.
    Drawing d;
    const Block block = addBlock(d, "Block");
    d.regenerate();

    std::vector<std::string> states;
    states.push_back(d.canonical());

    const SheetId sheet = run<drawing::CreateSheetCommand>(d, "Sheet1", a3())->sheetId();
    states.push_back(d.canonical());

    const ViewId view = run<drawing::CreateViewCommand>(d, "Front", baseView(sheet, block.pad))->viewId();
    states.push_back(d.canonical());

    run<drawing::MoveViewCommand>(d, view, Point2D{150_mm, 120_mm});
    states.push_back(d.canonical());

    run<drawing::CreateDimensionCommand>(d, "Width", widthDimension(view, block));
    states.push_back(d.canonical());

    run<drawing::CreateAnnotationCommand>(
        d, "Note",
        AnnotationDefinition{.view = view,
                             .type = AnnotationType::Note,
                             .text = "GENERAL TOLERANCE +/-0.2",
                             .placement = Point2D{40_mm, 40_mm}});
    states.push_back(d.canonical());

    SheetDefinition halved = a3();
    halved.scale = DrawingScale{1, 2};
    run<drawing::SetSheetDefinitionCommand>(d, sheet, halved);
    states.push_back(d.canonical());

    run<drawing::MoveViewCommand>(d, view, Point2D{170_mm, 110_mm});
    states.push_back(d.canonical());

    REQUIRE(states.size() == 8);
    REQUIRE(d.history.undoCount() == 7);
    REQUIRE(d.history.redoCount() == 0);

    // 7 -> 0, checking every step.
    for (std::size_t step = 7; step > 0; --step) {
        INFO("undo to state " << (step - 1));
        REQUIRE(d.history.undo(d.document).has_value());
        CHECK(d.canonical() == states[step - 1]);
        CHECK(d.history.undoCount() == step - 1);
        CHECK(d.history.redoCount() == 8 - step);
    }
    CHECK_FALSE(d.history.canUndo());

    // 0 -> 7, checking every step.
    for (std::size_t step = 1; step <= 7; ++step) {
        INFO("redo to state " << step);
        REQUIRE(d.history.redo(d.document).has_value());
        CHECK(d.canonical() == states[step]);
        CHECK(d.history.undoCount() == step);
        CHECK(d.history.redoCount() == 7 - step);
    }
    CHECK_FALSE(d.history.canRedo());
}

TEST_CASE("Command_ANewCommandAfterAnUndoDropsTheRedoBranch", "[drawing][cmd][p14]") {
    // A -> B -> C -> undo C -> D, and C must no longer be redoable.
    Simple s = simple();
    run<drawing::MoveViewCommand>(s.d, s.view, Point2D{120_mm, 120_mm}); // B
    run<drawing::MoveViewCommand>(s.d, s.view, Point2D{130_mm, 130_mm}); // C
    REQUIRE(s.d.history.undo(s.d.document).has_value());
    REQUIRE(s.d.history.canRedo());
    REQUIRE(s.d.history.redoCount() == 1);

    run<drawing::MoveViewCommand>(s.d, s.view, Point2D{140_mm, 140_mm}); // D
    CHECK_FALSE(s.d.history.canRedo());
    CHECK(s.d.history.redoCount() == 0);
    const auto redone = s.d.history.redo(s.d.document);
    REQUIRE_FALSE(redone.has_value());
    CHECK(redone.error().code == ErrorCode::FailedPrecondition);
    // D is where we are, not C.
    CHECK_THAT(drawing::findView(s.d.document, s.view)->definition().placement.x.in(units::mm),
               WithinAbs(140.0, kMm));
}

TEST_CASE("Command_AFailedCommandLeavesTheRedoBranchIntact", "[drawing][cmd][p14]") {
    // CommandHistory clears the redo stack only after a SUCCESSFUL execute,
    // so a command that is refused must not destroy work that is still
    // redoable. This pins that behaviour rather than assuming it.
    Simple s = simple();
    const std::size_t settled = s.d.history.undoCount(); // simple() ran commands of its own
    run<drawing::MoveViewCommand>(s.d, s.view, Point2D{120_mm, 120_mm});
    REQUIRE(s.d.history.undo(s.d.document).has_value());
    REQUIRE(s.d.history.redoCount() == 1);
    const std::string before = s.d.canonical();

    // Refused: there is no such view.
    const Error error = runExpectingFailure<drawing::MoveViewCommand>(
        s.d, ViewId::fromValue(9999), Point2D{10_mm, 10_mm});
    CHECK(error.code == ErrorCode::NotFound);

    CHECK(s.d.canonical() == before);
    CHECK(s.d.history.redoCount() == 1);
    CHECK(s.d.history.undoCount() == settled);
    // And the redo still works, and still lands where it should.
    REQUIRE(s.d.history.redo(s.d.document).has_value());
    CHECK_THAT(drawing::findView(s.d.document, s.view)->definition().placement.x.in(units::mm),
               WithinAbs(120.0, kMm));
}

TEST_CASE("Command_UndoAndRedoOnAnEmptyHistoryAreStructuredFailures", "[drawing][cmd][p14]") {
    Drawing d;
    const std::string before = d.canonical();

    const auto undone = d.history.undo(d.document);
    REQUIRE_FALSE(undone.has_value());
    CHECK(undone.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(undone.error().message, ContainsSubstring("nothing to undo"));

    const auto redone = d.history.redo(d.document);
    REQUIRE_FALSE(redone.has_value());
    CHECK(redone.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(redone.error().message, ContainsSubstring("nothing to redo"));

    CHECK(d.canonical() == before);
    CHECK_FALSE(d.history.canUndo());
    CHECK_FALSE(d.history.canRedo());
}

TEST_CASE("Command_AnEditToTheValueAlreadyThereIsAValidCommandAndIsReversible",
          "[drawing][cmd][p14]") {
    // The policy, stated and followed: a no-op edit is a COMMAND. That is
    // what the qualified assembly commands do -- SetComponentPlacementCommand
    // ignores the "did anything change" flag its setter returns -- and a
    // drawing command that quietly refused to enter history would make the
    // undo stack depend on whether the user happened to pick the same number.
    Simple s = simple();
    const Point2D where = drawing::findView(s.d.document, s.view)->definition().placement;
    const std::string before = s.d.canonical();
    const std::size_t depth = s.d.history.undoCount();

    // Something redoable is waiting, so what the no-op does to it is on the
    // record too.
    run<drawing::MoveViewCommand>(s.d, s.view, Point2D{99_mm, 99_mm});
    REQUIRE(s.d.history.undo(s.d.document).has_value());
    REQUIRE(s.d.history.redoCount() == 1);

    run<drawing::MoveViewCommand>(s.d, s.view, where);
    CHECK(s.d.history.undoCount() == depth + 1);
    CHECK(s.d.canonical() == before); // nothing moved: it was already there

    // The redo branch IS dropped, because a no-op is a successful command and
    // CommandHistory clears redo after every successful execute. That is the
    // existing rule applied consistently, not a drawing-specific one: making
    // an exception would mean the undo stack depended on whether the user
    // happened to pick the number that was already there.
    CHECK(s.d.history.redoCount() == 0);

    REQUIRE(s.d.history.undo(s.d.document).has_value());
    CHECK(s.d.canonical() == before);
    CHECK(s.d.history.undoCount() == depth);
}

// --- Failed commands are atomic -----------------------------------------------------------------

TEST_CASE("Command_EveryRefusedCommandLeavesTheDocumentAndBothStacksExactlyAsTheyWere",
          "[drawing][cmd][p14][atomic]") {
    Simple s = simple();
    auto* created = run<drawing::CreateDimensionCommand>(s.d, "Width", widthDimension(s.view, s.block));
    const DimensionId dimension = created->dimensionId();
    s.d.regenerate();

    const std::string before = s.d.canonical();
    const std::size_t undoDepth = s.d.history.undoCount();
    const std::size_t redoDepth = s.d.history.redoCount();

    const auto refused = [&](const std::string& what, auto makeCommand) {
        INFO(what);
        auto executed = s.d.history.execute(s.d.document, makeCommand());
        CHECK_FALSE(executed.has_value());
        CHECK(s.d.canonical() == before);
        CHECK(s.d.history.undoCount() == undoDepth);
        CHECK(s.d.history.redoCount() == redoDepth);
    };

    refused("delete a sheet that is not there", [] {
        return std::make_unique<drawing::DeleteSheetCommand>(SheetId::fromValue(9999));
    });
    refused("move a view that is not there", [] {
        return std::make_unique<drawing::MoveViewCommand>(ViewId::fromValue(9999),
                                                          Point2D{10_mm, 10_mm});
    });
    refused("delete a dimension that is not there", [] {
        return std::make_unique<drawing::DeleteDimensionCommand>(DimensionId::fromValue(9999));
    });
    refused("delete an annotation that is not there", [] {
        return std::make_unique<drawing::DeleteAnnotationCommand>(AnnotationId::fromValue(9999));
    });
    refused("create a view on a sheet that is not there", [&] {
        return std::make_unique<drawing::CreateViewCommand>(
            "Orphan", baseView(SheetId::fromValue(9999), s.block.pad));
    });
    refused("create a view of an object that is not there", [&] {
        return std::make_unique<drawing::CreateViewCommand>(
            "Nothing", baseView(s.sheet, ObjectId::fromValue(9999)));
    });
    refused("create a sheet whose margins leave no room", [] {
        SheetDefinition impossible = a3();
        impossible.margins = drawing::SheetMargins{500_mm, 500_mm, 500_mm, 500_mm};
        return std::make_unique<drawing::CreateSheetCommand>("Impossible", impossible);
    });
    refused("give a sheet margins that leave no room", [&] {
        SheetDefinition impossible = a3();
        impossible.margins = drawing::SheetMargins{500_mm, 500_mm, 500_mm, 500_mm};
        return std::make_unique<drawing::SetSheetDefinitionCommand>(s.sheet, impossible);
    });
    refused("create a dimension on a view that is not there", [&] {
        DimensionDefinition orphan = widthDimension(s.view, s.block);
        orphan.view = ViewId::fromValue(9999);
        return std::make_unique<drawing::CreateDimensionCommand>("Orphan", orphan);
    });
    refused("create an annotation whose target is not there", [&] {
        return std::make_unique<drawing::CreateAnnotationCommand>(
            "Bad", AnnotationDefinition{.view = s.view,
                                        .type = AnnotationType::Balloon,
                                        .target = AnnotationTarget{.object = ObjectId::fromValue(9999)},
                                        .placement = Point2D{40_mm, 40_mm}});
    });
    refused("retarget a dimension at a view that is not there", [&] {
        DimensionDefinition orphan = widthDimension(s.view, s.block);
        orphan.view = ViewId::fromValue(9999);
        return std::make_unique<drawing::SetDimensionDefinitionCommand>(dimension, orphan);
    });

    // The dimension still measures what it always did.
    const auto measured = drawing::measure(s.d.document, dimension, s.d.bodies(), s.d.transforms());
    REQUIRE(measured.has_value());
    REQUIRE(measured->length.has_value());
    CHECK_THAT(measured->length->in(units::mm), WithinAbs(100.0, kMm));
}

TEST_CASE("Command_AnEditRefusedAfterTheCommandHasReadTheOldStateRollsBackCleanly",
          "[drawing][cmd][p14][atomic]") {
    // Not a precondition failure: the command finds the object, reads its
    // current definition, and is then refused by the VALIDATOR inside the
    // setter -- the latest point at which a drawing command can fail. What
    // must not survive is a half-recorded command: undo afterwards has to be
    // the PREVIOUS command's, not a no-op that thinks it has work to do.
    Simple s = simple();
    run<drawing::MoveViewCommand>(s.d, s.view, Point2D{111_mm, 99_mm});
    const std::string before = s.d.canonical();
    const std::size_t depth = s.d.history.undoCount();

    // Structurally valid as a struct, refused against the document: a base
    // view may not carry a section plane.
    ViewDefinition confused = baseView(s.sheet, s.block.pad);
    confused.section = drawing::CuttingPlane{};
    const Error error =
        runExpectingFailure<drawing::SetViewDefinitionCommand>(s.d, s.view, confused);
    CHECK(error.code == ErrorCode::InvalidArgument);

    CHECK(s.d.canonical() == before);
    CHECK(s.d.history.undoCount() == depth);

    // The undo that follows is the MOVE's, and it lands exactly where the
    // view was before the move.
    REQUIRE(s.d.history.undo(s.d.document).has_value());
    CHECK_THAT(drawing::findView(s.d.document, s.view)->definition().placement.x.in(units::mm),
               WithinAbs(200.0, kMm));
}

TEST_CASE("Command_MovingAProjectedViewIsRefusedBecauseItStoresNoPlacement",
          "[drawing][cmd][p14][atomic]") {
    // A projected view's placement is DERIVED from its parent and the sheet's
    // convention (ADR-018), so there is nothing to move. The refusal is the
    // qualified validator's, in its own words, rather than a second opinion
    // added here -- and the command must leave the definition untouched, not
    // half-written with a placement the kind does not own.
    Simple s = simple();
    const ViewId top = run<drawing::CreateViewCommand>(
                           s.d, "Top",
                           ViewDefinition{.kind = drawing::ViewKind::Projected,
                                          .sheet = s.sheet,
                                          .parent = s.view,
                                          .direction = drawing::ProjectedDirection::Top,
                                          .spacing = 80_mm})
                           ->viewId();
    s.d.regenerate();
    const std::string before = s.d.canonical();
    const std::size_t depth = s.d.history.undoCount();
    const ViewDefinition intact = drawing::findView(s.d.document, top)->definition();

    const Error error = runExpectingFailure<drawing::MoveViewCommand>(s.d, top, Point2D{50_mm, 50_mm});
    CHECK(error.code == ErrorCode::InvalidArgument);
    CHECK_THAT(error.message, ContainsSubstring("derived from its parent"));

    CHECK(s.d.canonical() == before);
    CHECK(s.d.history.undoCount() == depth);
    CHECK(drawing::findView(s.d.document, top)->definition() == intact);
    // It can still be moved the way its kind IS moved: by its spacing.
    ViewDefinition further = intact;
    further.spacing = 110_mm;
    run<drawing::SetViewDefinitionCommand>(s.d, top, further);
    CHECK_THAT(drawing::findView(s.d.document, top)->definition().spacing.in(units::mm),
               WithinAbs(110.0, kMm));
    REQUIRE(s.d.history.undo(s.d.document).has_value());
    CHECK(s.d.canonical() == before);
}

TEST_CASE("Command_UndoingACommandThatWasNeverExecutedIsRefused", "[drawing][cmd][p14][atomic]") {
    // Commands are reachable outside the history, so undo() on a fresh one
    // must say so rather than corrupting the document.
    Simple s = simple();
    const std::string before = s.d.canonical();

    drawing::MoveViewCommand move{s.view, Point2D{10_mm, 10_mm}};
    const auto undone = move.undo(s.d.document);
    REQUIRE_FALSE(undone.has_value());
    CHECK(undone.error().code == ErrorCode::FailedPrecondition);
    const auto redone = move.redo(s.d.document);
    REQUIRE_FALSE(redone.has_value());
    CHECK(s.d.canonical() == before);

    drawing::CreateSheetCommand create{"Sheet2", a3()};
    CHECK_FALSE(create.undo(s.d.document).has_value());
    CHECK_FALSE(create.redo(s.d.document).has_value());
    CHECK(s.d.canonical() == before);
}

// --- Dependency policy and restoration -----------------------------------------------------------

TEST_CASE("Command_DeletingAViewWithAProjectedChildIsRefusedAndChangesNothing",
          "[drawing][cmd][p14][atomic]") {
    // THE reason these commands exist rather than core's DeleteObjectCommand.
    // removeView() refuses to orphan a child; DeleteObjectCommand calls
    // Document::removeObject() directly and walks straight past that.
    Simple s = simple();
    const ViewId top = run<drawing::CreateViewCommand>(
                           s.d, "Top",
                           ViewDefinition{.kind = drawing::ViewKind::Projected,
                                          .sheet = s.sheet,
                                          .parent = s.view,
                                          .direction = drawing::ProjectedDirection::Top,
                                          .spacing = 80_mm})
                           ->viewId();
    s.d.regenerate();
    const std::string before = s.d.canonical();
    const std::size_t depth = s.d.history.undoCount();

    const Error error = runExpectingFailure<drawing::DeleteViewCommand>(s.d, s.view);
    CHECK(error.code == ErrorCode::FailedPrecondition);
    CHECK_THAT(error.message, ContainsSubstring("projected from it"));
    CHECK(s.d.canonical() == before);
    CHECK(s.d.history.undoCount() == depth);

    // The generic command would have gone through. That is not a defect in
    // core -- it is why the drawing module wraps the policy.
    Document copy = s.d.document.clone();
    CommandHistory generic;
    CHECK(generic.execute(copy, std::make_unique<DeleteObjectCommand>(ObjectId{s.view})).has_value());
    CHECK(drawing::findView(copy, s.view) == nullptr);
    CHECK(drawing::findView(copy, top) != nullptr); // orphaned

    // Child first, then parent: allowed, and undo restores both exactly.
    const std::string withBoth = s.d.canonical();
    run<drawing::DeleteViewCommand>(s.d, top);
    run<drawing::DeleteViewCommand>(s.d, s.view);
    CHECK(drawing::views(s.d.document).empty());
    REQUIRE(s.d.history.undo(s.d.document).has_value());
    REQUIRE(s.d.history.undo(s.d.document).has_value());
    CHECK(s.d.canonical() == withBoth);
    REQUIRE(drawing::findView(s.d.document, top) != nullptr);
    CHECK(drawing::findView(s.d.document, top)->definition().parent == std::optional<ViewId>{s.view});
}

TEST_CASE("Command_UndoingACreationIsRefusedWhileSomethingIsProjectedFromIt",
          "[drawing][cmd][p14][atomic]") {
    // Undo of a creation is a removal, and the same policy governs it. If it
    // did not, undoing far enough would orphan a child that a LATER command
    // created -- which cannot happen through the history, but can when a
    // command is driven directly.
    Simple s = simple();
    auto* create = run<drawing::CreateViewCommand>(s.d, "Second",
                                                   baseView(s.sheet, s.block.pad, {80_mm, 80_mm}));
    const ViewId second = create->viewId();
    run<drawing::CreateViewCommand>(s.d, "SecondTop",
                                    ViewDefinition{.kind = drawing::ViewKind::Projected,
                                                   .sheet = s.sheet,
                                                   .parent = second,
                                                   .direction = drawing::ProjectedDirection::Top,
                                                   .spacing = 60_mm});
    const std::string before = s.d.canonical();

    // Undoing the creation of `second` directly, out of order.
    const auto undone = create->undo(s.d.document);
    REQUIRE_FALSE(undone.has_value());
    CHECK(undone.error().code == ErrorCode::FailedPrecondition);
    CHECK(s.d.canonical() == before);
}

TEST_CASE("Command_DeletingASheetAndUndoingItRestoresTheSheetAndWhatNamedIt",
          "[drawing][cmd][p14]") {
    Simple s = simple();
    run<drawing::CreateDimensionCommand>(s.d, "Width", widthDimension(s.view, s.block));
    s.d.regenerate();
    const std::string before = s.d.canonical();

    run<drawing::DeleteSheetCommand>(s.d, s.sheet);
    CHECK(drawing::findSheet(s.d.document, s.sheet) == nullptr);
    // The view outlives its sheet and is now unresolvable -- which P14-REGEN-001
    // REPORTS rather than passing over.
    const auto broken = s.d.regenerate();
    CHECK_FALSE(broken.succeeded());

    REQUIRE(s.d.history.undo(s.d.document).has_value());
    CHECK(s.d.canonical() == before);
    REQUIRE(drawing::findSheet(s.d.document, s.sheet) != nullptr);
    CHECK(drawing::findView(s.d.document, s.view)->definition().sheet == s.sheet);
    const auto healed = s.d.regenerate();
    CHECK(healed.succeeded());
}

// --- Nothing derived is ever undo payload --------------------------------------------------------

TEST_CASE("Command_ARedoneDimensionShowsTheCurrentModelValueNotTheOneItWasCreatedWith",
          "[drawing][cmd][p14][derived]") {
    // THE test for snapshotting. The dimension is created at 100 mm, undone,
    // the MODEL is changed to 137.5, and the creation is redone. A command
    // that had captured its measured value would put 100 back on the sheet.
    Simple s = simple();
    auto* created = run<drawing::CreateDimensionCommand>(s.d, "Width", widthDimension(s.view, s.block));
    const DimensionId dimension = created->dimensionId();
    s.d.regenerate();

    const auto measure = [&] {
        auto m = drawing::measure(s.d.document, dimension, s.d.bodies(), s.d.transforms());
        REQUIRE(m.has_value());
        REQUIRE(m->length.has_value());
        return m->length->in(units::mm);
    };
    REQUIRE_THAT(measure(), WithinAbs(100.0, kMm));

    REQUIRE(s.d.history.undo(s.d.document).has_value());
    widenTo(s.d, s.block, 137.5_mm);
    s.d.regenerate();

    REQUIRE(s.d.history.redo(s.d.document).has_value());
    s.d.regenerate();
    CHECK_THAT(measure(), WithinAbs(137.5, kMm));
    // ...and it is the same reference intent it always was.
    const DimensionDefinition& d = drawing::findDimension(s.d.document, dimension)->definition();
    CHECK(d.from.plane->face->entity == s.block.lines[3]);
    CHECK(d.to.plane->face->entity == s.block.lines[1]);
}

TEST_CASE("Command_UndoAndRedoKeepADimensionOnTheSameFaceWithAnIdenticalFacePresent",
          "[drawing][cmd][p14][derived]") {
    // P14-STREF-001's rule, carried through the history: recovery restores the
    // SAME target, and a look-alike is never adopted. The second block is
    // identical in every way a resemblance test could see.
    Simple s = simple();
    const Block twin = addBlock(s.d, "Twin");
    s.d.regenerate();
    auto* created = run<drawing::CreateDimensionCommand>(s.d, "Width", widthDimension(s.view, s.block));
    const DimensionId dimension = created->dimensionId();

    REQUIRE(s.d.history.undo(s.d.document).has_value());
    REQUIRE(s.d.history.redo(s.d.document).has_value());

    const DimensionDefinition& d = drawing::findDimension(s.d.document, dimension)->definition();
    CHECK(d.from.plane->object == s.block.pad);
    CHECK(d.to.plane->object == s.block.pad);
    CHECK_FALSE(d.from.plane->object == twin.pad);
    CHECK(d.from.plane->face->entity == s.block.lines[3]);
}

// --- Assembly drawings: BOM tables and balloons are annotations ----------------------------------

namespace {

struct AssemblyDrawing {
    Drawing d;
    SheetId sheet{};
    ViewId view{};
    ObjectId part{};
    ComponentId first{};
    ComponentId second{};
};

AssemblyDrawing twoOfOnePart() {
    AssemblyDrawing a;
    a.sheet = run<drawing::CreateSheetCommand>(a.d, "Sheet1", a3())->sheetId();
    const Block block = addBlock(a.d, "Bracket", 40_mm, 40_mm, 40_mm);
    a.part = block.pad;
    a.first = require(assembly::createComponent(
        a.d.document, "First",
        {.part = a.part, .placement = ComponentPlacement{.translation = {0_mm, 0_mm, 0_mm}}}));
    a.second = require(assembly::createComponent(
        a.d.document, "Second",
        {.part = a.part, .placement = ComponentPlacement{.translation = {80_mm, 0_mm, 0_mm}}}));
    a.view = run<drawing::CreateViewCommand>(a.d, "MainView",
                                             ViewDefinition{.sheet = a.sheet,
                                                            .subject = ViewSubject::Assembly,
                                                            .orientation = StandardView::Front,
                                                            .placement = Point2D{200_mm, 150_mm}})
                 ->viewId();
    a.d.regenerate();
    return a;
}

} // namespace

TEST_CASE("Command_ABomTableAndABalloonAreCreatedMovedAndDeletedAsAnnotations",
          "[drawing][cmd][p14][bom]") {
    AssemblyDrawing a = twoOfOnePart();
    const std::string empty = a.d.canonical();

    auto* table = run<drawing::CreateAnnotationCommand>(
        a.d, "Bom",
        AnnotationDefinition{.view = a.view,
                             .type = AnnotationType::BomTable,
                             .placement = Point2D{240_mm, 200_mm}});
    auto* balloon = run<drawing::CreateAnnotationCommand>(
        a.d, "B1",
        AnnotationDefinition{.view = a.view,
                             .type = AnnotationType::Balloon,
                             .target = AnnotationTarget{.object = ObjectId{a.first}},
                             .placement = Point2D{40_mm, 60_mm}});
    const AnnotationId tableId = table->annotationId();
    const AnnotationId balloonId = balloon->annotationId();
    a.d.regenerate();

    const std::string withBoth = a.d.canonical();
    run<drawing::MoveAnnotationCommand>(a.d, tableId, Point2D{250_mm, 190_mm});
    run<drawing::MoveAnnotationCommand>(a.d, balloonId, Point2D{45_mm, 65_mm});
    CHECK_THAT(drawing::findAnnotation(a.d.document, tableId)->definition().placement.x.in(units::mm),
               WithinAbs(250.0, kMm));

    REQUIRE(a.d.history.undo(a.d.document).has_value());
    REQUIRE(a.d.history.undo(a.d.document).has_value());
    CHECK(a.d.canonical() == withBoth);

    // Delete both, and undo back to where they were.
    REQUIRE(a.d.history.redo(a.d.document).has_value());
    REQUIRE(a.d.history.redo(a.d.document).has_value());
    const std::string moved = a.d.canonical();
    run<drawing::DeleteAnnotationCommand>(a.d, balloonId);
    run<drawing::DeleteAnnotationCommand>(a.d, tableId);
    CHECK(drawing::annotations(a.d.document).empty());
    REQUIRE(a.d.history.undo(a.d.document).has_value());
    REQUIRE(a.d.history.undo(a.d.document).has_value());
    CHECK(a.d.canonical() == moved);
    CHECK(drawing::findAnnotation(a.d.document, balloonId) != nullptr);
    CHECK(drawing::findAnnotation(a.d.document, tableId) != nullptr);
    (void)empty;
}

TEST_CASE("Command_ARedoneBalloonShowsTheCurrentItemNumberAndNeverAStoredOne",
          "[drawing][cmd][p14][bom][derived]") {
    // A balloon's canonical target is the OCCURRENCE. Its number is derived
    // from the BOM, which is derived from the active occurrence set
    // (ADR-022). So: balloon on the SECOND part's occurrence shows item 2;
    // undo; delete the first part's occurrence so the second becomes item 1;
    // redo -- and it must show 1.
    AssemblyDrawing a = twoOfOnePart();
    const Block cover = addBlock(a.d, "Cover", 20_mm, 20_mm, 5_mm);
    const ComponentId coverOccurrence = require(assembly::createComponent(
        a.d.document, "Cover1",
        {.part = cover.pad, .placement = ComponentPlacement{.translation = {0_mm, 0_mm, 60_mm}}}));
    a.d.regenerate();

    auto* balloon = run<drawing::CreateAnnotationCommand>(
        a.d, "B",
        AnnotationDefinition{.view = a.view,
                             .type = AnnotationType::Balloon,
                             .target = AnnotationTarget{.object = ObjectId{coverOccurrence}},
                             .placement = Point2D{40_mm, 60_mm}});
    const AnnotationId balloonId = balloon->annotationId();
    a.d.regenerate();
    REQUIRE(*drawing::itemNumberOf(a.d.document, a.view, coverOccurrence) == 2);

    REQUIRE(a.d.history.undo(a.d.document).has_value());
    // Remove both bracket occurrences, so the cover becomes item 1.
    REQUIRE(a.d.document.removeObject(ObjectId{a.first}).has_value());
    REQUIRE(a.d.document.removeObject(ObjectId{a.second}).has_value());
    a.d.regenerate();
    REQUIRE(*drawing::itemNumberOf(a.d.document, a.view, coverOccurrence) == 1);

    REQUIRE(a.d.history.redo(a.d.document).has_value());
    a.d.regenerate();
    // The target is the occurrence it always was...
    CHECK(drawing::findAnnotation(a.d.document, balloonId)->definition().target.object ==
          ObjectId{coverOccurrence});
    // ...and the number it draws is the CURRENT one.
    const auto drawn = drawing::draw(a.d.document, balloonId, a.d.bodies(), a.d.transforms());
    REQUIRE(drawn.has_value());
    REQUIRE_FALSE(drawn->texts.empty());
    CHECK(drawn->texts.front().text == "1");
}

TEST_CASE("Command_RetargetingABalloonIsAnExplicitEditAndKeepsTheOccurrenceIdentity",
          "[drawing][cmd][p14][bom]") {
    AssemblyDrawing a = twoOfOnePart();
    auto* balloon = run<drawing::CreateAnnotationCommand>(
        a.d, "B1",
        AnnotationDefinition{.view = a.view,
                             .type = AnnotationType::Balloon,
                             .target = AnnotationTarget{.object = ObjectId{a.first}},
                             .placement = Point2D{40_mm, 60_mm}});
    const AnnotationId id = balloon->annotationId();
    a.d.regenerate();

    AnnotationDefinition retargeted = drawing::findAnnotation(a.d.document, id)->definition();
    retargeted.target = AnnotationTarget{.object = ObjectId{a.second}};
    run<drawing::SetAnnotationDefinitionCommand>(a.d, id, retargeted);
    CHECK(drawing::findAnnotation(a.d.document, id)->definition().target.object ==
          ObjectId{a.second});

    // Undo puts the FIRST occurrence back -- the identity, not a number.
    REQUIRE(a.d.history.undo(a.d.document).has_value());
    CHECK(drawing::findAnnotation(a.d.document, id)->definition().target.object ==
          ObjectId{a.first});
    REQUIRE(a.d.history.redo(a.d.document).has_value());
    CHECK(drawing::findAnnotation(a.d.document, id)->definition().target.object ==
          ObjectId{a.second});
}

// --- Regeneration integration --------------------------------------------------------------------

TEST_CASE("Command_AfterACommandTheDrawingObjectsItReachesRegenerate",
          "[drawing][cmd][p14][regen]") {
    Simple s = simple();
    auto* created = run<drawing::CreateDimensionCommand>(s.d, "Width", widthDimension(s.view, s.block));
    const DimensionId dimension = created->dimensionId();

    const auto report = s.d.regenerate();
    CHECK(report.succeeded());
    CHECK(*s.d.regenerator.state(ObjectId{dimension}) == NodeState::Regenerated);

    // A move is a canonical change, so the view is regenerated again...
    run<drawing::MoveViewCommand>(s.d, s.view, Point2D{150_mm, 150_mm});
    s.d.registerAll();
    const auto after = s.d.regenerator.regenerate(s.d.document);
    REQUIRE(after.has_value());
    CHECK(std::ranges::find(after->regenerated, ObjectId{s.view}) != after->regenerated.end());
    // ...and what it draws follows, with nothing invalidated by hand.
    const auto drawn = drawing::projectedGeometry(s.d.document, s.view, s.d.bodies(), s.d.transforms());
    REQUIRE(drawn.has_value());
}

TEST_CASE("Command_ACommandThatBreaksADrawingCommitsTheIntentAndRegenerationReportsIt",
          "[drawing][cmd][p14][regen]") {
    // The stale-derived-state question, answered by the qualified semantics
    // rather than invented here. A drawing stores NO derived state (ADR-014),
    // so there is nothing to leave behind: the canonical edit commits, and
    // regeneration marks the object Failed. Option B of the brief, and it
    // holds because option A's danger -- new intent with old geometry still
    // presented as current -- cannot arise when no geometry is kept.
    Simple s = simple();
    auto* created = run<drawing::CreateDimensionCommand>(s.d, "Width", widthDimension(s.view, s.block));
    const DimensionId dimension = created->dimensionId();
    REQUIRE(s.d.regenerate().succeeded());

    // Retarget the dimension at a face role an extrude cannot produce. The
    // definition is well formed, so the command succeeds.
    DimensionDefinition impossible = drawing::findDimension(s.d.document, dimension)->definition();
    impossible.from = DimensionTarget{
        .plane = PlaneReference{.object = s.block.pad,
                                .face = FaceSelector{.role = FaceRole::HoleBottom}}};
    run<drawing::SetDimensionDefinitionCommand>(s.d, dimension, impossible);

    const auto report = s.d.regenerate();
    CHECK_FALSE(report.succeeded());
    CHECK(*s.d.regenerator.state(ObjectId{dimension}) == NodeState::Failed);
    // No stale answer is available: the drawing refuses rather than repeating
    // the last good number.
    CHECK_FALSE(drawing::measure(s.d.document, dimension, s.d.bodies(), s.d.transforms()).has_value());

    // Undo restores the intent, and the drawing resolves again.
    REQUIRE(s.d.history.undo(s.d.document).has_value());
    const auto healed = s.d.regenerate();
    CHECK(healed.succeeded());
    CHECK(*s.d.regenerator.state(ObjectId{dimension}) == NodeState::Regenerated);
    const auto measured = drawing::measure(s.d.document, dimension, s.d.bodies(), s.d.transforms());
    REQUIRE(measured.has_value());
    CHECK_THAT(measured->length->in(units::mm), WithinAbs(100.0, kMm));
}

// --- Drift ---------------------------------------------------------------------------------------

TEST_CASE("Command_AHundredMoveUndoRedoCyclesLandOnExactlyTheSameNumbers",
          "[drawing][cmd][p14][drift]") {
    // Undo restores the position that WAS there; it does not subtract the
    // delta that was added. The difference shows up only after many cycles,
    // which is why this runs a hundred and compares bit-for-bit through the
    // canonical serialization.
    Simple s = simple();
    const std::string start = s.d.canonical();
    const Point2D from = drawing::findView(s.d.document, s.view)->definition().placement;
    const Point2D to{200.1_mm, 150.3_mm};

    for (int cycle = 0; cycle < 100; ++cycle) {
        INFO("cycle " << cycle);
        run<drawing::MoveViewCommand>(s.d, s.view, to);
        REQUIRE(s.d.history.undo(s.d.document).has_value());
        REQUIRE(s.d.history.redo(s.d.document).has_value());
        REQUIRE(s.d.history.undo(s.d.document).has_value());
    }

    const Point2D end = drawing::findView(s.d.document, s.view)->definition().placement;
    CHECK(end.x.si() == from.x.si()); // exactly, not nearly
    CHECK(end.y.si() == from.y.si());
    CHECK(s.d.canonical() == start);
}

TEST_CASE("Command_TheIdAllocatorNeverRewindsSoARedoneObjectCannotCollide",
          "[drawing][cmd][p14]") {
    // The one piece of document bookkeeping that deliberately does NOT come
    // back on undo, and the reason it must not: a redo is holding an ID and
    // will put it back. If undo rewound the allocator, the next create would
    // hand that ID to something else and the redo would collide with it.
    Simple s = simple();
    std::vector<std::uint64_t> marks{s.d.watermark()};

    auto* first = run<drawing::CreateDimensionCommand>(s.d, "Width", widthDimension(s.view, s.block));
    marks.push_back(s.d.watermark());
    REQUIRE(s.d.history.undo(s.d.document).has_value());
    marks.push_back(s.d.watermark());

    // A NEW object, created while the first one is undone.
    auto* note = run<drawing::CreateAnnotationCommand>(
        s.d, "Note",
        AnnotationDefinition{.view = s.view,
                             .type = AnnotationType::Note,
                             .text = "NOTE",
                             .placement = Point2D{40_mm, 40_mm}});
    marks.push_back(s.d.watermark());

    CHECK(std::ranges::is_sorted(marks));
    // The note did not get the dimension's ID.
    CHECK_FALSE(ObjectId{note->annotationId()} == ObjectId{first->dimensionId()});

    // That new command dropped the redo branch, so the dimension is created
    // afresh rather than redone -- and correctly gets a NEW id, because it is
    // a new object. Both coexist.
    auto* again = run<drawing::CreateDimensionCommand>(s.d, "Width2", widthDimension(s.view, s.block));
    marks.push_back(s.d.watermark());
    CHECK(std::ranges::is_sorted(marks));
    CHECK_FALSE(ObjectId{again->dimensionId()} == ObjectId{note->annotationId()});
    CHECK(drawing::findAnnotation(s.d.document, note->annotationId()) != nullptr);
    CHECK(drawing::findDimension(s.d.document, again->dimensionId()) != nullptr);
}

TEST_CASE("Command_DeletingAViewAndUndoingItRestoresEveryReferenceToIt",
          "[drawing][cmd][p14]") {
    // Dependency structure, not just the object. A dimension and an
    // annotation both name the view; removeView() does not refuse for them
    // (only a projected CHILD stops it), so deleting it leaves both
    // unresolvable -- which regeneration now reports. Undo has to put the view
    // back so BOTH resolve again, with the same IDs on both sides of the edge.
    Simple s = simple();
    auto* dimension = run<drawing::CreateDimensionCommand>(s.d, "Width",
                                                           widthDimension(s.view, s.block));
    auto* note = run<drawing::CreateAnnotationCommand>(
        s.d, "Note",
        AnnotationDefinition{.view = s.view,
                             .type = AnnotationType::Note,
                             .text = "NOTE",
                             .placement = Point2D{40_mm, 40_mm}});
    const DimensionId dimensionId = dimension->dimensionId();
    const AnnotationId annotationId = note->annotationId();
    REQUIRE(s.d.regenerate().succeeded());
    const std::string before = s.d.canonical();

    run<drawing::DeleteViewCommand>(s.d, s.view);
    const auto broken = s.d.regenerate();
    CHECK_FALSE(broken.succeeded());
    // Both are reported, and neither was quietly re-pointed at something else.
    CHECK(drawing::findDimension(s.d.document, dimensionId)->definition().view == s.view);
    CHECK(drawing::findAnnotation(s.d.document, annotationId)->definition().view == s.view);

    REQUIRE(s.d.history.undo(s.d.document).has_value());
    CHECK(s.d.canonical() == before);
    const auto healed = s.d.regenerate();
    CHECK(healed.succeeded());
    CHECK(*s.d.regenerator.state(ObjectId{dimensionId}) == NodeState::Regenerated);
    CHECK(*s.d.regenerator.state(ObjectId{annotationId}) == NodeState::Regenerated);
    CHECK(drawing::dimensionsOn(s.d.document, s.view).size() == 1);
    CHECK(drawing::annotationsOn(s.d.document, s.view).size() == 1);
}

// --- Determinism -----------------------------------------------------------------------------------

TEST_CASE("Command_TheSameCommandSequenceFromTheSameBaselineGivesTheSameCanonicalState",
          "[drawing][cmd][p14][determinism]") {
    // The same script, run five times into five fresh documents, must give
    // five identical canonical states -- identical IDs included, because IDs
    // are allocated in a deterministic order from a deterministic baseline.
    const auto script = [] {
        Drawing d;
        const Block block = addBlock(d, "Block");
        d.regenerate();
        const SheetId sheet = run<drawing::CreateSheetCommand>(d, "Sheet1", a3())->sheetId();
        const ViewId view =
            run<drawing::CreateViewCommand>(d, "Front", baseView(sheet, block.pad))->viewId();
        run<drawing::MoveViewCommand>(d, view, Point2D{150_mm, 120_mm});
        run<drawing::CreateDimensionCommand>(d, "Width", widthDimension(view, block));
        run<drawing::CreateAnnotationCommand>(
            d, "Note",
            AnnotationDefinition{.view = view,
                                 .type = AnnotationType::Note,
                                 .text = "NOTE",
                                 .placement = Point2D{40_mm, 40_mm}});
        // ...and a round trip through the history, which must return to the
        // same place rather than merely somewhere that looks right.
        REQUIRE(d.history.undo(d.document).has_value());
        REQUIRE(d.history.undo(d.document).has_value());
        REQUIRE(d.history.redo(d.document).has_value());
        REQUIRE(d.history.redo(d.document).has_value());
        return std::pair{d.canonical(), d.history.undoCount()};
    };

    std::vector<std::string> states;
    std::vector<std::size_t> depths;
    for (int run5 = 0; run5 < 5; ++run5) {
        auto [state, depth] = script();
        states.push_back(std::move(state));
        depths.push_back(depth);
    }
    CHECK(std::ranges::adjacent_find(states, std::not_equal_to<>{}) == states.end());
    CHECK(std::ranges::adjacent_find(depths, std::not_equal_to<>{}) == depths.end());
    CHECK(depths.front() == 5);
}

TEST_CASE("Command_AWholeDrawingScriptUndoesAndRedoesThroughEveryState",
          "[drawing][cmd][p14][bom]") {
    // The brief's own sequence, on an assembly so the BOM and the balloon are
    // real rather than decorative: sheet, view, move, dimension, note, BOM,
    // balloon, move balloon. Every intermediate state recorded, then the whole
    // script walked down and back up.
    AssemblyDrawing a = twoOfOnePart(); // the sheet and the view already
    std::vector<std::string> states;
    states.push_back(a.d.canonical());

    run<drawing::MoveViewCommand>(a.d, a.view, Point2D{180_mm, 140_mm});
    states.push_back(a.d.canonical());

    run<drawing::CreateDimensionCommand>(
        a.d, "Height",
        DimensionDefinition{.view = a.view,
                            .type = DimensionType::Linear,
                            .from = DimensionTarget{.plane = PlaneReference{
                                        .object = a.part,
                                        .face = FaceSelector{.role = FaceRole::StartCap}}},
                            .to = DimensionTarget{.plane = PlaneReference{
                                      .object = a.part,
                                      .face = FaceSelector{.role = FaceRole::EndCap}}},
                            .format = drawing::DimensionFormat{.decimals = 2},
                            .placement = Point2D{120_mm, 100_mm}});
    states.push_back(a.d.canonical());

    run<drawing::CreateAnnotationCommand>(
        a.d, "Note",
        AnnotationDefinition{.view = a.view,
                             .type = AnnotationType::Note,
                             .text = "ASSEMBLE DRY",
                             .placement = Point2D{40_mm, 40_mm}});
    states.push_back(a.d.canonical());

    run<drawing::CreateAnnotationCommand>(
        a.d, "Bom",
        AnnotationDefinition{.view = a.view,
                             .type = AnnotationType::BomTable,
                             .placement = Point2D{240_mm, 200_mm}});
    states.push_back(a.d.canonical());

    auto* balloon = run<drawing::CreateAnnotationCommand>(
        a.d, "B1",
        AnnotationDefinition{.view = a.view,
                             .type = AnnotationType::Balloon,
                             .target = AnnotationTarget{.object = ObjectId{a.first}},
                             .placement = Point2D{40_mm, 60_mm}});
    states.push_back(a.d.canonical());

    run<drawing::MoveAnnotationCommand>(a.d, balloon->annotationId(), Point2D{50_mm, 70_mm});
    states.push_back(a.d.canonical());

    REQUIRE(states.size() == 7);
    REQUIRE(a.d.regenerate().succeeded());

    for (std::size_t step = 6; step > 0; --step) {
        INFO("undo to state " << (step - 1));
        REQUIRE(a.d.history.undo(a.d.document).has_value());
        CHECK(a.d.canonical() == states[step - 1]);
    }
    for (std::size_t step = 1; step <= 6; ++step) {
        INFO("redo to state " << step);
        REQUIRE(a.d.history.redo(a.d.document).has_value());
        CHECK(a.d.canonical() == states[step]);
    }
    // And the whole thing still regenerates, still draws, and the derived
    // table still says what the assembly says.
    REQUIRE(a.d.regenerate().succeeded());
    const auto drawn = drawing::drawAnnotations(a.d.document, a.view, a.d.bodies(), a.d.transforms());
    REQUIRE(drawn.has_value());
    CHECK(drawing::billOfMaterials(a.d.document, a.view)->totalOccurrences() == 2);
}

// --- The persistence boundary ----------------------------------------------------------------------

TEST_CASE("Command_HistoryIsSessionStateAndIsNotPartOfTheDocument", "[drawing][cmd][p14]") {
    // P14-PERSIST-001 is about drawing INTENT. Command history is not intent
    // and is not the document's: nothing here writes it, and a document
    // reloaded from its own serialization arrives with no history at all.
    Simple s = simple();
    run<drawing::MoveViewCommand>(s.d, s.view, Point2D{123_mm, 45_mm});
    REQUIRE(s.d.history.undoCount() >= 1);

    // The WHOLE file, not the redacted view: this is about what is written.
    auto json = io::documentToJson(s.d.document);
    REQUIRE(json.has_value());
    CHECK_THAT(*json, !ContainsSubstring("history"));
    CHECK_THAT(*json, !ContainsSubstring("\"undo\""));
    CHECK_THAT(*json, !ContainsSubstring("\"redo\""));
    CHECK_THAT(*json, !ContainsSubstring("command"));

    auto reloaded = io::documentFromJson(*json);
    REQUIRE(reloaded.has_value());
    // A reloaded document arrives with no history: there is nowhere in the
    // file for one, and nothing constructs one from it.
    CommandHistory fresh;
    CHECK_FALSE(fresh.canUndo());
    CHECK_FALSE(fresh.canRedo());
    const auto undone = fresh.undo(*reloaded);
    CHECK_FALSE(undone.has_value());

    // The canonical state survived the round trip exactly.
    auto again = io::documentToJson(*reloaded);
    REQUIRE(again.has_value());
    CHECK(*again == *json);
}
