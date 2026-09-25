#include "reference/DrawingTestSupport.hpp"

#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/drawing/Tolerance.hpp>
#include <bettercad/features/Validation.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <numbers>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::test;
using namespace bettercad::test::drawref;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

// RM-DWG-05 -- tolerances, fits, datums and a feature-control frame.
namespace {

constexpr double kLengthMm = 80.0;
constexpr double kWidthMm = 50.0;
constexpr double kHeightMm = 25.0;
constexpr double kBoreRadiusMm = 10.0;

/// ISO 286-1: H7 on a 20 mm nominal is EI = 0, ES = +0.021 mm.
///
/// Transcribed from the STANDARD, not from the codebase's table: a test that
/// read the limits out of the same data the implementation reads them from
/// would agree with a transcription error rather than catch one.
constexpr double kH7At20LowerMm = 0.000;
constexpr double kH7At20UpperMm = 0.021;

} // namespace

TEST_CASE("DrawingReference_ToleranceBlockHasTheVolumeItsBoreLeaves",
          "[reference][drawing][rm-dwg-05]") {
    const double expected = kLengthMm * kWidthMm * kHeightMm -
                            std::numbers::pi * kBoreRadiusMm * kBoreRadiusMm * kHeightMm;

    Drawn d = drawn(reference::DrawingReferenceModelKind::ToleranceBlock);
    CHECK(features::validateDocument(d.document()).valid());

    const auto bore = d.document().findByName("Bore");
    REQUIRE(bore.has_value());
    const geometry::Body* body = d.regenerator().body(*bore);
    REQUIRE(body != nullptr);
    CHECK_THAT(volumeMm3(*body), WithinAbs(expected, 1e-6));
}

TEST_CASE("DrawingReference_ToleranceBlockWritesADeviationPairAndAPairOfLimits",
          "[reference][drawing][rm-dwg-05][tol]") {
    // Two ways of saying the same kind of thing, and they must not be the
    // same text: 80 +/-0.10 is written as one number with a deviation, and
    // 50 +0.15 -0.05 is written as its two limits, larger first.
    auto m = reference::buildDrawnToleranceBlockReferenceModel();
    INFO(why(m));
    REQUIRE(m.has_value());
    Drawn model{std::move(m->document)};
    INFO(model.why());
    REQUIRE(model.succeeded());

    CHECK_THAT(measuredMm(model, m->length), WithinAbs(kLengthMm, 1e-9));
    CHECK_THAT(measuredMm(model, m->width), WithinAbs(kWidthMm, 1e-9));

    auto length = drawing::measure(model.document(), m->length, model.bodies(),
                                   model.transforms());
    REQUIRE(length.has_value());
    REQUIRE(length->interval.has_value());
    CHECK_THAT(length->interval->lower.in(units::mm), WithinAbs(kLengthMm - 0.10, 1e-12));
    CHECK_THAT(length->interval->upper.in(units::mm), WithinAbs(kLengthMm + 0.10, 1e-12));
    // A symmetric pair is written with the +/- sign and no second line.
    CHECK_THAT(length->text, ContainsSubstring("±0.10"));
    CHECK(length->lowerText.empty());

    auto width = drawing::measure(model.document(), m->width, model.bodies(),
                                  model.transforms());
    REQUIRE(width.has_value());
    REQUIRE(width->interval.has_value());
    CHECK_THAT(width->interval->lower.in(units::mm), WithinAbs(kWidthMm - 0.05, 1e-12));
    CHECK_THAT(width->interval->upper.in(units::mm), WithinAbs(kWidthMm + 0.15, 1e-12));
    // Limits: the upper in `text`, the lower in `lowerText`, and both present.
    CHECK_FALSE(width->lowerText.empty());
    CHECK(width->text != width->lowerText);
    CHECK_THAT(width->text, ContainsSubstring("50.15"));
    CHECK_THAT(width->lowerText, ContainsSubstring("49.95"));
}

