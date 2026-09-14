#include "TestHelpers.hpp"
#include "core/document/TestObjects.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/BracketModel.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/core/Uuid.hpp>
#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <bit>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using namespace bettercad::sketch;
using bettercad::test::BracketModel;
using bettercad::test::describe;
using bettercad::test::errorCode;
using bettercad::test::readFile;
using bettercad::test::require;
using bettercad::test::requireReport;
using bettercad::test::TempDir;
using bettercad::test::volumeMm3;
using bettercad::test::writeFile;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;

namespace {

constexpr double pi = std::numbers::pi;
constexpr double kRel = 1e-12;

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

SketchId sketchId(ObjectId id) {
    return SketchId::fromValue(id.value());
}

using Model = BracketModel;
constexpr double kPadVolume = BracketModel::kPadVolume;
constexpr double kPocketVolume = BracketModel::kPocketVolume;
constexpr double kSlotVolume = BracketModel::kSlotVolume;

/// The two regenerators hold bit-identical geometry for @p feature.
void checkSameGeometry(const Regenerator& expected, const Regenerator& actual, ObjectId feature) {
    INFO("feature " << feature);
    const geometry::Body* a = expected.body(feature);
    const geometry::Body* b = actual.body(feature);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    const auto pa = a->massProperties();
    const auto pb = b->massProperties();
    REQUIRE(pa.has_value());
    REQUIRE(pb.has_value());
    CHECK(bits(pa->volume.si()) == bits(pb->volume.si()));
    CHECK(bits(pa->surfaceArea.si()) == bits(pb->surfaceArea.si()));
    CHECK(bits(pa->centerOfMass.x.si()) == bits(pb->centerOfMass.x.si()));
    CHECK(bits(pa->centerOfMass.y.si()) == bits(pb->centerOfMass.y.si()));
    CHECK(bits(pa->centerOfMass.z.si()) == bits(pb->centerOfMass.z.si()));
    CHECK(a->boundingBox().value() == b->boundingBox().value());
    CHECK(a->topology() == b->topology());
}

/// Same nodes, same edges, same evaluation order.
void checkSameGraph(const Document& expected, const Document& actual) {
    const DocumentGraph a = buildDependencyGraph(expected);
    const DocumentGraph b = buildDependencyGraph(actual);
    REQUIRE(a.graph.nodes() == b.graph.nodes());
    for (const ObjectId node : a.graph.nodes()) {
        INFO("node " << node);
        CHECK(a.graph.dependenciesOf(node) == b.graph.dependenciesOf(node));
    }
    CHECK(a.missing == b.missing);
    CHECK(a.graph.topologicalOrder().order == b.graph.topologicalOrder().order);
}

// --- A small document with a known file text ------------------------------------------------

constexpr std::string_view kGoldenId = "0f8fad5b-d9cb-469f-a165-70867728950e";

Document makeGoldenDocument() {
    Document doc(DocumentId::fromValue(*Uuid::parse(kGoldenId)), "Plate");
    REQUIRE(doc.setMetadata({.description = "Golden file", .author = "tests", .properties = {{"material", "6061-T6"}}})
                .has_value());
    const ParameterId thickness = doc.createParameter("thickness", 5_mm, units::mm).value();
    auto disc = std::make_unique<Sketch>("Disc");
    const EntityId circle = require(disc->addCircle(Point2D{}, 10_mm));
    require(disc->addRadius(circle, 10_mm));
    const ObjectId sketch = doc.addObject(std::move(disc)).value();
    auto pad = ExtrudeFeature::create("Pad", {.profile = sketchId(sketch), .depthParameter = thickness});
    REQUIRE(pad.has_value());
    REQUIRE(doc.addObject(std::move(*pad)).has_value());
    return doc;
}

constexpr std::string_view kGoldenText = R"({
  "format": "bettercad-document",
  "version": 1,
  "units": "SI",
  "document": {
    "id": "0f8fad5b-d9cb-469f-a165-70867728950e",
    "name": "Plate",
    "metadata": {
      "description": "Golden file",
      "author": "tests",
      "properties": {
        "material": "6061-T6"
      }
    },
    "last_allocated_id": 3
  },
  "parameters": [
    {
      "id": 1,
      "name": "thickness",
      "si_value": 0.005,
      "unit": "mm",
      "dimension": {
        "length": 1
      }
    }
  ],
  "objects": [
    {
      "id": 2,
      "type": "sketch",
      "name": "Disc",
      "data": {
        "placement": {
          "origin": [
            0.0,
            0.0,
            0.0
          ],
          "x_axis": [
            1.0,
            0.0,
            0.0
          ],
          "y_axis": [
            0.0,
            1.0,
            0.0
          ],
          "normal": [
            0.0,
            0.0,
            1.0
          ]
        },
        "entities": [
          {
            "id": 1,
            "type": "point",
            "position": [
              0.0,
              0.0
            ],
            "construction": false
          },
          {
            "id": 2,
            "type": "circle",
            "center": 1,
            "radius": 0.01,
            "construction": false
          }
        ],
        "constraints": [
          {
            "id": 1,
            "type": "radius",
            "entities": [
              2
            ],
            "value": 0.01,
            "enabled": true
          }
        ],
        "last_entity_id": 2,
        "last_constraint_id": 1
      }
    },
    {
      "id": 3,
      "type": "extrude",
      "name": "Pad",
      "data": {
        "profile": 2,
        "depth": 0.0,
        "depth_parameter": 1,
        "direction": "normal",
        "operation": "new_body"
      }
    }
  ]
}
)";

