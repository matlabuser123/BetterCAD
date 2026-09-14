#include "TestObjects.hpp"
#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using bettercad::test::errorCode;
using bettercad::test::TestBlock;

namespace {

Document makeDocument() {
    // A fixed identity makes documents from separate runs comparable.
    return Document(DocumentId::fromValue(*Uuid::parse("00000000-0000-4000-8000-000000000001")),
                    "Commands");
}

template <typename CommandType, typename... Args>
CommandType* run(CommandHistory& history, Document& doc, Args&&... args) {
    auto command = std::make_unique<CommandType>(std::forward<Args>(args)...);
    CommandType* raw = command.get();
    const auto result = history.execute(doc, std::move(command));
    INFO((result ? std::string{} : result.error().message));
    REQUIRE(result.has_value());
    return raw;
}

// Executes, undoes and redoes one command, checking the document against
// snapshots at each step.
template <typename CommandType, typename... Args>
void checkRoundTrip(Document& doc, Args&&... args) {
    CommandHistory history;
    const Document before = doc.clone();
    run<CommandType>(history, doc, std::forward<Args>(args)...);
    const Document after = doc.clone();
    CHECK_FALSE(equivalent(before, after));

    REQUIRE(history.undo(doc).has_value());
    CHECK(equivalent(doc, before));
    REQUIRE(history.redo(doc).has_value());
    CHECK(equivalent(doc, after));
    REQUIRE(history.undo(doc).has_value());
    CHECK(equivalent(doc, before));
}

} // namespace

// --- Individual commands -----------------------------------------------------------

TEST_CASE("CreateParameterCommand executes, undoes and redoes with the same ID", "[commands]") {
    Document doc = makeDocument();
    CommandHistory history;

    auto* create = run<CreateParameterCommand>(history, doc, "width", 100_mm, units::mm);
    const ParameterId id = create->parameterId();
    CHECK(id.value() == 1);
    CHECK(doc.parameters().find(id)->displayValue() == 100.0);
    CHECK(create->description() == "Create parameter 'width'");

    REQUIRE(history.undo(doc).has_value());
    CHECK(doc.parameters().empty());

    REQUIRE(history.redo(doc).has_value());
    REQUIRE(doc.parameters().find(id) != nullptr);
    CHECK(doc.parameters().find(id)->name() == "width");

    // A new creation after undo gets a fresh ID: IDs are never reused.
    REQUIRE(history.undo(doc).has_value());
    auto* again = run<CreateParameterCommand>(history, doc, "width", 100_mm, units::mm);
    CHECK(again->parameterId().value() == 2);
}

TEST_CASE("ModifyParameterCommand round-trips every kind of change", "[commands]") {
    Document doc = makeDocument();
    const auto width = doc.createParameter("width", 100_mm, units::mm);
    REQUIRE(width.has_value());

    SECTION("value") {
        checkRoundTrip<ModifyParameterCommand>(
            doc, *width, ParameterChanges{.value = DimensionedValue::of(120_mm)});
    }
    SECTION("name") {
        checkRoundTrip<ModifyParameterCommand>(doc, *width, ParameterChanges{.name = "plate_width"});
    }
    SECTION("display unit") {
        checkRoundTrip<ModifyParameterCommand>(
            doc, *width, ParameterChanges{.displayUnit = describe(units::inch)});
    }
    SECTION("expression") {
        checkRoundTrip<ModifyParameterCommand>(
            doc, *width, ParameterChanges{.expression = std::optional<std::string>{"height * 2"}});
    }
    SECTION("everything at once") {
        checkRoundTrip<ModifyParameterCommand>(
            doc, *width,
            ParameterChanges{.name = "w",
                             .value = DimensionedValue::of(3_in),
                             .displayUnit = describe(units::inch),
                             .expression = std::optional<std::string>{"h / 2"}});
    }
}

