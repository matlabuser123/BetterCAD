#include <bettercad/sketch/Entities.hpp>

#include <cstddef>
#include <type_traits>

namespace bettercad::sketch {

namespace {

template <EntityType type, typename Alternative>
constexpr bool alternativeIs() {
    return std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(type), EntityGeometry>, Alternative>;
}

} // namespace

static_assert(alternativeIs<EntityType::Point, PointEntity>() && alternativeIs<EntityType::Line, LineEntity>() &&
                  alternativeIs<EntityType::Circle, CircleEntity>() && alternativeIs<EntityType::Arc, ArcEntity>() &&
                  alternativeIs<EntityType::Ellipse, EllipseEntity>() &&
                  alternativeIs<EntityType::Spline, SplineEntity>(),
              "EntityGeometry alternatives must follow EntityType order");

std::optional<std::array<EntityId, 2>> endPointIds(const Entity& entity) {
    if (const auto* line = std::get_if<LineEntity>(&entity.geometry)) {
        return std::array{line->start, line->end};
    }
    if (const auto* arc = std::get_if<ArcEntity>(&entity.geometry)) {
        return std::array{arc->start, arc->end};
    }
    if (const auto* spline = std::get_if<SplineEntity>(&entity.geometry);
        spline != nullptr && !spline->periodic && !spline->poles.empty()) {
        return std::array{spline->poles.front(), spline->poles.back()};
    }
    return std::nullopt;
}

std::string_view toString(EntityType type) noexcept {
    switch (type) {
    case EntityType::Point:
        return "point";
    case EntityType::Line:
        return "line";
    case EntityType::Circle:
        return "circle";
    case EntityType::Arc:
        return "arc";
    case EntityType::Ellipse:
        return "ellipse";
    case EntityType::Spline:
        return "spline";
    }
    return "unknown";
}

} // namespace bettercad::sketch
