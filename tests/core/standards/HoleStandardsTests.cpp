#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/standards/ClearanceHoles.hpp>
#include <bettercad/core/standards/HoleTolerances.hpp>
#include <bettercad/core/standards/MetricThreads.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::standards;
using bettercad::test::errorCode;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-HOLE-001: the standards data of holes, against published copies of the
// standards that are not the ones the tables were transcribed from (see
// docs/verification/P12-HOLE-001/). Values are compared in micrometres,
// which every value of these standards is a whole number of (tolerances of
// grades IT1 to IT4 go to a tenth).

namespace {

double micrometres(Length value) {
    return value.in(units::um);
}

/// Half of the last digit of the limits ISO 965-2 lists, which are rounded
/// to 0.001 mm.
constexpr double kRoundedToMicrometre = 0.5;

// --------------------------------------------------------------------------
// Reference data
// --------------------------------------------------------------------------

/// The standard tolerances IT1 to IT18 of the size ranges up to 500 mm, in
/// tenths of a micrometre, from the Wikipedia article "IT Grade" (ISO 286 -
/// Table 1). BetterCAD's own table is transcribed from the copy of the same
/// table in the published preview of ISO 286-2:2010.
struct ToleranceRow {
    int upToMm;
    std::array<int, 18> tenths;
};

constexpr std::array<ToleranceRow, 13> kStandardTolerances{{
    {3, {8, 12, 20, 30, 40, 60, 100, 140, 250, 400, 600, 1000, 1400, 2500, 4000, 6000, 10000, 14000}},
    {6, {10, 15, 25, 40, 50, 80, 120, 180, 300, 480, 750, 1200, 1800, 3000, 4800, 7500, 12000, 18000}},
    {10, {10, 15, 25, 40, 60, 90, 150, 220, 360, 580, 900, 1500, 2200, 3600, 5800, 9000, 15000, 22000}},
    {18, {12, 20, 30, 50, 80, 110, 180, 270, 430, 700, 1100, 1800, 2700, 4300, 7000, 11000, 18000, 27000}},
    {30, {15, 25, 40, 60, 90, 130, 210, 330, 520, 840, 1300, 2100, 3300, 5200, 8400, 13000, 21000, 33000}},
    {50, {15, 25, 40, 70, 110, 160, 250, 390, 620, 1000, 1600, 2500, 3900, 6200, 10000, 16000, 25000, 39000}},
    {80, {20, 30, 50, 80, 130, 190, 300, 460, 740, 1200, 1900, 3000, 4600, 7400, 12000, 19000, 30000, 46000}},
    {120, {25, 40, 60, 100, 150, 220, 350, 540, 870, 1400, 2200, 3500, 5400, 8700, 14000, 22000, 35000, 54000}},
    {180, {35, 50, 80, 120, 180, 250, 400, 630, 1000, 1600, 2500, 4000, 6300, 10000, 16000, 25000, 40000, 63000}},
    {250, {45, 70, 100, 140, 200, 290, 460, 720, 1150, 1850, 2900, 4600, 7200, 11500, 18500, 29000, 46000, 72000}},
    {315, {60, 80, 120, 160, 230, 320, 520, 810, 1300, 2100, 3200, 5200, 8100, 13000, 21000, 32000, 52000, 81000}},
    {400, {70, 90, 130, 180, 250, 360, 570, 890, 1400, 2300, 3600, 5700, 8900, 14000, 23000, 36000, 57000, 89000}},
    {500, {80, 100, 150, 200, 270, 400, 630, 970, 1550, 2500, 4000, 6300, 9700, 15500, 25000, 40000, 63000, 97000}},
}};

/// Limit deviations of holes in micrometres (upper, lower) by size range, as
/// MISUMI's excerpt of JIS B 0401 (1999), which is identical to ISO 286,
/// tabulates them: D8, D9, D10, E7, E8, E9, F6, F7, F8, G6, G7, H6, H7, H8,
/// H9 and H10.
struct DeviationRow {
    int upToMm;
    std::array<int, 16> upper;
    std::array<int, 5> lower; // D, E, F, G, H
};

constexpr std::array<std::string_view, 16> kDeviationClasses{"D8", "D9", "D10", "E7", "E8", "E9", "F6", "F7",
                                                             "F8", "G6", "G7", "H6", "H7", "H8", "H9", "H10"};

constexpr std::array<DeviationRow, 13> kLimitDeviations{{
    {3, {34, 45, 60, 24, 28, 39, 12, 16, 20, 8, 12, 6, 10, 14, 25, 40}, {20, 14, 6, 2, 0}},
    {6, {48, 60, 78, 32, 38, 50, 18, 22, 28, 12, 16, 8, 12, 18, 30, 48}, {30, 20, 10, 4, 0}},
    {10, {62, 76, 98, 40, 47, 61, 22, 28, 35, 14, 20, 9, 15, 22, 36, 58}, {40, 25, 13, 5, 0}},
    {18, {77, 93, 120, 50, 59, 75, 27, 34, 43, 17, 24, 11, 18, 27, 43, 70}, {50, 32, 16, 6, 0}},
    {30, {98, 117, 149, 61, 73, 92, 33, 41, 53, 20, 28, 13, 21, 33, 52, 84}, {65, 40, 20, 7, 0}},
    {50, {119, 142, 180, 75, 89, 112, 41, 50, 64, 25, 34, 16, 25, 39, 62, 100}, {80, 50, 25, 9, 0}},
    {80, {146, 174, 220, 90, 106, 134, 49, 60, 76, 29, 40, 19, 30, 46, 74, 120}, {100, 60, 30, 10, 0}},
    {120, {174, 207, 260, 107, 126, 159, 58, 71, 90, 34, 47, 22, 35, 54, 87, 140}, {120, 72, 36, 12, 0}},
    {180, {208, 245, 305, 125, 148, 185, 68, 83, 106, 39, 54, 25, 40, 63, 100, 160}, {145, 85, 43, 14, 0}},
    {250, {242, 285, 355, 146, 172, 215, 79, 96, 122, 44, 61, 29, 46, 72, 115, 185}, {170, 100, 50, 15, 0}},
    {315, {271, 320, 400, 162, 191, 240, 88, 108, 137, 49, 69, 32, 52, 81, 130, 210}, {190, 110, 56, 17, 0}},
    {400, {299, 350, 440, 182, 214, 265, 98, 119, 151, 54, 75, 36, 57, 89, 140, 230}, {210, 125, 62, 18, 0}},
    {500, {327, 385, 480, 198, 232, 290, 108, 131, 165, 60, 83, 40, 63, 97, 155, 250}, {230, 135, 68, 20, 0}},
}};

/// The limit deviations of D6 to D13 and E5 to E10 in micrometres, from
/// Table 3 of the published preview of ISO 286-2:2010 (the same table
/// BetterCAD's fundamental deviations of D and E come from, tabulated as
/// limits rather than as their parts).
struct DeviationRowDE {
    int upToMm;
    std::array<int, 8> upperD;
    int lowerD;
    std::array<int, 6> upperE;
    int lowerE;
};

constexpr std::array<DeviationRowDE, 13> kLimitDeviationsDE{{
    {3, {26, 30, 34, 45, 60, 80, 120, 160}, 20, {18, 20, 24, 28, 39, 54}, 14},
    {6, {38, 42, 48, 60, 78, 105, 150, 210}, 30, {25, 28, 32, 38, 50, 68}, 20},
    {10, {49, 55, 62, 76, 98, 130, 190, 260}, 40, {31, 34, 40, 47, 61, 83}, 25},
    {18, {61, 68, 77, 93, 120, 160, 230, 320}, 50, {40, 43, 50, 59, 75, 102}, 32},
    {30, {78, 86, 98, 117, 149, 195, 275, 395}, 65, {49, 53, 61, 73, 92, 124}, 40},
    {50, {96, 105, 119, 142, 180, 240, 330, 470}, 80, {61, 66, 75, 89, 112, 150}, 50},
    {80, {119, 130, 146, 174, 220, 290, 400, 560}, 100, {73, 79, 90, 106, 134, 180}, 60},
    {120, {142, 155, 174, 207, 260, 340, 470, 660}, 120, {87, 94, 107, 126, 159, 212}, 72},
    {180, {170, 185, 208, 245, 305, 395, 545, 775}, 145, {103, 110, 125, 148, 185, 245}, 85},
    {250, {199, 216, 242, 285, 355, 460, 630, 890}, 170, {120, 129, 146, 172, 215, 285}, 100},
    {315, {222, 242, 271, 320, 400, 510, 710, 1000}, 190, {133, 142, 162, 191, 240, 320}, 110},
    {400, {246, 267, 299, 350, 440, 570, 780, 1100}, 210, {150, 161, 182, 214, 265, 355}, 125},
    {500, {270, 293, 327, 385, 480, 630, 860, 1200}, 230, {162, 175, 198, 232, 290, 385}, 135},
}};

/// Clearance holes in tenths of a millimetre (fine, medium, coarse), from
/// two copies of ISO 273 that are not the published preview BetterCAD's
/// table was transcribed from: Engineering Hardware's chart (every size but
/// M1.8) and the German Wikipedia article "Durchgangsbohrung" (M1.8, and
/// every first-choice size).
struct ClearanceRow {
    std::string_view size;
    std::array<int, 3> holes;
};

constexpr std::array<ClearanceRow, 34> kClearanceHoles{{
    {"M1", {11, 12, 13}},        {"M1.2", {13, 14, 15}},    {"M1.4", {15, 16, 18}},    {"M1.6", {17, 18, 20}},
    {"M1.8", {20, 21, 22}},      {"M2", {22, 24, 26}},      {"M2.5", {27, 29, 31}},    {"M3", {32, 34, 36}},
    {"M3.5", {37, 39, 42}},      {"M4", {43, 45, 48}},      {"M5", {53, 55, 58}},      {"M6", {64, 66, 70}},
    {"M7", {74, 76, 80}},        {"M8", {84, 90, 100}},     {"M10", {105, 110, 120}},  {"M12", {130, 135, 145}},
    {"M14", {150, 155, 165}},    {"M16", {170, 175, 185}},  {"M18", {190, 200, 210}},  {"M20", {210, 220, 240}},
    {"M22", {230, 240, 260}},    {"M24", {250, 260, 280}},  {"M27", {280, 300, 320}},  {"M30", {310, 330, 350}},
    {"M33", {340, 360, 380}},    {"M36", {370, 390, 420}},  {"M39", {400, 420, 450}},  {"M42", {430, 450, 480}},
    {"M45", {460, 480, 520}},    {"M48", {500, 520, 560}},  {"M52", {540, 560, 620}},  {"M56", {580, 620, 660}},
    {"M60", {620, 660, 700}},    {"M64", {660, 700, 740}},
}};

/// The limits of size of the internal threads of ISO 965-2 in micrometres
/// (pitch diameter max and min, minor diameter max and min), for the class
/// ISO 965-2 gives each size: 5H up to M1.4, 6H above. BetterCAD computes
/// them from the basic profile and the tolerances of ISO 965-1.
struct ThreadRow {
    std::string_view size;
    int pitchMax;
    int pitchMin;
    int minorMax;
    int minorMin;
};

constexpr std::array<ThreadRow, 60> kThreadLimits{{
    {"M1", 894, 838, 785, 729},
    {"M1.2", 1094, 1038, 985, 929},
    {"M1.4", 1265, 1205, 1142, 1075},
    {"M1.6", 1458, 1373, 1321, 1221},
    {"M1.8", 1658, 1573, 1521, 1421},
    {"M2", 1830, 1740, 1679, 1567},
    {"M2.5", 2303, 2208, 2138, 2013},
    {"M3", 2775, 2675, 2599, 2459},
    {"M3.5", 3222, 3110, 3010, 2850},
    {"M4", 3663, 3545, 3422, 3242},
    {"M5", 4605, 4480, 4334, 4134},
    {"M6", 5500, 5350, 5153, 4917},
    {"M7", 6500, 6350, 6153, 5917},
    {"M8", 7348, 7188, 6912, 6647},
    {"M10", 9206, 9026, 8676, 8376},
    {"M12", 11063, 10863, 10441, 10106},
    {"M14", 12913, 12701, 12210, 11835},
    {"M16", 14913, 14701, 14210, 13835},
    {"M18", 16600, 16376, 15744, 15294},
    {"M20", 18600, 18376, 17744, 17294},
    {"M22", 20600, 20376, 19744, 19294},
    {"M24", 22316, 22051, 21252, 20752},
    {"M27", 25316, 25051, 24252, 23752},
    {"M30", 28007, 27727, 26771, 26211},
    {"M33", 31007, 30727, 29771, 29211},
    {"M36", 33702, 33402, 32270, 31670},
    {"M39", 36702, 36402, 35270, 34670},
    {"M42", 39392, 39077, 37799, 37129},
    {"M45", 42392, 42077, 40799, 40129},
    {"M48", 45087, 44752, 43297, 42587},
    {"M52", 49087, 48752, 47297, 46587},
    {"M56", 52783, 52428, 50796, 50046},
    {"M60", 56783, 56428, 54796, 54046},
    {"M64", 60478, 60103, 58305, 57505},
    {"M8x1", 7500, 7350, 7153, 6917},
    {"M10x1", 9500, 9350, 9153, 8917},
    {"M10x1.25", 9348, 9188, 8912, 8647},
    {"M12x1.25", 11368, 11188, 10912, 10647},
    {"M12x1.5", 11216, 11026, 10676, 10376},
    {"M14x1.5", 13216, 13026, 12676, 12376},
    {"M16x1.5", 15216, 15026, 14676, 14376},
    {"M18x1.5", 17216, 17026, 16676, 16376},
    {"M18x2", 16913, 16701, 16210, 15835},
    {"M20x1.5", 19216, 19026, 18676, 18376},
    {"M20x2", 18913, 18701, 18210, 17835},
    {"M22x1.5", 21216, 21026, 20676, 20376},
    {"M22x2", 20913, 20701, 20210, 19835},
    {"M24x2", 22925, 22701, 22210, 21835},
    {"M27x2", 25925, 25701, 25210, 24835},
    {"M30x2", 28925, 28701, 28210, 27835},
    {"M33x2", 31925, 31701, 31210, 30835},
    {"M36x3", 34316, 34051, 33252, 32752},
    {"M39x3", 37316, 37051, 36252, 35752},
    {"M42x3", 40316, 40051, 39252, 38752},
    {"M45x3", 43316, 43051, 42252, 41752},
    {"M48x3", 46331, 46051, 45252, 44752},
    {"M52x4", 49717, 49402, 48270, 47670},
    {"M56x4", 53717, 53402, 52270, 51670},
    {"M60x4", 57717, 57402, 56270, 55670},
    {"M64x4", 61717, 61402, 60270, 59670},
}};

/// The upper limit of the major diameter of the 6g external threads of
/// ISO 965-2 is the basic diameter less the fundamental deviation es, whose
/// magnitude ISO 965-1 gives position G of an internal thread (Table 1).
/// From Table 2 of the preview of ISO 965-2:1998, by pitch in micrometres.
const std::map<std::uint32_t, int> kExternalDeviations{
    {350, 19},  {400, 19},  {450, 20},  {500, 20},  {600, 21},  {700, 22},  {800, 24},
    {1000, 26}, {1250, 28}, {1500, 32}, {1750, 34}, {2000, 38}, {2500, 42}, {3000, 48},
    {3500, 53}, {4000, 60}, {4500, 63}, {5000, 71}, {5500, 75}, {6000, 80},
};

MetricThread thread(std::string_view designation) {
    auto parsed = parseMetricThread(designation);
    REQUIRE(parsed.has_value());
    return *parsed;
}

} // namespace

