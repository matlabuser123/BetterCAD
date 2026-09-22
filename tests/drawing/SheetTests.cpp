#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/drawing/Sheet.hpp>
#include <bettercad/drawing/Sheets.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using drawing::DrawingScale;
using drawing::SheetDefinition;
using drawing::SheetFormat;
using drawing::SheetMargins;
using drawing::SheetOrientation;

// P14-SHEET-001: the sheet model.
//
// WHERE THE EXPECTED NUMBERS COME FROM. The sheet sizes below are ISO 216's
// own rounded millimetre values, taken from the standard and not from
// BetterCAD. The A series is defined by an A0 of one square metre and a
// height:width ratio of sqrt(2), with each size the previous one halved
// across its long edge and rounded down to the millimetre:
//
//     A0  841 x 1189      A0 area = 0.841 * 1.189 = 0.999949 m^2
//     A1  594 x 841       841 / 2 = 420.5 -> 420 is A2's width
//     A2  420 x 594
//     A3  297 x 420
//     A4  210 x 297
//
// Every derived value below -- usable region, border, aspect ratio -- is
// arithmetic on those numbers done here, never a value read back out of a
// Sheet and asserted against itself.
namespace {

/// Tolerance for a derived length, in millimetres.
///
/// Lengths are stored in SI, so a sheet's usable height is
/// (0.297 - 0.015) - 0.005 in doubles, which is 0.27699999999999997 and not
/// 0.277: two subtractions of same-magnitude quantities, one ULP out. The
/// observed error is 5.7e-14 mm (5.7e-17 m).
///
/// CLAUDE.md's figure for well-conditioned double-precision algebra is 1e-12
/// in SI, which is 1e-9 mm and four orders of magnitude wider than anything
/// seen here. Exact decimal equality is not a property binary floating point
/// has, and asserting it would be asserting something false; what IS exact,
/// and is checked separately, is that the same definition gives bit-identical
/// geometry every time.
constexpr double kMm = 1e-9;

template <typename Id>
Id require(const Result<Id>& id) {
    REQUIRE(id.has_value());
    return *id;
}

/// A definition that validates, so a test can change one field at a time.
SheetDefinition goodDefinition() {
    return SheetDefinition{.format = SheetFormat::A3,
                           .orientation = SheetOrientation::Landscape,
                           .margins = SheetMargins{10_mm, 10_mm, 10_mm, 10_mm},
                           .scale = DrawingScale{1, 1}};
}

SheetId addSheet(Document& document, std::string name, const SheetDefinition& definition) {
    return require(drawing::createSheet(document, std::move(name), definition));
}

} // namespace

// --- Standard formats --------------------------------------------------------------

TEST_CASE("Sheet_StandardFormatsHaveTheirIsoSizes", "[drawing][sheet][p14]") {
    // From ISO 216, in portrait. Independent of anything BetterCAD computes.
    struct Expected {
        SheetFormat format;
        double widthMm;
        double heightMm;
    };
    constexpr std::array kIso{
        Expected{SheetFormat::A0, 841.0, 1189.0}, Expected{SheetFormat::A1, 594.0, 841.0},
        Expected{SheetFormat::A2, 420.0, 594.0},  Expected{SheetFormat::A3, 297.0, 420.0},
        Expected{SheetFormat::A4, 210.0, 297.0},
    };

    for (const Expected& want : kIso) {
        INFO("format " << drawing::toString(want.format));
        SheetDefinition definition = goodDefinition();
        definition.format = want.format;
        definition.orientation = SheetOrientation::Portrait;
        auto sheet = drawing::Sheet::create("Sheet1", definition);
        REQUIRE(sheet.has_value());

        const auto [width, height] = (*sheet)->size();
        CHECK(width.in(units::mm) == want.widthMm);
        CHECK(height.in(units::mm) == want.heightMm);
        // Portrait is the short edge horizontal, which is what makes the
        // orientation meaningful rather than a label.
        CHECK(width.si() < height.si());
    }
}

