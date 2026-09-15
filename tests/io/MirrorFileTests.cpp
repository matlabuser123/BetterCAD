#include "TestHelpers.hpp"
#include "core/geometry/GeometryTestSupport.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/BracketModel.hpp"
#include "support/MeshAnalysis.hpp"
#include "support/MirrorModels.hpp"
#include "support/TestFiles.hpp"
#include "support/TurnedPartModel.hpp"
#include "support/occt/StepReadBack.hpp"

#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/features/MirrorFeature.hpp>
#include <bettercad/features/Regeneration.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/io/ModelExport.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::CubeMirrorModel;
using bettercad::test::describe;
using bettercad::test::HoleBlockModel;
using bettercad::test::HoleMirrorModel;
using bettercad::test::readFile;
using bettercad::test::requireReport;
using bettercad::test::TempDir;
using bettercad::test::TurnedPartModel;
using bettercad::test::volumeMm3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double pi = std::numbers::pi;
constexpr double kRel = 1e-12;
// STEP stores coordinates as decimal text; volumes computed from a re-read
// file agree with the original to well within this (as in StepExportTests).
constexpr double kRelStep = 1e-9;

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

FeatureId featureId(ObjectId id) {
    return FeatureId::fromValue(id.value());
}

void checkSameGeometry(const Regenerator& expected, const Regenerator& actual, ObjectId feature) {
    INFO("feature " << feature);
    const geometry::Body* a = expected.body(feature);
    const geometry::Body* b = actual.body(feature);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    const auto pa = a->massProperties().value();
    const auto pb = b->massProperties().value();
    CHECK(bits(pa.volume.si()) == bits(pb.volume.si()));
    CHECK(bits(pa.surfaceArea.si()) == bits(pb.surfaceArea.si()));
    CHECK(bits(pa.centerOfMass.x.si()) == bits(pb.centerOfMass.x.si()));
    CHECK(bits(pa.centerOfMass.y.si()) == bits(pb.centerOfMass.y.si()));
    CHECK(bits(pa.centerOfMass.z.si()) == bits(pb.centerOfMass.z.si()));
    CHECK(a->boundingBox().value() == b->boundingBox().value());
    CHECK(a->topology() == b->topology());
}

/// Saves @p doc, replaces it with an empty document, loads the file and
/// checks that the model, its IDs, its order and its dependency graph are
/// unchanged.
Document saveDestroyLoad(Document& doc, const std::filesystem::path& path) {
    const Document expected = doc.clone();
    REQUIRE(io::saveDocument(doc, path).has_value());
    doc = Document("Closed");

    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    CHECK(equivalent(expected, *loaded));
    CHECK(loaded->itemIds() == expected.itemIds());
    const DocumentGraph a = buildDependencyGraph(expected);
    const DocumentGraph b = buildDependencyGraph(*loaded);
    REQUIRE(a.graph.nodes() == b.graph.nodes());
    for (const ObjectId node : a.graph.nodes()) {
        CHECK(a.graph.dependenciesOf(node) == b.graph.dependenciesOf(node));
    }
    return std::move(*loaded);
}

std::string replaceOnce(std::string text, std::string_view from, std::string_view to) {
    const auto pos = text.find(from);
    REQUIRE(pos != std::string::npos);
    text.replace(pos, from.size(), to);
    return text;
}

/// A mirror of @p source across the plane through @p p0 (mm) with normal @p n.
MirrorDefinition across(FeatureId source, const std::array<double, 3>& p0, const std::array<double, 3>& n,
                        MirrorScope scope, bool keepOriginal) {
    return {.source = source,
            .plane = {.origin = Point3D{p0[0] * units::mm, p0[1] * units::mm, p0[2] * units::mm},
                      .normal = {n[0], n[1], n[2]}},
            .scope = scope,
            .keepOriginal = keepOriginal};
}

// The independent reference: p mirrored across the plane through p0 with
// normal n (any length), p' = p - 2 ((p - p0) . n) n with n made unit.
using V = std::array<double, 3>;

