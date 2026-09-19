#include <bettercad/assembly/Placement.hpp>

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/core/document/Document.hpp>

#include <array>
#include <format>
#include <string_view>

namespace bettercad::assembly {
namespace {

constexpr std::array<std::string_view, 3> kAxes{"X", "Y", "Z"};

/// The value in force for a driving parameter, as quantity Q: NotFound if it
/// does not exist, DimensionMismatch if it is not a Q. The value is the one
/// the active configuration gives, so a placement follows a configuration
/// without knowing configurations exist (P12-PARAM-002).
///
/// This repeats features::detail::drivingValue, which lives in a private
/// header (src/features/SolidSupport.hpp) that a module above features may
/// not include. It is written against public Document API only. Promoting
/// the original to core would remove the repetition and is recorded as a
/// follow-up rather than done here, mid-milestone, to a qualified module.
template <QuantityType Q>
[[nodiscard]] Result<Q> drivenValue(const Document& document, ParameterId parameter, std::string_view role) {
    const Parameter* found = document.parameters().find(parameter);
    if (found == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("{} {} does not exist", role, parameter));
    }
    if (Q::dimension != found->dimension()) {
        // The parameter's own message names the dimensions.
        return found->as<Q>();
    }
    return Q::fromSi(document.effectiveParameterValue(parameter)->siValue);
}

template <QuantityType Q>
[[nodiscard]] Result<Q> valueOf(const Document& document, Q literal, const std::optional<ParameterId>& parameter,
                                std::string_view role) {
    if (!parameter) {
        return literal;
    }
    return drivenValue<Q>(document, *parameter, role);
}

} // namespace

Result<RigidTransform3D> resolvePlacement(const Document& document, const ComponentPlacement& placement) {
    std::array<Length, 3> translation{};
    std::array<Angle, 3> rotation{};
    for (std::size_t i = 0; i < 3; ++i) {
        auto t = valueOf(document, placement.translation[i], placement.translationParameters[i],
                         std::format("{} translation parameter", kAxes[i]));
        if (!t) {
            return std::unexpected(t.error());
        }
        auto r = valueOf(document, placement.rotation[i], placement.rotationParameters[i],
                         std::format("{} rotation parameter", kAxes[i]));
        if (!r) {
            return std::unexpected(r.error());
        }
        // A literal is checked by validate(); a parameter can only be checked
        // once it has been read.
        if (!isFinite(*t) || !isFinite(*r)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("a placement's {} translation and rotation must be finite", kAxes[i]));
        }
        translation[i] = *t;
        rotation[i] = *r;
    }

    // About the model's fixed X, then Y, then Z axis, all through its origin,
    // so the composed matrix is Rz * Ry * Rx. after(first) applies `first`
    // and then the receiver, so each turn wraps the ones before it.
    static constexpr std::array<Direction3D, 3> kModelAxes{Direction3D::unitX(), Direction3D::unitY(),
                                                           Direction3D::unitZ()};
    constexpr Point3D kOrigin{};
    RigidTransform3D motion;
    for (std::size_t i = 0; i < 3; ++i) {
        if (rotation[i].si() == 0.0) {
            continue;
        }
        motion = RigidTransform3D::rotation(Axis3D{kOrigin, kModelAxes[i]}, rotation[i]).after(motion);
    }
    // Along the model's axes, not the turned ones, and after the turns.
    const Translation3D offset{translation[0], translation[1], translation[2]};
    return RigidTransform3D::translation(offset).after(motion);
}

Result<RigidTransform3D> placementOf(const Document& document, ComponentId id) {
    const Component* component = findComponent(document, id);
    if (component == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("there is no component {}", id));
    }
    return resolvePlacement(document, component->definition().placement);
}

} // namespace bettercad::assembly