TEST_CASE("Sheet_EachAFormatIsTheNextOneHalvedAcrossItsLongEdge", "[drawing][sheet][p14]") {
    // The property that defines the series, checked against the rounded
    // values rather than assumed: halving A(n)'s height gives A(n+1)'s width,
    // to within the standard's millimetre rounding.
    constexpr std::array kSeries{SheetFormat::A0, SheetFormat::A1, SheetFormat::A2, SheetFormat::A3,
                                 SheetFormat::A4};
    for (std::size_t i = 0; i + 1 < kSeries.size(); ++i) {
        INFO("from " << drawing::toString(kSeries[i]) << " to " << drawing::toString(kSeries[i + 1]));
        const auto larger = drawing::standardSize(kSeries[i]);
        const auto smaller = drawing::standardSize(kSeries[i + 1]);
        REQUIRE(larger.has_value());
        REQUIRE(smaller.has_value());
        // The next size's width is this one's height halved, rounded down.
        CHECK_THAT(smaller->first.in(units::mm),
                   WithinAbs(larger->second.in(units::mm) / 2.0, 1.0));
        // And its height is this one's width exactly.
        CHECK(smaller->second.in(units::mm) == larger->first.in(units::mm));
    }
}

TEST_CASE("Sheet_CustomFormatHasNoStandardSize", "[drawing][sheet][p14]") {
    CHECK_FALSE(drawing::standardSize(SheetFormat::Custom).has_value());
}

TEST_CASE("Sheet_FormatAndOrientationNamesRoundTrip", "[drawing][sheet][p14]") {
    for (const SheetFormat format : {SheetFormat::A0, SheetFormat::A1, SheetFormat::A2, SheetFormat::A3,
                                     SheetFormat::A4, SheetFormat::Custom}) {
        INFO("format " << drawing::toString(format));
        const auto back = drawing::sheetFormatFromString(drawing::toString(format));
        REQUIRE(back.has_value());
        CHECK(*back == format);
    }
    for (const SheetOrientation orientation : {SheetOrientation::Portrait, SheetOrientation::Landscape}) {
        const auto back = drawing::sheetOrientationFromString(drawing::toString(orientation));
        REQUIRE(back.has_value());
        CHECK(*back == orientation);
    }
    CHECK_FALSE(drawing::sheetFormatFromString("A5").has_value());
    CHECK_FALSE(drawing::sheetFormatFromString("").has_value());
    CHECK_FALSE(drawing::sheetOrientationFromString("sideways").has_value());
}

// --- Orientation -------------------------------------------------------------------

TEST_CASE("Sheet_LandscapeSwapsWidthAndHeight", "[drawing][sheet][p14]") {
    SheetDefinition definition = goodDefinition();
    definition.format = SheetFormat::A3;

    definition.orientation = SheetOrientation::Portrait;
    auto portrait = drawing::Sheet::create("P", definition);
    REQUIRE(portrait.has_value());
    definition.orientation = SheetOrientation::Landscape;
    auto landscape = drawing::Sheet::create("L", definition);
    REQUIRE(landscape.has_value());

    const auto [pw, ph] = (*portrait)->size();
    const auto [lw, lh] = (*landscape)->size();
    // A3 is 297 x 420 portrait, so landscape is 420 x 297.
    CHECK(pw.in(units::mm) == 297.0);
    CHECK(ph.in(units::mm) == 420.0);
    CHECK(lw.in(units::mm) == 420.0);
    CHECK(lh.in(units::mm) == 297.0);
    CHECK(lw == ph);
    CHECK(lh == pw);
}

TEST_CASE("Sheet_OrientationRoundTripRestoresTheSizeExactly", "[drawing][sheet][p14]") {
    // The failure this prevents is a swap that stores width and height and
    // drifts: portrait -> landscape -> portrait must be bit-identical, which
    // it is because what is stored is the FORMAT and the orientation, and the
    // size is derived from them every time.
    Document document{"Drawing"};
    SheetDefinition definition = goodDefinition();
    definition.orientation = SheetOrientation::Portrait;
    const SheetId id = addSheet(document, "Sheet1", definition);
    const auto before = drawing::findSheet(document, id)->size();

    definition.orientation = SheetOrientation::Landscape;
    REQUIRE(drawing::setSheetDefinition(document, id, definition).has_value());
    const auto swapped = drawing::findSheet(document, id)->size();
    CHECK(swapped.first == before.second);
    CHECK(swapped.second == before.first);

    definition.orientation = SheetOrientation::Portrait;
    REQUIRE(drawing::setSheetDefinition(document, id, definition).has_value());
    const auto after = drawing::findSheet(document, id)->size();
    CHECK(after.first.si() == before.first.si());
    CHECK(after.second.si() == before.second.si());
}

