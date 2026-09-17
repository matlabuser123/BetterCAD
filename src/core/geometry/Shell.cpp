// Validation of shell requests: everything that can be checked without a
// body (P12-FEAT-003).
#include <bettercad/core/geometry/Shell.hpp>
#include <bettercad/core/units/Format.hpp>

#include <algorithm>
#include <cstddef>
#include <format>

namespace bettercad::geometry {

std::string_view toString(ShellSide side) noexcept {
    switch (side) {
    case ShellSide::Inward:
        return "inward";
    case ShellSide::Outward:
        return "outward";
    }
    return "unknown";
}

Result<void> validate(const ShellRequest& request) {
    if (request.openFaces.empty()) {
        return makeError(ErrorCode::InvalidArgument,
                         "a shell needs one or more open faces (a closed hollow is not built)");
    }
    for (std::size_t i = 0; i < request.openFaces.size(); ++i) {
        const FaceName& name = request.openFaces[i];
        if (!name.feature.isValid()) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("open face {} must name a valid feature", i + 1));
        }
        if (auto valid = validate(name.face); !valid) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("open face {}: {}", i + 1, valid.error().message));
        }
        const auto earlier = request.openFaces.begin() + static_cast<std::ptrdiff_t>(i);
        if (std::find(request.openFaces.begin(), earlier, name) != earlier) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("open face {} repeats an earlier open face", i + 1));
        }
    }
    if (!isFinite(request.thickness) || !(request.thickness > Length{})) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the shell thickness must be positive and finite, got {}",
                                     toString(request.thickness, units::mm)));
    }
    if (request.side != ShellSide::Inward && request.side != ShellSide::Outward) {
        return makeError(ErrorCode::InvalidArgument, "a shell's walls lie inward or outward");
    }
    return {};
}

} // namespace bettercad::geometry