V reflect(const V& p, const V& p0, const V& nIn) {
    const double length = std::sqrt(nIn[0] * nIn[0] + nIn[1] * nIn[1] + nIn[2] * nIn[2]);
    const V n{nIn[0] / length, nIn[1] / length, nIn[2] / length};
    const double d = (p[0] - p0[0]) * n[0] + (p[1] - p0[1]) * n[1] + (p[2] - p0[2]) * n[2];
    return {p[0] - 2.0 * d * n[0], p[1] - 2.0 * d * n[1], p[2] - 2.0 * d * n[2]};
}

/// The HoleMirrorModel's plane as the file stores it: through the origin,
/// facing +X, moved by parameter 10 (`mid`).
constexpr std::string_view kMirrorPlane = R"("plane": {
          "origin": [
            0.0,
            0.0,
            0.0
          ],
          "normal": [
            1.0,
            0.0,
            0.0
          ],
          "offset": 0.0,
          "offset_parameter": 10
        },)";

} // namespace

TEST_CASE("MirrorFeature_SaveLoadPreservesDefinition", "[mirror][io][acceptance]") {
    // The feature mirror of the hole across x = 50: the plane's origin off the
    // X axis (a move within the plane changes nothing), the normal stored as
    // given (2, 0, 0) and the offset driven by `mid`.
    TempDir dir;
    HoleMirrorModel m;
    MirrorDefinition d = m.mirrorOf(m.mirror);
    d.plane.origin = Point3D{0_mm, 12_mm, -(3_mm)};
    d.plane.normal = {2.0, 0.0, 0.0};
    m.setMirror(m.mirror, d);
    Regenerator before;
    const RegenerationReport built = requireReport(before, m.doc);
    INFO(describe(built));
    REQUIRE(built.succeeded());
    CHECK_THAT(volumeMm3(before, m.mirror), WithinRel(HoleMirrorModel::expectedVolume(100, 20, 10), kRel));
    const auto reflection = resolveMirrorReflection(d, m.doc);
    REQUIRE(reflection.has_value());

    Document loaded = saveDestroyLoad(m.doc, dir.path() / "mirror.bcad");
    // Every field, bit for bit: the source's ID, the plane's origin, normal
    // and offset parameter, the scope and keep-original; so the same plane.
    const auto* restored = loaded.findObjectAs<MirrorFeature>(m.mirror);
    REQUIRE(restored != nullptr);
    CHECK(restored->definition() == d);
    CHECK(restored->definition().source == featureId(m.drill));
    CHECK(restored->definition().plane.normal == Vector3D{2.0, 0.0, 0.0});
    CHECK(restored->definition().plane.offsetParameter == std::optional<ParameterId>{m.mid});
    CHECK(restored->definition().scope == MirrorScope::Feature);
    CHECK(restored->definition().keepOriginal);
    CHECK(restored->name() == "Mirror");
    CHECK(restored->dependencies() == std::vector<ObjectId>{m.drill, ObjectId{m.mid}});
    CHECK(resolveMirrorReflection(restored->definition(), loaded).value() == *reflection);

    Regenerator after;
    const RegenerationReport rebuilt = requireReport(after, loaded);
    CHECK(rebuilt.succeeded());
    CHECK(rebuilt.regenerated == built.regenerated); // same order
    for (const ObjectId feature : {m.pad, m.drill, m.mirror}) {
        checkSameGeometry(before, after, feature);
    }

    // The loaded model is still parametric: the plane and the source.
    REQUIRE(loaded.setParameterValue(m.mid, 60_mm).has_value());
    CHECK(requireReport(after, loaded).regenerated == std::vector<ObjectId>{m.mirror});
    const auto rim = [&](double x) {
        const auto circle = geometry::circleSignature(Point3D{x * units::mm, 25_mm, 20_mm}, Direction3D::unitZ(), 5_mm);
        REQUIRE(circle.has_value());
        return geometry::findEdges(*after.body(m.mirror), *circle).value().size();
    };
    CHECK(rim(90) == 1); // 2 x 60 - 30
    CHECK(rim(70) == 0);
    REQUIRE(loaded.setParameterValue(m.diameter, 12_mm).has_value());
    CHECK(requireReport(after, loaded).regenerated == std::vector<ObjectId>{m.drill, m.mirror});
    CHECK_THAT(volumeMm3(after, m.mirror), WithinRel(HoleMirrorModel::expectedVolume(100, 20, 12), kRel));

    // A body mirror without the original survives a second round trip.
    MirrorDefinition body = restored->definition();
    body.scope = MirrorScope::Body;
    body.keepOriginal = false;
    REQUIRE(loaded.modifyObject<MirrorFeature>(m.mirror, [&](MirrorFeature& f) {
                      return f.setDefinition(body);
                  }).has_value());
    Regenerator once;
    REQUIRE(requireReport(once, loaded).succeeded());
    Document again = saveDestroyLoad(loaded, dir.path() / "body.bcad");
    CHECK(again.findObjectAs<MirrorFeature>(m.mirror)->definition() == body);
    Regenerator twice;
    REQUIRE(requireReport(twice, again).succeeded());
    checkSameGeometry(once, twice, m.mirror);
    // Across x = 60 the block [0, 100] goes to [20, 120].
    bettercad::test::checkPoint(twice.body(m.mirror)->boundingBox()->min, 20, 0, 0);
    bettercad::test::checkPoint(twice.body(m.mirror)->boundingBox()->max, 120, 50, 20);
}