TEST_CASE("Sheet_CustomFormatUsesItsOwnSizeAndStillHonoursOrientation", "[drawing][sheet][p14]") {
    SheetDefinition definition = goodDefinition();
    definition.format = SheetFormat::Custom;
    definition.customWidth = 500_mm;
    definition.customHeight = 300_mm;

    definition.orientation = SheetOrientation::Portrait;
    auto portrait = drawing::Sheet::create("P", definition);
    REQUIRE(portrait.has_value());
    const auto [pw, ph] = (*portrait)->size();
    CHECK(pw.in(units::mm) == 500.0);
    CHECK(ph.in(units::mm) == 300.0);

    definition.orientation = SheetOrientation::Landscape;
    auto landscape = drawing::Sheet::create("L", definition);
    REQUIRE(landscape.has_value());
    const auto [lw, lh] = (*landscape)->size();
    CHECK(lw.in(units::mm) == 300.0);
    CHECK(lh.in(units::mm) == 500.0);
}

// --- Scale -------------------------------------------------------------------------

TEST_CASE("Sheet_ScaleHalvesAndDoublesTheWayItsLabelReads", "[drawing][sheet][p14]") {
    // ADR-013: the factor is paper / model, so 1:2 is a reduction.
    CHECK(DrawingScale{1, 1}.factor() == 1.0);
    CHECK(DrawingScale{1, 2}.factor() == 0.5);
    CHECK(DrawingScale{2, 1}.factor() == 2.0);
    CHECK(DrawingScale{1, 5}.factor() == 0.2);

    // A 100 mm feature at 1:2 measures 50 mm with a ruler on the sheet.
    CHECK(100.0 * DrawingScale{1, 2}.factor() == 50.0);
    CHECK(10.0 * DrawingScale{2, 1}.factor() == 20.0);

    CHECK(DrawingScale{1, 1}.label() == "1:1");
    CHECK(DrawingScale{1, 2}.label() == "1:2");
    CHECK(DrawingScale{2, 1}.label() == "2:1");
    CHECK(DrawingScale{7, 2}.label() == "7:2");
}

TEST_CASE("Sheet_ScaleKeepsARatioADoubleCouldNotHold", "[drawing][sheet][p14]") {
    // The reason ADR-013 stores a pair rather than a double: 1:3 has no exact
    // double, so a stored quotient could not be printed back as "1:3", and
    // 2:4 and 1:2 would become indistinguishable.
    const DrawingScale third{1, 3};
    CHECK(third.label() == "1:3");
    const DrawingScale twoFour{2, 4};
    CHECK(twoFour.label() == "2:4");
    CHECK(twoFour.factor() == DrawingScale{1, 2}.factor());
    // Same factor, different intent, and the pair keeps them apart.
    CHECK_FALSE(twoFour == DrawingScale{1, 2});
}

TEST_CASE("Sheet_ScaleRefusesAZeroTerm", "[drawing][sheet][p14]") {
    CHECK(errorCode(drawing::validate(DrawingScale{0, 1})) == ErrorCode::InvalidArgument);
    CHECK(errorCode(drawing::validate(DrawingScale{1, 0})) == ErrorCode::InvalidArgument);
    CHECK(errorCode(drawing::validate(DrawingScale{0, 0})) == ErrorCode::InvalidArgument);
    CHECK(drawing::validate(DrawingScale{1, 1}).has_value());
    // A very large ratio is legitimate: a detail view of a thread form.
    CHECK(drawing::validate(DrawingScale{1000, 1}).has_value());
}

// --- Margins and the usable region -------------------------------------------------

TEST_CASE("Sheet_UsableRegionIsTheSheetLessItsMargins", "[drawing][sheet][p14]") {
    SheetDefinition definition = goodDefinition();
    definition.format = SheetFormat::A3;
    definition.orientation = SheetOrientation::Landscape; // 420 x 297
    definition.margins = SheetMargins{20_mm, 10_mm, 15_mm, 5_mm};
    auto sheet = drawing::Sheet::create("Sheet1", definition);
    REQUIRE(sheet.has_value());

    const BoundingBox2D bounds = (*sheet)->bounds();
    CHECK(bounds.min.x.si() == 0.0);
    CHECK(bounds.min.y.si() == 0.0);
    CHECK_THAT(bounds.width().in(units::mm), WithinAbs(420.0, kMm));
    CHECK_THAT(bounds.height().in(units::mm), WithinAbs(297.0, kMm));

    // Derived by hand: x from left (20) to width - right (420 - 10 = 410);
    // y from bottom (5) to height - top (297 - 15 = 282).
    const BoundingBox2D usable = (*sheet)->usableRegion();
    CHECK_THAT(usable.min.x.in(units::mm), WithinAbs(20.0, kMm));
    CHECK_THAT(usable.max.x.in(units::mm), WithinAbs(410.0, kMm));
    CHECK_THAT(usable.min.y.in(units::mm), WithinAbs(5.0, kMm));
    CHECK_THAT(usable.max.y.in(units::mm), WithinAbs(282.0, kMm));
    CHECK_THAT(usable.width().in(units::mm), WithinAbs(390.0, kMm));
    CHECK_THAT(usable.height().in(units::mm), WithinAbs(277.0, kMm));
}

