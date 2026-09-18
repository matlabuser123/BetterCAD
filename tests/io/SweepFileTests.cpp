#include "TestHelpers.hpp"
#include "core/geometry/GeometryTestSupport.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/BracketModel.hpp"
#include "support/MeshAnalysis.hpp"
#include "support/SweepModels.hpp"
#include "support/TestFiles.hpp"
#include "support/occt/StepReadBack.hpp"

#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/features/Regeneration.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/SweepFeature.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/io/ModelExport.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::ArcSweepModel;
using bettercad::test::ChannelModel;
using bettercad::test::describe;
using bettercad::test::featureIdOf;
using bettercad::test::FixedPathSweepModel;
using bettercad::test::readFile;
using bettercad::test::requireReport;
using bettercad::test::StraightSweepModel;
using bettercad::test::TempDir;
using bettercad::test::TorusSweepModel;
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

/// The channel's path as the file stores it.
std::string channelPath(const ChannelModel& m) {
    return std::format(R"("path": {{
          "sketch": 8,
          "edges": [
            {}
          ]
        }},)",
                       m.channelLine.value());
}

} // namespace

TEST_CASE("SweepFeature_SaveLoadPreservesDefinition", "[sweep][io][acceptance]") {
    TempDir dir;
    SECTION("a cut from a target, driven by parameters") {
        ChannelModel m;
        Regenerator before;
        const RegenerationReport built = requireReport(before, m.doc);
        INFO(describe(built));
        REQUIRE(built.succeeded());
        const SweepDefinition definition = m.definitionOf<SweepFeature>(m.channel);

        Document loaded = saveDestroyLoad(m.doc, dir.path() / "channel.bcad");
        // Every field: the feature's ID and name, the profile, the path's
        // sketch and edge IDs in order, the orientation, the operation, the
        // target and so the dependencies.
        const auto* restored = loaded.findObjectAs<SweepFeature>(m.channel);
        REQUIRE(restored != nullptr);
        CHECK(restored->name() == "Channel");
        CHECK(restored->featureId() == featureIdOf(m.channel));
        CHECK(restored->definition() == definition);
        CHECK(restored->definition().profile == bettercad::test::sketchIdOf(m.channelProfile));
        CHECK(restored->definition().path.sketch == bettercad::test::sketchIdOf(m.channelPath));
        CHECK(restored->definition().path.edges == std::vector<EntityId>{m.channelLine});
        CHECK(restored->definition().orientation == SweepOrientation::FollowPath);
        CHECK(restored->definition().operation == FeatureOperation::Cut);
        CHECK(restored->definition().target == featureIdOf(m.pad));
        CHECK(restored->dependencies() == std::vector<ObjectId>{m.channelProfile, m.channelPath, m.pad});

        Regenerator after;
        const RegenerationReport rebuilt = requireReport(after, loaded);
        CHECK(rebuilt.succeeded());
        CHECK(rebuilt.regenerated == built.regenerated); // same order
        for (const ObjectId feature : {m.pad, m.channel}) {
            checkSameGeometry(before, after, feature);
        }
        CHECK(after.body(m.channel)->topology().solids == 1);

        // Still parametric: the channel's radius and the target's height.
        REQUIRE(loaded.setParameterValue(m.radius, 4_mm).has_value());
        CHECK(requireReport(after, loaded).regenerated == std::vector<ObjectId>{m.channelProfile, m.channel});
        CHECK_THAT(volumeMm3(after, m.channel), WithinRel(ChannelModel::expectedVolume(20, 4), kRel));
        REQUIRE(loaded.setParameterValue(m.height, 30_mm).has_value());
        CHECK(requireReport(after, loaded).regenerated == std::vector<ObjectId>{m.pad, m.channel});
        CHECK_THAT(volumeMm3(after, m.channel), WithinRel(ChannelModel::expectedVolume(30, 4), kRel));
    }
    SECTION("a curved sweep and a path of three edges") {
        ArcSweepModel arc;
        Regenerator arcBefore;
        REQUIRE(requireReport(arcBefore, arc.doc).succeeded());
        Document arcLoaded = saveDestroyLoad(arc.doc, dir.path() / "bend.bcad");
        Regenerator arcAfter;
        REQUIRE(requireReport(arcAfter, arcLoaded).succeeded());
        checkSameGeometry(arcBefore, arcAfter, arc.sweep);
        REQUIRE(arcLoaded.setParameterValue(arc.bend, 30_mm).has_value());
        CHECK(requireReport(arcAfter, arcLoaded).regenerated == std::vector<ObjectId>{arc.path, arc.sweep});
        CHECK_THAT(volumeMm3(arcAfter, arc.sweep), WithinRel(ArcSweepModel::expectedVolume(2, 30), kRel));

        FixedPathSweepModel route{FixedPathSweepModel::Route::LineArcLine};
        Regenerator routeBefore;
        REQUIRE(requireReport(routeBefore, route.doc).succeeded());
        Document routeLoaded = saveDestroyLoad(route.doc, dir.path() / "route.bcad");
        CHECK(routeLoaded.findObjectAs<SweepFeature>(route.sweep)->definition().path.edges == route.edges);
        Regenerator routeAfter;
        REQUIRE(requireReport(routeAfter, routeLoaded).succeeded());
        checkSameGeometry(routeBefore, routeAfter, route.sweep);
    }
}