TEST_CASE("MirrorFeature_SaveLoadKeepsAnArbitraryPlane", "[mirror][io][revolve]") {
    // The whole turned part (a revolve, bored and grooved) mirrored on its own
    // across the plane through (5, -2, 1) mm with normal (0.3, -0.1, 0.9):
    // stored as written, and the geometry comes back bit for bit.
    TempDir dir;
    TurnedPartModel m;
    const V p0{5, -2, 1};
    const V n{0.3, -0.1, 0.9};
    auto feature = MirrorFeature::create("Image", across(featureId(m.groove), p0, n, MirrorScope::Body, false));
    REQUIRE(feature.has_value());
    const ObjectId image = m.doc.addObject(std::move(*feature)).value();
    Regenerator before;
    REQUIRE(requireReport(before, m.doc).succeeded());
    const double exact = TurnedPartModel::expectedVolume(15, 40, 360, 5);
    CHECK_THAT(volumeMm3(before, image), WithinRel(exact, kRel));
    CHECK(before.body(image)->isValid());
    // The mirror image's centre of mass is the source's, reflected.
    const Point3D source = before.body(m.groove)->massProperties()->centerOfMass;
    const V expected = reflect({source.x.in(units::mm), source.y.in(units::mm), source.z.in(units::mm)}, p0, n);
    bettercad::test::checkPoint(before.body(image)->massProperties()->centerOfMass, expected[0], expected[1],
                                expected[2]);

    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    CHECK_THAT(*text, ContainsSubstring(R"("data": {
        "source": 10,
        "plane": {
          "origin": [
            0.005,
            -0.002,
            0.001
          ],
          "normal": [
            0.3,
            -0.1,
            0.9
          ],
          "offset": 0.0
        },
        "scope": "body",
        "keep_original": false
      })"));

    Document loaded = saveDestroyLoad(m.doc, dir.path() / "image.bcad");
    const MirrorDefinition& restored = loaded.findObjectAs<MirrorFeature>(image)->definition();
    CHECK(restored.plane.origin == Point3D{5_mm, -(2_mm), 1_mm});
    CHECK(restored.plane.normal == Vector3D{0.3, -0.1, 0.9});
    CHECK(restored.plane.offset == Length{});
    CHECK_FALSE(restored.plane.offsetParameter.has_value());
    Regenerator after;
    REQUIRE(requireReport(after, loaded).succeeded());
    checkSameGeometry(before, after, image);
    // Still driven by the revolve's parameters: a 270 deg sweep.
    REQUIRE(loaded.setParameterValue(m.sweep, 270_deg).has_value());
    REQUIRE(requireReport(after, loaded).succeeded());
    CHECK_THAT(volumeMm3(after, image), WithinRel(TurnedPartModel::expectedVolume(15, 40, 270, 5), kRel));
}