std::string replaceOnce(std::string_view text, std::string_view from, std::string_view to) {
    std::string result{text};
    const auto pos = result.find(from);
    REQUIRE(pos != std::string::npos);
    result.replace(pos, from.size(), to);
    return result;
}

/// Replaces the text from the first @p begin up to (not including) the next @p end.
std::string replaceBetween(std::string_view text, std::string_view begin, std::string_view end,
                           std::string_view replacement) {
    std::string result{text};
    const auto from = result.find(begin);
    REQUIRE(from != std::string::npos);
    const auto to = result.find(end, from);
    REQUIRE(to != std::string::npos);
    result.replace(from, to - from, replacement);
    return result;
}

Error loadError(std::string_view text) {
    const auto loaded = io::documentFromJson(text);
    REQUIRE_FALSE(loaded.has_value());
    UNSCOPED_INFO(loaded.error().message);
    return loaded.error();
}

} // namespace

// --- P9 acceptance ------------------------------------------------------------------------------

TEST_CASE("P9 acceptance: create, save, close, load and regenerate give the same geometry and IDs",
          "[io][document][acceptance]") {
    TempDir dir;
    const std::filesystem::path path = dir.path() / "bracket.bcad";

    // Create and regenerate the model, then save it.
    Model ids;
    Regenerator before;
    const RegenerationReport built = requireReport(before, ids.doc);
    INFO(describe(built));
    REQUIRE(built.succeeded());
    CHECK(built.regenerated ==
          std::vector<ObjectId>{ids.base, ids.pad, ids.pocketSketch, ids.pocket, ids.slotSketch, ids.slot});
    CHECK_THAT(volumeMm3(before, ids.pad), WithinRel(kPadVolume, kRel));
    CHECK_THAT(volumeMm3(before, ids.pocket), WithinRel(kPocketVolume, kRel));
    CHECK_THAT(volumeMm3(before, ids.slot), WithinRel(kSlotVolume, kRel));

    const Document expected = ids.doc.clone();
    REQUIRE(io::saveDocument(ids.doc, path).has_value());
    ids.doc = Document("Closed"); // close: the original document is destroyed; only its IDs are kept

    // Load.
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    Document& doc = *loaded;

    // Identity, metadata, parameters, sketches, constraints and features are
    // all preserved, with their IDs.
    CHECK(equivalent(expected, doc));
    CHECK(doc.id() == expected.id());
    CHECK(doc.itemIds() == expected.itemIds());
    CHECK(doc.metadata().properties.at("note") == "\xC2\xB5m \"quoted\"\ttab");
    CHECK(doc.parameters().find(ids.slotDepth)->expression() == "depth * 0.6");
    CHECK(doc.parameters().find(ids.draft)->dimension() == dimensions::angle);
    const auto* base = doc.findObjectAs<Sketch>(ids.base);
    REQUIRE(base != nullptr);
    CHECK(base->contentEquals(*expected.findObjectAs<Sketch>(ids.base)));
    CHECK(base->lastAllocatedEntityId() == 15);
    CHECK(base->lastAllocatedConstraintId() == 11);
    CHECK_FALSE(base->findConstraint(ConstraintId::fromValue(10))->enabled);
    CHECK(base->findEntity(EntityId::fromValue(14))->construction);
    const auto* slotSketch = doc.findObjectAs<Sketch>(ids.slotSketch);
    REQUIRE(slotSketch != nullptr);
    CHECK(slotSketch->placement() == expected.findObjectAs<Sketch>(ids.slotSketch)->placement());
    CHECK(doc.findObjectAs<ExtrudeFeature>(ids.pocket)->definition() ==
          expected.findObjectAs<ExtrudeFeature>(ids.pocket)->definition());
    CHECK_FALSE(doc.isDirty());

    // Dependency relationships are the same.
    checkSameGraph(expected, doc);
    const DocumentGraph graph = buildDependencyGraph(doc);
    CHECK(graph.graph.dependenciesOf(ids.base) ==
          std::set<ObjectId>{ObjectId{ids.width}, ObjectId{ids.height}, ObjectId{ids.holeRadius}});
    CHECK(graph.graph.dependenciesOf(ids.pocket) == std::set<ObjectId>{ids.pad, ids.pocketSketch});
    CHECK(graph.graph.dependenciesOf(ids.slot) == std::set<ObjectId>{ObjectId{ids.slotDepth}, ids.slotSketch});
    CHECK(graph.missing.empty());

    // Regeneration rebuilds everything in the same order, with bit-identical geometry.
    Regenerator after;
    const RegenerationReport rebuilt = requireReport(after, doc);
    CHECK(rebuilt.succeeded());
    CHECK(rebuilt.regenerated == built.regenerated);
    for (const ObjectId feature : {ids.pad, ids.pocket, ids.slot}) {
        checkSameGeometry(before, after, feature);
    }
    CHECK_THAT(volumeMm3(after, ids.pad), WithinRel(kPadVolume, kRel));
    CHECK_THAT(volumeMm3(after, ids.pocket), WithinRel(kPocketVolume, kRel));
    CHECK_THAT(volumeMm3(after, ids.slot), WithinRel(kSlotVolume, kRel));
    // The solved state was saved, so regenerating changed nothing.
    CHECK(equivalent(expected, doc));

    // New items get fresh IDs: IDs of deleted items are not reused.
    CHECK(doc.lastAllocatedId() == expected.lastAllocatedId());
    CHECK(doc.lastAllocatedId() == 14);
    CHECK(doc.addObject(std::make_unique<Sketch>("Added")).value() == ObjectId::fromValue(15));
    std::optional<EntityId> point;
    std::optional<ConstraintId> fixed;
    REQUIRE(doc.modifyObject<Sketch>(ids.base, [&](Sketch& sketch) -> Result<bool> {
                   point = require(sketch.addPoint(Point2D{1_mm, 1_mm}));
                   fixed = require(sketch.addFixed(*point));
                   return true;
               }).has_value());
    CHECK(point == EntityId::fromValue(16));
    CHECK(fixed == ConstraintId::fromValue(12));
}

