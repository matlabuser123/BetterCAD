#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/drawing/Annotations.hpp>
#include <bettercad/drawing/Dimensions.hpp>
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
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
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
using drawing::DimensionFormat;
using drawing::DimensionTarget;
using drawing::DimensionTolerance;
using drawing::DimensionType;
using drawing::DrawingScale;
using drawing::FeatureControlFrame;
using drawing::FitDesignation;
using drawing::FitRole;
using drawing::GeometricCharacteristic;
using drawing::SheetDefinition;
using drawing::StandardView;
using drawing::ToleranceDisplay;
using drawing::ToleranceZone;
using drawing::ViewDefinition;

// P14-TOL-001: tolerances, fits and the GD&T foundation.
//
// WHERE THE EXPECTED NUMBERS COME FROM. The deviation arithmetic is done
// here: 20 +/-0.05 admits [19.95, 20.05] and 20 +0.10/-0.02 admits
// [19.98, 20.10], both written out rather than asked for. The ISO 286 figures
// are the published ones for a 20 mm nominal -- IT6 = 13 um and IT7 = 21 um
// for sizes over 18 up to and including 30 mm, IT7 = 15 um over 6 up to 10,
// and H places the interval at zero -- quoted with their source and NOT
// produced by calling the routine under test.
//
// SIGNS. Both deviations are signed from the nominal, so a symmetric
// tolerance is -x and +x. The nominal need not lie inside the interval: H7
// puts the whole of it at or above the nominal, and a contract demanding
// otherwise could not express H7.
namespace {

constexpr double kMm = 1e-9;

struct Fixture {
    Document document{"Drawing"};
    ObjectId part{};
    ObjectId hole{};
    SheetId sheet{};
    ViewId view{};
    std::array<EntityId, 4> lines{};
    features::Regenerator regenerator;

    drawing::BodyLookup bodies() {
        const features::Regenerator* r = &regenerator;
        return [r](ObjectId object) { return r->body(object); };
    }
    void regenerate() { REQUIRE(regenerator.regenerateAll(document).has_value()); }
};

/// A 100 x 60 x 40 block with a 10 mm hole through its top, so a width
/// dimension between two named side faces reads exactly 100.
Fixture makeBlock(DrawingScale scale = DrawingScale{1, 1}) {
    Fixture f;
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    f.lines = addRectangle(*sketch, 0_mm, 0_mm, 100_mm, 60_mm);
    const ObjectId sketchId = require(f.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Block", {.profile = SketchId::fromValue(sketchId.value()), .depth = 40_mm});
    REQUIRE(extrude.has_value());
    f.part = require(f.document.addObject(std::move(*extrude)));

    auto bore = features::HoleFeature::create(
        "Bore", {.target = FeatureId::fromValue(f.part.value()),
                 .face = geometry::planeSignature(Point3D{0_mm, 0_mm, 40_mm}, Direction3D::unitZ()),
                 .center = Point2D{50_mm, 30_mm},
                 .diameter = 10_mm});
    REQUIRE(bore.has_value());
    f.hole = require(f.document.addObject(std::move(*bore)));

    f.sheet = require(drawing::createSheet(
        f.document, "Sheet1",
        SheetDefinition{.format = drawing::SheetFormat::A3,
                        .orientation = drawing::SheetOrientation::Landscape,
                        .margins = drawing::SheetMargins{10_mm, 10_mm, 10_mm, 10_mm},
                        .scale = scale}));
    f.view = require(drawing::createView(
        f.document, "MainView",
        ViewDefinition{.sheet = f.sheet,
                       .source = ObjectReference{f.hole},
                       .orientation = StandardView::Front,
                       .placement = Point2D{200_mm, 150_mm}}));
    f.regenerate();
    return f;
}

PlaneReference sideFace(ObjectId feature, EntityId entity) {
    return PlaneReference{.object = feature,
                          .face = FaceSelector{.role = FaceRole::Side, .entity = entity}};
}

DimensionTarget onPlane(const PlaneReference& reference) {
    return DimensionTarget{.plane = reference};
}

/// A width dimension across the block, optionally toleranced.
DimensionId width(Fixture& f, const std::string& name,
                  std::optional<DimensionTolerance> tolerance = std::nullopt,
                  DimensionFormat format = DimensionFormat{}) {
    return require(drawing::createDimension(
        f.document, name,
        DimensionDefinition{.view = f.view,
                            .type = DimensionType::Linear,
                            .from = onPlane(sideFace(f.part, f.lines[3])),
                            .to = onPlane(sideFace(f.part, f.lines[1])),
                            .format = format,
                            .tolerance = tolerance}));
}

drawing::MeasuredDimension measured(Fixture& f, DimensionId id) {
    auto result = drawing::measure(f.document, id, f.bodies());
    REQUIRE(result.has_value());
    return std::move(*result);
}

/// A symmetric tolerance of the given magnitude, signed as the model stores
/// deviations: -x and +x.
DimensionTolerance symmetric(Length magnitude,
                             ToleranceDisplay display = ToleranceDisplay::PlusMinus) {
    return DimensionTolerance{.lower = Length::fromSi(-magnitude.si()),
                              .upper = magnitude,
                              .display = display};
}

} // namespace

// --- The interval ------------------------------------------------------------------------

TEST_CASE("Tolerance_SymmetricDeviationsAdmitTheIntervalTheyName",
          "[drawing][tolerance][p14]") {
    // 20 +/-0.05 admits 19.95 to 20.05. Computed here, not asked for.
    const auto interval = drawing::intervalOf(20_mm, symmetric(0.05_mm));
    REQUIRE(interval.has_value());
    CHECK_THAT(interval->lower.in(units::mm), WithinAbs(20.0 - 0.05, kMm));
    CHECK_THAT(interval->upper.in(units::mm), WithinAbs(20.0 + 0.05, kMm));
}

TEST_CASE("Tolerance_AsymmetricDeviationsAdmitTheIntervalTheyName",
          "[drawing][tolerance][p14]") {
    // 20 +0.10 / -0.02 admits 19.98 to 20.10.
    const DimensionTolerance tolerance{.lower = -0.02_mm, .upper = 0.10_mm};
    const auto interval = drawing::intervalOf(20_mm, tolerance);
    REQUIRE(interval.has_value());
    CHECK_THAT(interval->lower.in(units::mm), WithinAbs(20.0 - 0.02, kMm));
    CHECK_THAT(interval->upper.in(units::mm), WithinAbs(20.0 + 0.10, kMm));
}

TEST_CASE("Tolerance_TheNominalNeedNotLieInsideTheInterval", "[drawing][tolerance][p14]") {
    // Both deviations above zero is legal, and is what ISO 286 does: H7's
    // whole interval is at or above the nominal size. A contract that
    // demanded the nominal be interior could not express it.
    const DimensionTolerance tolerance{.lower = 0.01_mm, .upper = 0.03_mm};
    REQUIRE(validate(tolerance).has_value());
    const auto interval = drawing::intervalOf(20_mm, tolerance);
    REQUIRE(interval.has_value());
    CHECK_THAT(interval->lower.in(units::mm), WithinAbs(20.01, kMm));
    CHECK_THAT(interval->upper.in(units::mm), WithinAbs(20.03, kMm));
}

TEST_CASE("Tolerance_AReversedIntervalIsRefused", "[drawing][tolerance][p14]") {
    const DimensionTolerance backwards{.lower = 0.05_mm, .upper = -0.05_mm};
    const auto refused = validate(backwards);
    REQUIRE_FALSE(refused.has_value());
    CHECK_THAT(refused.error().message, ContainsSubstring("must not be above its upper"));

    for (const double bad : {std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity()}) {
        INFO(bad);
        const DimensionTolerance notFinite{.lower = -0.001_mm, .upper = Length::fromSi(bad)};
        const auto result = validate(notFinite);
        REQUIRE_FALSE(result.has_value());
        CHECK_THAT(result.error().message, ContainsSubstring("must be finite"));
    }

    // And a nominal that is not a number cannot have an interval about it.
    const auto noNominal = drawing::intervalOf(
        Length::fromSi(std::numeric_limits<double>::quiet_NaN()), symmetric(0.05_mm));
    REQUIRE_FALSE(noNominal.has_value());
    CHECK_THAT(noNominal.error().message, ContainsSubstring("not finite"));
}

TEST_CASE("Tolerance_AToleranceOfNoWidthIsRefused", "[drawing][tolerance][p14]") {
    // An interval that admits one size admits nothing that can be made, and
    // a default-constructed tolerance is exactly that -- so a dimension
    // cannot come to read "100 +/-0" because somebody meant to leave the
    // tolerance off.
    for (const DimensionTolerance narrow :
         {DimensionTolerance{}, DimensionTolerance{.lower = 0.02_mm, .upper = 0.02_mm}}) {
        const auto refused = validate(narrow);
        REQUIRE_FALSE(refused.has_value());
        CHECK_THAT(refused.error().message, ContainsSubstring("no width"));
    }
    // The smallest interval that IS a width is accepted, so the rule refuses
    // only what it says it refuses.
    CHECK(validate(DimensionTolerance{.lower = 0.02_mm, .upper = 0.0201_mm}).has_value());

    Fixture f = makeBlock();
    const auto refused = drawing::createDimension(
        f.document, "NoWidth",
        DimensionDefinition{.view = f.view,
                            .type = DimensionType::Linear,
                            .from = onPlane(sideFace(f.part, f.lines[3])),
                            .to = onPlane(sideFace(f.part, f.lines[1])),
                            .tolerance = DimensionTolerance{}});
    REQUIRE_FALSE(refused.has_value());
    CHECK_THAT(refused.error().message, ContainsSubstring("no width"));
}

// --- One interval, two presentations ------------------------------------------------------

TEST_CASE("Tolerance_LimitsAndPlusMinusDescribeTheSamePart", "[drawing][tolerance][p14]") {
    // The whole point of storing an interval rather than a string: 20 +/-0.05
    // and the pair 20.05 / 19.95 are ONE fact shown two ways, so they cannot
    // come to disagree.
    Fixture f = makeBlock();
    const DimensionFormat format{.decimals = 2};
    const DimensionId asPair = width(f, "AsPair", symmetric(0.05_mm), format);
    const DimensionId asLimits =
        width(f, "AsLimits", symmetric(0.05_mm, ToleranceDisplay::Limits), format);

    const drawing::MeasuredDimension pair = measured(f, asPair);
    const drawing::MeasuredDimension limits = measured(f, asLimits);

    // The same admitted interval, bit for bit.
    REQUIRE(pair.interval.has_value());
    REQUIRE(limits.interval.has_value());
    CHECK(pair.interval->lower.si() == limits.interval->lower.si());
    CHECK(pair.interval->upper.si() == limits.interval->upper.si());
    CHECK_THAT(pair.interval->lower.in(units::mm), WithinAbs(100.0 - 0.05, kMm));
    CHECK_THAT(pair.interval->upper.in(units::mm), WithinAbs(100.0 + 0.05, kMm));

    // Written differently.
    CHECK(pair.text == "100.00 ±0.05");
    CHECK(pair.lowerText.empty());
    CHECK(limits.text == "100.05"); // larger over smaller, as a drawing stacks them
    CHECK(limits.lowerText == "99.95");
}

TEST_CASE("Tolerance_AnAsymmetricPairWritesBothSigns", "[drawing][tolerance][p14]") {
    Fixture f = makeBlock();
    const DimensionId id =
        width(f, "Asym", DimensionTolerance{.lower = -0.02_mm, .upper = 0.10_mm},
              DimensionFormat{.decimals = 2});
    const drawing::MeasuredDimension result = measured(f, id);
    // A deviation is a signed offset, so the sign is always written: "+0.10
    // -0.02" and never "0.10 -0.02", which reads as a range.
    CHECK(result.text == "100.00 +0.10 -0.02");
    CHECK_THAT(result.interval->lower.in(units::mm), WithinAbs(99.98, kMm));
    CHECK_THAT(result.interval->upper.in(units::mm), WithinAbs(100.10, kMm));
}

TEST_CASE("Tolerance_DisplayPrecisionDoesNotChangeWhatThePartIsMadeTo",
          "[drawing][tolerance][p14]") {
    Fixture f = makeBlock();
    const DimensionId id = width(f, "W", symmetric(0.05_mm), DimensionFormat{.decimals = 3});
    const double lower = measured(f, id).interval->lower.si();
    const double upper = measured(f, id).interval->upper.si();

    for (const std::uint8_t decimals : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{4}}) {
        INFO(static_cast<int>(decimals));
        DimensionDefinition definition = drawing::findDimension(f.document, id)->definition();
        definition.format.decimals = decimals;
        REQUIRE(drawing::setDimensionDefinition(f.document, id, definition).has_value());
        const drawing::MeasuredDimension again = measured(f, id);
        CHECK(again.interval->lower.si() == lower); // bit for bit
        CHECK(again.interval->upper.si() == upper);
    }
}

