#pragma once

#include <bettercad/core/Id.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/sketch/Export.hpp>

#include <optional>
#include <string_view>
#include <vector>

// Sketch constraints: the representation that the solver (P6) turns into
// equations. Which entities a constraint type accepts:
//
//   Coincident     point, point
//   Horizontal     line  |  point, point
//   Vertical       line  |  point, point
//   Parallel       line, line
//   Perpendicular  line, line
//   Distance       point, point  |  line (its length)  |  point, line   + value
//   Radius         circle  |  arc                                      + value
//   Equal          line, line (lengths)  |  circle/arc, circle/arc (radii)
//   Fixed          point (its current position is held)
//
// Referenced entities must exist and be distinct.
namespace bettercad::sketch {

enum class ConstraintType {
    Coincident,
    Horizontal,
    Vertical,
    Parallel,
    Perpendicular,
    Distance,
    Radius,
    Equal,
    Fixed,
};

[[nodiscard]] BETTERCAD_SKETCH_EXPORT std::string_view toString(ConstraintType type) noexcept;

/// True for constraint types that carry a value (Distance, Radius).
[[nodiscard]] constexpr bool hasValue(ConstraintType type) noexcept {
    return type == ConstraintType::Distance || type == ConstraintType::Radius;
}

struct Constraint {
    ConstraintId id{};
    ConstraintType type = ConstraintType::Coincident;
    /// Referenced entities, in the order given by the type's signature.
    std::vector<EntityId> entities{};
    /// Target value for Distance and Radius; empty for the other types.
    std::optional<Length> value{};
    /// Document parameter that drives the value, if any. Regeneration copies
    /// the parameter's value into `value` before solving.
    std::optional<ParameterId> parameter{};
    /// Disabled constraints are kept but ignored by the solver.
    bool enabled = true;

    friend bool operator==(const Constraint&, const Constraint&) = default;
};

} // namespace bettercad::sketch
