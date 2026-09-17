#pragma once

// Checks of the face-name lists that shells and drafts take (P12-FEAT-003,
// P12-FEAT-004).

#include <bettercad/core/Error.hpp>
#include <bettercad/core/document/References.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <span>
#include <string_view>

namespace bettercad::geometry::detail {

/// Each name has a valid feature and selector and none repeats an earlier
/// one. InvalidArgument otherwise, naming the entry as "<noun> <n>" from 1,
/// e.g. "open face 2 repeats an earlier open face".
[[nodiscard]] inline Result<void> validateFaceNames(std::span<const FaceName> names, std::string_view noun) {
    for (std::size_t i = 0; i < names.size(); ++i) {
        const FaceName& name = names[i];
        if (!name.feature.isValid()) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} {} must name a valid feature", noun, i + 1));
        }
        if (auto valid = validate(name.face); !valid) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} {}: {}", noun, i + 1, valid.error().message));
        }
        const auto earlier = names.begin() + static_cast<std::ptrdiff_t>(i);
        if (std::find(names.begin(), earlier, name) != earlier) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} {} repeats an earlier {}", noun, i + 1, noun));
        }
    }
    return {};
}

} // namespace bettercad::geometry::detail