TEST_CASE("SweepFeature_FailedSweepSavesAndLoadsUnchanged", "[sweep][io]") {
    // A profile reaching past the arc's centre is still part of the model.
    TempDir dir;
    ArcSweepModel m;
    REQUIRE(m.doc.setParameterValue(m.radius, 25_mm).has_value());
    Regenerator before;
    const RegenerationReport failed = requireReport(before, m.doc);
    REQUIRE(failed.failed == std::vector<ObjectId>{m.sweep});

    Document loaded = saveDestroyLoad(m.doc, dir.path() / "failed.bcad");
    Regenerator after;
    const RegenerationReport again = requireReport(after, loaded);
    CHECK(again.failed == std::vector<ObjectId>{m.sweep});
    CHECK(again.errors.at(m.sweep).message == failed.errors.at(m.sweep).message);
    CHECK_THAT(again.errors.at(m.sweep).message, StartsWith("Sweep: makeSweep: the profile reaches 25 mm"));
    CHECK(after.body(m.sweep) == nullptr);

    REQUIRE(loaded.setParameterValue(m.radius, 2_mm).has_value());
    REQUIRE(requireReport(after, loaded).succeeded());
    CHECK_THAT(volumeMm3(after, m.sweep), WithinRel(ArcSweepModel::expectedVolume(2, 20), kRel));
}

TEST_CASE("SweepFeature_DataIsStoredAsTransparentJson", "[sweep][io]") {
    ChannelModel m;
    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    CHECK_THAT(*text, ContainsSubstring(std::string{R"("type": "sweep",
      "name": "Channel",
      "data": {
        "profile": 7,
        )"} + channelPath(m) +
                                        R"(
        "orientation": "follow_path",
        "operation": "cut",
        "target": 6
      })"));

    // A new body has no target; a path of several edges keeps their order.
    FixedPathSweepModel route{FixedPathSweepModel::Route::LineArcLine};
    const auto routeText = io::documentToJson(route.doc);
    REQUIRE(routeText.has_value());
    CHECK_THAT(*routeText, ContainsSubstring(std::format(R"("path": {{
          "sketch": 2,
          "edges": [
            {},
            {},
            {}
          ]
        }},
        "orientation": "follow_path",
        "operation": "new_body"
      }})",
                                                         route.edges[0].value(), route.edges[1].value(),
                                                         route.edges[2].value())));
}