TEST_CASE("DrawingReference_ToleranceBlockResolvesItsFitFromTheStandard",
          "[reference][drawing][rm-dwg-05][tol]") {
    // Ø20 H7. The drawing stores the DESIGNATION; the limits come from
    // ISO 286 when the dimension is measured. The expected numbers here are
    // the standard's, written out above.
    auto m = reference::buildDrawnToleranceBlockReferenceModel();
    INFO(why(m));
    REQUIRE(m.has_value());
    Drawn model{std::move(m->document)};
    REQUIRE(model.succeeded());

    auto bore = drawing::measure(model.document(), m->boreSize, model.bodies(),
                                 model.transforms());
    INFO(why(bore));
    REQUIRE(bore.has_value());
    REQUIRE(bore->length.has_value());
    const double nominal = bore->length->in(units::mm);
    CHECK_THAT(nominal, WithinAbs(2.0 * kBoreRadiusMm, 1e-9));

    REQUIRE(bore->interval.has_value());
    // 1e-9 mm is a nanometre; the deviations are exact tabulated values, so
    // anything looser would not be checking the table.
    CHECK_THAT(bore->interval->lower.in(units::mm),
               WithinAbs(nominal + kH7At20LowerMm, 1e-9));
    CHECK_THAT(bore->interval->upper.in(units::mm),
               WithinAbs(nominal + kH7At20UpperMm, 1e-9));

    // The file stores no numbers for it: only H7, and the role that decides
    // the letter's case.
    const drawing::Dimension* dimension = drawing::findDimension(model.document(), m->boreSize);
    REQUIRE(dimension != nullptr);
    const auto& tolerance = dimension->definition().tolerance;
    REQUIRE(tolerance.has_value());
    REQUIRE(tolerance->fit.has_value());
    CHECK(drawing::toString(*tolerance->fit) == "H7");
    CHECK(tolerance->lower.si() == 0.0);
    CHECK(tolerance->upper.si() == 0.0);
}

TEST_CASE("DrawingReference_ToleranceBlockHoldsItsBoreAgainstAThenB",
          "[reference][drawing][rm-dwg-05][gdt]") {
    // The datum ORDER is the requirement. A|B is not B|A: the primary datum
    // takes three degrees of freedom and the secondary takes what is left, so
    // a frame that kept its datums as a set would be a different instruction.
    auto m = reference::buildDrawnToleranceBlockReferenceModel();
    INFO(why(m));
    REQUIRE(m.has_value());
    Drawn model{std::move(m->document)};
    REQUIRE(model.succeeded());

    const drawing::Annotation* frame = drawing::findAnnotation(model.document(), m->borePosition);
    REQUIRE(frame != nullptr);
    const auto& control = frame->definition().frame;
    REQUIRE(control.has_value());
    CHECK(control->characteristic == drawing::GeometricCharacteristic::Position);
    CHECK(control->zone == drawing::ToleranceZone::Cylindrical);
    CHECK_THAT(control->tolerance.in(units::mm), WithinAbs(0.05, 1e-12));
    REQUIRE(control->datums.size() == 2);
    CHECK(control->datums[0].letter == 'A');
    CHECK(control->datums[1].letter == 'B');

    // Both datums it cites are actually defined on this drawing: a frame
    // against a letter nobody declared is a drawing that cannot be inspected.
    auto undefined = drawing::undefinedDatums(model.document(), m->borePosition);
    INFO(why(undefined));
    REQUIRE(undefined.has_value());
    CHECK(undefined->empty());

    CHECK(annotationTextOf(model, m->datumA) == "A");
    CHECK(annotationTextOf(model, m->datumB) == "B");
}

