#pragma once

#include <bettercad/core/Export.hpp>
#include <bettercad/core/standards/HoleTolerances.hpp>
#include <bettercad/core/standards/MetricThreads.hpp>
#include <bettercad/core/units/Units.hpp>

#include <string_view>

namespace bettercad::standards {

/// The series of clearance holes for bolts and screws (ISO 273).
enum class ClearanceSeries {
    Fine,
    Medium,
    Coarse,
};

/// "fine", "medium" or "coarse".
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string_view toString(ClearanceSeries series) noexcept;

/// The diameter of the clearance hole ISO 273 gives a bolt or screw of
/// @p bolt's nominal diameter in @p series, whatever its pitch.
[[nodiscard]] BETTERCAD_CORE_EXPORT Length clearanceHoleDiameter(const MetricThread& bolt,
                                                                ClearanceSeries series) noexcept;

/// The tolerance class ISO 273 gives the series for information, where a
/// tolerance is wanted: H12 (fine), H13 (medium), H14 (coarse).
[[nodiscard]] BETTERCAD_CORE_EXPORT HoleToleranceClass clearanceHoleTolerance(ClearanceSeries series) noexcept;

} // namespace bettercad::standards
