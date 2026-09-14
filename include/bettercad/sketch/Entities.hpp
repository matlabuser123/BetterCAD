#pragma once

#include <bettercad/core/Id.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/sketch/Export.hpp>

#include <string_view>
#include <variant>

// Sketch entities. Points are entities of their own; lines, circles and arcs
// reference point entities. Connected geometry shares points, and constraints
// (P5) reference whole entities only.
namespace bettercad::sketch {

enum class EntityType {
    Point,
    Line,
    Circle,
    Arc,
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

/// Alternatives are in EntityType order.
using EntityGeometry = std::variant<PointEntity, LineEntity, CircleEntity, ArcEntity>;

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

/// Start and end point of a line or arc.
struct Endpoints {
    Point2D start{};
    Point2D end{};

    friend constexpr bool operator==(const Endpoints&, const Endpoints&) = default;
};

} // namespace bettercad::sketch
