#include "TestObjects.hpp"
#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using bettercad::test::errorCode;
using bettercad::test::TestBlock;
using bettercad::test::TestMarker;

namespace {

ObjectId addBlock(Document& doc, std::string name, double size = 1.0) {
    auto id = doc.addObject(std::make_unique<TestBlock>(std::move(name), size));
    REQUIRE(id.has_value());
    return *id;
}

} // namespace

// --- Identity -------------------------------------------------------------------

TEST_CASE("A new document has an identity, a name and no content", "[document]") {
    const Document doc("Bracket");

    CHECK(doc.id().isValid());
    CHECK(doc.id().value().version() == 4);
    CHECK(doc.name() == "Bracket");
    CHECK(doc.parameters().empty());
    CHECK(doc.objectCount() == 0);
    CHECK(doc.revision() == 0);
    CHECK_FALSE(doc.isDirty());
    CHECK(doc.metadata() == DocumentMetadata{});
}

TEST_CASE("Documents get distinct IDs unless one is given", "[document]") {
    const Document a;
    const Document b;
    CHECK(a.id() != b.id());
    CHECK(a.name() == "Untitled");

    const auto uuid = Uuid::parse("123e4567-e89b-42d3-a456-426614174000");
    REQUIRE(uuid.has_value());
    const Document known(DocumentId::fromValue(*uuid), "Known");
    CHECK(known.id().value() == *uuid);
}

TEST_CASE("Document names are validated and renames are tracked", "[document]") {
    Document doc("Bracket");

    CHECK(errorCode(doc.setName("")) == ErrorCode::InvalidArgument);
    CHECK(errorCode(doc.setName("bad\nname")) == ErrorCode::InvalidArgument);
    CHECK(errorCode(doc.setName(std::string(256, 'x'))) == ErrorCode::InvalidArgument);
    CHECK(doc.name() == "Bracket");
    CHECK(doc.revision() == 0);

    REQUIRE(doc.setName("Bracket v2 (steel)").value());
    CHECK(doc.name() == "Bracket v2 (steel)");
    CHECK(doc.revision() == 1);
    CHECK_FALSE(doc.setName("Bracket v2 (steel)").value());
    CHECK(doc.revision() == 1);
}

TEST_CASE("Metadata changes are tracked", "[document]") {
    Document doc;
    DocumentMetadata metadata;
    metadata.author = "A. Engineer";
    metadata.description = "Mounting bracket";
    metadata.properties["material"] = "S355";

    REQUIRE(doc.setMetadata(metadata).value());
    CHECK(doc.metadata() == metadata);
    CHECK(doc.revision() == 1);
    CHECK_FALSE(doc.setMetadata(metadata).value());
    CHECK(doc.revision() == 1);
}

// --- Contents -------------------------------------------------------------------

TEST_CASE("Parameters and objects share one ID space", "[document]") {
    Document doc;
    const auto width = doc.createParameter("width", 100_mm, units::mm);
    const ObjectId block = addBlock(doc, "Block1");
    const auto height = doc.createParameter("height", 50_mm, units::mm);

    REQUIRE(width.has_value());
    REQUIRE(height.has_value());
    CHECK(width->value() == 1);
    CHECK(block.value() == 2);
    CHECK(height->value() == 3);
    CHECK(doc.lastAllocatedId() == 3);
}

TEST_CASE("Objects can be added, found by ID and name, and removed", "[document]") {
    Document doc;
    const ObjectId id = addBlock(doc, "Block1", 2.5);

    const DocumentObject* found = doc.findObject(id);
    REQUIRE(found != nullptr);
    CHECK(found->id() == id);
    CHECK(found->name() == "Block1");
    CHECK(found->typeName() == "test_block");
    CHECK(doc.findObjectByName("Block1") == found);
    CHECK(doc.objectCount() == 1);

    auto removed = doc.removeObject(id);
    REQUIRE(removed.has_value());
    CHECK((*removed)->name() == "Block1");
    CHECK(doc.findObject(id) == nullptr);
    CHECK(doc.findObjectByName("Block1") == nullptr);
    CHECK(errorCode(doc.removeObject(id)) == ErrorCode::NotFound);
}