// --------------------------------------------------------------------------
// ISO 262 / ISO 965-2 sizes
// --------------------------------------------------------------------------

TEST_CASE("MetricThreads_KnowTheSizesOfIso965Part2", "[standards][thread][p12]") {
    const std::span<const MetricThread> threads = metricThreads();
    REQUIRE(threads.size() == kThreadLimits.size());

    SECTION("every size is in the reference table, once, in its order") {
        for (std::size_t i = 0; i < threads.size(); ++i) {
            CAPTURE(i, threads[i].designation());
            CHECK(threads[i].designation() == kThreadLimits[i].size);
        }
    }
    SECTION("the coarse series comes first, by diameter") {
        std::size_t coarse = 0;
        for (const MetricThread& size : threads) {
            coarse += size.coarse() ? std::size_t{1} : std::size_t{0};
        }
        CHECK(coarse == 34);
        for (std::size_t i = 1; i < coarse; ++i) {
            CHECK(threads[i].diameter() > threads[i - 1].diameter());
            CHECK(threads[i].coarse());
        }
        for (std::size_t i = coarse; i < threads.size(); ++i) {
            CHECK_FALSE(threads[i].coarse());
        }
    }
    SECTION("each diameter has one coarse pitch, and the fine ones are smaller") {
        for (const MetricThread& size : threads) {
            CAPTURE(size.designation());
            if (size.coarse()) {
                continue;
            }
            const auto coarse = std::ranges::find_if(threads, [&](const MetricThread& other) {
                return other.coarse() && other.diameter() == size.diameter();
            });
            REQUIRE(coarse != threads.end());
            CHECK(size.pitch() < coarse->pitch());
        }
    }
}