TEST_CASE("A model saved before its first regeneration regenerates identically after loading",
          "[io][document]") {
    Model m;
    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    auto loaded = io::documentFromJson(*text);
    REQUIRE(loaded.has_value());
    CHECK(equivalent(m.doc, *loaded));

    Regenerator a;
    Regenerator b;
    const RegenerationReport ra = requireReport(a, m.doc);
    const RegenerationReport rb = requireReport(b, *loaded);
    INFO(describe(rb));
    REQUIRE(ra.succeeded());
    REQUIRE(rb.succeeded());
    CHECK(ra.regenerated == rb.regenerated);
    for (const ObjectId feature : {m.pad, m.pocket, m.slot}) {
        checkSameGeometry(a, b, feature);
    }
    // Both copies reach the same solved state.
    CHECK(equivalent(m.doc, *loaded));

    // Loaded documents stay editable and parametric.
    REQUIRE(loaded->setParameterValue(m.width, 150_mm).has_value());
    const RegenerationReport edited = requireReport(b, *loaded);
    CHECK(edited.regenerated == std::vector<ObjectId>{m.base, m.pad, m.pocket});
    CHECK_THAT(volumeMm3(b, m.pad), WithinRel((150.0 * 60.0 - pi * 64.0) * 20.0, kRel));
}

