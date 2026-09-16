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
// Added by P12-SKETCH-001:
//
//   Angle          line, line                                          + angle
//   Tangent        line, circle/arc  |  circle/arc, circle/arc
//   Concentric     circle/arc, circle/arc (with different centre points)
//   Midpoint       point, line (not one of the line's own end points)
//   Symmetric      point, point, line (the symmetry axis)
//   Diameter       circle  |  arc                                      + value
//
// Referenced entities must exist and be distinct. A (line, point) distance
// or midpoint is stored as (point, line), a (circle/arc, line) tangent as
// (line, circle/arc), and a symmetric constraint with its line first as
// (point, point, line).
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
    /// The angle from the first line counter-clockwise to the second, as
    /// undirected lines: in (0, 180°), so reversing a line changes nothing.
    Angle,
    /// A line touches a circle or arc (the centre's distance from the line is
    /// the radius, on the side where the centre starts), or two circles or
    /// arcs touch: externally or internally, whichever the geometry is
    /// nearer when solving starts.
    Tangent,
    /// Two circles or arcs share a centre position.
    Concentric,
    /// A point lies halfway between a line's end points.
    Midpoint,
    /// Two points are mirror images across a line: their midpoint lies on the
    /// line and the segment joining them is perpendicular to it.
    Symmetric,
    /// Twice the radius of a circle or arc.
    Diameter,
};

[[nodiscard]] BETTERCAD_SKETCH_EXPORT std::string_view toString(ConstraintType type) noexcept;

/// True for constraint types that carry a length value (Distance, Radius,
/// Diameter).
[[nodiscard]] constexpr bool hasValue(ConstraintType type) noexcept {
    return type == ConstraintType::Distance || type == ConstraintType::Radius || type == ConstraintType::Diameter;
}

/// True for constraint types that carry an angle (Angle).
[[nodiscard]] constexpr bool hasAngle(ConstraintType type) noexcept {
    return type == ConstraintType::Angle;
}

/// True for constraint types a document parameter can drive: a length
/// parameter for hasValue() types, an angle parameter for hasAngle() types.
[[nodiscard]] constexpr bool isDrivable(ConstraintType type) noexcept {
    return hasValue(type) || hasAngle(type);
}

struct Constraint {
    ConstraintId id{};
    ConstraintType type = ConstraintType::Coincident;
    /// Referenced entities, in the order given by the type's signature.
    std::vector<EntityId> entities{};
    /// Target value for Distance, Radius and Diameter; empty for the other
    /// types.
    std::optional<Length> value{};
    /// Target angle for Angle; empty for the other types.
    std::optional<Angle> angle{};
    /// Document parameter that drives the value (or angle), if any.
    /// Regeneration copies the parameter's value into `value` (or `angle`)
    /// before solving.
    std::optional<ParameterId> parameter{};
    /// Disabled constraints are kept but ignored by the solver.
    bool enabled = true;

    friend bool operator==(const Constraint&, const Constraint&) = default;
};

} // namespace bettercad::sketch