TEST_CASE("MetricThreads_ParseTheirDesignations", "[standards][thread][p12]") {
    SECTION("every known size round-trips through its designation") {
        for (const MetricThread& size : metricThreads()) {
            CAPTURE(size.designation());
            const auto parsed = parseMetricThread(size.designation());
            REQUIRE(parsed.has_value());
            CHECK(*parsed == size);
        }
    }
    SECTION("a coarse pitch may be written out or left out") {
        CHECK(thread("M8x1.25") == thread("M8"));
        CHECK(thread("M8").designation() == "M8");
        CHECK(thread("M1.6").designation() == "M1.6");
        // The multiplication sign of ISO 965-1 is accepted as well as "x".
        CHECK(thread("M8\xC3\x97" "1") == thread("M8x1"));
        CHECK(thread("M8x1").designation() == "M8x1");
        CHECK(thread("M10x1.25").designation() == "M10x1.25");
    }
    SECTION("malformed designations are refused, naming the text") {
        for (const std::string_view text : {"", "8", "m8", "M", "M8x", "Mx1", "M8x0", "M08", "M8.0", "M 8", "M8-6H",
                                            "M8x1,25", "M-8", "M8x1.2500"}) {
            CAPTURE(text);
            const auto parsed = parseMetricThread(text);
            REQUIRE_FALSE(parsed.has_value());
            CHECK(parsed.error().code == ErrorCode::InvalidArgument);
            CHECK_THAT(parsed.error().message,
                       ContainsSubstring(std::format("'{}' is not a metric thread size", text)));
        }
    }
    SECTION("sizes outside ISO 965-2 are refused") {
        // M9 is not a size; M6x0.75 and M8x0.75 are ISO 262 fine sizes that
        // ISO 965-2 gives no limits for; M8x1.5 is not a pitch of M8.
        for (const std::string_view text : {"M9", "M6x0.75", "M8x0.75", "M8x1.5", "M70", "M0.8", "M1x0.2"}) {
            CAPTURE(text);
            const auto parsed = parseMetricThread(text);
            REQUIRE_FALSE(parsed.has_value());
            CHECK_THAT(parsed.error().message, ContainsSubstring("BetterCAD knows the sizes of ISO 965-2"));
        }
    }
}

