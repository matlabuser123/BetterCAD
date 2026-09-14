#include "support/TestFiles.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/features/Validation.hpp>
#include <bettercad/io/DocumentFile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <numbers>

using namespace bettercad;
using bettercad::test::readFile;
using Catch::Matchers::WithinRel;

namespace {

const std::filesystem::path kExamples{BETTERCAD_EXAMPLE_MODELS_DIR};

} // namespace

// examples/models/plate.bcad is written by hand: it shows that the format is
// readable and editable without BetterCAD, and serves the CLI process tests.
TEST_CASE("The hand-written example plate is a valid, canonical document", "[io][example]") {
    const auto path = kExamples / "plate.bcad";
    const auto document = io::loadDocument(path);
    REQUIRE(document.has_value());
    CHECK(document->name() == "Plate");

    // Saving it reproduces the file byte for byte.
    const auto text = io::documentToJson(*document);
    REQUIRE(text.has_value());
    CHECK(*text == readFile(path));

    const features::ValidationReport report = features::validateDocument(*document);
    CHECK(report.valid());
    CHECK(report.issues.empty()); // the sketch is fully constrained
    REQUIRE(report.bodies.size() == 1);
    REQUIRE(report.bodies[0].properties.has_value());
    // 100 x 50 x 20 mm minus a hole of radius 10 mm.
    CHECK_THAT(report.bodies[0].properties->volume.in(units::mm3),
               WithinRel(100.0 * 50.0 * 20.0 - std::numbers::pi * 10.0 * 10.0 * 20.0, 1e-12));
}