TEST_CASE("Saving is deterministic and loss-free", "[io][document]") {
    Model m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());

    const auto first = io::documentToJson(m.doc);
    const auto second = io::documentToJson(m.doc);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(*first == *second);

    // Writing a loaded document reproduces the file byte for byte, including
    // every solved coordinate.
    const auto loaded = io::documentFromJson(*first);
    REQUIRE(loaded.has_value());
    const auto again = io::documentToJson(*loaded);
    REQUIRE(again.has_value());
    CHECK(*again == *first);

    const auto* slot = loaded->findObjectAs<Sketch>(m.slotSketch);
    const auto* original = m.doc.findObjectAs<Sketch>(m.slotSketch);
    for (const Entity& entity : original->entities()) {
        if (const auto* p = std::get_if<PointEntity>(&entity.geometry)) {
            const auto position = slot->position(entity.id);
            REQUIRE(position.has_value());
            CHECK(bits(position->x.si()) == bits(p->position.x.si()));
            CHECK(bits(position->y.si()) == bits(p->position.y.si()));
        }
    }
    CHECK(bits(slot->placement().normal().x()) == bits(original->placement().normal().x()));
}

TEST_CASE("The document format is transparent JSON", "[io][document]") {
    const Document doc = makeGoldenDocument();
    const auto text = io::documentToJson(doc);
    REQUIRE(text.has_value());
    CHECK(*text == kGoldenText);

    const auto loaded = io::documentFromJson(kGoldenText);
    REQUIRE(loaded.has_value());
    CHECK(equivalent(doc, *loaded));
    CHECK(loaded->lastAllocatedId() == 3);
}

TEST_CASE("Every extrude direction and operation round-trips", "[io][document]") {
    const auto direction =
        GENERATE(ExtrudeDirection::Normal, ExtrudeDirection::Reversed, ExtrudeDirection::Symmetric);
    const auto operation = GENERATE(FeatureOperation::NewBody, FeatureOperation::Join, FeatureOperation::Cut,
                                    FeatureOperation::Intersect);
    CAPTURE(toString(direction), toString(operation));

    Document doc = makeGoldenDocument();
    const ExtrudeDefinition definition{
        .profile = SketchId::fromValue(2),
        .depth = 7.25_mm,
        .depthParameter = std::nullopt,
        .direction = direction,
        .operation = operation,
        .target = operation == FeatureOperation::NewBody ? std::nullopt
                                                         : std::optional{FeatureId::fromValue(3)},
    };
    auto feature = ExtrudeFeature::create("Variant", definition);
    REQUIRE(feature.has_value());
    const ObjectId id = doc.addObject(std::move(*feature)).value();

    const auto loaded = io::documentFromJson(io::documentToJson(doc).value());
    REQUIRE(loaded.has_value());
    CHECK(loaded->findObjectAs<ExtrudeFeature>(id)->definition() == definition);
    CHECK(equivalent(doc, *loaded));
}