// --------------------------------------------------------------------------
// ISO 68-1 basic profile
// --------------------------------------------------------------------------

TEST_CASE("MetricThreads_BasicDiametersFollowIso68", "[standards][thread][p12]") {
    SECTION("the diameters are the profile's, computed independently") {
        for (const MetricThread& size : metricThreads()) {
            CAPTURE(size.designation());
            const ThreadDiameters basic = basicDiameters(size);
            const double d = size.diameter().in(units::mm);
            const double p = size.pitch().in(units::mm);
            // H = sqrt(3) / 2 P; the profile cuts 3/8 H and 5/8 H of it.
            const double h = std::sqrt(3.0) / 2.0 * p;
            CHECK(basic.major == size.diameter());
            CHECK_THAT(basic.pitch.in(units::mm), WithinRel(d - 0.75 * h, 1e-15));
            CHECK_THAT(basic.minor.in(units::mm), WithinRel(d - 1.25 * h, 1e-15));
            // The usual decimal forms of the same numbers.
            CHECK_THAT(basic.pitch.in(units::mm), WithinAbs(d - 0.649519052838329 * p, 1e-12));
            CHECK_THAT(basic.minor.in(units::mm), WithinAbs(d - 1.082531754730548 * p, 1e-12));
        }
    }
    SECTION("M8 and M10 have the dimensions the tables list") {
        CHECK_THAT(basicDiameters(thread("M8")).pitch.in(units::mm), WithinAbs(7.188, 5e-4));
        CHECK_THAT(basicDiameters(thread("M8")).minor.in(units::mm), WithinAbs(6.647, 5e-4));
        CHECK_THAT(basicDiameters(thread("M10")).pitch.in(units::mm), WithinAbs(9.026, 5e-4));
        CHECK_THAT(basicDiameters(thread("M10")).minor.in(units::mm), WithinAbs(8.376, 5e-4));
    }
}

// --------------------------------------------------------------------------
// ISO 965-1 / ISO 965-2 tolerances
// --------------------------------------------------------------------------