TEST_CASE("Sheet_UsableRegionFollowsAnOrientationChange", "[drawing][sheet][p14]") {
    Document document{"Drawing"};
    SheetDefinition definition = goodDefinition();
    definition.format = SheetFormat::A4; // 210 x 297 portrait
    definition.orientation = SheetOrientation::Portrait;
    definition.margins = SheetMargins{10_mm, 10_mm, 10_mm, 10_mm};
    const SheetId id = addSheet(document, "Sheet1", definition);

    const BoundingBox2D portrait = drawing::findSheet(document, id)->usableRegion();
    CHECK_THAT(portrait.width().in(units::mm), WithinAbs(190.0, kMm));  // 210 - 20
    CHECK_THAT(portrait.height().in(units::mm), WithinAbs(277.0, kMm)); // 297 - 20

    definition.orientation = SheetOrientation::Landscape;
    REQUIRE(drawing::setSheetDefinition(document, id, definition).has_value());
    const BoundingBox2D landscape = drawing::findSheet(document, id)->usableRegion();
    CHECK_THAT(landscape.width().in(units::mm), WithinAbs(277.0, kMm));
    CHECK_THAT(landscape.height().in(units::mm), WithinAbs(190.0, kMm));
}

TEST_CASE("Sheet_MarginsThatLeaveNoRoomAreRefused", "[drawing][sheet][p14]") {
    SheetDefinition definition = goodDefinition();
    definition.format = SheetFormat::A4;
    definition.orientation = SheetOrientation::Portrait; // 210 x 297

    SECTION("wider than the sheet") {
        definition.margins = SheetMargins{150_mm, 150_mm, 10_mm, 10_mm};
        const auto result = drawing::validate(definition);
        CHECK(errorCode(result) == ErrorCode::InvalidArgument);
        REQUIRE_FALSE(result.has_value());
        CHECK_THAT(result.error().message, ContainsSubstring("usable width"));
    }
    SECTION("taller than the sheet") {
        definition.margins = SheetMargins{10_mm, 10_mm, 200_mm, 200_mm};
        const auto result = drawing::validate(definition);
        CHECK(errorCode(result) == ErrorCode::InvalidArgument);
        REQUIRE_FALSE(result.has_value());
        CHECK_THAT(result.error().message, ContainsSubstring("usable height"));
    }
    SECTION("exactly the sheet leaves nothing") {
        definition.margins = SheetMargins{105_mm, 105_mm, 10_mm, 10_mm};
        CHECK(errorCode(drawing::validate(definition)) == ErrorCode::InvalidArgument);
    }
    SECTION("a margin that only just fits is allowed") {
        definition.margins = SheetMargins{104_mm, 105_mm, 10_mm, 10_mm};
        CHECK(drawing::validate(definition).has_value());
    }
}

TEST_CASE("Sheet_NegativeAndNonFiniteMarginsAreRefused", "[drawing][sheet][p14]") {
    CHECK(errorCode(drawing::validate(SheetMargins{-1_mm, 0_mm, 0_mm, 0_mm})) == ErrorCode::InvalidArgument);
    CHECK(errorCode(drawing::validate(SheetMargins{0_mm, -1_mm, 0_mm, 0_mm})) == ErrorCode::InvalidArgument);
    CHECK(errorCode(drawing::validate(SheetMargins{0_mm, 0_mm, -1_mm, 0_mm})) == ErrorCode::InvalidArgument);
    CHECK(errorCode(drawing::validate(SheetMargins{0_mm, 0_mm, 0_mm, -1_mm})) == ErrorCode::InvalidArgument);
    CHECK(drawing::validate(SheetMargins{}).has_value());

    const Length infinite = Length::fromSi(std::numeric_limits<double>::infinity());
    CHECK(errorCode(drawing::validate(SheetMargins{infinite, 0_mm, 0_mm, 0_mm})) ==
          ErrorCode::InvalidArgument);
    const Length notANumber = Length::fromSi(std::numeric_limits<double>::quiet_NaN());
    CHECK(errorCode(drawing::validate(SheetMargins{0_mm, notANumber, 0_mm, 0_mm})) ==
          ErrorCode::InvalidArgument);
}