TEST_CASE("Typed lookup checks the object kind", "[document]") {
    Document doc;
    const ObjectId block = addBlock(doc, "Block1", 4.0);
    const auto marker = doc.addObject(std::make_unique<TestMarker>("Marker1"));
    REQUIRE(marker.has_value());

    const TestBlock* typed = doc.findObjectAs<TestBlock>(block);
    REQUIRE(typed != nullptr);
    CHECK(typed->size() == 4.0);
    CHECK(doc.findObjectAs<TestBlock>(*marker) == nullptr);
    CHECK(doc.findObjectAs<TestBlock>(ObjectId::fromValue(99)) == nullptr);
}

TEST_CASE("Objects are listed in ID order", "[document]") {
    Document doc;
    addBlock(doc, "Zeta");
    addBlock(doc, "Alpha");
    addBlock(doc, "Mid");

    std::vector<std::string> names;
    for (const DocumentObject& object : doc.objects()) {
        names.push_back(object.name());
    }
    CHECK(names == std::vector<std::string>{"Zeta", "Alpha", "Mid"});
}

TEST_CASE("Names are unique across parameters and objects", "[document]") {
    Document doc;
    REQUIRE(doc.createParameter("width", 100_mm, units::mm).has_value());
    addBlock(doc, "Block1");

    CHECK(errorCode(doc.addObject(std::make_unique<TestBlock>("width", 1.0))) ==
          ErrorCode::AlreadyExists);
    CHECK(errorCode(doc.createParameter("Block1", 1_mm, units::mm)) == ErrorCode::AlreadyExists);
    CHECK(errorCode(doc.addObject(std::make_unique<TestBlock>("Block1", 1.0))) ==
          ErrorCode::AlreadyExists);
    CHECK(errorCode(doc.addObject(std::make_unique<TestBlock>("Block 2", 1.0))) ==
          ErrorCode::InvalidArgument);
    CHECK(errorCode(doc.addObject(nullptr)) == ErrorCode::InvalidArgument);
    CHECK(doc.objectCount() == 1);
}

TEST_CASE("Lookup by name and ID works across kinds", "[document]") {
    Document doc;
    const auto width = doc.createParameter("width", 100_mm, units::mm);
    const ObjectId block = addBlock(doc, "Block1");
    REQUIRE(width.has_value());

    CHECK(doc.findByName("width") == ObjectId{*width});
    CHECK(doc.findByName("Block1") == block);
    CHECK_FALSE(doc.findByName("missing").has_value());
    CHECK(doc.nameOf(*width) == "width");
    CHECK(doc.nameOf(block) == "Block1");
    CHECK(doc.contains(*width));
    CHECK(doc.contains(block));
    CHECK(doc.asParameter(*width) == *width);
    CHECK_FALSE(doc.asParameter(block).has_value());
}

TEST_CASE("Renaming works for parameters and objects and keeps names unique", "[document]") {
    Document doc;
    const auto width = doc.createParameter("width", 100_mm, units::mm);
    const ObjectId block = addBlock(doc, "Block1");
    REQUIRE(width.has_value());

    REQUIRE(doc.rename(*width, "plate_width").value());
    REQUIRE(doc.rename(block, "Plate").value());
    CHECK(doc.findByName("plate_width") == ObjectId{*width});
    CHECK(doc.findObjectByName("Plate")->id() == block);
    CHECK(doc.findObject(block)->revision() == 2);

    CHECK(errorCode(doc.rename(block, "plate_width")) == ErrorCode::AlreadyExists);
    CHECK(errorCode(doc.rename(*width, "Plate")) == ErrorCode::AlreadyExists);
    CHECK(errorCode(doc.rename(block, "not valid")) == ErrorCode::InvalidArgument);
    CHECK(errorCode(doc.rename(ObjectId::fromValue(42), "x")) == ErrorCode::NotFound);
    CHECK_FALSE(doc.rename(block, "Plate").value());
}