TEST_CASE("Sketch entities may appear in any order in the file", "[io][document]") {
    // The circle is listed before the point it references.
    const std::string reordered = replaceBetween(
        kGoldenText, "\"entities\": [", "\"constraints\"",
        R"("entities": [{"id": 2, "type": "circle", "center": 1, "radius": 0.01, "construction": false},
                        {"id": 1, "type": "point", "position": [0.0, 0.0], "construction": false}],
        )");
    const auto loaded = io::documentFromJson(reordered);
    REQUIRE(loaded.has_value());
    CHECK(equivalent(makeGoldenDocument(), *loaded));
}

TEST_CASE("References to missing items are kept and reported by regeneration", "[io][document]") {
    // Deleting the profile sketch leaves the extrude pointing at nothing: a
    // broken but valid document state, which must survive saving.
    Document doc = makeGoldenDocument();
    REQUIRE(doc.removeObject(ObjectId::fromValue(2)).has_value());

    auto loaded = io::documentFromJson(io::documentToJson(doc).value());
    REQUIRE(loaded.has_value());
    CHECK(equivalent(doc, *loaded));
    const DocumentGraph graph = buildDependencyGraph(*loaded);
    REQUIRE(graph.missing.size() == 1);
    CHECK(graph.missing.front() == MissingReference{ObjectId::fromValue(3), ObjectId::fromValue(2)});

    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, *loaded);
    CHECK(report.failed == std::vector<ObjectId>{ObjectId::fromValue(3)});
    CHECK(report.errors.at(ObjectId::fromValue(3)).code == ErrorCode::NotFound);
}