TEST_CASE("SweepFeature_MalformedDataIsRejectedWithTheJsonPath", "[sweep][io]") {
    ChannelModel m;
    const std::string good = io::documentToJson(m.doc).value();
    REQUIRE(io::documentFromJson(good).has_value());
    const std::string path = channelPath(m);
    const auto loadError = [](const std::string& text) {
        const auto loaded = io::documentFromJson(text);
        REQUIRE_FALSE(loaded.has_value());
        UNSCOPED_INFO(loaded.error().message);
        return loaded.error();
    };
    const auto message = [&](const std::string& text) { return loadError(text).message; };
    const auto withPath = [&](std::string_view replacement) { return replaceOnce(good, path, replacement); };

    // Structure: every problem is reported where it is.
    CHECK(message(withPath("")) == "objects[4].data.path: missing required field");
    CHECK(message(withPath(R"("path": [8, 3],)")) == "objects[4].data.path: expected an object");
    CHECK(message(withPath(R"("path": {"edges": [3]},)")) == "objects[4].data.path.sketch: missing required field");
    CHECK(message(withPath(R"("path": {"sketch": 8},)")) == "objects[4].data.path.edges: missing required field");
    CHECK(message(withPath(R"("path": {"sketch": 8, "edges": 3},)")) == "objects[4].data.path.edges: expected an array");
    CHECK(message(withPath(R"("path": {"sketch": 8, "edges": [3, -1]},)")) ==
          "objects[4].data.path.edges[1]: expected an ID (a non-negative integer)");
    CHECK(message(withPath(R"("path": {"sketch": 8, "edges": [3], "closed": true},)")) ==
          "objects[4].data.path.closed: unknown field");
    CHECK(message(replaceOnce(good, "\"follow_path\"", "\"frenet\"")) ==
          "objects[4].data.orientation: unknown value 'frenet'");
    CHECK(message(replaceOnce(good, "\"orientation\": \"follow_path\",", "")) ==
          "objects[4].data.orientation: missing required field");
    // P12-SWEEP-001 added "twist", "twist_parameter", "guide" and the
    // path's "runs"; before it, every one of them was an unknown field.
    CHECK(io::documentFromJson(replaceOnce(good, "\"profile\": 7,", "\"profile\": 7, \"twist\": 0,")).has_value());
    CHECK(message(replaceOnce(good, "\"profile\": 7,", "\"profile\": 7, \"twist\": \"90 deg\",")) ==
          "objects[4].data.twist: expected a number");
    CHECK(message(replaceOnce(good, "\"profile\": 7,", "\"profile\": 7, \"twist_parameter\": -1,")) ==
          "objects[4].data.twist_parameter: expected an ID (a non-negative integer)");
    CHECK(message(replaceOnce(good, "\"profile\": 7,", "\"profile\": 7, \"spin\": 1,")) ==
          "objects[4].data.spin: unknown field");
    CHECK(message(withPath(R"("path": {"sketch": 8, "edges": [3], "runs": 4},)")) ==
          "objects[4].data.path.runs: expected an array");
    CHECK(message(withPath(R"("path": {"sketch": 8, "edges": [3], "runs": [{"edges": [4]}]},)")) ==
          "objects[4].data.path.runs[0].sketch: missing required field");
    CHECK(message(withPath(R"("path": {"sketch": 8, "edges": [3], "runs": [{"sketch": 9, "edges": [4], "x": 1}]},)")) ==
          "objects[4].data.path.runs[0].x: unknown field");
    CHECK(message(replaceOnce(good, "\"profile\": 7,", R"("profile": 7, "guide": {"sketch": 9},)")) ==
          "objects[4].data.guide.edges: missing required field");

    // Content: the definition's own rules, reported at the sweep.
    const auto invalid = [&](const std::string& text) {
        const Error error = loadError(text);
        CHECK(error.code == ErrorCode::InvalidArgument);
        return error.message;
    };
    CHECK(invalid(withPath(R"("path": {"sketch": 8, "edges": []},)")) ==
          "objects[4].data: a sweep path needs at least one edge");
    CHECK(invalid(withPath(R"("path": {"sketch": 8, "edges": [3, 3]},)")) ==
          "objects[4].data: entity:3 is listed twice in the path");
    CHECK(invalid(withPath(R"("path": {"sketch": 7, "edges": [3]},)")) ==
          "objects[4].data: the path must be in another sketch than the profile: it leaves the profile's plane at "
          "right angles");
    CHECK(invalid(replaceOnce(good, ",\n        \"target\": 6", "")) ==
          "objects[4].data: a cut feature needs a target feature");
    CHECK(invalid(replaceOnce(good, "\"profile\": 7,",
                              R"("profile": 7, "twist": 1.5, "guide": {"sketch": 9, "edges": [4]},)")) ==
          "objects[4].data: a sweep takes a twist or a guide curve, not both: a guide already says how the section "
          "turns");
    // The same ID in another run is another sketch's edge, and legal; only
    // a repeat inside one run is not (P12-SWEEP-001).
    CHECK(io::documentFromJson(
              withPath(R"("path": {"sketch": 8, "edges": [3], "runs": [{"sketch": 9, "edges": [3]}]},)"))
              .has_value());
    CHECK(invalid(withPath(R"("path": {"sketch": 8, "edges": [3], "runs": [{"sketch": 9, "edges": [3, 3]}]},)")) ==
          "objects[4].data: entity:3 is listed twice in the path");
    CHECK(invalid(withPath(R"("path": {"sketch": 8, "edges": [3], "runs": [{"sketch": 9, "edges": []}]},)")) ==
          "objects[4].data: path run 2 needs at least one edge");
}