TEST_CASE("MirrorFeature_FailedMirrorSavesAndLoadsUnchanged", "[mirror][io]") {
    // A mirror whose hole would land 2 mm from the block's end is still part
    // of the model.
    TempDir dir;
    HoleMirrorModel m;
    REQUIRE(m.doc.setParameterValue(m.mid, 64_mm).has_value());
    Regenerator before;
    const RegenerationReport failed = requireReport(before, m.doc);
    REQUIRE(failed.failed == std::vector<ObjectId>{m.mirror});

    Document loaded = saveDestroyLoad(m.doc, dir.path() / "failed.bcad");
    Regenerator after;
    const RegenerationReport again = requireReport(after, loaded);
    CHECK(again.failed == std::vector<ObjectId>{m.mirror});
    CHECK(again.errors.at(m.mirror).message == failed.errors.at(m.mirror).message);
    CHECK_THAT(again.errors.at(m.mirror).message,
               StartsWith("Mirror: mirror: the mirror image across the plane through (64, 0, 0) mm facing (1, 0, 0): "
                          "hole: "));
    CHECK(after.body(m.mirror) == nullptr);

    REQUIRE(loaded.setParameterValue(m.mid, 50_mm).has_value());
    REQUIRE(requireReport(after, loaded).succeeded());
    CHECK_THAT(volumeMm3(after, m.mirror), WithinRel(HoleMirrorModel::expectedVolume(100, 20, 10), kRel));
}

TEST_CASE("MirrorFeature_DataIsStoredAsTransparentJson", "[mirror][io]") {
    HoleMirrorModel m;
    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    CHECK_THAT(*text, ContainsSubstring(std::string{R"("type": "mirror",
      "name": "Mirror",
      "data": {
        "source": 9,
        )"} + std::string{kMirrorPlane} +
                                        R"(
        "scope": "feature",
        "keep_original": true
      })"));

    // A body mirror without the original, across a literal offset.
    MirrorDefinition d = m.mirrorOf(m.mirror);
    d.plane.offsetParameter.reset();
    d.plane.offset = 12.5_mm;
    d.scope = MirrorScope::Body;
    d.keepOriginal = false;
    m.setMirror(m.mirror, d);
    const auto body = io::documentToJson(m.doc);
    REQUIRE(body.has_value());
    CHECK_THAT(*body, ContainsSubstring(R"("offset": 0.0125
        },
        "scope": "body",
        "keep_original": false
      })"));
    CHECK_THAT(*body, !ContainsSubstring("offset_parameter"));
}