TEST_CASE("Sheet_CustomFormatNeedsAPositiveFiniteSize", "[drawing][sheet][p14]") {
    SheetDefinition definition = goodDefinition();
    definition.format = SheetFormat::Custom;

    definition.customWidth = 0_mm;
    definition.customHeight = 100_mm;
    CHECK(errorCode(drawing::validate(definition)) == ErrorCode::InvalidArgument);

    definition.customWidth = -100_mm;
    CHECK(errorCode(drawing::validate(definition)) == ErrorCode::InvalidArgument);

    definition.customWidth = 100_mm;
    definition.customHeight = Length::fromSi(std::numeric_limits<double>::infinity());
    CHECK(errorCode(drawing::validate(definition)) == ErrorCode::InvalidArgument);

    definition.customHeight = 100_mm;
    CHECK(drawing::validate(definition).has_value());
}

// --- Identity and the document -----------------------------------------------------

TEST_CASE("Sheet_IsADocumentObjectWithItsOwnIdentity", "[drawing][sheet][p14]") {
    Document document{"Drawing"};
    const SheetId id = addSheet(document, "Sheet1", goodDefinition());

    const drawing::Sheet* sheet = drawing::findSheet(document, id);
    REQUIRE(sheet != nullptr);
    CHECK(sheet->typeName() == "sheet");
    CHECK(sheet->name() == "Sheet1");
    CHECK(sheet->sheetId() == id);
    // The ID widens to ObjectId, which is what puts a sheet in the document's
    // one ID space and in the dependency graph (ADR-017).
    CHECK(ObjectId{id} == sheet->id());
    CHECK(document.findObject(ObjectId{id}) == sheet);
    // A sheet depends on nothing: a view names its sheet, not the reverse.
    CHECK(sheet->dependencies().empty());
}

TEST_CASE("Sheet_SheetIdIsNotAffectedByDeletingAnotherSheet", "[drawing][sheet][p14]") {
    // The failure this prevents is identity that is really a position. Delete
    // the middle sheet and the others must keep the IDs they had.
    Document document{"Drawing"};
    const SheetId a = addSheet(document, "SheetA", goodDefinition());
    const SheetId b = addSheet(document, "SheetB", goodDefinition());
    const SheetId c = addSheet(document, "SheetC", goodDefinition());
    CHECK(drawing::sheets(document) == std::vector{a, b, c});

    REQUIRE(drawing::removeSheet(document, b).has_value());

    CHECK(drawing::sheets(document) == std::vector{a, c});
    CHECK(drawing::findSheet(document, a) != nullptr);
    CHECK(drawing::findSheet(document, c) != nullptr);
    CHECK(drawing::findSheet(document, b) == nullptr);
    // c is still c. Its POSITION moved from third to second; its identity did
    // not move at all.
    CHECK(drawing::findSheet(document, c)->sheetId() == c);

    // A new sheet gets a new ID, never the one that was freed.
    const SheetId d = addSheet(document, "SheetD", goodDefinition());
    CHECK(d != b);
    CHECK(d.value() > c.value());
}

TEST_CASE("Sheet_NumberIsAPositionAndTheIdIsNot", "[drawing][sheet][p14]") {
    Document document{"Drawing"};
    const SheetId a = addSheet(document, "SheetA", goodDefinition());
    const SheetId b = addSheet(document, "SheetB", goodDefinition());
    const SheetId c = addSheet(document, "SheetC", goodDefinition());

    CHECK(drawing::sheetNumber(document, a) == 1);
    CHECK(drawing::sheetNumber(document, b) == 2);
    CHECK(drawing::sheetNumber(document, c) == 3);
    CHECK(drawing::sheetCount(document) == 3);

    REQUIRE(drawing::removeSheet(document, a).has_value());

    // Every number shifted; no ID did. "Sheet 2 of 3" became "sheet 1 of 2",
    // which is why neither is stored (ADR-017).
    CHECK(drawing::sheetNumber(document, b) == 1);
    CHECK(drawing::sheetNumber(document, c) == 2);
    CHECK(drawing::sheetCount(document) == 2);
    CHECK(drawing::sheetNumber(document, a) == 0);
    CHECK(drawing::findSheet(document, b)->sheetId() == b);
    CHECK(drawing::findSheet(document, c)->sheetId() == c);
}