TEST_CASE("Tolerance_ATolerancesOwnPrecisionSurvivesTheNominalsFormat",
          "[drawing][tolerance][p14]") {
    // The decimals an engineer picks are a presentation choice about the
    // NOMINAL. Applied to the tolerance they stop being one: at no decimals
    // +/-0.05 would be written "+/-0", which is a requirement no part can
    // meet, and the two limits would be written as the same number twice.
    CHECK(drawing::decimalsWithoutRounding(0.2_mm) == 1);
    CHECK(drawing::decimalsWithoutRounding(0.05_mm) == 2);
    CHECK(drawing::decimalsWithoutRounding(0.005_mm) == 3);
    CHECK(drawing::decimalsWithoutRounding(0.0125_mm) == 4);
    CHECK(drawing::decimalsWithoutRounding(0.021_mm, 2) == 3); // at least, not exactly
    CHECK(drawing::decimalsWithoutRounding(1_mm, 3) == 3);     // and never fewer than asked
    CHECK(drawing::decimalsWithoutRounding(0.0000001_mm) == 6); // a nanometre is the floor

    Fixture f = makeBlock();
    const DimensionFormat whole{.decimals = 0};
    CHECK(measured(f, width(f, "Rough", symmetric(0.05_mm), whole)).text == "100 ±0.05");

    const drawing::MeasuredDimension limits =
        measured(f, width(f, "RoughLimits", symmetric(0.05_mm, ToleranceDisplay::Limits), whole));
    CHECK(limits.text == "100.05");
    CHECK(limits.lowerText == "99.95");
    CHECK(limits.text != limits.lowerText); // which two decimals of nominal would not give

    // An asymmetric pair takes the precision the WIDER of the two needs, so
    // neither is written to a precision that loses it.
    CHECK(measured(f, width(f, "Mixed",
                            DimensionTolerance{.lower = -0.002_mm, .upper = 0.1_mm}, whole))
              .text == "100 +0.100 -0.002");
}

TEST_CASE("Tolerance_AFitsLimitsAreTheStandardsAndNotARounding",
          "[drawing][tolerance][p14]") {
    // ISO 286-1: over 80 up to and including 120 mm, IT7 is 35 um. So H7 on
    // the block's 100 mm width admits 100.000 to 100.035. Written to the
    // nominal's two decimals those become 100.04 and 100.00 -- an interval
    // that is neither the standard's width nor its position, on the face of
    // the drawing.
    Fixture f = makeBlock();
    const DimensionId id = width(f, "FittedWidth",
                                 DimensionTolerance{.fit = FitDesignation{FitRole::Hole, 'H', 7},
                                                    .display = ToleranceDisplay::Limits},
                                 DimensionFormat{.decimals = 2});
    const drawing::MeasuredDimension result = measured(f, id);
    CHECK(result.text == "100.035");
    CHECK(result.lowerText == "100.000");
    CHECK_THAT((result.interval->upper - result.interval->lower).in(units::mm),
               WithinAbs(0.035, kMm));

    // And shown as a designation it is still the designation, whatever the
    // dimension's decimals.
    DimensionDefinition definition = drawing::findDimension(f.document, id)->definition();
    definition.tolerance->display = ToleranceDisplay::PlusMinus;
    REQUIRE(drawing::setDimensionDefinition(f.document, id, definition).has_value());
    CHECK(measured(f, id).text == "100.00 H7");
}