TEST_CASE("ModifyParameterCommand is atomic", "[commands]") {
    Document doc = makeDocument();
    const auto width = doc.createParameter("width", 100_mm, units::mm);
    REQUIRE(doc.createParameter("height", 50_mm, units::mm).has_value());
    REQUIRE(width.has_value());
    const Document before = doc.clone();
    CommandHistory history;

    SECTION("a valid value with a name that is taken") {
        auto command = std::make_unique<ModifyParameterCommand>(
            *width, ParameterChanges{.name = "height", .value = DimensionedValue::of(200_mm)});
        CHECK(errorCode(history.execute(doc, std::move(command))) == ErrorCode::AlreadyExists);
    }
    SECTION("a valid name with a value of the wrong dimension") {
        auto command = std::make_unique<ModifyParameterCommand>(
            *width, ParameterChanges{.name = "plate_width", .value = DimensionedValue::of(45_deg)});
        CHECK(errorCode(history.execute(doc, std::move(command))) == ErrorCode::DimensionMismatch);
    }
    SECTION("a missing parameter") {
        CHECK(errorCode(history.execute(
                  doc, ModifyParameterCommand::setValue(ParameterId::fromValue(99), 1_mm))) ==
              ErrorCode::NotFound);
    }

    CHECK(equivalent(doc, before));
    CHECK_FALSE(history.canUndo());
}

TEST_CASE("AddObjectCommand re-adds the same object with the same ID", "[commands]") {
    Document doc = makeDocument();
    CommandHistory history;

    auto* add = run<AddObjectCommand>(history, doc, std::make_unique<TestBlock>("Block1", 2.0));
    const ObjectId id = add->objectId();
    CHECK(id.value() == 1);
    CHECK(add->description() == "Add 'Block1'");
    const Document after = doc.clone();

    REQUIRE(history.undo(doc).has_value());
    CHECK(doc.objectCount() == 0);
    REQUIRE(history.redo(doc).has_value());
    CHECK(equivalent(doc, after));
    CHECK(doc.findObjectAs<TestBlock>(id)->size() == 2.0);
}

TEST_CASE("DeleteObjectCommand deletes and restores objects and parameters", "[commands]") {
    Document doc = makeDocument();
    const auto width = doc.createParameter("width", 100_mm, units::mm);
    REQUIRE(width.has_value());
    const auto block = doc.addObject(std::make_unique<TestBlock>("Block1", 2.0));
    REQUIRE(block.has_value());

    SECTION("object") {
        checkRoundTrip<DeleteObjectCommand>(doc, *block);
    }
    SECTION("parameter") {
        checkRoundTrip<DeleteObjectCommand>(doc, ObjectId{*width});
    }
    SECTION("missing item") {
        CommandHistory history;
        CHECK(errorCode(history.execute(
                  doc, std::make_unique<DeleteObjectCommand>(ObjectId::fromValue(50)))) ==
              ErrorCode::NotFound);
    }
}

TEST_CASE("RenameObjectCommand renames parameters and objects", "[commands]") {
    Document doc = makeDocument();
    const auto width = doc.createParameter("width", 100_mm, units::mm);
    REQUIRE(width.has_value());
    const auto block = doc.addObject(std::make_unique<TestBlock>("Block1", 2.0));
    REQUIRE(block.has_value());

    SECTION("parameter") {
        checkRoundTrip<RenameObjectCommand>(doc, ObjectId{*width}, "plate_width");
    }
    SECTION("object") {
        checkRoundTrip<RenameObjectCommand>(doc, *block, "Plate");
    }
    SECTION("description") {
        CommandHistory history;
        auto* rename = run<RenameObjectCommand>(history, doc, *block, "Plate");
        CHECK(rename->description() == "Rename 'Block1' to 'Plate'");
    }
}

// --- History ----------------------------------------------------------------------

TEST_CASE("CommandHistory keeps undo and redo stacks", "[commands][history]") {
    Document doc = makeDocument();
    CommandHistory history;
    CHECK_FALSE(history.canUndo());
    CHECK_FALSE(history.canRedo());

    run<CreateParameterCommand>(history, doc, "width", 100_mm, units::mm);
    run<CreateParameterCommand>(history, doc, "height", 50_mm, units::mm);
    CHECK(history.undoCount() == 2);
    CHECK(history.undoDescription() == "Create parameter 'height'");

    REQUIRE(history.undo(doc).has_value());
    CHECK(history.undoCount() == 1);
    CHECK(history.redoCount() == 1);
    CHECK(history.redoDescription() == "Create parameter 'height'");

    // A new command discards the redo stack.
    run<CreateParameterCommand>(history, doc, "depth", 20_mm, units::mm);
    CHECK_FALSE(history.canRedo());
    CHECK(history.undoCount() == 2);
}