TEST_CASE("Sheet_ManySheetsEachKeepTheirOwnSettings", "[drawing][sheet][p14]") {
    Document document{"Drawing"};
    SheetDefinition first = goodDefinition();
    first.format = SheetFormat::A0;
    first.orientation = SheetOrientation::Portrait;
    first.scale = DrawingScale{1, 10};
    SheetDefinition second = goodDefinition();
    second.format = SheetFormat::A4;
    second.orientation = SheetOrientation::Landscape;
    second.scale = DrawingScale{2, 1};

    const SheetId a = addSheet(document, "Overall", first);
    const SheetId b = addSheet(document, "Detail", second);

    CHECK(drawing::findSheet(document, a)->definition().format == SheetFormat::A0);
    CHECK(drawing::findSheet(document, a)->definition().scale.label() == "1:10");
    CHECK(drawing::findSheet(document, a)->size().first.in(units::mm) == 841.0);
    CHECK(drawing::findSheet(document, b)->definition().format == SheetFormat::A4);
    CHECK(drawing::findSheet(document, b)->definition().scale.label() == "2:1");
    CHECK(drawing::findSheet(document, b)->size().first.in(units::mm) == 297.0);
}

// --- Failure atomicity -------------------------------------------------------------

TEST_CASE("Sheet_ARejectedSheetConsumesNoIdAndChangesNothing", "[drawing][sheet][p14]") {
    Document document{"Drawing"};
    const SheetId first = addSheet(document, "Sheet1", goodDefinition());
    const std::size_t before = document.objectCount();

    SheetDefinition broken = goodDefinition();
    broken.scale = DrawingScale{1, 0};
    CHECK(errorCode(drawing::createSheet(document, "Sheet2", broken)) == ErrorCode::InvalidArgument);
    CHECK(document.objectCount() == before);
    CHECK(drawing::sheets(document) == std::vector{first});

    // The next good sheet gets the ID the rejected one would have had, which
    // is how we know the failed call consumed none.
    const SheetId second = addSheet(document, "Sheet2", goodDefinition());
    CHECK(second.value() == first.value() + 1);
}

TEST_CASE("Sheet_AFailedEditLeavesTheSheetExactlyAsItWas", "[drawing][sheet][p14]") {
    Document document{"Drawing"};
    const SheetId id = addSheet(document, "Sheet1", goodDefinition());
    const SheetDefinition before = drawing::findSheet(document, id)->definition();
    const std::uint64_t revision = drawing::findSheet(document, id)->revision();

    SheetDefinition broken = goodDefinition();
    broken.format = SheetFormat::A4;
    broken.orientation = SheetOrientation::Portrait;
    broken.margins = SheetMargins{500_mm, 500_mm, 0_mm, 0_mm};
    CHECK(errorCode(drawing::setSheetDefinition(document, id, broken)) == ErrorCode::InvalidArgument);

    CHECK(drawing::findSheet(document, id)->definition() == before);
    // Not even the revision moved: nothing about the sheet changed.
    CHECK(drawing::findSheet(document, id)->revision() == revision);
}

TEST_CASE("Sheet_AnEditThatChangesNothingDoesNotBumpTheRevision", "[drawing][sheet][p14]") {
    Document document{"Drawing"};
    const SheetId id = addSheet(document, "Sheet1", goodDefinition());
    const std::uint64_t revision = drawing::findSheet(document, id)->revision();

    const auto changed = drawing::setSheetDefinition(document, id, goodDefinition());
    REQUIRE(changed.has_value());
    CHECK_FALSE(*changed);
    CHECK(drawing::findSheet(document, id)->revision() == revision);

    SheetDefinition moved = goodDefinition();
    moved.format = SheetFormat::A2;
    const auto second = drawing::setSheetDefinition(document, id, moved);
    REQUIRE(second.has_value());
    CHECK(*second);
    CHECK(drawing::findSheet(document, id)->revision() > revision);
}