TEST_CASE("MirrorFeature_MalformedDataIsRejectedWithTheJsonPath", "[mirror][io]") {
    HoleMirrorModel m;
    const std::string good = io::documentToJson(m.doc).value();
    REQUIRE(io::documentFromJson(good).has_value());
    const auto loadError = [](const std::string& text) {
        const auto loaded = io::documentFromJson(text);
        REQUIRE_FALSE(loaded.has_value());
        UNSCOPED_INFO(loaded.error().message);
        return loaded.error();
    };
    const auto message = [&](const std::string& text) { return loadError(text).message; };
    const auto withPlane = [&](std::string_view plane) { return replaceOnce(good, kMirrorPlane, plane); };

    // Structure: every problem is reported where it is.
    CHECK(message(withPlane(R"("plane": {"origin": [0, 0, 0], "normal": [1, 0], "offset": 0},)")) ==
          "objects[3].data.plane.normal: expected 3 numbers, got 2");
    CHECK(message(withPlane(R"("plane": {"origin": [0, 0, 0], "normal": [1, "0", 0], "offset": 0},)")) ==
          "objects[3].data.plane.normal[1]: expected a number");
    CHECK(message(withPlane(R"("plane": {"normal": [1, 0, 0], "offset": 0},)")) ==
          "objects[3].data.plane.origin: missing required field");
    CHECK(message(withPlane(R"("plane": {"origin": [0, 0, 0], "normal": [1, 0, 0]},)")) ==
          "objects[3].data.plane.offset: missing required field");
    CHECK(message(withPlane(R"("plane": {"origin": [0, 0, 0], "normal": [1, 0, 0], "offset": "50 mm"},)")) ==
          "objects[3].data.plane.offset: expected a number");
    CHECK(message(withPlane(R"("plane": {"origin": [0, 0, 0], "normal": [1, 0, 0], "offset": 0, "offset_parameter": -1},)")) ==
          "objects[3].data.plane.offset_parameter: expected an ID (a non-negative integer)");
    CHECK(message(withPlane(R"("plane": {"origin": [0, 0, 0], "normal": [1, 0, 0], "offset": 0, "radius": 0.04},)")) ==
          "objects[3].data.plane.radius: unknown field");
    CHECK(message(withPlane(R"("plane": [0, 0, 0],)")) == "objects[3].data.plane: expected an object");
    CHECK(message(withPlane("")) == "objects[3].data.plane: missing required field");
    CHECK(message(replaceOnce(good, "\"scope\": \"feature\"", "\"scope\": \"left\"")) ==
          "objects[3].data.scope: unknown value 'left'");
    CHECK(message(replaceOnce(good, "\"keep_original\": true", "\"keep_original\": \"yes\"")) ==
          "objects[3].data.keep_original: expected true or false");
    CHECK(message(replaceOnce(good, "\"keep_original\": true", "\"copies\": 2")) ==
          "objects[3].data.copies: unknown field");
    CHECK(message(replaceOnce(good, "\"source\": 9", "\"source\": 9, \"count\": 2")) ==
          "objects[3].data.count: unknown field");

    // Content: the definition's own rules, reported at the mirror.
    const auto invalid = [&](const std::string& text) {
        const Error error = loadError(text);
        CHECK(error.code == ErrorCode::InvalidArgument);
        return error.message;
    };
    CHECK(invalid(withPlane(R"("plane": {"origin": [0, 0, 0], "normal": [0, 0, 0], "offset": 0},)")) ==
          "objects[3].data: the plane's normal must be a finite, non-zero vector, got (0, 0, 0)");
    CHECK(invalid(replaceOnce(good, "\"keep_original\": true", "\"keep_original\": false")) ==
          "objects[3].data: a feature mirror keeps its source: the mirrored operation is added to the body the source "
          "made (mirror the body to keep only the mirror image)");
    CHECK(invalid(replaceOnce(good, "\"source\": 9", "\"source\": 0")) ==
          "objects[3].data: a mirror needs a source feature");
    CHECK(invalid(replaceOnce(good, "\"offset_parameter\": 10", "\"offset_parameter\": 0")) ==
          "objects[3].data: the mirror's parameter IDs must be valid");
    // A body mirror may leave the original out.
    CHECK(io::documentFromJson(replaceOnce(replaceOnce(good, "\"scope\": \"feature\"", "\"scope\": \"body\""),
                                           "\"keep_original\": true", "\"keep_original\": false"))
              .has_value());
}