TEST_CASE("MetricThreads_LimitsMatchIso965Part2", "[standards][thread][p12]") {
    for (const ThreadRow& row : kThreadLimits) {
        const MetricThread size = thread(row.size);
        CAPTURE(row.size);
        const ThreadToleranceClass tolerance = standardToleranceClass(size);
        CHECK(tolerance.grade == (size.diameter() <= 1.4_mm ? 5 : 6));
        CHECK(tolerance.position == ThreadPosition::H);
        const auto limits = internalThreadLimits(size, tolerance);
        REQUIRE(limits.has_value());
        // The tabulated limits are rounded to 0.001 mm.
        CHECK_THAT(micrometres(limits->pitchDiameter.max), WithinAbs(row.pitchMax, kRoundedToMicrometre));
        CHECK_THAT(micrometres(limits->pitchDiameter.min), WithinAbs(row.pitchMin, kRoundedToMicrometre));
        CHECK_THAT(micrometres(limits->minorDiameter.max), WithinAbs(row.minorMax, kRoundedToMicrometre));
        CHECK_THAT(micrometres(limits->minorDiameter.min), WithinAbs(row.minorMin, kRoundedToMicrometre));
        // The tolerances themselves are whole micrometres, so the difference
        // of the rounded limits is exact.
        CHECK_THAT(micrometres(limits->pitchDiameter.max - limits->pitchDiameter.min),
                   WithinAbs(row.pitchMax - row.pitchMin, 1e-9));
        CHECK_THAT(micrometres(limits->minorDiameter.max - limits->minorDiameter.min),
                   WithinAbs(row.minorMax - row.minorMin, 1e-9));
        // Position H starts at the basic profile.
        CHECK_THAT(micrometres(limits->minimumMajorDiameter - size.diameter()), WithinAbs(0.0, 1e-9));
        CHECK(limits->pitchDiameter.min == basicDiameters(size).pitch);
        CHECK(limits->minorDiameter.min == basicDiameters(size).minor);
    }
}

TEST_CASE("MetricThreads_PositionGRaisesEveryLimitByItsDeviation", "[standards][thread][p12]") {
    for (const MetricThread& size : metricThreads()) {
        CAPTURE(size.designation());
        const int grade = standardToleranceClass(size).grade;
        const auto h = internalThreadLimits(size, {.grade = grade, .position = ThreadPosition::H});
        const auto g = internalThreadLimits(size, {.grade = grade, .position = ThreadPosition::G});
        REQUIRE(h.has_value());
        REQUIRE(g.has_value());
        const double deviation = micrometres(g->minorDiameter.min - h->minorDiameter.min);
        CHECK(deviation > 0.0);
        // One deviation moves the whole interval of every diameter.
        CHECK_THAT(micrometres(g->minorDiameter.max - h->minorDiameter.max), WithinAbs(deviation, 1e-9));
        CHECK_THAT(micrometres(g->pitchDiameter.min - h->pitchDiameter.min), WithinAbs(deviation, 1e-9));
        CHECK_THAT(micrometres(g->pitchDiameter.max - h->pitchDiameter.max), WithinAbs(deviation, 1e-9));
        CHECK_THAT(micrometres(g->minimumMajorDiameter - h->minimumMajorDiameter), WithinAbs(deviation, 1e-9));
        // ISO 965-2's external threads of position g are the same distance
        // below the basic profile.
        const auto external = kExternalDeviations.find(static_cast<std::uint32_t>(
            std::lround(size.pitch().in(units::um))));
        if (external != kExternalDeviations.end()) {
            CHECK_THAT(deviation, WithinAbs(external->second, 1e-9));
        } else {
            // The pitches of M1 to M1.4, whose ISO 965-2 external threads are
            // of position h.
            CHECK_THAT(deviation, WithinAbs(18.0, 1e-9));
        }
    }
}

TEST_CASE("MetricThreads_TolerancesFollowTheIso965Part1Formulae", "[standards][thread][p12]") {
    // ISO 965-1's tolerances are rounded to the R40 series, whose steps are
    // 5.93 % apart, so a tabulated value is within one step of its formula.
    constexpr double kStep = 0.0593;
    for (const MetricThread& size : metricThreads()) {
        CAPTURE(size.designation());
        const int grade = standardToleranceClass(size).grade;
        const auto limits = internalThreadLimits(size, {.grade = grade, .position = ThreadPosition::H});
        REQUIRE(limits.has_value());
        const double p = size.pitch().in(units::mm);
        // TD1(6) = 433 P - 190 P^1.22 for P up to 0.8 mm, 230 P^0.7 above;
        // grade 5 is 0.8 of grade 6.
        const double minor6 = p <= 0.8 ? 433.0 * p - 190.0 * std::pow(p, 1.22) : 230.0 * std::pow(p, 0.7);
        const double minor = (grade == 5 ? 0.8 : 1.0) * minor6;
        CHECK_THAT(micrometres(limits->minorDiameter.max - limits->minorDiameter.min), WithinRel(minor, kStep));
        // TD2 = 1.32 (grade 6) or 1.06 (grade 5) of Td2(6) = 90 P^0.4 d^0.1,
        // where d is the geometric mean of the diameter range's limits.
        static constexpr std::array<double, 7> kRangeLimits{1.4, 2.8, 5.6, 11.2, 22.4, 45.0, 90.0};
        const double diameter = size.diameter().in(units::mm);
        double lower = 0.99;
        double upper = kRangeLimits.front();
        for (const double limit : kRangeLimits) {
            if (diameter <= limit) {
                upper = limit;
                break;
            }
            lower = limit;
        }
        const double mean = std::sqrt(lower * upper);
        const double pitch6 = 1.32 * 90.0 * std::pow(p, 0.4) * std::pow(mean, 0.1);
        const double pitch = (grade == 5 ? 1.06 / 1.32 : 1.0) * pitch6;
        CHECK_THAT(micrometres(limits->pitchDiameter.max - limits->pitchDiameter.min), WithinRel(pitch, kStep));
    }
}