TEST_CASE("Tolerance_TheViewScaleDoesNotChangeTheInterval", "[drawing][tolerance][p14]") {
    // The hard gate, restated for tolerances: a drawing scale is a way of
    // showing a part and never a change to it. A 1:2 view of a 100 mm block
    // still dimensions 100, and still admits [99.95, 100.05].
    for (const DrawingScale scale : {DrawingScale{1, 1}, DrawingScale{1, 2}, DrawingScale{5, 1}}) {
        INFO("scale " << scale.label());
        Fixture f = makeBlock(scale);
        const drawing::MeasuredDimension result =
            measured(f, width(f, "W", symmetric(0.05_mm), DimensionFormat{.decimals = 2}));
        CHECK(result.text == "100.00 ±0.05");
        CHECK_THAT(result.interval->lower.in(units::mm), WithinAbs(99.95, kMm));
        CHECK_THAT(result.interval->upper.in(units::mm), WithinAbs(100.05, kMm));
    }
}

// --- Fits ---------------------------------------------------------------------------------

TEST_CASE("Tolerance_AHoleFitResolvesToItsTabulatedDeviations", "[drawing][tolerance][p14]") {
    // ISO 286-1: for a nominal size over 18 up to and including 30 mm the
    // standard tolerance IT7 is 21 um, and the fundamental deviation of H is
    // zero. So H7 on 20 mm admits 20.000 to 20.021. Those figures are the
    // published ones, quoted here, not produced by the routine under test.
    const FitDesignation h7{FitRole::Hole, 'H', 7};
    CHECK(toString(h7) == "H7"); // a hole's letter is written as a capital

    const auto deviations = drawing::fitDeviations(20_mm, h7);
    REQUIRE(deviations.has_value());
    CHECK_THAT(deviations->lower.in(units::mm), WithinAbs(0.0, kMm));
    CHECK_THAT(deviations->upper.in(units::mm), WithinAbs(0.021, kMm));

    const auto interval = drawing::intervalOf(20_mm, DimensionTolerance{.fit = h7});
    REQUIRE(interval.has_value());
    CHECK_THAT(interval->lower.in(units::mm), WithinAbs(20.000, kMm));
    CHECK_THAT(interval->upper.in(units::mm), WithinAbs(20.021, kMm));

    // The width is the standard tolerance itself.
    const auto itWidth = drawing::fitWidth(20_mm, h7);
    REQUIRE(itWidth.has_value());
    CHECK_THAT(itWidth->in(units::mm), WithinAbs(0.021, kMm));
    CHECK_THAT((interval->upper - interval->lower).in(units::mm), WithinAbs(0.021, kMm));
}

TEST_CASE("Tolerance_AShaftFitKeepsItsNotationAndInventsNoNumbers",
          "[drawing][tolerance][p14]") {
    // The hard one. Shaft fundamental deviations are not tabulated in this
    // build, so g6 has a known tolerance WIDTH and no known limits. It must
    // say so rather than produce a number somebody would machine to.
    const FitDesignation g6{FitRole::Shaft, 'G', 6};
    CHECK(toString(g6) == "g6"); // a shaft's letter is written in lower case

    const auto deviations = drawing::fitDeviations(20_mm, g6);
    REQUIRE_FALSE(deviations.has_value());
    CHECK(errorCode(deviations) == ErrorCode::FailedPrecondition);
    CHECK_THAT(deviations.error().message, ContainsSubstring("not tabulated"));
    CHECK_THAT(deviations.error().message, ContainsSubstring("no numbers are invented"));

    // The width IS known: IT is shared by holes and shafts, and IT6 over 18
    // up to and including 30 mm is 13 um.
    const auto itWidth = drawing::fitWidth(20_mm, g6);
    REQUIRE(itWidth.has_value());
    CHECK_THAT(itWidth->in(units::mm), WithinAbs(0.013, kMm));

    // A dimension citing it still writes the designation, and still refuses
    // to write limits.
    const auto text = drawing::formatTolerance(DimensionTolerance{.fit = g6}, DimensionFormat{});
    REQUIRE(text.has_value());
    CHECK(*text == "g6");

    Fixture f = makeBlock();
    const DimensionId id = width(f, "Shaft", DimensionTolerance{.fit = g6});
    const auto result = drawing::measure(f.document, id, f.bodies());
    REQUIRE_FALSE(result.has_value()); // the interval cannot be worked out
    CHECK_THAT(result.error().message, ContainsSubstring("not tabulated"));
    CHECK_THAT(result.error().message, ContainsSubstring("Shaft")); // and it says which dimension
}

TEST_CASE("Tolerance_AnUntabulatedHolePositionIsRefusedRatherThanGuessed",
          "[drawing][tolerance][p14]") {
    // This build carries D, E, F, G and H. K is a transition position and is
    // not here; it must not be estimated.
    const auto deviations = drawing::fitDeviations(20_mm, FitDesignation{FitRole::Hole, 'K', 7});
    REQUIRE_FALSE(deviations.has_value());
    CHECK(errorCode(deviations) == ErrorCode::FailedPrecondition);
    CHECK_THAT(deviations.error().message, ContainsSubstring("not tabulated"));
    CHECK_THAT(deviations.error().message, ContainsSubstring("no numbers are invented"));
    // The grade is fine, so the WIDTH is still known.
    CHECK(drawing::fitWidth(20_mm, FitDesignation{FitRole::Hole, 'K', 7}).has_value());
}

TEST_CASE("Tolerance_AFitAndAPairOfDeviationsCannotBothBeGiven",
          "[drawing][tolerance][p14]") {
    const DimensionTolerance both{
        .lower = -0.05_mm, .upper = 0.05_mm, .fit = FitDesignation{FitRole::Hole, 'H', 7}};
    const auto refused = validate(both);
    REQUIRE_FALSE(refused.has_value());
    CHECK_THAT(refused.error().message, ContainsSubstring("not both"));
}

TEST_CASE("Tolerance_AFitDesignationIsCheckedOnItsOwn", "[drawing][tolerance][p14]") {
    CHECK_THAT(validate(FitDesignation{FitRole::Hole, 'h', 7}).error().message,
               ContainsSubstring("capital letter A to Z"));
    CHECK_THAT(validate(FitDesignation{FitRole::Hole, 'H', 0}).error().message,
               ContainsSubstring("1 to 18"));
    CHECK_THAT(validate(FitDesignation{FitRole::Hole, 'H', 19}).error().message,
               ContainsSubstring("1 to 18"));
    CHECK(validate(FitDesignation{FitRole::Hole, 'H', 7}).has_value());
}

TEST_CASE("Tolerance_AFitIsReadFromTheStandardAndNotStored", "[drawing][tolerance][p14]") {
    // H7 means whatever ISO 286 says H7 means AT THE SIZE IT IS APPLIED TO,
    // and the size ranges are not linear. Over 6 up to and including 10 mm
    // IT7 is 15 um; over 18 up to 30 it is 21 um. A tolerance that had copied
    // its numbers when it was created would show the same figure at both.
    const DimensionTolerance h7{.fit = FitDesignation{FitRole::Hole, 'H', 7}};
    const auto small = drawing::intervalOf(8_mm, h7);
    const auto large = drawing::intervalOf(20_mm, h7);
    REQUIRE(small.has_value());
    REQUIRE(large.has_value());
    CHECK_THAT((small->upper - small->lower).in(units::mm), WithinAbs(0.015, kMm));
    CHECK_THAT((large->upper - large->lower).in(units::mm), WithinAbs(0.021, kMm));
}

TEST_CASE("Tolerance_ClearanceFollowsFromTwoResolvedIntervals", "[drawing][tolerance][p14]") {
    // Where BOTH sides resolve, the clearance follows from the limits:
    //     C_min = D_min - d_max      C_max = D_max - d_min
    // Only holes resolve here, so the pair is H7 against H6 -- which is not a
    // fit anyone would specify, and is exactly why this is a foundation and
    // not a fit calculator. IT6 over 18 to 30 is 13 um, IT7 is 21 um, and H
    // places both intervals from zero.
    const auto hole = drawing::intervalOf(
        20_mm, DimensionTolerance{.fit = FitDesignation{FitRole::Hole, 'H', 7}});
    const auto shaftLike = drawing::intervalOf(
        20_mm, DimensionTolerance{.fit = FitDesignation{FitRole::Hole, 'H', 6}});
    REQUIRE(hole.has_value());
    REQUIRE(shaftLike.has_value());

    const double minimumClearance = (hole->lower - shaftLike->upper).in(units::mm);
    const double maximumClearance = (hole->upper - shaftLike->lower).in(units::mm);
    CHECK_THAT(minimumClearance, WithinAbs(0.0 - 0.013, kMm));
    CHECK_THAT(maximumClearance, WithinAbs(0.021 - 0.0, kMm));
}