TEST_CASE("MirrorFeature_ExportsStep", "[mirror][io][export][acceptance]") {
    TempDir dir;
    SECTION("the block with a hole and its mirror image") {
        HoleMirrorModel m;
        const auto path = dir.path() / "holes.step";
        const auto summary = io::exportStep(m.doc, path);
        REQUIRE(summary.has_value());
        REQUIRE(summary->bodies.size() == 1);
        CHECK(summary->bodies[0].name == "Mirror");
        CHECK_THAT(readFile(path), ContainsSubstring("PRODUCT('Mirror','Mirror'"));
        const auto contents = test::readStepFile(path);
        REQUIRE(contents.has_value());
        CHECK(contents->solids == 1);
        CHECK(contents->valid);
        CHECK_THAT(contents->volumeMm3, WithinRel(HoleMirrorModel::expectedVolume(100, 20, 10), kRelStep));
        CHECK_THAT(contents->areaMm2, WithinRel(16000.0 + 300.0 * pi, kRelStep));
        const std::array<double, 3> max{100, 50, 20};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            CHECK_THAT(contents->minMm[axis], WithinAbs(0.0, 1e-6));
            CHECK_THAT(contents->maxMm[axis], WithinAbs(max[axis], 1e-6));
        }
    }
    SECTION("a cube and its mirror image: one body of two solids") {
        CubeMirrorModel m;
        const auto path = dir.path() / "cubes.step";
        const auto summary = io::exportStep(m.doc, path);
        REQUIRE(summary.has_value());
        REQUIRE(summary->bodies.size() == 1);
        const auto contents = test::readStepFile(path);
        REQUIRE(contents.has_value());
        CHECK(contents->solids == 2);
        CHECK(contents->valid);
        CHECK_THAT(contents->volumeMm3, WithinRel(2000.0, kRelStep));
        CHECK_THAT(contents->areaMm2, WithinRel(1200.0, kRelStep));
        CHECK_THAT(contents->minMm[0], WithinAbs(-25.0, 1e-6));
        CHECK_THAT(contents->maxMm[0], WithinAbs(25.0, 1e-6));
        for (const std::size_t axis : {1U, 2U}) {
            CHECK_THAT(contents->minMm[axis], WithinAbs(-5.0, 1e-6));
            CHECK_THAT(contents->maxMm[axis], WithinAbs(5.0, 1e-6));
        }
    }
    SECTION("the cube's mirror image alone, across a skew plane") {
        // The tight bounds are those of the eight corners, each reflected by
        // the formula.
        CubeMirrorModel m;
        const V p0{3, -2, 1};
        const V n{1, 2, 2};
        m.setDefinition(m.mirror, across(featureId(m.cube), p0, n, MirrorScope::Body, false));
        const auto path = dir.path() / "image.step";
        REQUIRE(io::exportStep(m.doc, path).has_value());
        const auto contents = test::readStepFile(path);
        REQUIRE(contents.has_value());
        CHECK(contents->solids == 1);
        CHECK(contents->valid);
        CHECK_THAT(contents->volumeMm3, WithinRel(1000.0, kRelStep));
        CHECK_THAT(contents->areaMm2, WithinRel(600.0, kRelStep));
        V lo{1e9, 1e9, 1e9};
        V hi{-1e9, -1e9, -1e9};
        for (const double x : {15.0, 25.0}) {
            for (const double y : {-5.0, 5.0}) {
                for (const double z : {-5.0, 5.0}) {
                    const V corner = reflect({x, y, z}, p0, n);
                    for (std::size_t axis = 0; axis < 3; ++axis) {
                        lo[axis] = std::min(lo[axis], corner[axis]);
                        hi[axis] = std::max(hi[axis], corner[axis]);
                    }
                }
            }
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            CAPTURE(axis);
            CHECK_THAT(contents->minMm[axis], WithinAbs(lo[axis], 1e-6));
            CHECK_THAT(contents->maxMm[axis], WithinAbs(hi[axis], 1e-6));
        }
    }
}

