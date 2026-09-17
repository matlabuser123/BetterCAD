// Validation of draft requests: everything that can be checked without a
// body (P12-FEAT-004).
#include "core/geometry/FaceNameList.hpp"

#include <bettercad/core/geometry/Draft.hpp>
#include <bettercad/core/units/Format.hpp>

#include <cmath>
#include <format>
#include <numbers>

namespace bettercad::geometry {

Result<void> validate(const DraftRequest& request) {
    if (request.faces.empty()) {
        return makeError(ErrorCode::InvalidArgument, "a draft needs one or more faces");
    }
    if (auto names = detail::validateFaceNames(request.faces, "face"); !names) {
        return names;
    }
    if (!isFinite(request.angle) || !(std::abs(request.angle.si()) < std::numbers::pi / 2.0)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the draft angle must be in (-90, 90) deg, got {}",
                                     toString(request.angle, units::deg)));
    }
    return {};
}

} // namespace bettercad::geometry