TEST_CASE("Tolerance_AFitAtASizeRangeBoundaryDoesNotFallThroughIt",
          "[drawing][tolerance][p14]") {
    // ISO 286's size ranges are "over 18 up to AND INCLUDING 30", so 30 mm
    // belongs to the lower range: IT7 is 21 um there and 25 um in the next
    // one. A measured length arrives as a double, so a size that is 30 mm to
    // the nanometre must not land in the range above and widen the tolerance
    // by a fifth.
    const DimensionTolerance h7{.fit = FitDesignation{FitRole::Hole, 'H', 7}};
    const auto at = drawing::intervalOf(30_mm, h7);
    REQUIRE(at.has_value());
    CHECK_THAT((at->upper - at->lower).in(units::mm), WithinAbs(0.021, kMm));

    for (const double wobble : {-1e-10, -1e-12, 1e-12, 1e-10}) {
        INFO(wobble);
        const auto near = drawing::intervalOf(Length::fromSi(0.030 + wobble * 1e-3), h7);
        REQUIRE(near.has_value());
        CHECK_THAT((near->upper - near->lower).in(units::mm), WithinAbs(0.021, kMm));
    }
    // And the range above really is different, so the check above is not
    // passing because every size gives 21 um.
    const auto beyond = drawing::intervalOf(40_mm, h7);
    REQUIRE(beyond.has_value());
    CHECK_THAT((beyond->upper - beyond->lower).in(units::mm), WithinAbs(0.025, kMm));
}

// --- Datum letters, one rule shared with the annotation foundation ------------------------

TEST_CASE("Tolerance_ADatumLetterFollowsOneRuleEverywhere", "[drawing][tolerance][p14]") {
    // The same rule a datum feature symbol uses, so a frame cannot cite a
    // letter that no datum could ever be called.
    CHECK(drawing::validateDatumLetter('A').has_value());
    CHECK_THAT(drawing::validateDatumLetter('a').error().message, ContainsSubstring("capital"));
    CHECK_THAT(drawing::validateDatumLetter('1').error().message, ContainsSubstring("capital"));
    for (const char letter : {'I', 'O', 'Q'}) {
        INFO(letter);
        CHECK_THAT(drawing::validateDatumLetter(letter).error().message,
                   ContainsSubstring("I, O or Q"));
    }

    // And the datum feature symbol reaches the same verdict, word for word,
    // because it is the same function.
    Fixture f = makeBlock();
    for (const std::string& letter : {std::string{"a"}, std::string{"I"}}) {
        INFO(letter);
        const auto refused = drawing::createAnnotation(
            f.document, "Datum",
            AnnotationDefinition{.view = f.view,
                                 .type = AnnotationType::Datum,
                                 .target = AnnotationTarget{.plane = sideFace(f.part, f.lines[3])},
                                 .text = letter,
                                 .placement = Point2D{40_mm, 40_mm}});
        REQUIRE_FALSE(refused.has_value());
        const auto direct = drawing::validateDatumLetter(letter.front());
        REQUIRE_FALSE(direct.has_value());
        CHECK(refused.error().message == direct.error().message);
    }
}

// --- Feature-control frames ----------------------------------------------------------------

TEST_CASE("Tolerance_DatumOrderIsPartOfTheRequirement", "[drawing][tolerance][p14]") {
    // A|B|C is a different requirement from B|A|C. If the datums were held in
    // anything unordered this would not hold, and a drawing would silently
    // mean something else.
    const FeatureControlFrame abc{.characteristic = GeometricCharacteristic::Position,
                                  .zone = ToleranceZone::Cylindrical,
                                  .tolerance = 0.2_mm,
                                  .datums = {{'A'}, {'B'}, {'C'}}};
    FeatureControlFrame bac = abc;
    bac.datums = {{'B'}, {'A'}, {'C'}};

    REQUIRE(validate(abc).has_value());
    REQUIRE(validate(bac).has_value());
    CHECK_FALSE(abc == bac);
    CHECK(abc.datums[0].letter == 'A');
    CHECK(bac.datums[0].letter == 'B');
}

TEST_CASE("Tolerance_FormCharacteristicsCiteNoDatumAndTheOthersMust",
          "[drawing][tolerance][p14]") {
    // A flat face is flat by itself; parallel is parallel to something.
    for (const GeometricCharacteristic form :
         {GeometricCharacteristic::Straightness, GeometricCharacteristic::Flatness,
          GeometricCharacteristic::Circularity, GeometricCharacteristic::Cylindricity}) {
        INFO(drawing::toString(form));
        CHECK(drawing::isForm(form));
        CHECK(validate(FeatureControlFrame{.characteristic = form, .tolerance = 0.1_mm})
                  .has_value());
        const auto withDatum = validate(
            FeatureControlFrame{.characteristic = form, .tolerance = 0.1_mm, .datums = {{'A'}}});
        REQUIRE_FALSE(withDatum.has_value());
        CHECK_THAT(withDatum.error().message, ContainsSubstring("cites no datum"));
    }

    for (const GeometricCharacteristic related :
         {GeometricCharacteristic::Parallelism, GeometricCharacteristic::Perpendicularity,
          GeometricCharacteristic::Angularity, GeometricCharacteristic::Position,
          GeometricCharacteristic::CircularRunout, GeometricCharacteristic::TotalRunout}) {
        INFO(drawing::toString(related));
        CHECK_FALSE(drawing::isForm(related));
        const auto without =
            validate(FeatureControlFrame{.characteristic = related, .tolerance = 0.1_mm});
        REQUIRE_FALSE(without.has_value());
        CHECK_THAT(without.error().message, ContainsSubstring("parallel to what?"));
        CHECK(validate(FeatureControlFrame{
                           .characteristic = related, .tolerance = 0.1_mm, .datums = {{'A'}}})
                  .has_value());
    }
}

TEST_CASE("Tolerance_ACylindricalZoneOnlyHoldsALine", "[drawing][tolerance][p14]") {
    // A flatness zone is two planes and a circularity zone two concentric
    // circles; neither is ever a cylinder.
    for (const GeometricCharacteristic surface :
         {GeometricCharacteristic::Flatness, GeometricCharacteristic::Circularity,
          GeometricCharacteristic::Cylindricity}) {
        INFO(drawing::toString(surface));
        CHECK_FALSE(drawing::allowsCylindricalZone(surface));
        const auto refused = validate(FeatureControlFrame{.characteristic = surface,
                                                          .zone = ToleranceZone::Cylindrical,
                                                          .tolerance = 0.1_mm});
        REQUIRE_FALSE(refused.has_value());
        CHECK_THAT(refused.error().message, ContainsSubstring("cannot be a cylinder"));
    }
    CHECK(drawing::allowsCylindricalZone(GeometricCharacteristic::Position));
    CHECK(drawing::allowsCylindricalZone(GeometricCharacteristic::Straightness));
}

TEST_CASE("Tolerance_AFrameIsCheckedForWhatIsIncoherent", "[drawing][tolerance][p14]") {
    const auto refuse = [](const FeatureControlFrame& frame, std::string_view expected) {
        INFO(expected);
        const auto result = validate(frame);
        REQUIRE_FALSE(result.has_value());
        CHECK_THAT(result.error().message, ContainsSubstring(std::string{expected}));
    };
    const FeatureControlFrame good{.characteristic = GeometricCharacteristic::Position,
                                   .zone = ToleranceZone::Cylindrical,
                                   .tolerance = 0.2_mm,
                                   .datums = {{'A'}}};
    REQUIRE(validate(good).has_value());

    FeatureControlFrame zero = good;
    zero.tolerance = 0_mm;
    refuse(zero, "greater than zero");

    FeatureControlFrame negative = good;
    negative.tolerance = -0.1_mm;
    refuse(negative, "greater than zero");

    FeatureControlFrame notFinite = good;
    notFinite.tolerance = Length::fromSi(std::numeric_limits<double>::infinity());
    refuse(notFinite, "finite");

    FeatureControlFrame tooMany = good;
    tooMany.datums = {{'A'}, {'B'}, {'C'}, {'D'}};
    refuse(tooMany, "at most three datums");

    FeatureControlFrame repeated = good;
    repeated.datums = {{'A'}, {'B'}, {'A'}};
    refuse(repeated, "cited twice");

    FeatureControlFrame badLetter = good;
    badLetter.datums = {{'I'}};
    refuse(badLetter, "I, O or Q");
}

