// ISO 286-1 tolerance classes of holes (P12-HOLE-001). The tables are
// transcribed from ISO 286-2:2010, Table 1 (standard tolerances) and Table 3
// (the lower limit deviations of D and E), and from the lower limit
// deviations of F and G in two published copies of ISO 286-2; see
// docs/verification/P12-HOLE-001/ for the sources and their cross-checks.
#include "core/standards/Designations.hpp"

#include <bettercad/core/standards/HoleTolerances.hpp>
#include <bettercad/core/units/Format.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>

namespace bettercad::standards {

namespace {

constexpr std::size_t kRanges = 13;

/// Upper limits of the nominal size ranges up to 500 mm, in mm; each range
/// starts above the previous limit (the first above 0).
constexpr std::array<int, kRanges> kRangeLimitsMm{3, 6, 10, 18, 30, 50, 80, 120, 180, 250, 315, 400, 500};

/// Standard tolerances IT1 to IT18 by range, in tenths of a micrometre
/// (ISO 286-1:2010, Table 1, as reproduced in ISO 286-2:2010, Table 1).
constexpr std::array<std::array<std::int32_t, kRanges>, 18> kStandardTolerances{{
    {8, 10, 10, 12, 15, 15, 20, 25, 35, 45, 60, 70, 80},
    {12, 15, 15, 20, 25, 25, 30, 40, 50, 70, 80, 90, 100},
    {20, 25, 25, 30, 40, 40, 50, 60, 80, 100, 120, 130, 150},
    {30, 40, 40, 50, 60, 70, 80, 100, 120, 140, 160, 180, 200},
    {40, 50, 60, 80, 90, 110, 130, 150, 180, 200, 230, 250, 270},
    {60, 80, 90, 110, 130, 160, 190, 220, 250, 290, 320, 360, 400},
    {100, 120, 150, 180, 210, 250, 300, 350, 400, 460, 520, 570, 630},
    {140, 180, 220, 270, 330, 390, 460, 540, 630, 720, 810, 890, 970},
    {250, 300, 360, 430, 520, 620, 740, 870, 1000, 1150, 1300, 1400, 1550},
    {400, 480, 580, 700, 840, 1000, 1200, 1400, 1600, 1850, 2100, 2300, 2500},
    {600, 750, 900, 1100, 1300, 1600, 1900, 2200, 2500, 2900, 3200, 3600, 4000},
    {1000, 1200, 1500, 1800, 2100, 2500, 3000, 3500, 4000, 4600, 5200, 5700, 6300},
    {1400, 1800, 2200, 2700, 3300, 3900, 4600, 5400, 6300, 7200, 8100, 8900, 9700},
    {2500, 3000, 3600, 4300, 5200, 6200, 7400, 8700, 10000, 11500, 13000, 14000, 15500},
    {4000, 4800, 5800, 7000, 8400, 10000, 12000, 14000, 16000, 18500, 21000, 23000, 25000},
    {6000, 7500, 9000, 11000, 13000, 16000, 19000, 22000, 25000, 29000, 32000, 36000, 40000},
    {10000, 12000, 15000, 18000, 21000, 25000, 30000, 35000, 40000, 46000, 52000, 57000, 63000},
    {14000, 18000, 22000, 27000, 33000, 39000, 46000, 54000, 63000, 72000, 81000, 89000, 97000},
}};

/// The lower limit deviations EI of D, E, F and G by range, in micrometres
/// (ISO 286-1; the D and E values as ISO 286-2:2010, Table 3 lists them).
/// The ranges these deviations subdivide (10 to 14 and 14 to 18, ...) have
/// equal values, so the tolerance ranges carry them.
constexpr std::array<std::array<std::int32_t, kRanges>, 4> kFundamentalDeviations{{
    {20, 30, 40, 50, 65, 80, 100, 120, 145, 170, 190, 210, 230},
    {14, 20, 25, 32, 40, 50, 60, 72, 85, 100, 110, 125, 135},
    {6, 10, 13, 16, 20, 25, 30, 36, 43, 50, 56, 62, 68},
    {2, 4, 5, 6, 7, 9, 10, 12, 14, 15, 17, 18, 20},
}};

struct GradeRange {
    int lowest;
    int highest;
};

/// The grades ISO 286-2:2010 tabulates for sizes up to 500 mm (Figure 3).
constexpr GradeRange gradesOf(HoleDeviation deviation) noexcept {
    switch (deviation) {
    case HoleDeviation::D:
        return {6, 13};
    case HoleDeviation::E:
        return {5, 10};
    case HoleDeviation::F:
    case HoleDeviation::G:
        return {3, 10};
    case HoleDeviation::H:
        break;
    }
    return {1, 18};
}

// Sizes within this distance of a range limit are taken as the limit (mm).
constexpr double kLimitToleranceMm = 1e-9;

/// The range of a nominal size, or none if it is not in (0, 500] mm.
std::optional<std::size_t> rangeOf(double sizeMm) {
    if (!std::isfinite(sizeMm) || !(sizeMm > 0.0)) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < kRanges; ++i) {
        if (sizeMm <= kRangeLimitsMm[i] + kLimitToleranceMm) {
            return i;
        }
    }
    return std::nullopt;
}

Length fromTenthsOfMicrometre(std::int32_t value) {
    return Length::fromSi(value / 1e7);
}

} // namespace