TEST_CASE("Sheet_OperationsOnAMissingSheetFailWithNotFound", "[drawing][sheet][p14]") {
    Document document{"Drawing"};
    const auto ghost = SheetId::fromValue(42);
    CHECK(drawing::findSheet(document, ghost) == nullptr);
    CHECK(errorCode(drawing::setSheetDefinition(document, ghost, goodDefinition())) == ErrorCode::NotFound);
    CHECK(errorCode(drawing::removeSheet(document, ghost)) == ErrorCode::NotFound);
    CHECK(drawing::sheetNumber(document, ghost) == 0);
}

TEST_CASE("Sheet_ADuplicateNameIsRefused", "[drawing][sheet][p14]") {
    Document document{"Drawing"};
    const SheetId first = addSheet(document, "Sheet1", goodDefinition());
    CHECK_FALSE(drawing::createSheet(document, "Sheet1", goodDefinition()).has_value());
    CHECK(drawing::sheets(document) == std::vector{first});
}

TEST_CASE("Sheet_FindRefusesAnIdThatBelongsToAnotherKind", "[drawing][sheet][p14]") {
    // A SheetId built from a parameter's number must not find the parameter.
    Document document{"Drawing"};
    const ParameterId parameter =
        require(document.createParameter("width", 10.0 * units::mm, units::mm));
    CHECK(drawing::findSheet(document, SheetId::fromValue(parameter.value())) == nullptr);
}

// --- Determinism -------------------------------------------------------------------

TEST_CASE("Sheet_TheSameDefinitionAlwaysGivesTheSameGeometry", "[drawing][sheet][p14][determinism]") {
    // Bit for bit, and independent of how the sheet was reached: derived from
    // the definition every time, with nothing cached (ADR-011).
    for (const SheetFormat format : {SheetFormat::A0, SheetFormat::A1, SheetFormat::A2, SheetFormat::A3,
                                     SheetFormat::A4}) {
        for (const SheetOrientation orientation : {SheetOrientation::Portrait, SheetOrientation::Landscape}) {
            INFO("format " << drawing::toString(format) << " " << drawing::toString(orientation));
            SheetDefinition definition = goodDefinition();
            definition.format = format;
            definition.orientation = orientation;
            definition.margins = SheetMargins{7_mm, 11_mm, 13_mm, 17_mm};

            auto first = drawing::Sheet::create("A", definition);
            auto second = drawing::Sheet::create("B", definition);
            REQUIRE(first.has_value());
            REQUIRE(second.has_value());

            CHECK((*first)->size().first.si() == (*second)->size().first.si());
            CHECK((*first)->size().second.si() == (*second)->size().second.si());
            CHECK((*first)->bounds() == (*second)->bounds());
            CHECK((*first)->usableRegion() == (*second)->usableRegion());
            // Asking twice gives the same answer too.
            CHECK((*first)->usableRegion() == (*first)->usableRegion());
        }
    }
}

// --- Title block -------------------------------------------------------------------

TEST_CASE("Sheet_TitleBlockHoldsWhatItSaysAndNothingDerived", "[drawing][sheet][p14]") {
    drawing::TitleBlock block;
    CHECK(block.empty());
    block.title = "Bearing housing";
    block.drawingNumber = "BC-1042";
    block.revision = "B";
    block.designer = "A. Engineer";
    block.date = "2026-09-22";
    CHECK_FALSE(block.empty());

    Document document{"Drawing"};
    SheetDefinition definition = goodDefinition();
    definition.titleBlock = block;
    definition.scale = DrawingScale{1, 2};
    const SheetId id = addSheet(document, "Sheet1", definition);

    const drawing::Sheet* sheet = drawing::findSheet(document, id);
    CHECK(sheet->definition().titleBlock.title == "Bearing housing");
    CHECK(sheet->definition().titleBlock.revision == "B");
    // The three things a title block shows that are NOT stored, because the
    // document already knows them and a stored copy could disagree.
    CHECK(sheet->definition().scale.label() == "1:2");
    CHECK(drawing::sheetNumber(document, id) == 1);
    CHECK(drawing::sheetCount(document) == 1);
}