TEST_CASE("MetricThreads_RefuseClassesWithoutKnownTolerances", "[standards][thread][p12]") {
    SECTION("only the grade of ISO 965-2 is known, in either position") {
        for (const int grade : {4, 5, 7, 8}) {
            CAPTURE(grade);
            const auto limits = internalThreadLimits(thread("M8"), {.grade = grade});
            REQUIRE_FALSE(limits.has_value());
            CHECK(limits.error().code == ErrorCode::InvalidArgument);
            CHECK_THAT(limits.error().message,
                       ContainsSubstring("BetterCAD knows the M8 thread tolerances of grade 6 only"));
        }
        CHECK(internalThreadLimits(thread("M8"), {.grade = 6}).has_value());
        CHECK(internalThreadLimits(thread("M8"), {.grade = 6, .position = ThreadPosition::G}).has_value());
        // M1.4 and smaller are grade 5 in ISO 965-2.
        CHECK(internalThreadLimits(thread("M1.4"), {.grade = 5}).has_value());
        CHECK_FALSE(internalThreadLimits(thread("M1.4"), {.grade = 6}).has_value());
    }
    SECTION("classes are parsed from their designations") {
        CHECK(*parseThreadToleranceClass("6H") == ThreadToleranceClass{});
        CHECK(*parseThreadToleranceClass("5G") == ThreadToleranceClass{.grade = 5, .position = ThreadPosition::G});
        CHECK(toString(ThreadToleranceClass{.grade = 4, .position = ThreadPosition::G}) == "4G");
        CHECK(designation(thread("M8"), {}) == "M8-6H");
        CHECK(designation(thread("M8x1"), {.grade = 6, .position = ThreadPosition::G}) == "M8x1-6G");
        for (const std::string_view text : {"", "6", "H", "H6", "6h", "6g", "9H", "3H", "6HH", "5H6H", "06H", "6 H"}) {
            CAPTURE(text);
            const auto parsed = parseThreadToleranceClass(text);
            REQUIRE_FALSE(parsed.has_value());
            CHECK(parsed.error().code == ErrorCode::InvalidArgument);
            CHECK_THAT(parsed.error().message, ContainsSubstring("a grade from 4 to 8 and the position G or H"));
        }
    }
}

// --------------------------------------------------------------------------
// ISO 273 clearance holes
// --------------------------------------------------------------------------

TEST_CASE("ClearanceHoles_MatchIso273", "[standards][clearance][p12]") {
    REQUIRE(kClearanceHoles.size() == 34);
    for (const ClearanceRow& row : kClearanceHoles) {
        CAPTURE(row.size);
        const MetricThread bolt = thread(row.size);
        const std::array<ClearanceSeries, 3> series{ClearanceSeries::Fine, ClearanceSeries::Medium,
                                                    ClearanceSeries::Coarse};
        Length previous;
        for (std::size_t i = 0; i < series.size(); ++i) {
            const Length hole = clearanceHoleDiameter(bolt, series[i]);
            CHECK_THAT(hole.in(units::mm), WithinAbs(row.holes[i] / 10.0, 1e-12));
            // Every clearance hole is wider than the bolt, and the series
            // grow.
            CHECK(hole > bolt.diameter());
            CHECK(hole > previous);
            previous = hole;
        }
    }
    SECTION("the hole depends on the nominal diameter, not the pitch") {
        for (const MetricThread& size : metricThreads()) {
            CAPTURE(size.designation());
            for (const ClearanceSeries series : {ClearanceSeries::Fine, ClearanceSeries::Medium,
                                                 ClearanceSeries::Coarse}) {
                const auto coarse = std::ranges::find_if(metricThreads(), [&](const MetricThread& other) {
                    return other.coarse() && other.diameter() == size.diameter();
                });
                CHECK(clearanceHoleDiameter(size, series) == clearanceHoleDiameter(*coarse, series));
            }
        }
    }
    SECTION("each series has the tolerance class ISO 273 gives it") {
        CHECK(clearanceHoleTolerance(ClearanceSeries::Fine) == HoleToleranceClass{HoleDeviation::H, 12});
        CHECK(clearanceHoleTolerance(ClearanceSeries::Medium) == HoleToleranceClass{HoleDeviation::H, 13});
        CHECK(clearanceHoleTolerance(ClearanceSeries::Coarse) == HoleToleranceClass{HoleDeviation::H, 14});
        CHECK(toString(ClearanceSeries::Fine) == "fine");
        CHECK(toString(ClearanceSeries::Medium) == "medium");
        CHECK(toString(ClearanceSeries::Coarse) == "coarse");
    }
}

// --------------------------------------------------------------------------
// ISO 286 tolerance classes
// --------------------------------------------------------------------------