std::string_view toString(HoleDeviation deviation) noexcept {
    switch (deviation) {
    case HoleDeviation::D:
        return "D";
    case HoleDeviation::E:
        return "E";
    case HoleDeviation::F:
        return "F";
    case HoleDeviation::G:
        return "G";
    case HoleDeviation::H:
        return "H";
    }
    return "unknown";
}

std::string toString(const HoleToleranceClass& tolerance) {
    return std::format("{}{}", toString(tolerance.deviation), tolerance.grade);
}

Result<void> validate(const HoleToleranceClass& tolerance) {
    const GradeRange grades = gradesOf(tolerance.deviation);
    if (tolerance.grade < grades.lowest || tolerance.grade > grades.highest) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the hole tolerance class {}{} is not one BetterCAD knows: {} takes grades {} "
                                     "to {}",
                                     toString(tolerance.deviation), tolerance.grade, toString(tolerance.deviation),
                                     grades.lowest, grades.highest));
    }
    return {};
}

Result<HoleToleranceClass> parseHoleToleranceClass(std::string_view text) {
    const auto unknown = [&] {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("'{}' is not a hole tolerance class BetterCAD knows (D6 to D13, E5 to E10, F3 "
                                     "to F10, G3 to G10, H1 to H18)",
                                     text));
    };
    if (text.size() < 2) {
        return unknown();
    }
    HoleToleranceClass tolerance;
    switch (text.front()) {
    case 'D':
        tolerance.deviation = HoleDeviation::D;
        break;
    case 'E':
        tolerance.deviation = HoleDeviation::E;
        break;
    case 'F':
        tolerance.deviation = HoleDeviation::F;
        break;
    case 'G':
        tolerance.deviation = HoleDeviation::G;
        break;
    case 'H':
        tolerance.deviation = HoleDeviation::H;
        break;
    default:
        return unknown();
    }
    const auto grade = detail::parseWholeNumber(text.substr(1));
    if (!grade) {
        return unknown();
    }
    tolerance.grade = *grade;
    if (!validate(tolerance)) {
        return unknown();
    }
    return tolerance;
}

Result<Length> standardTolerance(Length nominalSize, int grade) {
    if (grade < 1 || grade > 18) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the standard tolerance grade must be from 1 to 18, got {}", grade));
    }
    const double sizeMm = nominalSize.in(units::mm);
    const auto range = rangeOf(sizeMm);
    if (!range) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("BetterCAD knows the ISO 286 tolerances of sizes above 0 up to 500 mm, not "
                                     "{}",
                                     toString(nominalSize, units::mm)));
    }
    if (grade >= 14 && sizeMm <= 1.0 + kLimitToleranceMm) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("ISO 286 does not use grade IT{} for sizes up to 1 mm, like {}", grade,
                                     toString(nominalSize, units::mm)));
    }
    return fromTenthsOfMicrometre(kStandardTolerances[static_cast<std::size_t>(grade - 1)][*range]);
}

Result<LimitDeviations> limitDeviations(Length nominalSize, const HoleToleranceClass& tolerance) {
    if (auto valid = validate(tolerance); !valid) {
        return std::unexpected(valid.error());
    }
    auto it = standardTolerance(nominalSize, tolerance.grade);
    if (!it) {
        return std::unexpected(it.error());
    }
    // In tenths of a micrometre, so that EI + IT is exact. The size is in
    // range: standardTolerance() accepted it.
    const std::size_t range = *rangeOf(nominalSize.in(units::mm));
    std::int32_t lower = 0;
    if (tolerance.deviation != HoleDeviation::H) {
        lower = 10 * kFundamentalDeviations[static_cast<std::size_t>(tolerance.deviation)][range];
    }
    const std::int32_t width = kStandardTolerances[static_cast<std::size_t>(tolerance.grade - 1)][range];
    return LimitDeviations{.lower = fromTenthsOfMicrometre(lower), .upper = fromTenthsOfMicrometre(lower + width)};
}

} // namespace bettercad::standards
