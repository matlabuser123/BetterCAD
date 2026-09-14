#include <bettercad/sketch/Entities.hpp>

#include <cstddef>
#include <type_traits>

namespace bettercad::sketch {

static_assert(std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(EntityType::Point),
                                                        EntityGeometry>,
                             PointEntity> &&
                  std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(EntityType::Line),
                                                            EntityGeometry>,
                                 LineEntity> &&
                  std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(EntityType::Circle),
                                                            EntityGeometry>,
                                 CircleEntity> &&
                  std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(EntityType::Arc),
                                                            EntityGeometry>,
                                 ArcEntity>,
              "EntityGeometry alternatives must follow EntityType order");

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
    }
    return "unknown";
}

} // namespace bettercad::sketch