TEST_CASE("HoleTolerances_StandardTolerancesMatchIso286", "[standards][tolerance][p12]") {
    SECTION("every grade of every range") {
        for (std::size_t r = 0; r < kStandardTolerances.size(); ++r) {
            const ToleranceRow& row = kStandardTolerances[r];
            // A size in the middle of the range, and its upper limit.
            const double lower = r == 0 ? 0.5 : kStandardTolerances[r - 1].upToMm;
            for (const double sizeMm : {(lower + row.upToMm) / 2.0, static_cast<double>(row.upToMm)}) {
                for (int grade = 1; grade <= 18; ++grade) {
                    CAPTURE(sizeMm, grade);
                    const auto tolerance = standardTolerance(sizeMm * units::mm, grade);
                    if (grade >= 14 && sizeMm <= 1.0) {
                        CHECK_FALSE(tolerance.has_value());
                        continue;
                    }
                    REQUIRE(tolerance.has_value());
                    CHECK_THAT(micrometres(*tolerance),
                               WithinAbs(row.tenths[static_cast<std::size_t>(grade - 1)] / 10.0, 1e-9));
                }
            }
        }
    }
    SECTION("the grades grow by a factor of 10 every fifth step") {
        // ISO 286-1 gives this rule for IT6 and above, to extrapolate grades
        // its table does not list. Its own table keeps it from IT7 up; at
        // IT6 it holds everywhere but for sizes of 3 to 6 mm, whose IT6 is
        // 8 um and IT11 75 um rather than 80 um (every published copy of the
        // table agrees).
        for (const ToleranceRow& row : kStandardTolerances) {
            for (int grade = 7; grade <= 13; ++grade) {
                CAPTURE(row.upToMm, grade);
                const auto small = standardTolerance(row.upToMm * units::mm, grade);
                const auto large = standardTolerance(row.upToMm * units::mm, grade + 5);
                REQUIRE(small.has_value());
                REQUIRE(large.has_value());
                CHECK_THAT(micrometres(*large), WithinRel(10.0 * micrometres(*small), 1e-12));
            }
            const auto it6 = standardTolerance(row.upToMm * units::mm, 6);
            const auto it11 = standardTolerance(row.upToMm * units::mm, 11);
            REQUIRE(it6.has_value());
            REQUIRE(it11.has_value());
            if (row.upToMm == 6) {
                CHECK_THAT(micrometres(*it6), WithinAbs(8.0, 1e-9));
                CHECK_THAT(micrometres(*it11), WithinAbs(75.0, 1e-9));
            } else {
                CHECK_THAT(micrometres(*it11), WithinRel(10.0 * micrometres(*it6), 1e-12));
            }
        }
    }
    SECTION("grades 14 to 18 are not used for sizes up to 1 mm") {
        for (int grade = 14; grade <= 18; ++grade) {
            CAPTURE(grade);
            const auto refused = standardTolerance(1_mm, grade);
            REQUIRE_FALSE(refused.has_value());
            CHECK(refused.error().code == ErrorCode::InvalidArgument);
            CHECK_THAT(refused.error().message,
                       ContainsSubstring(std::format("ISO 286 does not use grade IT{} for sizes up to 1 mm", grade)));
            CHECK(standardTolerance(1.001_mm, grade).has_value());
            CHECK(standardTolerance(1_mm, 13).has_value());
        }
    }
    SECTION("sizes and grades out of range are refused") {
        for (const Length size : {0_mm, -5_mm, 500.001_mm, 1000_mm,
                                  Length::fromSi(std::numeric_limits<double>::quiet_NaN())}) {
            const auto refused = standardTolerance(size, 7);
            REQUIRE_FALSE(refused.has_value());
            CHECK_THAT(refused.error().message,
                       ContainsSubstring("BetterCAD knows the ISO 286 tolerances of sizes above 0 up to 500 mm"));
        }
        for (const int grade : {0, -1, 19, 100}) {
            CAPTURE(grade);
            const auto refused = standardTolerance(10_mm, grade);
            REQUIRE_FALSE(refused.has_value());
            CHECK_THAT(refused.error().message,
                       ContainsSubstring("the standard tolerance grade must be from 1 to 18"));
        }
    }
    SECTION("a range includes its upper limit") {
        // 3 mm is in the first range, 3.0000001 mm in the second.
        CHECK_THAT(micrometres(*standardTolerance(3_mm, 7)), WithinAbs(10.0, 1e-9));
        CHECK_THAT(micrometres(*standardTolerance(3.0000001_mm, 7)), WithinAbs(12.0, 1e-9));
        CHECK_THAT(micrometres(*standardTolerance(500_mm, 7)), WithinAbs(63.0, 1e-9));
        CHECK_THAT(micrometres(*standardTolerance(0.001_mm, 7)), WithinAbs(10.0, 1e-9));
    }
}

TEST_CASE("HoleTolerances_LimitDeviationsMatchIso286", "[standards][tolerance][p12]") {
    SECTION("the classes JIS B 0401 tabulates") {
        for (std::size_t r = 0; r < kLimitDeviations.size(); ++r) {
            const DeviationRow& row = kLimitDeviations[r];
            const double lower = r == 0 ? 0.5 : kLimitDeviations[r - 1].upToMm;
            const Length size = (lower + row.upToMm) / 2.0 * units::mm;
            for (std::size_t c = 0; c < kDeviationClasses.size(); ++c) {
                CAPTURE(row.upToMm, kDeviationClasses[c]);
                const auto tolerance = parseHoleToleranceClass(kDeviationClasses[c]);
                REQUIRE(tolerance.has_value());
                const auto deviations = limitDeviations(size, *tolerance);
                REQUIRE(deviations.has_value());
                const std::size_t group = static_cast<std::size_t>(tolerance->deviation);
                CHECK_THAT(micrometres(deviations->upper), WithinAbs(row.upper[c], 1e-9));
                CHECK_THAT(micrometres(deviations->lower), WithinAbs(row.lower[group], 1e-9));
            }
        }
    }
    SECTION("D6 to D13 and E5 to E10 of ISO 286-2") {
        for (std::size_t r = 0; r < kLimitDeviationsDE.size(); ++r) {
            const DeviationRowDE& row = kLimitDeviationsDE[r];
            const double lower = r == 0 ? 0.5 : kLimitDeviationsDE[r - 1].upToMm;
            const Length size = (lower + row.upToMm) / 2.0 * units::mm;
            for (int grade = 6; grade <= 13; ++grade) {
                CAPTURE(row.upToMm, grade);
                const auto deviations = limitDeviations(size, {HoleDeviation::D, grade});
                REQUIRE(deviations.has_value());
                CHECK_THAT(micrometres(deviations->upper),
                           WithinAbs(row.upperD[static_cast<std::size_t>(grade - 6)], 1e-9));
                CHECK_THAT(micrometres(deviations->lower), WithinAbs(row.lowerD, 1e-9));
            }
            for (int grade = 5; grade <= 10; ++grade) {
                CAPTURE(row.upToMm, grade);
                const auto deviations = limitDeviations(size, {HoleDeviation::E, grade});
                REQUIRE(deviations.has_value());
                CHECK_THAT(micrometres(deviations->upper),
                           WithinAbs(row.upperE[static_cast<std::size_t>(grade - 5)], 1e-9));
                CHECK_THAT(micrometres(deviations->lower), WithinAbs(row.lowerE, 1e-9));
            }
        }
    }
    SECTION("the upper deviation is the lower one plus the tolerance") {
        for (const std::string_view text : {"D6", "D13", "E5", "E10", "F3", "F10", "G3", "G10", "H1", "H18"}) {
            CAPTURE(text);
            const auto tolerance = parseHoleToleranceClass(text);
            REQUIRE(tolerance.has_value());
            for (const double sizeMm : {1.5, 25.0, 500.0}) {
                CAPTURE(sizeMm);
                const auto deviations = limitDeviations(sizeMm * units::mm, *tolerance);
                const auto it = standardTolerance(sizeMm * units::mm, tolerance->grade);
                REQUIRE(deviations.has_value());
                REQUIRE(it.has_value());
                CHECK_THAT(micrometres(deviations->upper - deviations->lower), WithinAbs(micrometres(*it), 1e-9));
                CHECK(deviations->lower >= Length{});
                CHECK((tolerance->deviation == HoleDeviation::H) == (deviations->lower == Length{}));
            }
        }
    }
    SECTION("H7 of a 10 mm hole is the widely tabulated 0 to +15 um") {
        const auto deviations = limitDeviations(10_mm, {HoleDeviation::H, 7});
        REQUIRE(deviations.has_value());
        CHECK(deviations->lower == Length{});
        CHECK_THAT(micrometres(deviations->upper), WithinAbs(15.0, 1e-9));
        // A hole one micrometre wider is in the next range: 0 to +18 um.
        CHECK_THAT(micrometres(limitDeviations(10.001_mm, {HoleDeviation::H, 7})->upper), WithinAbs(18.0, 1e-9));
    }
}