TEST_CASE("Tolerance_EveryCharacteristicHasASymbolAndAName", "[drawing][tolerance][p14]") {
    // A characteristic is in the enum only when it has everything, and a
    // symbol and a round trip are part of everything.
    for (const GeometricCharacteristic characteristic :
         {GeometricCharacteristic::Straightness, GeometricCharacteristic::Flatness,
          GeometricCharacteristic::Circularity, GeometricCharacteristic::Cylindricity,
          GeometricCharacteristic::Parallelism, GeometricCharacteristic::Perpendicularity,
          GeometricCharacteristic::Angularity, GeometricCharacteristic::Position,
          GeometricCharacteristic::CircularRunout, GeometricCharacteristic::TotalRunout}) {
        INFO(drawing::toString(characteristic));
        CHECK(drawing::toString(characteristic) != "unknown");
        CHECK(drawing::symbolOf(characteristic) != "?");
        CHECK_FALSE(drawing::symbolOf(characteristic).empty());
        const auto back =
            drawing::geometricCharacteristicFromString(drawing::toString(characteristic));
        REQUIRE(back.has_value());
        CHECK(*back == characteristic);
    }
    // A few codepoints outright, so the mapping is pinned rather than assumed.
    CHECK(drawing::symbolOf(GeometricCharacteristic::Position) == "⌖");
    CHECK(drawing::symbolOf(GeometricCharacteristic::Flatness) == "⏥");
    CHECK(drawing::symbolOf(GeometricCharacteristic::Perpendicularity) == "⟂");
    CHECK(drawing::geometricCharacteristicFromString("wobbliness") == std::nullopt);
}

// --- The frame on a drawing -----------------------------------------------------------------

namespace {

/// A frame annotation on the block's left-hand face.
AnnotationId addFrame(Fixture& f, const std::string& name, const FeatureControlFrame& frame,
                      Length height = 3.5_mm) {
    return require(drawing::createAnnotation(
        f.document, name,
        AnnotationDefinition{.view = f.view,
                             .type = AnnotationType::FeatureControlFrame,
                             .target = AnnotationTarget{.plane = sideFace(f.part, f.lines[3])},
                             .style = drawing::TextStyle{height},
                             .placement = Point2D{40_mm, 40_mm},
                             .frame = frame}));
}

/// A datum feature symbol lettered @p letter.
AnnotationId addDatum(Fixture& f, const std::string& name, const std::string& letter) {
    return require(drawing::createAnnotation(
        f.document, name,
        AnnotationDefinition{.view = f.view,
                             .type = AnnotationType::Datum,
                             .target = AnnotationTarget{.plane = sideFace(f.part, f.lines[0])},
                             .text = letter,
                             .placement = Point2D{60_mm, 20_mm}}));
}

drawing::SceneItems drawn(Fixture& f, AnnotationId id) {
    auto items = drawing::draw(f.document, id, f.bodies());
    REQUIRE(items.has_value());
    return std::move(*items);
}

} // namespace

TEST_CASE("Tolerance_AFrameDrawsItsCellsInTheOrderItReads", "[drawing][tolerance][p14]") {
    // | position | diameter 0.2 | A | B | C |, in that order. The order IS
    // the requirement, so it is asserted cell by cell.
    Fixture f = makeBlock();
    const AnnotationId id = addFrame(f, "Frame",
                                     {.characteristic = GeometricCharacteristic::Position,
                                      .zone = ToleranceZone::Cylindrical,
                                      .tolerance = 0.2_mm,
                                      .datums = {{'A'}, {'B'}, {'C'}}});
    const drawing::SceneItems items = drawn(f, id);

    REQUIRE(items.texts.size() == 5);
    CHECK(items.texts[0].text == "⌖");    // the characteristic
    CHECK(items.texts[1].text == "Ø0.2"); // the zone and its size
    CHECK(items.texts[2].text == "A");
    CHECK(items.texts[3].text == "B");
    CHECK(items.texts[4].text == "C");

    // The cells run left to right in that order, on one line.
    for (std::size_t i = 1; i < items.texts.size(); ++i) {
        CHECK(items.texts[i - 1].at.x.si() < items.texts[i].at.x.si());
        CHECK(items.texts[i].at.y.si() == items.texts.front().at.y.si());
    }
    // The outer frame, a divider at each of the four inner boundaries, and
    // the leader with its arrowhead.
    CHECK(items.lines.size() == 1 + 4 + 2);
    CHECK(validate(items).has_value());
}

TEST_CASE("Tolerance_TheFrameIsDrawnAroundWhatItSays", "[drawing][tolerance][p14]") {
    // The box and its dividers are not decoration: every cell's text has to
    // sit inside the compartment drawn for it. That is what a reader uses to
    // tell which datum is primary.
    Fixture f = makeBlock();
    const drawing::SceneItems items =
        drawn(f, addFrame(f, "Frame",
                          {.characteristic = GeometricCharacteristic::Position,
                           .zone = ToleranceZone::Cylindrical,
                           .tolerance = 0.2_mm,
                           .datums = {{'A'}, {'B'}}}));

    // The boundaries: the outer box's two vertical edges and the dividers.
    std::vector<double> boundaries;
    const drawing::SceneLine& box = items.lines.front();
    REQUIRE(box.points.size() == 5);
    for (const Point2D& point : box.points) {
        boundaries.push_back(point.x.si());
    }
    for (std::size_t i = 1; i <= 3; ++i) { // one divider per inner boundary
        const drawing::SceneLine& divider = items.lines[i];
        REQUIRE(divider.points.size() == 2);
        CHECK(divider.points[0].x.si() == divider.points[1].x.si()); // vertical
        boundaries.push_back(divider.points[0].x.si());
    }
    std::ranges::sort(boundaries);
    boundaries.erase(std::ranges::unique(boundaries).begin(), boundaries.end());
    REQUIRE(boundaries.size() == 5); // four cells

    // Each text sits strictly between its own two boundaries.
    REQUIRE(items.texts.size() == 4);
    for (std::size_t i = 0; i < items.texts.size(); ++i) {
        INFO(items.texts[i].text);
        CHECK(items.texts[i].at.x.si() > boundaries[i]);
        CHECK(items.texts[i].at.x.si() < boundaries[i + 1]);
    }
}

TEST_CASE("Tolerance_AFrameWritesItsToleranceWithoutRounding",
          "[drawing][tolerance][p14]") {
    // A zone is a small number, and it is the number a part is inspected
    // against. Writing it to a fixed two decimals would draw a 0.005 zone as
    // "0.01" -- twice what the model holds, on the face of the drawing.
    Fixture f = makeBlock();
    const std::vector<std::pair<Length, std::string>> expected{
        {0.2_mm, "0.2"}, {0.05_mm, "0.05"}, {0.005_mm, "0.005"}, {0.0125_mm, "0.0125"}};
    int index = 0;
    for (const auto& [tolerance, written] : expected) {
        INFO(written);
        const drawing::SceneItems items =
            drawn(f, addFrame(f, "Frame" + std::to_string(++index),
                              {.characteristic = GeometricCharacteristic::Flatness,
                               .zone = ToleranceZone::Width,
                               .tolerance = tolerance}));
        REQUIRE(items.texts.size() == 2);
        CHECK(items.texts[1].text == written);
    }
}

