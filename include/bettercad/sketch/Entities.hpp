#pragma once

#include <bettercad/core/Id.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/sketch/Export.hpp>

#include <array>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

// Sketch entities. Points are entities of their own; lines, circles, arcs,
// ellipses and splines reference point entities. Connected geometry shares
// points, and constraints (P5) reference whole entities only.
namespace bettercad::sketch {

enum class EntityType {
    Point,
    Line,
    Circle,
    Arc,
    Ellipse, ///< P12-SKETCH-002
    Spline,  ///< P12-SKETCH-002
};

[[nodiscard]] BETTERCAD_SKETCH_EXPORT std::string_view toString(EntityType type) noexcept;

struct PointEntity {
    Point2D position{};

    friend constexpr bool operator==(const PointEntity&, const PointEntity&) = default;
};

/// Straight segment between two point entities.
struct LineEntity {
    EntityId start{};
    EntityId end{};

    friend constexpr bool operator==(const LineEntity&, const LineEntity&) = default;
};

/// Circle around a centre point entity.
struct CircleEntity {
    EntityId center{};
    Length radius{};

    friend constexpr bool operator==(const CircleEntity&, const CircleEntity&) = default;
};

/// Counter-clockwise arc around a centre point entity, from a start point
/// entity to an end point entity. The radius is |start - center|; the end
/// point lies on the same circle.
struct ArcEntity {
    EntityId center{};
    EntityId start{};
    EntityId end{};

    friend constexpr bool operator==(const ArcEntity&, const ArcEntity&) = default;
};

/// A full ellipse around a centre point entity. Its two semi-axes end at
/// the point entities xVertex and yVertex, which lie on the ellipse; the
/// solver keeps the axes perpendicular. Either axis may be the longer one.
/// Distances from the centre to the vertices size the ellipse, and a
/// horizontal or vertical constraint on (center, xVertex) orients it.
struct EllipseEntity {
    EntityId center{};
    EntityId xVertex{};
    EntityId yVertex{};

    friend constexpr bool operator==(const EllipseEntity&, const EllipseEntity&) = default;
};

/// A non-rational B-spline with uniform knots, of degree 2 to 5, whose poles
/// (control points) are point entities. An open spline starts at its first
/// pole and ends at its last, so it joins other entities through them; a
/// periodic spline is a closed, smooth curve on its own. The curve passes
/// through no other pole (see geometry::SplineSegment2D).
struct SplineEntity {
    std::vector<EntityId> poles{};
    int degree = 3;
    bool periodic = false;

    friend bool operator==(const SplineEntity&, const SplineEntity&) = default;
};

/// Alternatives are in EntityType order.
using EntityGeometry = std::variant<PointEntity, LineEntity, CircleEntity, ArcEntity, EllipseEntity, SplineEntity>;

/// An entity stored in a sketch.
struct Entity {
    EntityId id{};
    EntityGeometry geometry{};
    /// Construction geometry helps to constrain a sketch but is not part of
    /// the profiles used by features.
    bool construction = false;

    [[nodiscard]] constexpr EntityType type() const noexcept {
        return static_cast<EntityType>(geometry.index());
    }

    friend bool operator==(const Entity&, const Entity&) = default;
};

/// The point entities where a line, an arc or an open spline starts and
/// ends; std::nullopt for points, circles, ellipses and periodic splines.
[[nodiscard]] BETTERCAD_SKETCH_EXPORT std::optional<std::array<EntityId, 2>> endPointIds(const Entity& entity);

/// Start and end point of a line, an arc or an open spline.
struct Endpoints {
    Point2D start{};
    Point2D end{};

    friend constexpr bool operator==(const Endpoints&, const Endpoints&) = default;
};

} // namespace bettercad::sketch
