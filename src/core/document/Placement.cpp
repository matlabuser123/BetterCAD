#include <bettercad/core/document/Placement.hpp>

#include <algorithm>
#include <format>

namespace bettercad {
namespace {

constexpr std::array<std::string_view, 3> kAxes{"X", "Y", "Z"};

} // namespace

Result<void> validate(const ComponentPlacement& placement) {
    for (std::size_t i = 0; i < 3; ++i) {
        if (!isFinite(placement.translation[i])) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("a placement's {} translation must be finite", kAxes[i]));
        }
        if (!isFinite(placement.rotation[i])) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("a placement's {} rotation must be finite", kAxes[i]));
        }
    }
    return {};
}

bool isIdentity(const ComponentPlacement& placement) noexcept {
    return placement == ComponentPlacement{};
}

std::vector<ParameterId> referencedParameters(const ComponentPlacement& placement) {
    std::vector<ParameterId> found;
    const auto push = [&found](const std::optional<ParameterId>& parameter) {
        if (parameter && std::ranges::find(found, *parameter) == found.end()) {
            found.push_back(*parameter);
        }
    };
    for (const auto& parameter : placement.translationParameters) {
        push(parameter);
    }
    for (const auto& parameter : placement.rotationParameters) {
        push(parameter);
    }
    return found;
}

} // namespace bettercad