TEST_CASE("Tolerance_ACellIsSizedByItsCharactersAndNotItsBytes",
          "[drawing][tolerance][p14]") {
    // The GD&T symbols are outside ASCII: U+2316 is three bytes of UTF-8. A
    // cell sized by its byte count would draw the one-character symbol cell
    // three characters wide, and a frame's compartments would be a different
    // size depending on which symbol they held.
    Fixture f = makeBlock();
    const drawing::SceneItems items =
        drawn(f, addFrame(f, "Frame",
                          {.characteristic = GeometricCharacteristic::Perpendicularity,
                           .zone = ToleranceZone::Width,
                           .tolerance = 0.1_mm,
                           .datums = {{'A'}}}));
    // Cells: the symbol, "0.1", and "A". Their boundaries are the box's left
    // and right edges plus the two dividers.
    REQUIRE(items.lines.size() >= 3);
    std::vector<double> boundaries{items.lines.front().points.front().x.si()};
    for (std::size_t i = 1; i <= 2; ++i) {
        boundaries.push_back(items.lines[i].points.front().x.si());
    }
    boundaries.push_back(items.lines.front().points[1].x.si());
    std::ranges::sort(boundaries);
    REQUIRE(boundaries.size() == 4);

    const double symbolCell = boundaries[1] - boundaries[0];
    const double datumCell = boundaries[3] - boundaries[2];
    // One character each, so the same width -- which is only true if the
    // three-byte symbol was counted as one character.
    CHECK_THAT(symbolCell, WithinAbs(datumCell, 1e-12));
}

TEST_CASE("Tolerance_AWidthZoneIsWrittenWithoutTheDiameterSign",
          "[drawing][tolerance][p14]") {
    // The diameter sign is what tells a reader the zone is a cylinder. A
    // width zone must not carry it, or the drawing says something else.
    Fixture f = makeBlock();
    const drawing::SceneItems items =
        drawn(f, addFrame(f, "Flat",
                          {.characteristic = GeometricCharacteristic::Flatness,
                           .zone = ToleranceZone::Width,
                           .tolerance = 0.05_mm}));
    REQUIRE(items.texts.size() == 2); // a form characteristic cites no datum
    CHECK(items.texts[0].text == "⏥");
    CHECK(items.texts[1].text == "0.05");
    CHECK_THAT(items.texts[1].text, !ContainsSubstring("Ø"));
}

TEST_CASE("Tolerance_AFramesPaperSizeDoesNotFollowTheViewScale",
          "[drawing][tolerance][p14]") {
    // P14-ANNO-001's rule, restated for the frame: what it points at moves
    // with the scale, the symbol does not. ISO 1101 draws the frame twice the
    // lettering high, so a 3.5 mm frame is 7 mm high at every scale.
    double firstHeight = 0.0;
    double firstWidth = 0.0;
    Point2D firstTarget{};
    for (const DrawingScale scale :
         {DrawingScale{1, 1}, DrawingScale{1, 2}, DrawingScale{2, 1}, DrawingScale{1, 5}}) {
        INFO("scale " << scale.label());
        Fixture f = makeBlock(scale);
        const drawing::SceneItems items =
            drawn(f, addFrame(f, "Frame",
                              {.characteristic = GeometricCharacteristic::Position,
                               .zone = ToleranceZone::Cylindrical,
                               .tolerance = 0.2_mm,
                               .datums = {{'A'}}}));
        for (const drawing::SceneText& text : items.texts) {
            CHECK_THAT(text.height.in(units::mm), WithinAbs(3.5, kMm));
        }
        double minX = std::numeric_limits<double>::max();
        double maxX = std::numeric_limits<double>::lowest();
        double minY = minX;
        double maxY = maxX;
        for (const Point2D& p : items.lines.front().points) {
            minX = std::min(minX, p.x.in(units::mm));
            maxX = std::max(maxX, p.x.in(units::mm));
            minY = std::min(minY, p.y.in(units::mm));
            maxY = std::max(maxY, p.y.in(units::mm));
        }
        CHECK_THAT(maxY - minY, WithinAbs(7.0, 1e-6)); // twice the lettering
        // The arrowhead's tip is where the frame points, and THAT moves.
        const Point2D target = items.lines.back().points.front();
        if (firstHeight == 0.0) {
            firstHeight = maxY - minY;
            firstWidth = maxX - minX;
            firstTarget = target;
        } else {
            CHECK_THAT(maxY - minY, WithinAbs(firstHeight, kMm));
            CHECK_THAT(maxX - minX, WithinAbs(firstWidth, kMm));
            CHECK(target != firstTarget); // not everything is simply frozen
        }
    }
}

TEST_CASE("Tolerance_AFrameAnnotationIsCheckedWhenItIsMade", "[drawing][tolerance][p14]") {
    Fixture f = makeBlock();
    const auto missing = drawing::createAnnotation(
        f.document, "Bad",
        AnnotationDefinition{.view = f.view,
                             .type = AnnotationType::FeatureControlFrame,
                             .target = AnnotationTarget{.plane = sideFace(f.part, f.lines[3])},
                             .placement = Point2D{40_mm, 40_mm}});
    REQUIRE_FALSE(missing.has_value());
    CHECK_THAT(missing.error().message, ContainsSubstring("must say what it controls"));

    const auto stray = drawing::createAnnotation(
        f.document, "Bad",
        AnnotationDefinition{
            .view = f.view,
            .type = AnnotationType::Note,
            .text = "X",
            .placement = Point2D{40_mm, 40_mm},
            .frame = FeatureControlFrame{.characteristic = GeometricCharacteristic::Flatness,
                                         .tolerance = 0.1_mm}});
    REQUIRE_FALSE(stray.has_value());
    CHECK_THAT(stray.error().message, ContainsSubstring("carries no feature-control frame"));

    // A frame whose contents are incoherent is refused by the same rule
    // whether it is built in memory or read from a file.
    const auto incoherent = drawing::createAnnotation(
        f.document, "Bad",
        AnnotationDefinition{
            .view = f.view,
            .type = AnnotationType::FeatureControlFrame,
            .target = AnnotationTarget{.plane = sideFace(f.part, f.lines[3])},
            .placement = Point2D{40_mm, 40_mm},
            .frame = FeatureControlFrame{.characteristic = GeometricCharacteristic::Flatness,
                                         .tolerance = 0.1_mm,
                                         .datums = {{'A'}}}});
    REQUIRE_FALSE(incoherent.has_value());
    CHECK_THAT(incoherent.error().message, ContainsSubstring("cites no datum"));
}

// --- A frame's datums against the datums that exist -----------------------------------------

TEST_CASE("Tolerance_AFrameSaysWhichDatumsNobodyHasDefined", "[drawing][tolerance][p14]") {
    // A datum reference is a LETTER, and a letter binds to nothing by itself.
    // So a frame can cite a datum that no datum feature symbol defines, and
    // the drawing would look complete. This is what says otherwise.
    Fixture f = makeBlock();
    const AnnotationId frame = addFrame(f, "Frame",
                                        {.characteristic = GeometricCharacteristic::Position,
                                         .zone = ToleranceZone::Cylindrical,
                                         .tolerance = 0.2_mm,
                                         .datums = {{'A'}, {'B'}, {'C'}}});

    // Nothing defined yet: all three, in the order the frame cites them.
    auto missing = drawing::undefinedDatums(f.document, frame);
    REQUIRE(missing.has_value());
    CHECK(*missing == std::vector<char>{'A', 'B', 'C'});

    // Defining them out of order does not change the order they are reported
    // in, because that order is the frame's.
    const AnnotationId c = addDatum(f, "DatumC", "C");
    (void)addDatum(f, "DatumA", "A");
    missing = drawing::undefinedDatums(f.document, frame);
    REQUIRE(missing.has_value());
    CHECK(*missing == std::vector<char>{'B'});

    (void)addDatum(f, "DatumB", "B");
    missing = drawing::undefinedDatums(f.document, frame);
    REQUIRE(missing.has_value());
    CHECK(missing->empty());

    // And it is a live answer, not one cached when the frame was made:
    // deleting a datum symbol brings its letter back.
    REQUIRE(drawing::removeAnnotation(f.document, c).has_value());
    missing = drawing::undefinedDatums(f.document, frame);
    REQUIRE(missing.has_value());
    CHECK(*missing == std::vector<char>{'C'});

    // The frame still draws: an unfinished drawing is not a wrong one, and
    // the report is what says it is unfinished.
    CHECK(drawing::draw(f.document, frame, f.bodies()).has_value());
}