TEST_CASE("IDs are never reused and insertObject restores an ID", "[document]") {
    Document doc;
    addBlock(doc, "Block1");
    const ObjectId second = addBlock(doc, "Block2");

    auto removed = doc.removeObject(second);
    REQUIRE(removed.has_value());
    const ObjectId third = addBlock(doc, "Block3");
    CHECK(third.value() == 3); // not 2

    REQUIRE(doc.insertObject(std::move(*removed)).has_value());
    CHECK(doc.findObject(second)->name() == "Block2");
    CHECK(errorCode(doc.insertObject(doc.findObject(second)->clone())) == ErrorCode::AlreadyExists);

    // An inserted object with a higher ID (e.g. from a loaded file) reserves
    // that ID, so later allocations continue above it.
    Document source;
    ObjectId high;
    for (int i = 1; i <= 7; ++i) {
        high = addBlock(source, "Src" + std::to_string(i));
    }
    REQUIRE(high.value() == 7);
    REQUIRE(doc.insertObject(source.findObject(high)->clone()).has_value());
    CHECK(doc.lastAllocatedId() == 7);
    CHECK(addBlock(doc, "Block4").value() == 8);
}

TEST_CASE("addObject needs a new object and insertObject an identified one", "[document]") {
    Document doc;
    const ObjectId id = addBlock(doc, "Block1");
    auto copy = doc.findObject(id)->clone();
    CHECK(errorCode(doc.addObject(std::move(copy))) == ErrorCode::InvalidArgument);
    CHECK(errorCode(doc.insertObject(std::make_unique<TestBlock>("Fresh", 1.0))) ==
          ErrorCode::InvalidArgument);
}

TEST_CASE("modifyObject tracks changes and checks the kind", "[document]") {
    Document doc;
    const ObjectId block = addBlock(doc, "Block1", 1.0);
    const auto marker = doc.addObject(std::make_unique<TestMarker>("Marker1"));
    REQUIRE(marker.has_value());
    const auto revision = doc.revision();

    REQUIRE(doc.modifyObject<TestBlock>(block, [](TestBlock& b) { return b.setSize(3.0); }).value());
    CHECK(doc.findObjectAs<TestBlock>(block)->size() == 3.0);
    CHECK(doc.findObject(block)->revision() == 2);
    CHECK(doc.revision() == revision + 1);

    CHECK_FALSE(doc.modifyObject<TestBlock>(block, [](TestBlock& b) { return b.setSize(3.0); }).value());
    CHECK(doc.revision() == revision + 1);

    const auto wrongKind =
        doc.modifyObject<TestBlock>(*marker, [](TestBlock& b) { return b.setSize(1.0); });
    CHECK(errorCode(wrongKind) == ErrorCode::FailedPrecondition);
    const auto missing =
        doc.modifyObject<TestBlock>(ObjectId::fromValue(77), [](TestBlock& b) { return b.setSize(1.0); });
    CHECK(errorCode(missing) == ErrorCode::NotFound);
}

TEST_CASE("uniqueName finds the first free numbered name", "[document]") {
    Document doc;
    CHECK(doc.uniqueName("Block") == "Block1");
    addBlock(doc, "Block1");
    REQUIRE(doc.createParameter("Block2", 1_mm, units::mm).has_value());
    CHECK(doc.uniqueName("Block") == "Block3");
}

// --- Change tracking ------------------------------------------------------------

TEST_CASE("Dirty state follows effective changes since markClean", "[document]") {
    Document doc;
    const auto width = doc.createParameter("width", 100_mm, units::mm);
    REQUIRE(width.has_value());
    CHECK(doc.isDirty());

    doc.markClean();
    CHECK_FALSE(doc.isDirty());

    CHECK_FALSE(doc.setParameterValue(*width, 100_mm).value()); // no-op
    CHECK_FALSE(doc.isDirty());
    CHECK_FALSE(doc.setParameterValue(*width, 5_kg).has_value()); // failure
    CHECK_FALSE(doc.isDirty());

    REQUIRE(doc.setParameterValue(*width, 120_mm).value());
    CHECK(doc.isDirty());
    doc.markClean();
    CHECK_FALSE(doc.isDirty());
}

