#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/sketch/SketchCommands.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::sketch;

TEST_CASE("CreateSketchCommand creates a sketch that undo removes and redo restores",
          "[sketch][document]") {
    Document doc("Part");
    CommandHistory history;

    auto command = std::make_unique<CreateSketchCommand>("Sketch1");
    CreateSketchCommand* create = command.get();
    REQUIRE(history.execute(doc, std::move(command)).has_value());
    const SketchId id = create->sketchId();
    CHECK(id.isValid());
    CHECK(create->description() == "Create sketch 'Sketch1'");

    const Sketch* sketch = doc.findObjectAs<Sketch>(id);
    REQUIRE(sketch != nullptr);
    CHECK(sketch->sketchId() == id);
    CHECK(sketch->placement() == Frame3D::xy());
    CHECK(doc.findObjectByName("Sketch1") == sketch);
    const Document created = doc.clone();

    REQUIRE(history.undo(doc).has_value());
    CHECK(doc.findObject(id) == nullptr);
    REQUIRE(history.redo(doc).has_value());
    CHECK(doc.findObjectAs<Sketch>(id) != nullptr);
    CHECK(equivalent(doc, created));
}

TEST_CASE("Sketch edits inside a document are tracked", "[sketch][document]") {
    Document doc("Part");
    const auto id = doc.addObject(std::make_unique<Sketch>("Sketch1", Frame3D::xz()));
    REQUIRE(id.has_value());
    const auto revision = doc.revision();

    EntityId line;
    const auto changed = doc.modifyObject<Sketch>(*id, [&](Sketch& sketch) -> Result<bool> {
        auto added = sketch.addLine(Point2D{0_mm, 0_mm}, Point2D{100_mm, 0_mm});
        if (!added) {
            return std::unexpected(added.error());
        }
        line = *added;
        return true;
    });
    REQUIRE(changed.has_value());
    CHECK(doc.revision() == revision + 1);
    CHECK(doc.findObject(*id)->revision() == 2);

    const Sketch* sketch = doc.findObjectAs<Sketch>(*id);
    REQUIRE(sketch != nullptr);
    CHECK(sketch->length(line).value() == 100_mm);
    CHECK(sketch->toGlobal(sketch->endpoints(line)->end) == Point3D{100_mm, 0_mm, 0_mm});

    // A clone of the document carries an independent copy of the sketch.
    Document copy = doc.clone();
    CHECK(equivalent(doc, copy));
    REQUIRE(copy.modifyObject<Sketch>(*id, [](Sketch& s) {
                    return s.setPointPosition(EntityId::fromValue(2), Point2D{50_mm, 0_mm});
                }).value());
    CHECK_FALSE(equivalent(doc, copy));
    CHECK(sketch->length(line).value() == 100_mm);
}