TEST_CASE("SweepFeature_ExportsStep", "[sweep][io][export][acceptance]") {
    TempDir dir;
    SECTION("the straight sweep") {
        StraightSweepModel m;
        const auto path = dir.path() / "straight.step";
        const auto summary = io::exportStep(m.doc, path);
        REQUIRE(summary.has_value());
        REQUIRE(summary->bodies.size() == 1);
        CHECK(summary->bodies[0].name == "Sweep");
        CHECK_THAT(readFile(path), ContainsSubstring("PRODUCT('Sweep','Sweep'"));
        const auto contents = test::readStepFile(path);
        REQUIRE(contents.has_value());
        CHECK(contents->solids == 1);
        CHECK(contents->valid);
        CHECK_THAT(contents->volumeMm3, WithinRel(20000.0, kRelStep));
        CHECK_THAT(contents->areaMm2, WithinRel(6400.0, kRelStep));
        const std::array<double, 3> max{10, 20, 100};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            CHECK_THAT(contents->minMm[axis], WithinAbs(0.0, 1e-6));
            CHECK_THAT(contents->maxMm[axis], WithinAbs(max[axis], 1e-6));
        }
    }
    SECTION("the torus (curved, closed path)") {
        TorusSweepModel m;
        const auto path = dir.path() / "torus.step";
        REQUIRE(io::exportStep(m.doc, path).has_value());
        const auto contents = test::readStepFile(path);
        REQUIRE(contents.has_value());
        CHECK(contents->solids == 1);
        CHECK(contents->valid);
        CHECK_THAT(contents->volumeMm3, WithinRel(TorusSweepModel::expectedVolume(2, 20), kRelStep));
        CHECK_THAT(contents->areaMm2, WithinRel(4.0 * pi * pi * 20.0 * 2.0, kRelStep));
        for (const std::size_t axis : {0U, 1U}) {
            CHECK_THAT(contents->minMm[axis], WithinAbs(-22.0, 1e-6));
            CHECK_THAT(contents->maxMm[axis], WithinAbs(22.0, 1e-6));
        }
        CHECK_THAT(contents->minMm[2], WithinAbs(-2.0, 1e-6));
        CHECK_THAT(contents->maxMm[2], WithinAbs(2.0, 1e-6));
    }
    SECTION("the quarter bend and the channel cut from the block") {
        ArcSweepModel arc;
        const auto bend = dir.path() / "bend.step";
        REQUIRE(io::exportStep(arc.doc, bend).has_value());
        const auto bent = test::readStepFile(bend);
        REQUIRE(bent.has_value());
        CHECK(bent->solids == 1);
        CHECK(bent->valid);
        CHECK_THAT(bent->volumeMm3, WithinRel(40.0 * pi * pi, kRelStep));
        CHECK_THAT(bent->minMm[2], WithinAbs(-22.0, 1e-6));
        CHECK_THAT(bent->maxMm[0], WithinAbs(20.0, 1e-6));
        ChannelModel channel;
        const auto cut = dir.path() / "channel.step";
        REQUIRE(io::exportStep(channel.doc, cut).has_value());
        const auto block = test::readStepFile(cut);
        REQUIRE(block.has_value());
        CHECK(block->solids == 1);
        CHECK(block->valid);
        CHECK_THAT(block->volumeMm3, WithinRel(ChannelModel::expectedVolume(20, 5), kRelStep));
    }
}

