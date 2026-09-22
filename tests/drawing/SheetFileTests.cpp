#include "TestHelpers.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/drawing/Sheet.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/io/DocumentFile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using drawing::DrawingScale;
using drawing::SheetDefinition;
using drawing::SheetFormat;
using drawing::SheetMargins;
using drawing::SheetOrientation;

// P14-SHEET-001 persistence.
//
// ADR-010 claims sheets need no change to the .bcad format: they go through
// the existing objects[] envelope as a new `type`, with no new top-level key
// and no version bump. ADR-011 claims no derived geometry reaches the file.
// These tests check both against the actual bytes rather than taking the
// ADRs' word for it.
namespace {

template <typename Id>
Id require(const Result<Id>& id) {
    REQUIRE(id.has_value());
    return *id;
}

SheetDefinition goodDefinition() {
    return SheetDefinition{.format = SheetFormat::A3,
                           .orientation = SheetOrientation::Landscape,
                           .margins = SheetMargins{10_mm, 10_mm, 10_mm, 10_mm},
                           .scale = DrawingScale{1, 1}};
}

/// Saves @p document, loads it back, and returns the loaded copy.
Document roundTrip(const Document& document, const std::filesystem::path& path) {
    REQUIRE(io::saveDocument(document, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    return std::move(*loaded);
}

} // namespace

// --- The round trip ----------------------------------------------------------------

TEST_CASE("SheetFile_RoundTripPreservesEveryFieldAndTheId", "[drawing][sheet][p14][io]") {
    TempDir dir;
    Document document{"Drawing"};

    SheetDefinition first = goodDefinition();
    first.format = SheetFormat::A1;
    first.orientation = SheetOrientation::Portrait;
    first.margins = SheetMargins{25_mm, 10_mm, 10_mm, 10_mm};
    first.scale = DrawingScale{1, 5};
    first.titleBlock.title = "Gearbox cover";
    first.titleBlock.drawingNumber = "BC-2001";
    first.titleBlock.revision = "C";
    first.titleBlock.designer = "A. Engineer";
    first.titleBlock.checkedBy = "B. Checker";
    first.titleBlock.approvedBy = "C. Approver";
    first.titleBlock.date = "2026-09-22";
    first.titleBlock.organization = "BetterCAD";

    SheetDefinition second = goodDefinition();
    second.format = SheetFormat::Custom;
    second.customWidth = 500_mm;
    second.customHeight = 350_mm;
    second.scale = DrawingScale{2, 1};

    const SheetId a = require(drawing::createSheet(document, "Overall", first));
    const SheetId b = require(drawing::createSheet(document, "Detail", second));

    const Document loaded = roundTrip(document, dir.path() / "drawing.bcad");

    CHECK(drawing::sheets(loaded) == std::vector{a, b});
    const drawing::Sheet* loadedA = drawing::findSheet(loaded, a);
    const drawing::Sheet* loadedB = drawing::findSheet(loaded, b);
    REQUIRE(loadedA != nullptr);
    REQUIRE(loadedB != nullptr);
    CHECK(loadedA->name() == "Overall");
    CHECK(loadedB->name() == "Detail");
    // Every field, by value: a field the writer forgot would show here.
    CHECK(loadedA->definition() == first);
    CHECK(loadedB->definition() == second);
    CHECK(equivalent(document, loaded));
}

TEST_CASE("SheetFile_DerivedGeometryIsRecomputedAndMatches", "[drawing][sheet][p14][io]") {
    TempDir dir;
    Document document{"Drawing"};
    SheetDefinition definition = goodDefinition();
    definition.format = SheetFormat::A2;
    definition.orientation = SheetOrientation::Portrait;
    definition.margins = SheetMargins{20_mm, 15_mm, 10_mm, 30_mm};
    const SheetId id = require(drawing::createSheet(document, "Sheet1", definition));

    const auto before = drawing::findSheet(document, id)->usableRegion();
    const Document loaded = roundTrip(document, dir.path() / "drawing.bcad");
    const auto after = drawing::findSheet(loaded, id)->usableRegion();

    // Bit for bit: the geometry was not stored, it was derived again from the
    // same intent by the same arithmetic (ADR-011).
    CHECK(before == after);
    CHECK(drawing::findSheet(document, id)->size() == drawing::findSheet(loaded, id)->size());
}

TEST_CASE("SheetFile_SavingTwiceGivesTheSameBytes", "[drawing][sheet][p14][io][determinism]") {
    TempDir dir;
    Document document{"Drawing"};
    SheetDefinition definition = goodDefinition();
    definition.titleBlock.title = "Repeatable";
    const SheetId id = require(drawing::createSheet(document, "Sheet1", definition));
    (void)id;

    const auto first = dir.path() / "first.bcad";
    const auto second = dir.path() / "second.bcad";
    REQUIRE(io::saveDocument(document, first).has_value());
    REQUIRE(io::saveDocument(document, second).has_value());
    CHECK(readFile(first) == readFile(second));

    // And a load/save cycle reproduces the file exactly.
    auto loaded = io::loadDocument(first);
    REQUIRE(loaded.has_value());
    const auto again = dir.path() / "again.bcad";
    REQUIRE(io::saveDocument(*loaded, again).has_value());
    CHECK(readFile(again) == readFile(first));
}

// --- What the bytes say ------------------------------------------------------------

TEST_CASE("SheetFile_UsesTheExistingObjectEnvelopeAndNoNewKey", "[drawing][sheet][p14][io]") {
    TempDir dir;
    Document document{"Drawing"};
    const SheetId id = require(drawing::createSheet(document, "Sheet1", goodDefinition()));
    (void)id;

    const auto path = dir.path() / "drawing.bcad";
    REQUIRE(io::saveDocument(document, path).has_value());
    const std::string text = readFile(path);

    // The existing envelope, and the objects[] array it has always used.
    CHECK_THAT(text, ContainsSubstring("\"type\": \"sheet\""));
    CHECK_THAT(text, ContainsSubstring("\"name\": \"Sheet1\""));
    CHECK_THAT(text, ContainsSubstring("\"objects\""));
    // No top-level section of its own: ADR-010's whole argument for putting
    // drawings in the document is that this stays true.
    CHECK_THAT(text, !ContainsSubstring("\"sheets\""));
    CHECK_THAT(text, !ContainsSubstring("\"drawings\""));
    // And no version bump.
    CHECK_THAT(text, ContainsSubstring("\"version\": 1"));
}

TEST_CASE("SheetFile_HoldsIntentAndNeverDerivedGeometry", "[drawing][sheet][p14][io]") {
    TempDir dir;
    Document document{"Drawing"};
    SheetDefinition definition = goodDefinition();
    definition.format = SheetFormat::A3; // a STANDARD format
    const SheetId id = require(drawing::createSheet(document, "Sheet1", definition));
    (void)id;

    const auto path = dir.path() / "drawing.bcad";
    REQUIRE(io::saveDocument(document, path).has_value());
    const std::string text = readFile(path);

    // The intent is there.
    CHECK_THAT(text, ContainsSubstring("\"format\": \"A3\""));
    CHECK_THAT(text, ContainsSubstring("\"orientation\""));
    CHECK_THAT(text, ContainsSubstring("\"margins\""));
    CHECK_THAT(text, ContainsSubstring("\"scale\""));

    // The derived geometry is not. A standard sheet's size comes from ISO 216
    // via its format; writing it would let the file disagree with the
    // standard, which is the derived-state-as-truth failure ADR-011 forbids.
    CHECK_THAT(text, !ContainsSubstring("\"width\""));
    CHECK_THAT(text, !ContainsSubstring("\"height\""));
    CHECK_THAT(text, !ContainsSubstring("usable"));
    CHECK_THAT(text, !ContainsSubstring("bounds"));
    // Nor the sheet's number or count, which are positions in the document.
    CHECK_THAT(text, !ContainsSubstring("sheet_number"));
    CHECK_THAT(text, !ContainsSubstring("sheet_count"));
}

TEST_CASE("SheetFile_ACustomSheetWritesItsSizeBecauseThatIsIntent", "[drawing][sheet][p14][io]") {
    TempDir dir;
    Document document{"Drawing"};
    SheetDefinition definition = goodDefinition();
    definition.format = SheetFormat::Custom;
    definition.customWidth = 500_mm;
    definition.customHeight = 350_mm;
    const SheetId id = require(drawing::createSheet(document, "Sheet1", definition));

    const auto path = dir.path() / "drawing.bcad";
    REQUIRE(io::saveDocument(document, path).has_value());
    const std::string text = readFile(path);
    CHECK_THAT(text, ContainsSubstring("\"format\": \"custom\""));
    CHECK_THAT(text, ContainsSubstring("\"width\""));
    CHECK_THAT(text, ContainsSubstring("\"height\""));

    const Document loaded = roundTrip(document, dir.path() / "again.bcad");
    CHECK(drawing::findSheet(loaded, id)->definition().customWidth == 500_mm);
    CHECK(drawing::findSheet(loaded, id)->definition().customHeight == 350_mm);
}

TEST_CASE("SheetFile_TheScaleIsWrittenAsItsPairNotItsQuotient", "[drawing][sheet][p14][io]") {
    TempDir dir;
    Document document{"Drawing"};
    SheetDefinition definition = goodDefinition();
    definition.scale = DrawingScale{1, 3}; // no exact double
    const SheetId id = require(drawing::createSheet(document, "Sheet1", definition));

    const Document loaded = roundTrip(document, dir.path() / "drawing.bcad");
    const DrawingScale back = drawing::findSheet(loaded, id)->definition().scale;
    CHECK(back.paper == 1);
    CHECK(back.model == 3);
    CHECK(back.label() == "1:3");

    // 2:4 and 1:2 have the same factor and different intent, and the file
    // keeps them apart (ADR-013).
    Document other{"Other"};
    SheetDefinition twoFour = goodDefinition();
    twoFour.scale = DrawingScale{2, 4};
    const SheetId id2 = require(drawing::createSheet(other, "Sheet1", twoFour));
    const Document loadedOther = roundTrip(other, dir.path() / "other.bcad");
    CHECK(drawing::findSheet(loadedOther, id2)->definition().scale.label() == "2:4");
}

TEST_CASE("SheetFile_AnEmptyTitleBlockIsNotWritten", "[drawing][sheet][p14][io]") {
    TempDir dir;
    Document document{"Drawing"};
    const SheetId id = require(drawing::createSheet(document, "Sheet1", goodDefinition()));

    const auto path = dir.path() / "drawing.bcad";
    REQUIRE(io::saveDocument(document, path).has_value());
    CHECK_THAT(readFile(path), !ContainsSubstring("title_block"));

    // And a partially filled one writes only what it says.
    SheetDefinition definition = goodDefinition();
    definition.titleBlock.title = "Only a title";
    REQUIRE(drawing::setSheetDefinition(document, id, definition).has_value());
    REQUIRE(io::saveDocument(document, path).has_value());
    const std::string text = readFile(path);
    CHECK_THAT(text, ContainsSubstring("\"title\": \"Only a title\""));
    CHECK_THAT(text, !ContainsSubstring("\"organization\""));
    CHECK_THAT(text, !ContainsSubstring("\"checked_by\""));

    const Document loaded = roundTrip(document, dir.path() / "again.bcad");
    CHECK(drawing::findSheet(loaded, id)->definition().titleBlock.title == "Only a title");
    CHECK(drawing::findSheet(loaded, id)->definition().titleBlock.organization.empty());
}

// --- Existing files ----------------------------------------------------------------

TEST_CASE("SheetFile_ADocumentWithoutSheetsIsUnchanged", "[drawing][sheet][p14][io]") {
    // ADR-010's claim, checked directly: a document with no sheet writes what
    // it always wrote.
    TempDir dir;
    Document document{"Parts"};
    const ParameterId p = require(document.createParameter("width", 10.0 * units::mm, units::mm));
    (void)p;

    const auto path = dir.path() / "parts.bcad";
    REQUIRE(io::saveDocument(document, path).has_value());
    const std::string text = readFile(path);
    CHECK_THAT(text, !ContainsSubstring("sheet"));
    CHECK_THAT(text, ContainsSubstring("\"version\": 1"));
}

TEST_CASE("SheetFile_EveryCommittedModelStillLoadsAndHasNoSheet", "[drawing][sheet][p14][io]") {
    // Every model in the repository predates P14. All must still load, and
    // none may have acquired a sheet.
    const std::filesystem::path models{BETTERCAD_EXAMPLE_MODELS_DIR};
    REQUIRE(std::filesystem::exists(models));
    std::size_t checked = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator{models}) {
        if (!entry.is_regular_file() || entry.path().extension() != ".bcad") {
            continue;
        }
        INFO("model " << entry.path().filename().string());
        auto loaded = io::loadDocument(entry.path());
        REQUIRE(loaded.has_value());
        CHECK(drawing::sheets(*loaded).empty());
        ++checked;
    }
    CHECK(checked >= 20);
}

// --- Malformed files ---------------------------------------------------------------

TEST_CASE("SheetFile_MalformedSheetsAreRefusedAndNothingPartiallyLoads",
          "[drawing][sheet][p14][io]") {
    TempDir dir;
    const auto path = dir.path() / "broken.bcad";

    const std::string prefix =
        R"({"format":"bettercad-document","version":1,"units":"SI",)"
        R"("document":{"id":"a55e0000-0000-4000-8000-00000000f14e","name":"Drawing",)"
        R"("metadata":{"description":"","author":"","properties":{}},"last_allocated_id":1},)"
        R"("parameters":[],"objects":[{"id":1,"type":"sheet","name":"Sheet1","data":)";
    const std::string suffix = R"(}]})";

    struct Case {
        std::string_view what;
        std::string data;
        std::string_view expect;
    };
    const std::vector<Case> cases{
        {"an unknown format",
         R"({"format":"A5","orientation":"portrait","margins":{"left":0,"right":0,"top":0,"bottom":0},"scale":[1,1]})",
         "unknown sheet format"},
        {"an unknown orientation",
         R"({"format":"A3","orientation":"sideways","margins":{"left":0,"right":0,"top":0,"bottom":0},"scale":[1,1]})",
         "unknown sheet orientation"},
        {"a zero scale term",
         R"({"format":"A3","orientation":"portrait","margins":{"left":0,"right":0,"top":0,"bottom":0},"scale":[1,0]})",
         "zero term"},
        {"margins that leave no room",
         R"({"format":"A4","orientation":"portrait","margins":{"left":0.5,"right":0.5,"top":0,"bottom":0},"scale":[1,1]})",
         "usable width"},
        {"a standard sheet carrying a size",
         R"({"format":"A3","orientation":"portrait","width":0.5,"height":0.4,"margins":{"left":0,"right":0,"top":0,"bottom":0},"scale":[1,1]})",
         "takes its size from the format"},
        {"a missing scale",
         R"({"format":"A3","orientation":"portrait","margins":{"left":0,"right":0,"top":0,"bottom":0}})",
         "scale"},
        {"a missing margins block",
         R"({"format":"A3","orientation":"portrait","scale":[1,1]})", "margins"},
        {"a scale that is not a pair",
         R"({"format":"A3","orientation":"portrait","margins":{"left":0,"right":0,"top":0,"bottom":0},"scale":[1,2,3]})",
         "two whole numbers"},
        {"a negative scale term",
         R"({"format":"A3","orientation":"portrait","margins":{"left":0,"right":0,"top":0,"bottom":0},"scale":[-1,2]})",
         "two whole numbers"},
        {"an unknown key",
         R"({"format":"A3","orientation":"portrait","margins":{"left":0,"right":0,"top":0,"bottom":0},"scale":[1,1],"colour":"red"})",
         "colour"},
        {"an unknown title-block key",
         R"({"format":"A3","orientation":"portrait","margins":{"left":0,"right":0,"top":0,"bottom":0},"scale":[1,1],"title_block":{"author":"x"}})",
         "author"},
    };

    for (const Case& c : cases) {
        INFO(c.what);
        writeFile(path, prefix + c.data + suffix);
        auto loaded = io::loadDocument(path);
        REQUIRE_FALSE(loaded.has_value());
        CHECK_THAT(loaded.error().message, ContainsSubstring(std::string{c.expect}));
    }

    // The control: the same envelope with a good sheet loads.
    writeFile(path, prefix +
                        R"({"format":"A3","orientation":"portrait","margins":{"left":0.01,"right":0.01,"top":0.01,"bottom":0.01},"scale":[1,2]})" +
                        suffix);
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    CHECK(drawing::sheets(*loaded).size() == 1);
    CHECK(drawing::findSheet(*loaded, SheetId::fromValue(1))->definition().scale.label() == "1:2");
}