TEST_CASE("Invalid document files are rejected with the JSON path", "[io][document]") {
    REQUIRE(io::documentFromJson(kGoldenText).has_value());
    const std::string_view good = kGoldenText;

    SECTION("not JSON, or not a document") {
        CHECK(loadError("").code == ErrorCode::ParseError);
        CHECK(loadError("{ \"format\": ").code == ErrorCode::ParseError);
        CHECK(loadError(good.substr(0, good.size() / 2)).code == ErrorCode::ParseError);
        CHECK(loadError("[]").message == "document: expected an object");
    }
    SECTION("format, version and units") {
        CHECK(loadError(replaceOnce(good, "bettercad-document", "bettercad-parameters")).message ==
              "format: expected 'bettercad-document', got 'bettercad-parameters'");
        CHECK(loadError(replaceOnce(good, "\"version\": 1", "\"version\": 2")).message ==
              "version: unsupported version 2 (supported: 1)");
        CHECK(loadError(replaceOnce(good, "\"SI\"", "\"imperial\"")).code == ErrorCode::ParseError);
    }
    SECTION("missing, unknown and mistyped fields") {
        CHECK(loadError(replaceOnce(good, "\"last_entity_id\": 2,", "")).message ==
              "objects[0].data.last_entity_id: missing required field");
        CHECK(loadError(replaceOnce(good, "\"type\": \"point\",", "\"type\": \"point\", \"radius\": 1.0,"))
                  .message == "objects[0].data.entities[0].radius: unknown field");
        CHECK(loadError(replaceOnce(good, "\"radius\": 0.01,", "\"radius\": \"0.01\",")).message ==
              "objects[0].data.entities[1].radius: expected a number");
        CHECK(loadError(replaceOnce(good, "\"enabled\": true", "\"enabled\": 1")).message ==
              "objects[0].data.constraints[0].enabled: expected true or false");
        CHECK(loadError(replaceOnce(good, "\"material\": \"6061-T6\"", "\"material\": 6061")).message ==
              "document.metadata.properties.material: expected a string");
    }
    SECTION("unknown kinds and names") {
        CHECK(loadError(replaceOnce(good, "\"type\": \"extrude\"", "\"type\": \"revolve\"")).message ==
              "objects[1].type: unknown object type 'revolve'");
        CHECK(loadError(replaceOnce(good, "\"type\": \"circle\"", "\"type\": \"spline\"")).message ==
              "objects[0].data.entities[1].type: unknown type 'spline'");
        CHECK(loadError(replaceOnce(good, "\"direction\": \"normal\"", "\"direction\": \"sideways\"")).message ==
              "objects[1].data.direction: unknown value 'sideways'");
    }
    SECTION("identity and IDs") {
        CHECK(loadError(replaceOnce(good, kGoldenId, "not-a-uuid")).message == "document.id: expected a UUID");
        const Error duplicate = loadError(replaceOnce(good, "\"id\": 3,", "\"id\": 2,"));
        CHECK(duplicate.code == ErrorCode::AlreadyExists);
        CHECK_THAT(duplicate.message, ContainsSubstring("objects[1]: "));
        CHECK(loadError(replaceOnce(good, "\"id\": 3,", "\"id\": 1,")).code == ErrorCode::AlreadyExists);
        CHECK(loadError(replaceOnce(good, "\"id\": 3,", "\"id\": 0,")).code == ErrorCode::InvalidArgument);
        CHECK(loadError(replaceOnce(good, "\"id\": 3,", "\"id\": -3,")).code == ErrorCode::ParseError);
        CHECK(loadError(replaceOnce(good, "\"id\": 3,", "\"id\": 9007199254740992,")).message ==
              "objects[1].id: ID 9007199254740992 exceeds the maximum 9007199254740991");
        CHECK(loadError(replaceOnce(good, "\"last_allocated_id\": 3", "\"last_allocated_id\": 2")).message ==
              "document.last_allocated_id: less than an ID in use");
        CHECK(loadError(replaceOnce(good, "\"last_entity_id\": 2,", "\"last_entity_id\": 1,")).message ==
              "objects[0].data.last_entity_id: less than an entity ID in use");
        const Error entity = loadError(replaceOnce(good, "\"id\": 2,\n            \"type\": \"circle\"",
                                                   "\"id\": 1,\n            \"type\": \"circle\""));
        CHECK(entity.code == ErrorCode::AlreadyExists);
        CHECK_THAT(entity.message, ContainsSubstring("objects[0].data.entities[1]: "));
    }
    SECTION("names") {
        CHECK(loadError(replaceOnce(good, "\"name\": \"Pad\"", "\"name\": \"Disc\"")).code ==
              ErrorCode::AlreadyExists);
        CHECK(loadError(replaceOnce(good, "\"name\": \"Pad\"", "\"name\": \"thickness\"")).code ==
              ErrorCode::AlreadyExists);
        const Error name = loadError(replaceOnce(good, "\"name\": \"Plate\"", "\"name\": \"\""));
        CHECK(name.code == ErrorCode::InvalidArgument);
        CHECK_THAT(name.message, ContainsSubstring("document.name: "));
    }
    SECTION("sketch contents are validated") {
        const Error dangling = loadError(replaceOnce(good, "\"center\": 1,", "\"center\": 7,"));
        CHECK(dangling.code == ErrorCode::NotFound);
        CHECK_THAT(dangling.message, ContainsSubstring("objects[0].data.entities[1]: "));
        CHECK(loadError(replaceOnce(good, "\"radius\": 0.01,", "\"radius\": -0.01,")).code ==
              ErrorCode::InvalidArgument);
        const Error constraint =
            loadError(replaceOnce(good, "\"entities\": [\n              2\n", "\"entities\": [\n              1\n"));
        CHECK(constraint.code == ErrorCode::InvalidArgument);
        CHECK_THAT(constraint.message, ContainsSubstring("objects[0].data.constraints[0]: "));
        // JSON cannot hold NaN or infinity; numbers too large for a double are rejected.
        CHECK_THAT(loadError(replaceOnce(good, "\"origin\": [\n            0.0,", "\"origin\": [\n            1e400,"))
                       .message,
                   ContainsSubstring("number overflow"));
    }
    SECTION("placements must be orthonormal frames") {
        CHECK(loadError(replaceOnce(good, "1.0\n", "2.0\n")).message ==
              "objects[0].data.placement.normal: expected a unit vector");
        const Error handed = loadError(replaceOnce(good, "1.0\n", "-1.0\n"));
        CHECK(handed.code == ErrorCode::InvalidArgument);
        CHECK_THAT(handed.message, ContainsSubstring("objects[0].data.placement: "));
    }
    SECTION("feature definitions are validated") {
        CHECK(loadError(replaceOnce(good, "\"operation\": \"new_body\"", "\"operation\": \"new_body\", \"target\": 2"))
                  .code == ErrorCode::InvalidArgument);
        CHECK(loadError(replaceOnce(good, "\"depth_parameter\": 1,", "\"depth_parameter\": 0,")).code ==
              ErrorCode::InvalidArgument);
        CHECK(loadError(replaceOnce(good, "\"depth_parameter\": 1,", "")).code == ErrorCode::InvalidArgument);
    }
}