TEST_CASE("SweepFeature_ExportsClosedStl", "[sweep][io][export][acceptance]") {
    // Curved sweeps test the tessellation: every mesh must be closed and
    // consistently oriented, enclose a positive volume, and lie within the
    // deflection of the B-Rep (|V_mesh - V| <= deflection x area).
    const Length deflection = 0.01_mm;
    TempDir dir;
    const auto check = [&](Document& doc, const std::string& name, io::StlFormat format, double exact, double area) {
        const auto path = dir.path() / name;
        const auto summary = io::exportStl(doc, path, {.mesh = {.linearDeflection = deflection}, .format = format});
        REQUIRE(summary.has_value());
        REQUIRE(summary->bodies.size() == 1);
        const std::string bytes = readFile(path);
        const auto mesh = format == io::StlFormat::Binary ? test::parseBinaryStl(bytes) : test::parseAsciiStl(bytes);
        REQUIRE(mesh.has_value());
        CHECK_FALSE(mesh->triangles.empty());
        const test::SurfaceCheck surface = test::checkSurface(mesh->triangles);
        CHECK(surface.watertight());
        bool finite = true;
        for (const test::Triangle& t : mesh->triangles) {
            for (const test::Vertex& v : t) {
                finite = finite && std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
            }
        }
        CHECK(finite);
        const double meshed = test::enclosedVolume(mesh->triangles);
        INFO(name << ": meshed " << meshed << " mm^3, exact " << exact << " mm^3, bound "
                  << deflection.in(units::mm) * area << " mm^3");
        CHECK(meshed > 0.0);
        CHECK(std::abs(meshed - exact) <= deflection.in(units::mm) * area);
    };
    for (const io::StlFormat format : {io::StlFormat::Binary, io::StlFormat::Ascii}) {
        DYNAMIC_SECTION("the torus (" << (format == io::StlFormat::Binary ? "binary" : "ASCII") << ")") {
            TorusSweepModel m;
            check(m.doc, "torus.stl", format, TorusSweepModel::expectedVolume(2, 20), 4.0 * pi * pi * 20.0 * 2.0);
        }
    }
    SECTION("the quarter bend, the line-arc-line path and the mitred polyline") {
        ArcSweepModel arc;
        check(arc.doc, "bend.stl", io::StlFormat::Binary, 40.0 * pi * pi, 40.0 * pi * pi + 8.0 * pi);
        FixedPathSweepModel route{FixedPathSweepModel::Route::LineArcLine};
        const double length = 100.0 + 10.0 * pi;
        check(route.doc, "route.stl", io::StlFormat::Binary, 4.0 * pi * length, 4.0 * pi * length + 8.0 * pi);
        FixedPathSweepModel corner{FixedPathSweepModel::Route::Polyline};
        check(corner.doc, "corner.stl", io::StlFormat::Binary, 400.0 * pi, 4.0 * pi * 100.0 + 8.0 * pi);
    }
}