TEST_CASE("Failed commands are not recorded", "[commands][history]") {
    Document doc = makeDocument();
    CommandHistory history;
    run<CreateParameterCommand>(history, doc, "width", 100_mm, units::mm);
    const Document before = doc.clone();

    const auto duplicate =
        history.execute(doc, std::make_unique<CreateParameterCommand>("width", 1_mm, units::mm));
    CHECK(errorCode(duplicate) == ErrorCode::AlreadyExists);
    CHECK(history.undoCount() == 1);
    CHECK(equivalent(doc, before));
    CHECK(errorCode(history.execute(doc, nullptr)) == ErrorCode::InvalidArgument);
}

TEST_CASE("Undo and redo report when there is nothing to do", "[commands][history]") {
    Document doc = makeDocument();
    CommandHistory history;
    CHECK(errorCode(history.undo(doc)) == ErrorCode::FailedPrecondition);
    CHECK(errorCode(history.redo(doc)) == ErrorCode::FailedPrecondition);
    CHECK_FALSE(history.undoDescription().has_value());
}

TEST_CASE("A history refuses to operate on another document", "[commands][history]") {
    Document first = makeDocument();
    Document second("Other");
    CommandHistory history;
    run<CreateParameterCommand>(history, first, "width", 100_mm, units::mm);

    CHECK(errorCode(history.undo(second)) == ErrorCode::FailedPrecondition);
    CHECK(errorCode(history.execute(
              second, std::make_unique<CreateParameterCommand>("x", 1_mm, units::mm))) ==
          ErrorCode::FailedPrecondition);
    CHECK(second.parameters().empty());

    history.clear();
    CHECK_FALSE(history.canUndo());
    run<CreateParameterCommand>(history, second, "x", 1_mm, units::mm);
}

TEST_CASE("A history limit drops the oldest commands", "[commands][history]") {
    Document doc = makeDocument();
    CommandHistory history(2);
    for (const char* name : {"a", "b", "c"}) {
        run<CreateParameterCommand>(history, doc, name, 1_mm, units::mm);
    }
    CHECK(history.undoCount() == 2);
    REQUIRE(history.undo(doc).has_value());
    REQUIRE(history.undo(doc).has_value());
    CHECK_FALSE(history.canUndo());
    CHECK(doc.parameters().findByName("a") != nullptr);
    CHECK(doc.parameters().size() == 1);
}

TEST_CASE("Commands make the document dirty; undo after saving does too", "[commands][history]") {
    Document doc = makeDocument();
    CommandHistory history;
    run<CreateParameterCommand>(history, doc, "width", 100_mm, units::mm);
    CHECK(doc.isDirty());

    doc.markClean();
    run<CreateParameterCommand>(history, doc, "height", 50_mm, units::mm);
    CHECK(doc.isDirty());

    doc.markClean();
    REQUIRE(history.undo(doc).has_value());
    // Dirty tracking is revision based and conservative: the undone state
    // differs from what was saved.
    CHECK(doc.isDirty());
}

// --- Acceptance: edit / undo / redo is deterministic --------------------------------

TEST_CASE("Edit, undo and redo return the document to equivalent states", "[commands][acceptance]") {
    Document doc = makeDocument();
    CommandHistory history;
    std::vector<Document> states;
    states.push_back(doc.clone());

    auto* width = run<CreateParameterCommand>(history, doc, "width", 100_mm, units::mm);
    states.push_back(doc.clone());
    run<CreateParameterCommand>(history, doc, "height", 50_mm, units::mm);
    states.push_back(doc.clone());
    auto* block = run<AddObjectCommand>(history, doc, std::make_unique<TestBlock>("Block1", 2.0));
    states.push_back(doc.clone());
    REQUIRE(history.execute(doc, ModifyParameterCommand::setValue(width->parameterId(), 120_mm))
                .has_value());
    states.push_back(doc.clone());
    run<RenameObjectCommand>(history, doc, block->objectId(), "Plate");
    states.push_back(doc.clone());
    run<DeleteObjectCommand>(history, doc, ObjectId{width->parameterId()});
    states.push_back(doc.clone());

    const std::size_t count = states.size() - 1;
    for (std::size_t i = count; i > 0; --i) {
        REQUIRE(history.undo(doc).has_value());
        CAPTURE(i);
        CHECK(equivalent(doc, states[i - 1]));
    }
    CHECK_FALSE(history.canUndo());
    for (std::size_t i = 1; i <= count; ++i) {
        REQUIRE(history.redo(doc).has_value());
        CAPTURE(i);
        CHECK(equivalent(doc, states[i]));
    }
    CHECK_FALSE(history.canRedo());
}

