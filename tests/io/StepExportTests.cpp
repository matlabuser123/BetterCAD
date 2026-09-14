#include "TestHelpers.hpp"
#include "support/BracketModel.hpp"
#include "support/TestFiles.hpp"
#include "support/occt/StepReadBack.hpp"

#include <bettercad/core/BuildInfo.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Exchange.hpp>
#include <bettercad/core/geometry/Primitives.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/io/ModelExport.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <format>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using bettercad::test::BracketModel;
using bettercad::test::errorCode;
using bettercad::test::readFile;
using bettercad::test::readStepFile;
using bettercad::test::TempDir;
using bettercad::test::writeFile;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinRel;

namespace {

// STEP stores coordinates as decimal text; volumes computed from a re-read
// file agree with the original to well within this.
constexpr double kRelStep = 1e-9;

std::string requireStep(const std::vector<geometry::NamedBody>& bodies, const geometry::StepOptions& options = {}) {
    auto step = geometry::writeStep(bodies, options);
    REQUIRE(step.has_value());
    return *step;
}

} // namespace

TEST_CASE("STEP output is an ISO 10303-21 AP214 file in millimetres", "[io][step]") {
    const auto box = geometry::makeBox(100_mm, 50_mm, 20_mm);
    REQUIRE(box.has_value());
    const std::string step = requireStep({{"Block", *box}}, {.name = "block.step", .author = "Ada"});

    CHECK_THAT(step, StartsWith("ISO-10303-21;\nHEADER;\n"));
    CHECK(step.ends_with("END-ISO-10303-21;\n"));
    CHECK_THAT(step, ContainsSubstring("FILE_SCHEMA(('AUTOMOTIVE_DESIGN"));
    CHECK_THAT(step, ContainsSubstring("SI_UNIT(.MILLI.,.METRE.)"));
    CHECK_THAT(step, ContainsSubstring("PRODUCT('Block','Block'"));
    CHECK_THAT(step, ContainsSubstring("MANIFOLD_SOLID_BREP"));
    CHECK_THAT(step, ContainsSubstring(std::format("'BetterCAD {}'", buildInfo().version)));
    CHECK_THAT(step, ContainsSubstring("('Ada')"));
    CHECK_THAT(step, ContainsSubstring("FILE_NAME('block.step'"));
}

TEST_CASE("A written STEP file reads back as the same solid", "[io][step]") {
    TempDir dir;
    const auto cylinder = geometry::makeCylinder(10_mm, 20_mm);
    REQUIRE(cylinder.has_value());
    const auto path = dir.path() / "cylinder.step";
    writeFile(path, requireStep({{"Cylinder", *cylinder}}));

    const auto contents = readStepFile(path);
    REQUIRE(contents.has_value());
    CHECK(contents->roots == 1);
    CHECK(contents->solids == 1);
    CHECK(contents->valid);
    const auto properties = cylinder->massProperties();
    REQUIRE(properties.has_value());
    CHECK_THAT(contents->volumeMm3, WithinRel(properties->volume.in(units::mm3), kRelStep));
    CHECK_THAT(contents->areaMm2, WithinRel(properties->surfaceArea.in(units::mm2), kRelStep));
}

TEST_CASE("A fixed time stamp makes STEP output reproducible", "[io][step]") {
    const auto box = geometry::makeBox(1_mm, 2_mm, 3_mm);
    REQUIRE(box.has_value());
    const geometry::StepOptions options{.timeStamp = "2026-01-01T00:00:00"};
    const std::string first = requireStep({{"Box", *box}}, options);
    CHECK(requireStep({{"Box", *box}}, options) == first);
    CHECK_THAT(first, ContainsSubstring("'2026-01-01T00:00:00'"));
}

TEST_CASE("Names that need quoting or are not ASCII survive STEP export", "[io][step]") {
    TempDir dir;
    const auto box = geometry::makeBox(10_mm, 10_mm, 10_mm);
    REQUIRE(box.has_value());
    const auto path = dir.path() / "names.step";
    writeFile(path, requireStep({{"Bob's \xC3\xA5 part", *box}}, {.author = "O'Brien \xC3\xA5"}));
    const auto contents = readStepFile(path);
    REQUIRE(contents.has_value());
    CHECK(contents->solids == 1);
    CHECK_THAT(contents->volumeMm3, WithinRel(1000.0, kRelStep));
}

TEST_CASE("STEP output rejects missing and empty bodies", "[io][step]") {
    CHECK(errorCode(geometry::writeStep({})) == ErrorCode::InvalidArgument);
    const std::vector<geometry::NamedBody> empty{{"Nothing", geometry::Body{}}};
    CHECK(errorCode(geometry::writeStep(empty)) == ErrorCode::InvalidArgument);
}

TEST_CASE("Exporting a document writes its result bodies as named STEP products", "[io][step][export]") {
    BracketModel model;
    TempDir dir;
    const auto path = dir.path() / "bracket.step";

    const auto summary = io::exportStep(model.doc, path);
    REQUIRE(summary.has_value());
    REQUIRE(summary->bodies.size() == 2);
    CHECK(summary->bodies[0].feature == model.pocket);
    CHECK(summary->bodies[1].feature == model.slot);

    const std::string text = readFile(path);
    CHECK(summary->bytes == text.size());
    CHECK_THAT(text, ContainsSubstring("PRODUCT('Pocket','Pocket'"));
    CHECK_THAT(text, ContainsSubstring("PRODUCT('Slot','Slot'"));
    CHECK_THAT(text, !ContainsSubstring("PRODUCT('Pad'")); // consumed by the pocket
    CHECK_THAT(text, ContainsSubstring("FILE_NAME('bracket.step'"));
    CHECK_THAT(text, ContainsSubstring("('BetterCAD tests')")); // the document's author

    const auto contents = readStepFile(path);
    REQUIRE(contents.has_value());
    CHECK(contents->solids == 2);
    CHECK(contents->valid);
    CHECK_THAT(contents->volumeMm3,
               WithinRel(BracketModel::kPocketVolume + BracketModel::kSlotVolume, kRelStep));
    // The export regenerated a copy: the document itself was not solved
    // (the width constraint still holds its initial value, not width's).
    CHECK(model.doc.findObjectAs<sketch::Sketch>(model.base)->findConstraint(ConstraintId::fromValue(6))->value ==
          1_mm);
}

TEST_CASE("Document STEP export reports failures without writing", "[io][step][export]") {
    TempDir dir;
    const auto path = dir.path() / "out.step";
    SECTION("no bodies") {
        CHECK(errorCode(io::exportStep(Document("Empty"), path)) == ErrorCode::FailedPrecondition);
    }
    SECTION("the model does not regenerate") {
        BracketModel model;
        REQUIRE(model.doc.setParameterValue(model.depth, 0_mm).has_value());
        const auto result = io::exportStep(model.doc, path);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::FailedPrecondition);
        CHECK_THAT(result.error().message, ContainsSubstring("Pad"));
    }
    SECTION("the file cannot be written") {
        BracketModel model;
        CHECK(errorCode(io::exportStep(model.doc, dir.path() / "missing" / "out.step")) == ErrorCode::IoError);
    }
    CHECK_FALSE(std::filesystem::exists(path));
}