TEST_CASE("HoleTolerances_KnowOnlyTheTabulatedClasses", "[standards][tolerance][p12]") {
    SECTION("the classes ISO 286-2 tabulates for the known positions") {
        const std::array<std::pair<HoleDeviation, std::pair<int, int>>, 5> known{{
            {HoleDeviation::D, {6, 13}},
            {HoleDeviation::E, {5, 10}},
            {HoleDeviation::F, {3, 10}},
            {HoleDeviation::G, {3, 10}},
            {HoleDeviation::H, {1, 18}},
        }};
        for (const auto& [deviation, grades] : known) {
            for (int grade = 1; grade <= 18; ++grade) {
                CAPTURE(toString(deviation), grade);
                const HoleToleranceClass tolerance{deviation, grade};
                const bool inRange = grade >= grades.first && grade <= grades.second;
                CHECK(validate(tolerance).has_value() == inRange);
                CHECK(parseHoleToleranceClass(toString(tolerance)).has_value() == inRange);
                if (!inRange) {
                    const auto refused = validate(tolerance);
                    CHECK_THAT(refused.error().message,
                               ContainsSubstring(std::format("the hole tolerance class {}{} is not one BetterCAD "
                                                             "knows",
                                                             toString(deviation), grade)));
                }
            }
        }
    }
    SECTION("designations are parsed and printed") {
        CHECK(*parseHoleToleranceClass("H7") == HoleToleranceClass{});
        CHECK(*parseHoleToleranceClass("D10") == HoleToleranceClass{HoleDeviation::D, 10});
        CHECK(toString(HoleToleranceClass{HoleDeviation::G, 6}) == "G6");
        CHECK(toString(HoleToleranceClass{HoleDeviation::E, 9}) == "E9");
    }
    SECTION("positions BetterCAD does not know are refused") {
        // JS (whose copies disagree on rounding), the positions below D and
        // the transition and interference positions.
        for (const std::string_view text : {"JS7", "js7", "A11", "B11", "C11", "CD7", "EF7", "FG7", "J7", "K7", "M7",
                                            "N7", "P7", "R7", "S7", "T7", "U7", "X7", "Z7", "ZC9"}) {
            CAPTURE(text);
            const auto parsed = parseHoleToleranceClass(text);
            REQUIRE_FALSE(parsed.has_value());
            CHECK(parsed.error().code == ErrorCode::InvalidArgument);
            CHECK_THAT(parsed.error().message,
                       ContainsSubstring("is not a hole tolerance class BetterCAD knows (D6 to D13, E5 to E10, F3 "
                                         "to F10, G3 to G10, H1 to H18)"));
        }
    }
    SECTION("malformed designations are refused") {
        for (const std::string_view text : {"", "7", "h7", "H", "H07", "H 7", "H7.5", "H7x", "HH7", "H0", "H19"}) {
            CAPTURE(text);
            CHECK_FALSE(parseHoleToleranceClass(text).has_value());
        }
    }
}

TEST_CASE("HoleTolerances_RefuseSizesWithoutTabulatedDeviations", "[standards][tolerance][p12]") {
    // The fundamental deviations of D to G are tabulated for the same sizes
    // as the standard tolerances.
    for (const std::string_view text : {"D9", "E8", "F7", "G6", "H7"}) {
        CAPTURE(text);
        const auto tolerance = parseHoleToleranceClass(text);
        REQUIRE(tolerance.has_value());
        CHECK(limitDeviations(500_mm, *tolerance).has_value());
        const auto refused = limitDeviations(500.5_mm, *tolerance);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == ErrorCode::InvalidArgument);
        CHECK_THAT(refused.error().message, ContainsSubstring("up to 500 mm"));
    }
    CHECK_FALSE(limitDeviations(10_mm, {HoleDeviation::D, 5}).has_value());
    CHECK_FALSE(limitDeviations(0.5_mm, {HoleDeviation::H, 14}).has_value());
    CHECK(limitDeviations(0.5_mm, {HoleDeviation::H, 13}).has_value());
}