TEST_CASE("DrawingReference_ToleranceBlockDatumsAreUndefinedWhenTheirSymbolsGo",
          "[reference][drawing][rm-dwg-05][gdt][recovery]") {
    // VALID -> BROKEN -> REPAIRED, on the datum structure. Removing the datum
    // B symbol must make the frame report B as undefined -- not silently pick
    // another letter, and not quietly drop the requirement.
    auto m = reference::buildDrawnToleranceBlockReferenceModel();
    INFO(why(m));
    REQUIRE(m.has_value());
    const AnnotationId datumB = m->datumB;
    const AnnotationId position = m->borePosition;
    const drawing::Annotation* symbolB = drawing::findAnnotation(m->document, datumB);
    REQUIRE(symbolB != nullptr);
    const drawing::AnnotationDefinition definitionB = symbolB->definition();
    Drawn model{std::move(m->document)};
    REQUIRE(model.succeeded());

    auto atFirst = drawing::undefinedDatums(model.document(), position);
    REQUIRE(atFirst.has_value());
    REQUIRE(atFirst->empty());

    REQUIRE(drawing::removeAnnotation(model.document(), datumB).has_value());
    model.regenerate();
    auto missing = drawing::undefinedDatums(model.document(), position);
    INFO(why(missing));
    REQUIRE(missing.has_value());
    REQUIRE(missing->size() == 1);
    CHECK(missing->front() == 'B');

    // Put it back, and the frame is satisfied again.
    REQUIRE(drawing::createAnnotation(model.document(), "DatumB", definitionB).has_value());
    model.regenerate();
    INFO(model.why());
    REQUIRE(model.succeeded());
    auto repaired = drawing::undefinedDatums(model.document(), position);
    REQUIRE(repaired.has_value());
    CHECK(repaired->empty());
}

TEST_CASE("DrawingReference_ToleranceBlockTolerancesFollowTheModel",
          "[reference][drawing][rm-dwg-05][regen]") {
    // The interval is about the NOMINAL, and the nominal is measured. Growing
    // the block moves the whole interval with it, because nothing in the file
    // is a copy of the size.
    auto m = reference::buildDrawnToleranceBlockReferenceModel();
    INFO(why(m));
    REQUIRE(m.has_value());
    Drawn model{std::move(m->document)};
    REQUIRE(model.succeeded());

    constexpr double kGrownMm = 90.0;
    driveMm(model, "block_length", kGrownMm);

    auto grown = drawing::measure(model.document(), m->length, model.bodies(),
                                  model.transforms());
    REQUIRE(grown.has_value());
    CHECK_THAT(grown->length->in(units::mm), WithinAbs(kGrownMm, 1e-9));
    REQUIRE(grown->interval.has_value());
    CHECK_THAT(grown->interval->lower.in(units::mm), WithinAbs(kGrownMm - 0.10, 1e-12));
    CHECK_THAT(grown->interval->upper.in(units::mm), WithinAbs(kGrownMm + 0.10, 1e-12));

    driveMm(model, "block_length", kLengthMm);
    CHECK_THAT(measuredMm(model, m->length), WithinAbs(kLengthMm, 1e-9));
}

TEST_CASE("DrawingReference_ToleranceBlockPutsItsSymbolsOnTheSheet",
          "[reference][drawing][rm-dwg-05][gdt]") {
    // What reaches the page: the two datum letters, the frame's tolerance,
    // and the roughness. Read off the SCENE, which is what the writers get.
    Drawn d = drawn(reference::DrawingReferenceModelKind::ToleranceBlock);
    const drawing::DrawingScene scene = sceneOf(d, drawing::sheets(d.document()).front());
    const std::vector<std::string> texts = sceneTexts(scene);

    const auto says = [&](std::string_view needle) {
        return std::ranges::any_of(texts, [&](const std::string& text) {
            return text.find(needle) != std::string::npos;
        });
    };
    CHECK(says("A"));
    CHECK(says("B"));
    CHECK(says("0.05"));
    // Ra in micrometres, written with its unit because a bare 1.6 on a
    // drawing would be a length.
    CHECK(says("1.6"));
    // The fit, as the designation rather than as its numbers.
    CHECK(says("H7"));
}
