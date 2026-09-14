#pragma once

#include <bettercad/core/geometry/Export.hpp>

#include <string_view>

namespace bettercad::geometry {

/// Identity of the geometry kernel behind the geometry API, for version
/// reports and bug reports.
struct KernelInfo {
    std::string_view name;
    std::string_view version;
};

[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT KernelInfo geometryKernel() noexcept;

} // namespace bettercad::geometry