TEST_CASE("Sheet_CloneAndContentEqualityCoverEveryField", "[drawing][sheet][p14]") {
    // A field added to SheetDefinition without being compared would make two
    // different sheets look equal, and undo would restore the wrong one.
    SheetDefinition definition = goodDefinition();
    definition.titleBlock.title = "Original";
    auto original = drawing::Sheet::create("Sheet1", definition);
    REQUIRE(original.has_value());

    const auto copy = (*original)->clone();
    REQUIRE(copy != nullptr);
    CHECK(copy->typeName() == "sheet");
    CHECK(copy->name() == "Sheet1");
    CHECK((*original)->contentEquals(*copy));

    const std::array<SheetDefinition, 6> different{[&] {
                                                       SheetDefinition d = definition;
                                                       d.format = SheetFormat::A1;
                                                       return d;
                                                   }(),
                                                   [&] {
                                                       SheetDefinition d = definition;
                                                       d.orientation = SheetOrientation::Portrait;
                                                       return d;
                                                   }(),
                                                   [&] {
                                                       SheetDefinition d = definition;
                                                       d.margins.left = 99_mm;
                                                       return d;
                                                   }(),
                                                   [&] {
                                                       SheetDefinition d = definition;
                                                       d.scale = DrawingScale{1, 4};
                                                       return d;
                                                   }(),
                                                   [&] {
                                                       SheetDefinition d = definition;
                                                       d.titleBlock.title = "Changed";
                                                       return d;
                                                   }(),
                                                   [&] {
                                                       SheetDefinition d = definition;
                                                       d.titleBlock.organization = "Added";
                                                       return d;
                                                   }()};
    for (const SheetDefinition& other : different) {
        auto sheet = drawing::Sheet::create("Other", other);
        REQUIRE(sheet.has_value());
        CHECK_FALSE((*original)->contentEquals(**sheet));
    }
}

// --- Undo, and regeneration ---------------------------------------------------------

TEST_CASE("Sheet_UndoAndRedoWorkThroughTheGenericCommands", "[drawing][sheet][p14]") {
    // ADR-017 claims a sheet needs no command of its own in this milestone:
    // core's AddObjectCommand and DeleteObjectCommand work on any
    // DocumentObject, and redo restores the SAME id. Checked rather than
    // assumed, because that claim is what defers drawing commands to
    // P14-CMD-001.
    Document document{"Drawing"};
    CommandHistory history;

    auto sheet = drawing::Sheet::create("Sheet1", goodDefinition());
    REQUIRE(sheet.has_value());
    auto add = std::make_unique<AddObjectCommand>(std::move(*sheet));
    auto* addRaw = add.get();
    REQUIRE(history.execute(document, std::move(add)).has_value());
    const SheetId id = SheetId::fromValue(addRaw->objectId().value());
    REQUIRE(drawing::findSheet(document, id) != nullptr);

    REQUIRE(history.undo(document).has_value());
    CHECK(drawing::findSheet(document, id) == nullptr);
    CHECK(drawing::sheets(document).empty());

    REQUIRE(history.redo(document).has_value());
    const drawing::Sheet* back = drawing::findSheet(document, id);
    REQUIRE(back != nullptr);
    // The same identity, not a new sheet that looks like it.
    CHECK(back->sheetId() == id);
    CHECK(back->name() == "Sheet1");
    CHECK(back->definition() == goodDefinition());

    // And a delete undoes the same way.
    REQUIRE(history.execute(document, std::make_unique<DeleteObjectCommand>(ObjectId{id})).has_value());
    CHECK(drawing::findSheet(document, id) == nullptr);
    REQUIRE(history.undo(document).has_value());
    REQUIRE(drawing::findSheet(document, id) != nullptr);
    CHECK(drawing::findSheet(document, id)->definition() == goodDefinition());
}

TEST_CASE("Sheet_RegeneratesCleanlyAndProducesNoGeometry", "[drawing][sheet][p14]") {
    // A sheet references nothing and produces no body, so a document of
    // sheets regenerates with nothing to do and nothing failed. This is the
    // boundary ADR-014 draws: a drawing object gets a handler when it has
    // references to resolve, and a sheet has none.
    Document document{"Drawing"};
    const SheetId a = addSheet(document, "SheetA", goodDefinition());
    const SheetId b = addSheet(document, "SheetB", goodDefinition());

    features::Regenerator regenerator;
    auto report = regenerator.regenerateAll(document);
    REQUIRE(report.has_value());
    CHECK(report->succeeded());
    CHECK(report->failed.empty());
    CHECK(report->blocked.empty());
    CHECK(regenerator.body(ObjectId{a}) == nullptr);
    CHECK(regenerator.body(ObjectId{b}) == nullptr);
    // The sheets are still there and still say what they said.
    CHECK(drawing::sheets(document) == std::vector{a, b});
}
