// ISO 273 clearance holes for bolts and screws (P12-HOLE-001), transcribed
// from ISO 273:1979 for the nominal diameters of the known metric threads;
// see docs/verification/P12-HOLE-001/ for the sources and their
// cross-checks.
#include <bettercad/core/standards/ClearanceHoles.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace bettercad::standards {

namespace {

/// A nominal diameter and its fine, medium and coarse clearance holes, in
/// tenths of a millimetre.
struct ClearanceRow {
    std::int32_t diameter;
    std::array<std::int32_t, 3> holes;
};

constexpr std::array<ClearanceRow, 34> kClearanceHoles{{
    {10, {11, 12, 13}},       {12, {13, 14, 15}},       {14, {15, 16, 18}},       {16, {17, 18, 20}},
    {18, {20, 21, 22}},       {20, {22, 24, 26}},       {25, {27, 29, 31}},       {30, {32, 34, 36}},
    {35, {37, 39, 42}},       {40, {43, 45, 48}},       {50, {53, 55, 58}},       {60, {64, 66, 70}},
    {70, {74, 76, 80}},       {80, {84, 90, 100}},      {100, {105, 110, 120}},   {120, {130, 135, 145}},
    {140, {150, 155, 165}},   {160, {170, 175, 185}},   {180, {190, 200, 210}},   {200, {210, 220, 240}},
    {220, {230, 240, 260}},   {240, {250, 260, 280}},   {270, {280, 300, 320}},   {300, {310, 330, 350}},
    {330, {340, 360, 380}},   {360, {370, 390, 420}},   {390, {400, 420, 450}},   {420, {430, 450, 480}},
    {450, {460, 480, 520}},   {480, {500, 520, 560}},   {520, {540, 560, 620}},   {560, {580, 620, 660}},
    {600, {620, 660, 700}},   {640, {660, 700, 740}},
}};

} // namespace

std::string_view toString(ClearanceSeries series) noexcept {
    switch (series) {
    case ClearanceSeries::Fine:
        return "fine";
    case ClearanceSeries::Medium:
        return "medium";
    case ClearanceSeries::Coarse:
        return "coarse";
    }
    return "unknown";
}

Length clearanceHoleDiameter(const MetricThread& bolt, ClearanceSeries series) noexcept {
    // Every known thread's diameter is a row (checked by the tests); tenths
    // of a millimetre are 1e-4 m.
    const auto tenths = static_cast<std::int32_t>(std::lround(bolt.diameter().si() * 1e4));
    const auto row = std::ranges::find(kClearanceHoles, tenths, &ClearanceRow::diameter);
    return Length::fromSi(row->holes[static_cast<std::size_t>(series)] / 1e4);
}

HoleToleranceClass clearanceHoleTolerance(ClearanceSeries series) noexcept {
    switch (series) {
    case ClearanceSeries::Fine:
        return {.deviation = HoleDeviation::H, .grade = 12};
    case ClearanceSeries::Medium:
        break;
    case ClearanceSeries::Coarse:
        return {.deviation = HoleDeviation::H, .grade = 14};
    }
    return {.deviation = HoleDeviation::H, .grade = 13};
}

} // namespace bettercad::standards