TEST_CASE("Tolerance_OnlyAFrameHasDatumsToBeUndefined", "[drawing][tolerance][p14]") {
    Fixture f = makeBlock();
    const AnnotationId note = require(drawing::createAnnotation(
        f.document, "Note",
        AnnotationDefinition{.view = f.view,
                             .type = AnnotationType::Note,
                             .text = "DEBURR ALL EDGES",
                             .placement = Point2D{30_mm, 20_mm}}));
    const auto missing = drawing::undefinedDatums(f.document, note);
    REQUIRE(missing.has_value());
    CHECK(missing->empty());
    CHECK(errorCode(drawing::undefinedDatums(f.document, AnnotationId::fromValue(9999))) ==
          ErrorCode::NotFound);
}

// --- References and regeneration -------------------------------------------------------------

TEST_CASE("Tolerance_ADimensionFollowsItsModelAndKeepsItsTolerance",
          "[drawing][tolerance][p14]") {
    // The nominal is the model's and the tolerance is the engineer's. Change
    // the part and the interval moves with it; the deviations do not.
    Fixture f = makeBlock();
    const DimensionId id = require(drawing::createDimension(
        f.document, "Height",
        DimensionDefinition{
            .view = f.view,
            .type = DimensionType::Linear,
            .from = onPlane(PlaneReference{.object = f.part,
                                           .face = FaceSelector{.role = FaceRole::StartCap}}),
            .to = onPlane(PlaneReference{.object = f.part,
                                         .face = FaceSelector{.role = FaceRole::EndCap}}),
            .format = DimensionFormat{.decimals = 2},
            .tolerance = symmetric(0.05_mm)}));
    CHECK(measured(f, id).text == "40.00 ±0.05");

    const auto* extrude = f.document.findObjectAs<features::ExtrudeFeature>(f.part);
    REQUIRE(extrude != nullptr);
    auto definition = extrude->definition();
    definition.depth = 22_mm;
    REQUIRE(f.document
                .modifyObject<features::ExtrudeFeature>(
                    f.part, [&](features::ExtrudeFeature& e) { return e.setDefinition(definition); })
                .has_value());
    f.regenerate();

    const drawing::MeasuredDimension after = measured(f, id);
    CHECK(after.text == "22.00 ±0.05");
    CHECK_THAT(after.interval->lower.in(units::mm), WithinAbs(21.95, kMm));
    CHECK_THAT(after.interval->upper.in(units::mm), WithinAbs(22.05, kMm));
    // The tolerance itself did not move.
    CHECK(drawing::findDimension(f.document, id)->definition().tolerance == symmetric(0.05_mm));
}

TEST_CASE("Tolerance_AFrameWhoseFeatureIsGoneIsUnresolved", "[drawing][tolerance][p14]") {
    // The frame resolves its target through the same path every annotation
    // does, and when the target goes it fails by name instead of drawing
    // where the feature used to be.
    Fixture f = makeBlock();
    const AnnotationId id = require(drawing::createAnnotation(
        f.document, "Frame",
        AnnotationDefinition{
            .view = f.view,
            .type = AnnotationType::FeatureControlFrame,
            .target = AnnotationTarget{.object = f.hole},
            .placement = Point2D{40_mm, 40_mm},
            .frame = FeatureControlFrame{.characteristic = GeometricCharacteristic::Position,
                                         .zone = ToleranceZone::Cylindrical,
                                         .tolerance = 0.2_mm,
                                         .datums = {{'A'}}}}));
    REQUIRE(drawing::draw(f.document, id, f.bodies()).has_value());

    REQUIRE(f.document.removeObject(f.hole).has_value());
    f.regenerate();
    const auto redrawn = drawing::draw(f.document, id, f.bodies());
    REQUIRE_FALSE(redrawn.has_value());
    CHECK_THAT(redrawn.error().message, ContainsSubstring("Frame"));
}

TEST_CASE("Tolerance_AnAngularDimensionTakesNoLengthTolerance",
          "[drawing][tolerance][p14]") {
    Fixture f = makeBlock();
    const auto bad = drawing::createDimension(
        f.document, "Bad",
        DimensionDefinition{.view = f.view,
                            .type = DimensionType::Angular,
                            .from = onPlane(sideFace(f.part, f.lines[0])),
                            .to = onPlane(sideFace(f.part, f.lines[1])),
                            .format = DimensionFormat{.unit = "deg"},
                            .tolerance = symmetric(0.05_mm)});
    REQUIRE_FALSE(bad.has_value());
    CHECK_THAT(bad.error().message, ContainsSubstring("a length on an angle"));
}

// --- Persistence -------------------------------------------------------------------------