namespace {

// Random edit sessions: commands (some of which fail), undos and redos. The
// document must always equal the snapshot for the current history position.
struct RandomSession {
    explicit RandomSession(std::uint32_t seed) : rng(seed), doc(makeDocument()) {
        timeline.push_back(doc.clone());
    }

    std::mt19937 rng;
    Document doc;
    CommandHistory history;
    std::vector<Document> timeline; // timeline[i]: state after i applied commands
    std::size_t position = 0;
    int executed = 0;
    int failed = 0;

    std::size_t pick(std::size_t n) { return std::uniform_int_distribution<std::size_t>(0, n - 1)(rng); }

    ObjectId randomExistingItem() {
        std::vector<ObjectId> ids;
        for (const Parameter& p : doc.parameters().all()) {
            ids.push_back(p.id());
        }
        for (const DocumentObject& o : doc.objects()) {
            ids.push_back(o.id());
        }
        return ids.empty() ? ObjectId::fromValue(1000) : ids[pick(ids.size())];
    }

    std::unique_ptr<Command> randomCommand() {
        const std::string name = std::string(1, static_cast<char>('a' + pick(6)));
        switch (pick(6)) {
        case 0:
            return std::make_unique<CreateParameterCommand>(
                name, static_cast<double>(pick(100) + 1) * units::mm, units::mm);
        case 1: {
            const ObjectId item = randomExistingItem();
            return ModifyParameterCommand::setValue(ParameterId::fromValue(item.value()),
                                                    static_cast<double>(pick(100)) * units::mm);
        }
        case 2:
            return std::make_unique<AddObjectCommand>(
                std::make_unique<TestBlock>("B" + name, static_cast<double>(pick(10))));
        case 3:
            return std::make_unique<DeleteObjectCommand>(randomExistingItem());
        case 4:
            return std::make_unique<RenameObjectCommand>(randomExistingItem(), "n" + name);
        default: {
            // Rename plus value change; the angle variant is always rejected
            // (all parameters here are lengths) and must leave no trace.
            ParameterChanges changes{.name = "p" + name};
            if (pick(2) == 0) {
                changes.value = DimensionedValue::of(static_cast<double>(pick(50)) * units::mm);
            } else {
                changes.value = DimensionedValue::of(1_deg);
            }
            return std::make_unique<ModifyParameterCommand>(
                ParameterId::fromValue(randomExistingItem().value()), std::move(changes));
        }
        }
    }

    void step() {
        const std::size_t action = pick(4);
        if (action <= 1) {
            const bool ok = history.execute(doc, randomCommand()).has_value();
            if (ok) {
                timeline.resize(position + 1);
                timeline.push_back(doc.clone());
                ++position;
                ++executed;
            } else {
                ++failed;
            }
        } else if (action == 2 && history.canUndo()) {
            REQUIRE(history.undo(doc).has_value());
            --position;
        } else if (action == 3 && history.canRedo()) {
            REQUIRE(history.redo(doc).has_value());
            ++position;
        }
        REQUIRE(history.undoCount() == position);
        REQUIRE(equivalent(doc, timeline[position]));
    }
};

} // namespace

TEST_CASE("Random edit, undo and redo sequences stay consistent and deterministic",
          "[commands][acceptance]") {
    const std::uint32_t seed = 20260914;
    RandomSession first(seed);
    for (int i = 0; i < 500; ++i) {
        first.step();
    }
    // Both successful and failing commands were exercised.
    CHECK(first.executed > 50);
    CHECK(first.failed > 20);

    // Undo everything, then redo everything.
    while (first.history.canUndo()) {
        REQUIRE(first.history.undo(first.doc).has_value());
    }
    CHECK(equivalent(first.doc, first.timeline.front()));
    while (first.history.canRedo()) {
        REQUIRE(first.history.redo(first.doc).has_value());
    }
    CHECK(equivalent(first.doc, first.timeline.back()));

    // The same session replayed produces the same document, IDs included.
    RandomSession second(seed);
    for (int i = 0; i < 500; ++i) {
        second.step();
    }
    CHECK(equivalent(first.timeline.back(), second.timeline.back()));
    CHECK(first.doc.lastAllocatedId() == second.doc.lastAllocatedId());
}