TEST_CASE("MirrorFeature_ExportsClosedStl", "[mirror][io][export][acceptance]") {
    // A reflection turns a right-handed face frame left-handed, so a mesh taken
    // from mirrored faces could come out inside out. Every file must be closed
    // and consistently oriented, enclose a positive volume matching the B-Rep,
    // and store facet normals that point out of the material.
    const Length deflection = 0.01_mm;
    TempDir dir;
    const auto exportMesh = [&](Document& doc, const std::string& name, io::StlFormat format) {
        const auto path = dir.path() / name;
        const auto summary = io::exportStl(doc, path, {.mesh = {.linearDeflection = deflection}, .format = format});
        REQUIRE(summary.has_value());
        REQUIRE(summary->bodies.size() == 1);
        const std::string bytes = readFile(path);
        const auto mesh = format == io::StlFormat::Binary ? test::parseBinaryStl(bytes) : test::parseAsciiStl(bytes);
        REQUIRE(mesh.has_value());
        CHECK(test::checkSurface(mesh->triangles).watertight());
        return *mesh;
    };
    // Each stored normal agrees with its triangle's winding and points away
    // from @p centre (for boxes: every face faces away from the centre).
    const auto checkOutward = [](const test::StlData& mesh, const V& centre) {
        std::size_t inward = 0;
        std::size_t disagreeing = 0;
        for (std::size_t i = 0; i < mesh.triangles.size(); ++i) {
            const test::Triangle& t = mesh.triangles[i];
            const test::Vertex winding = test::cross(test::minus(t[1], t[0]), test::minus(t[2], t[0]));
            const test::Vertex middle{(t[0][0] + t[1][0] + t[2][0]) / 3.0 - centre[0],
                                      (t[0][1] + t[1][1] + t[2][1]) / 3.0 - centre[1],
                                      (t[0][2] + t[1][2] + t[2][2]) / 3.0 - centre[2]};
            inward += test::dot(mesh.normals[i], middle) > 0.0 ? 0U : 1U;
            disagreeing += test::dot(mesh.normals[i], winding) > 0.0 ? 0U : 1U;
        }
        CHECK(inward == 0);
        CHECK(disagreeing == 0);
    };

    for (const io::StlFormat format : {io::StlFormat::Binary, io::StlFormat::Ascii}) {
        DYNAMIC_SECTION("the block with a hole and its mirror image ("
                        << (format == io::StlFormat::Binary ? "binary" : "ASCII") << ")") {
            HoleMirrorModel m;
            const double exact = HoleMirrorModel::expectedVolume(100, 20, 10);
            const double area = 16000.0 + 300.0 * pi;
            const test::StlData mesh = exportMesh(m.doc, "holes.stl", format);
            // The holes are concave (their chords cross the holes); every
            // point of the mesh is within the deflection of the surface.
            const double meshed = test::enclosedVolume(mesh.triangles);
            INFO("meshed " << meshed << " mm^3, exact " << exact << " mm^3");
            CHECK(meshed > 0.0);
            CHECK(std::abs(meshed - exact) <= deflection.in(units::mm) * area);
        }
    }
    SECTION("a cube and its mirror image: two closed shells in one file") {
        CubeMirrorModel m;
        const test::StlData mesh = exportMesh(m.doc, "cubes.stl", io::StlFormat::Binary);
        CHECK(mesh.triangles.size() == 2 * 12);
        // Flat faces are meshed exactly, and the mirrored corners (x -> -x)
        // are exact in single precision.
        CHECK_THAT(test::enclosedVolume(mesh.triangles), WithinRel(2000.0, 1e-12));
        // Each cube's triangles face away from that cube's centre.
        test::StlData image;
        test::StlData source;
        for (std::size_t i = 0; i < mesh.triangles.size(); ++i) {
            test::StlData& half = mesh.triangles[i][0][0] < 0.0 ? image : source;
            half.triangles.push_back(mesh.triangles[i]);
            half.normals.push_back(mesh.normals[i]);
        }
        REQUIRE(image.triangles.size() == 12);
        checkOutward(image, {-20, 0, 0});
        checkOutward(source, {20, 0, 0});
    }
    SECTION("the cube's mirror image alone, across x = 0 and across a skew plane") {
        CubeMirrorModel m;
        m.setDefinition(m.mirror, across(featureId(m.cube), {0, 0, 0}, {1, 0, 0}, MirrorScope::Body, false));
        const test::StlData yz = exportMesh(m.doc, "yz.stl", io::StlFormat::Binary);
        CHECK(yz.triangles.size() == 12);
        CHECK_THAT(test::enclosedVolume(yz.triangles), WithinRel(1000.0, 1e-12));
        checkOutward(yz, {-20, 0, 0});

        const V p0{3, -2, 1};
        const V n{1, 2, 2};
        m.setDefinition(m.mirror, across(featureId(m.cube), p0, n, MirrorScope::Body, false));
        const test::StlData skew = exportMesh(m.doc, "skew.stl", io::StlFormat::Binary);
        CHECK(skew.triangles.size() == 12);
        // Corners are stored in single precision: every coordinate is below
        // 32 mm, so within 32 x 2^-24 mm of the exact corner; that moves the
        // enclosed volume by at most the area (600 mm^2) times sqrt(3) times
        // that.
        const double rounding = 600.0 * std::sqrt(3.0) * 32.0 * std::ldexp(1.0, -24);
        const double meshed = test::enclosedVolume(skew.triangles);
        INFO("meshed " << meshed << " mm^3; rounding bound " << rounding << " mm^3");
        CHECK(std::abs(meshed - 1000.0) <= rounding);
        checkOutward(skew, reflect({20, 0, 0}, p0, n));
    }
    SECTION("curved faces: the drilled block and the turned part, mirrored on their own") {
        // The drilled block across its end x = 100 (the hole goes to x = 150).
        HoleBlockModel block;
        block.add<MirrorFeature>("Image",
                                 across(featureId(block.drill), {100, 0, 0}, {1, 0, 0}, MirrorScope::Body, false));
        const test::StlData drilled = exportMesh(block.doc, "block.stl", io::StlFormat::Binary);
        const double blockExact = HoleBlockModel::expectedVolume(100, 50, 20, 10);
        const double blockMeshed = test::enclosedVolume(drilled.triangles);
        INFO("block: meshed " << blockMeshed << " mm^3, exact " << blockExact << " mm^3");
        CHECK(blockMeshed > 0.0);
        CHECK(std::abs(blockMeshed - blockExact) <= deflection.in(units::mm) * (16000.0 + 150.0 * pi));

        // The turned part across the skew plane: cylinders, planes and the
        // groove's faces, all mirrored. Its area: the end annuli 2 pi (15^2 -
        // 5^2), the bore 2 pi 5 40, the outside 2 pi 15 (40 - 5), the groove's
        // floor 2 pi 13 5 and its sides 2 pi (15^2 - 13^2): 2092 pi.
        TurnedPartModel turned;
        auto image = MirrorFeature::create(
            "Image", across(featureId(turned.groove), {5, -2, 1}, {0.3, -0.1, 0.9}, MirrorScope::Body, false));
        REQUIRE(image.has_value());
        const ObjectId imageId = turned.doc.addObject(std::move(*image)).value();
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, turned.doc).succeeded());
        const double turnedArea = 2092.0 * pi;
        CHECK_THAT(regenerator.body(imageId)->massProperties()->surfaceArea.in(units::mm2),
                   WithinRel(turnedArea, kRel));
        const test::StlData part = exportMesh(turned.doc, "turned.stl", io::StlFormat::Binary);
        const double turnedExact = TurnedPartModel::expectedVolume(15, 40, 360, 5);
        const double turnedMeshed = test::enclosedVolume(part.triangles);
        INFO("turned part: meshed " << turnedMeshed << " mm^3, exact " << turnedExact << " mm^3");
        CHECK(turnedMeshed > 0.0);
        CHECK(std::abs(turnedMeshed - turnedExact) <= deflection.in(units::mm) * turnedArea);
    }
}