TEST_CASE("Tolerance_SemanticsRoundTripAndStillReadTheSame",
          "[drawing][tolerance][p14][persistence]") {
    Fixture f = makeBlock();
    const DimensionId pair =
        width(f, "Pair", DimensionTolerance{.lower = -0.02_mm, .upper = 0.10_mm});
    const DimensionId limits = width(f, "Limits", symmetric(0.05_mm, ToleranceDisplay::Limits));
    const DimensionId fit = require(drawing::createDimension(
        f.document, "Fitted",
        DimensionDefinition{
            .view = f.view,
            .type = DimensionType::Linear,
            .from = onPlane(sideFace(f.part, f.lines[3])),
            .to = onPlane(sideFace(f.part, f.lines[1])),
            .tolerance = DimensionTolerance{.fit = FitDesignation{FitRole::Shaft, 'G', 6}}}));
    const AnnotationId frame = addFrame(f, "Frame",
                                        {.characteristic = GeometricCharacteristic::Position,
                                         .zone = ToleranceZone::Cylindrical,
                                         .tolerance = 0.2_mm,
                                         .datums = {{'A'}, {'B'}, {'C'}}});

    const TempDir directory;
    const std::filesystem::path path = directory.path() / "tolerances.bcad";
    REQUIRE(io::saveDocument(f.document, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());

    for (const DimensionId id : {pair, limits, fit}) {
        INFO("dimension " << id.value());
        const drawing::Dimension* after = drawing::findDimension(*loaded, id);
        REQUIRE(after != nullptr);
        // deserialize(serialize(intent)) == intent, the whole definition.
        CHECK(after->definition() == drawing::findDimension(f.document, id)->definition());
    }

    const drawing::Annotation* after = drawing::findAnnotation(*loaded, frame);
    REQUIRE(after != nullptr);
    CHECK(after->definition() == drawing::findAnnotation(f.document, frame)->definition());
    // The datum ORDER survived, which a set would have lost.
    REQUIRE(after->definition().frame->datums.size() == 3);
    CHECK(after->definition().frame->datums[0].letter == 'A');
    CHECK(after->definition().frame->datums[1].letter == 'B');
    CHECK(after->definition().frame->datums[2].letter == 'C');

    // A|B|C did not come back as B|A|C, and the file itself says so.
    const std::string text = readFile(path);
    CHECK(text.find(R"("A")") < text.find(R"("B")"));
    CHECK(text.find(R"("B")") < text.find(R"("C")"));

    // And the loaded drawing reads the same.
    features::Regenerator reloaded;
    REQUIRE(reloaded.regenerateAll(*loaded).has_value());
    const features::Regenerator* r = &reloaded;
    const drawing::BodyLookup bodies = [r](ObjectId object) { return r->body(object); };
    CHECK(drawing::measure(*loaded, pair, bodies)->text ==
          drawing::measure(f.document, pair, f.bodies())->text);
    CHECK(drawing::measure(*loaded, limits, bodies)->lowerText ==
          drawing::measure(f.document, limits, f.bodies())->lowerText);
    CHECK(drawing::draw(*loaded, frame, bodies)->texts ==
          drawing::draw(f.document, frame, f.bodies())->texts);
}

TEST_CASE("Tolerance_NoMeasuredNumberIsWrittenToTheFile",
          "[drawing][tolerance][p14][persistence]") {
    // ADR-011: the file stores the intent. A toleranced dimension writes its
    // deviations, which ARE intent, and never the limits it worked out --
    // a stored limit is a number that could go stale against the model.
    Fixture f = makeBlock();
    (void)width(f, "Pair", symmetric(0.05_mm, ToleranceDisplay::Limits),
                DimensionFormat{.decimals = 2});
    const TempDir directory;
    const std::filesystem::path path = directory.path() / "limits.bcad";
    REQUIRE(io::saveDocument(f.document, path).has_value());

    const std::string text = readFile(path);
    CHECK_THAT(text, !ContainsSubstring("100.05"));
    CHECK_THAT(text, !ContainsSubstring("99.95"));
    CHECK_THAT(text, ContainsSubstring(R"("display": "limits")"));
}

TEST_CASE("Tolerance_MalformedFilesAreRefused", "[drawing][tolerance][p14][persistence]") {
    Fixture f = makeBlock();
    (void)width(f, "Pair", symmetric(0.05_mm));
    (void)require(drawing::createDimension(
        f.document, "Fitted",
        DimensionDefinition{
            .view = f.view,
            .type = DimensionType::Linear,
            .from = onPlane(sideFace(f.part, f.lines[3])),
            .to = onPlane(sideFace(f.part, f.lines[1])),
            .tolerance = DimensionTolerance{.fit = FitDesignation{FitRole::Hole, 'H', 7}}}));
    (void)addFrame(f, "Frame",
                   {.characteristic = GeometricCharacteristic::Position,
                    .zone = ToleranceZone::Cylindrical,
                    .tolerance = 0.2_mm,
                    .datums = {{'A'}}});

    const TempDir directory;
    const std::filesystem::path good = directory.path() / "good.bcad";
    REQUIRE(io::saveDocument(f.document, good).has_value());
    const std::string original = readFile(good);

    const auto rewrite = [&](const std::string& text, std::string_view expected) {
        const std::filesystem::path path = directory.path() / "bad.bcad";
        writeFile(path, text);
        const auto loaded = io::loadDocument(path);
        REQUIRE_FALSE(loaded.has_value());
        CHECK_THAT(loaded.error().message, ContainsSubstring(std::string{expected}));
    };
    const auto refuse = [&](const std::string& from, const std::string& to,
                            std::string_view expected) {
        INFO(from << " -> " << to);
        std::string text = original;
        const auto at = text.find(from);
        REQUIRE(at != std::string::npos);
        text.replace(at, from.size(), to);
        rewrite(text, expected);
    };

    refuse(R"("characteristic": "position")", R"("characteristic": "wobbliness")",
           "unknown geometric characteristic 'wobbliness'");
    refuse(R"("zone": "cylindrical")", R"("zone": "triangular")", "unknown tolerance zone");
    refuse(R"("display": "plus_minus")", R"("display": "sideways")", "unknown tolerance display");
    refuse(R"("role": "hole")", R"("role": "sprocket")", "unknown fit role");
    refuse(R"("letter": "H")", R"("letter": "HH")", "a fundamental deviation is one letter");
    refuse(R"("grade": 7)", R"("grade": 99)", "1 to 18");
    refuse(R"("A")", R"("AB")", "a datum is named by a single letter");
    refuse(R"("A")", R"("A", "B", "C", "D")", "at most three datums");

    // A frame whose datums are not a list at all: the whole array is replaced
    // by a number, so the reader meets a shape it cannot read rather than a
    // key it does not know.
    {
        std::string text = original;
        const auto at = text.find(R"("datums": [)");
        REQUIRE(at != std::string::npos);
        const auto close = text.find(']', at);
        REQUIRE(close != std::string::npos);
        text.replace(at, close - at + 1, R"("datums": 3)");
        rewrite(text, "lists the datums it cites");
    }

    // And the deviations themselves: a file whose lower deviation is above
    // its upper describes an interval that runs backwards, and is refused by
    // the same rule a caller in memory meets. How the number was spelled is
    // not assumed -- it is overwritten by position.
    {
        std::string text = original;
        const std::string key = R"("lower": )";
        const auto at = text.find(key);
        REQUIRE(at != std::string::npos);
        const auto start = at + key.size();
        const auto end = text.find_first_of(",\n", start);
        REQUIRE(end != std::string::npos);
        text.replace(start, end - start, "1.0"); // a metre above the nominal
        rewrite(text, "must not be above its upper");
    }
}

// --- What an exporter is handed ---------------------------------------------------------

TEST_CASE("Tolerance_TheExportedRepresentationIsCompleteOnItsOwn",
          "[drawing][tolerance][p14]") {
    // ADR-016: the scene is the export boundary and a writer computes
    // nothing. So everything a writer needs to put a tolerance on paper has
    // to be in what it is handed, and the two strings of a limits dimension
    // have to be the two ends of the interval -- not one of them with the
    // other left for the writer to work out.
    Fixture f = makeBlock();
    const DimensionFormat format{.decimals = 2};
    const drawing::MeasuredDimension limits =
        measured(f, width(f, "Limits", symmetric(0.05_mm, ToleranceDisplay::Limits), format));
    REQUIRE(limits.interval.has_value());
    CHECK(limits.text == *drawing::formatLength(limits.interval->upper, format));
    CHECK(limits.lowerText == *drawing::formatLength(limits.interval->lower, format));

    // A deviation pair carries the whole thing in one string, and says so by
    // leaving the other empty rather than repeating the nominal in it.
    const drawing::MeasuredDimension pair =
        measured(f, width(f, "Pair", symmetric(0.05_mm), format));
    CHECK(pair.lowerText.empty());
    CHECK_THAT(pair.text, ContainsSubstring("±0.05"));
    CHECK(pair.interval.has_value()); // and the numbers are there for a writer that wants them

    // An untoleranced dimension carries neither, so a writer can tell the
    // three cases apart without guessing.
    const drawing::MeasuredDimension plain = measured(f, width(f, "Plain", std::nullopt, format));
    CHECK(plain.lowerText.empty());
    CHECK_FALSE(plain.interval.has_value());
    CHECK(plain.text == "100.00");
}

TEST_CASE("Tolerance_AFrameReachesTheWholeSheetsScene", "[drawing][tolerance][p14]") {
    // A frame that drew on its own but was dropped from the view's scene
    // would be a drawing that looked right in a test and printed without its
    // tolerances.
    Fixture f = makeBlock();
    const AnnotationId frame = addFrame(f, "Frame",
                                        {.characteristic = GeometricCharacteristic::Position,
                                         .zone = ToleranceZone::Cylindrical,
                                         .tolerance = 0.2_mm,
                                         .datums = {{'A'}}});
    (void)addDatum(f, "DatumA", "A");

    auto whole = drawing::drawAnnotations(f.document, f.view, f.bodies());
    REQUIRE(whole.has_value());
    const drawing::SceneItems own = drawn(f, frame);
    for (const drawing::SceneText& text : own.texts) {
        INFO(text.text);
        CHECK(std::ranges::find(whole->texts, text) != whole->texts.end());
    }
    for (const drawing::SceneLine& line : own.lines) {
        CHECK(std::ranges::find(whole->lines, line) != whole->lines.end());
    }
    CHECK(validate(*whole).has_value());

    // And a frame that cannot be drawn fails the sheet rather than quietly
    // leaving a tolerance off it.
    REQUIRE(f.document.removeObject(f.part).has_value());
    CHECK_FALSE(drawing::drawAnnotations(f.document, f.view, f.bodies()).has_value());
}

// --- Determinism --------------------------------------------------------------------------

TEST_CASE("Tolerance_ReadsAndDrawsIdenticallyEveryTime",
          "[drawing][tolerance][p14][determinism]") {
    Fixture f = makeBlock();
    const DimensionId id = width(f, "W", symmetric(0.05_mm), DimensionFormat{.decimals = 3});
    const AnnotationId frame = addFrame(f, "Frame",
                                        {.characteristic = GeometricCharacteristic::Position,
                                         .zone = ToleranceZone::Cylindrical,
                                         .tolerance = 0.2_mm,
                                         .datums = {{'A'}, {'B'}}});
    (void)addDatum(f, "DatumA", "A");

    const drawing::MeasuredDimension first = measured(f, id);
    const drawing::SceneItems firstItems = drawn(f, frame);
    const auto firstMissing = drawing::undefinedDatums(f.document, frame);
    REQUIRE(firstMissing.has_value());

    for (int i = 0; i < 6; ++i) {
        INFO(i);
        const drawing::MeasuredDimension again = measured(f, id);
        CHECK(again.text == first.text);
        CHECK(again.interval->lower.si() == first.interval->lower.si());
        CHECK(again.interval->upper.si() == first.interval->upper.si());
        const drawing::SceneItems items = drawn(f, frame);
        CHECK(items.lines == firstItems.lines);
        CHECK(items.texts == firstItems.texts);
        const auto missing = drawing::undefinedDatums(f.document, frame);
        REQUIRE(missing.has_value());
        CHECK(*missing == *firstMissing);
    }
}