TEST_CASE("Documents are saved to and loaded from files", "[io][document][file]") {
    TempDir dir;
    const std::filesystem::path path = dir.path() / "plate.bcad";
    const Document doc = makeGoldenDocument();

    SECTION("save, then load") {
        REQUIRE(io::saveDocument(doc, path).has_value());
        CHECK(readFile(path) == kGoldenText);
        CHECK(doc.isDirty()); // marking the document saved is the caller's decision
        // Only the document itself is left: no temporary file.
        CHECK(std::distance(std::filesystem::directory_iterator(dir.path()), std::filesystem::directory_iterator{}) ==
              1);
        const auto loaded = io::loadDocument(path);
        REQUIRE(loaded.has_value());
        CHECK(equivalent(doc, *loaded));
        CHECK_FALSE(loaded->isDirty());
    }
    SECTION("saving replaces an existing file completely") {
        writeFile(path, std::string(10000, 'x'));
        REQUIRE(io::saveDocument(doc, path).has_value());
        CHECK(readFile(path) == kGoldenText);
    }
    SECTION("non-ASCII file names") {
        const std::filesystem::path unicode = dir.path() / std::filesystem::path(u8"Plåt ✓.bcad");
        REQUIRE(io::saveDocument(doc, unicode).has_value());
        const auto loaded = io::loadDocument(unicode);
        REQUIRE(loaded.has_value());
        CHECK(equivalent(doc, *loaded));
    }
    SECTION("missing files and directories") {
        CHECK(errorCode(io::loadDocument(dir.path() / "missing.bcad")) == ErrorCode::NotFound);
        CHECK(errorCode(io::loadDocument(dir.path())) == ErrorCode::NotFound);
        CHECK(errorCode(io::saveDocument(doc, dir.path() / "no-such-directory" / "plate.bcad")) ==
              ErrorCode::IoError);
    }
    SECTION("corrupt files name the file and the problem") {
        writeFile(path, replaceOnce(kGoldenText, "\"radius\": 0.01,", "\"radius\": null,"));
        const auto loaded = io::loadDocument(path);
        REQUIRE_FALSE(loaded.has_value());
        CHECK(loaded.error().code == ErrorCode::ParseError);
        CHECK(loaded.error().message == "plate.bcad: objects[0].data.entities[1].radius: expected a number");
    }
    SECTION("text that is not UTF-8 is refused, and nothing is written") {
        Document binary = makeGoldenDocument();
        REQUIRE(binary.setMetadata({.description = "caf\xE9", .author = {}, .properties = {}}).has_value());
        const auto text = io::documentToJson(binary);
        REQUIRE_FALSE(text.has_value());
        CHECK(text.error().code == ErrorCode::InvalidArgument);
        CHECK(errorCode(io::saveDocument(binary, path)) == ErrorCode::InvalidArgument);
        CHECK_FALSE(std::filesystem::exists(path));
    }
    SECTION("object kinds without a file format are refused, and nothing is written") {
        Document unsupported = makeGoldenDocument();
        REQUIRE(unsupported.addObject(std::make_unique<test::TestBlock>("Block", 1.0)).has_value());
        const auto text = io::documentToJson(unsupported);
        REQUIRE_FALSE(text.has_value());
        CHECK(text.error().code == ErrorCode::InvalidArgument);
        CHECK_THAT(text.error().message, ContainsSubstring("test_block"));
        CHECK(errorCode(io::saveDocument(unsupported, path)) == ErrorCode::InvalidArgument);
        CHECK_FALSE(std::filesystem::exists(path));
    }
}
