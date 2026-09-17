// Validation of shell requests: everything that can be checked without a
// body (P12-FEAT-003).
#include "core/geometry/FaceNameList.hpp"

#include <bettercad/core/geometry/Shell.hpp>
#include <bettercad/core/units/Format.hpp>

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
    if (auto names = detail::validateFaceNames(request.openFaces, "open face"); !names) {
        return names;
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