TEST_CASE("The revision counts every effective change", "[document]") {
    Document doc;
    const auto width = doc.createParameter("width", 100_mm, units::mm); // 1
    REQUIRE(width.has_value());
    REQUIRE(doc.setParameterValue(*width, 110_mm).value());             // 2
    REQUIRE(doc.setParameterDisplayUnit(*width, describe(units::cm)).value()); // 3
    REQUIRE(doc.setParameterExpression(*width, "2 * height").value());  // 4
    const ObjectId block = addBlock(doc, "Block1");                     // 5
    REQUIRE(doc.rename(block, "Plate").value());                        // 6
    REQUIRE(doc.removeObject(block).has_value());                       // 7
    REQUIRE(doc.removeParameter(*width).has_value());                   // 8
    CHECK(doc.revision() == 8);
}

TEST_CASE("restoreParameter makes a parameter equal to a stored state", "[document]") {
    Document doc;
    const auto width = doc.createParameter("width", 100_mm, units::mm);
    REQUIRE(width.has_value());
    const Parameter before = *doc.parameters().find(*width);

    REQUIRE(doc.rename(*width, "plate_width").value());
    REQUIRE(doc.setParameterValue(*width, 250_mm).value());
    REQUIRE(doc.setParameterExpression(*width, "height * 2").value());

    REQUIRE(doc.restoreParameter(before).value());
    CHECK(equivalent(*doc.parameters().find(*width), before));
    CHECK(doc.parameters().find(*width)->revision() > before.revision());
    CHECK(doc.findByName("width") == ObjectId{*width});
}

// --- Copies and equivalence -------------------------------------------------------

TEST_CASE("clone produces an equivalent, independent document", "[document]") {
    Document doc("Bracket");
    REQUIRE(doc.createParameter("width", 100_mm, units::mm).has_value());
    const ObjectId block = addBlock(doc, "Block1", 2.0);

    Document copy = doc.clone();
    CHECK(equivalent(doc, copy));
    CHECK(copy.revision() == doc.revision());

    REQUIRE(copy.modifyObject<TestBlock>(block, [](TestBlock& b) { return b.setSize(9.0); }).value());
    CHECK(doc.findObjectAs<TestBlock>(block)->size() == 2.0);
    CHECK_FALSE(equivalent(doc, copy));
}

TEST_CASE("equivalent compares identity and content, not revisions", "[document]") {
    Document a("Bracket");
    REQUIRE(a.createParameter("width", 100_mm, units::mm).has_value());
    addBlock(a, "Block1", 2.0);
    Document b = a.clone();

    const auto width = ParameterId::fromValue(1);
    REQUIRE(b.setParameterValue(width, 150_mm).value());
    REQUIRE(b.setParameterValue(width, 100_mm).value());
    CHECK(b.revision() != a.revision());
    CHECK(equivalent(a, b));

    SECTION("name") {
        REQUIRE(b.setName("Other").value());
        CHECK_FALSE(equivalent(a, b));
    }
    SECTION("metadata") {
        REQUIRE(b.setMetadata({.description = "x", .author = "", .properties = {}}).value());
        CHECK_FALSE(equivalent(a, b));
    }
    SECTION("parameter value") {
        REQUIRE(b.setParameterValue(width, 101_mm).value());
        CHECK_FALSE(equivalent(a, b));
    }
    SECTION("object content") {
        REQUIRE(b.modifyObject<TestBlock>(ObjectId::fromValue(2), [](TestBlock& t) {
                     return t.setSize(3.0);
                 }).value());
        CHECK_FALSE(equivalent(a, b));
    }
    SECTION("identity") {
        const Document c(DocumentId::fromValue(Uuid::generateV4()), "Bracket");
        CHECK_FALSE(equivalent(a, c));
    }
}
