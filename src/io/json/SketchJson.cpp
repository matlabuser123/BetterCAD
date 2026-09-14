#include "io/json/ObjectJson.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <vector>

namespace bettercad::io::detail {

namespace {

Json pointToJson(const Point2D& p) {
    return Json::array({p.x.si(), p.y.si()});
}

constexpr std::array kEntityTypes{sketch::EntityType::Point, sketch::EntityType::Line,
                                  sketch::EntityType::Circle, sketch::EntityType::Arc};
constexpr std::array kConstraintTypes{
    sketch::ConstraintType::Coincident, sketch::ConstraintType::Horizontal,
    sketch::ConstraintType::Vertical,   sketch::ConstraintType::Parallel,
    sketch::ConstraintType::Perpendicular, sketch::ConstraintType::Distance,
    sketch::ConstraintType::Radius,     sketch::ConstraintType::Equal,
    sketch::ConstraintType::Fixed,
};

template <typename Enum, std::size_t N>
Result<Enum> enumFromJson(const std::array<Enum, N>& values, const Json& object, std::string_view key,
                          std::string_view path) {
    auto text = readString(object, key, path);
    if (!text) {
        return std::unexpected(text.error());
    }
    for (const Enum value : values) {
        if (sketch::toString(value) == *text) {
            return value;
        }
    }
    return parseError(childPath(path, key), std::format("unknown type '{}'", *text));
}

Json entityToJson(const sketch::Entity& entity) {
    Json json = Json::object();
    json["id"] = entity.id.value();
    json["type"] = std::string{sketch::toString(entity.type())};
    if (const auto* point = std::get_if<sketch::PointEntity>(&entity.geometry)) {
        json["position"] = pointToJson(point->position);
    } else if (const auto* line = std::get_if<sketch::LineEntity>(&entity.geometry)) {
        json["start"] = line->start.value();
        json["end"] = line->end.value();
    } else if (const auto* circle = std::get_if<sketch::CircleEntity>(&entity.geometry)) {
        json["center"] = circle->center.value();
        json["radius"] = circle->radius.si();
    } else {
        const auto& arc = std::get<sketch::ArcEntity>(entity.geometry);
        json["center"] = arc.center.value();
        json["start"] = arc.start.value();
        json["end"] = arc.end.value();
    }
    json["construction"] = entity.construction;
    return json;
}

Result<sketch::Entity> entityFromJson(const Json& value, std::string_view path) {
    if (!value.is_object()) {
        return parseError(path, "expected an object");
    }
    auto type = enumFromJson(kEntityTypes, value, "type", path);
    if (!type) {
        return std::unexpected(type.error());
    }
    Result<void> fields;
    switch (*type) {
    case sketch::EntityType::Point:
        fields = requireObject(value, path, {"id", "type", "position", "construction"});
        break;
    case sketch::EntityType::Line:
        fields = requireObject(value, path, {"id", "type", "start", "end", "construction"});
        break;
    case sketch::EntityType::Circle:
        fields = requireObject(value, path, {"id", "type", "center", "radius", "construction"});
        break;
    case sketch::EntityType::Arc:
        fields = requireObject(value, path, {"id", "type", "center", "start", "end", "construction"});
        break;
    }
    if (!fields) {
        return std::unexpected(fields.error());
    }
    auto id = readId(value, "id", path);
    auto construction = readBool(value, "construction", path);
    if (!id || !construction) {
        return std::unexpected(!id ? id.error() : construction.error());
    }
    sketch::Entity entity{.id = EntityId::fromValue(*id), .geometry = {}, .construction = *construction};
    const auto reference = [&](std::string_view key) -> Result<EntityId> {
        auto ref = readId(value, key, path);
        if (!ref) {
            return std::unexpected(ref.error());
        }
        return EntityId::fromValue(*ref);
    };
    switch (*type) {
    case sketch::EntityType::Point: {
        auto position = readNumbers(value, "position", path, 2);
        if (!position) {
            return std::unexpected(position.error());
        }
        entity.geometry = sketch::PointEntity{
            Point2D{Length::fromSi((*position)[0]), Length::fromSi((*position)[1])}};
        break;
    }
    case sketch::EntityType::Line: {
        auto start = reference("start");
        auto end = reference("end");
        if (!start || !end) {
            return std::unexpected(!start ? start.error() : end.error());
        }
        entity.geometry = sketch::LineEntity{*start, *end};
        break;
    }
    case sketch::EntityType::Circle: {
        auto center = reference("center");
        auto radius = readNumber(value, "radius", path);
        if (!center || !radius) {
            return std::unexpected(!center ? center.error() : radius.error());
        }
        entity.geometry = sketch::CircleEntity{*center, Length::fromSi(*radius)};
        break;
    }
    case sketch::EntityType::Arc: {
        auto center = reference("center");
        auto start = reference("start");
        auto end = reference("end");
        if (!center || !start || !end) {
            return std::unexpected(!center ? center.error() : !start ? start.error() : end.error());
        }
        entity.geometry = sketch::ArcEntity{*center, *start, *end};
        break;
    }
    }
    return entity;
}

Json constraintToJson(const sketch::Constraint& constraint) {
    Json json = Json::object();
    json["id"] = constraint.id.value();
    json["type"] = std::string{sketch::toString(constraint.type)};
    Json entities = Json::array();
    for (const EntityId entity : constraint.entities) {
        entities.push_back(entity.value());
    }
    json["entities"] = std::move(entities);
    if (constraint.value) {
        json["value"] = constraint.value->si();
    }
    if (constraint.parameter) {
        json["parameter"] = constraint.parameter->value();
    }
    json["enabled"] = constraint.enabled;
    return json;
}

Result<sketch::Constraint> constraintFromJson(const Json& value, std::string_view path) {
    if (auto object = requireObject(value, path, {"id", "type", "entities", "value", "parameter", "enabled"});
        !object) {
        return std::unexpected(object.error());
    }
    auto id = readId(value, "id", path);
    auto type = enumFromJson(kConstraintTypes, value, "type", path);
    auto entities = requireArray(value, "entities", path);
    auto enabled = readBool(value, "enabled", path);
    auto parameter = readOptionalId(value, "parameter", path);
    if (!id || !type || !entities || !enabled || !parameter) {
        const Error& error = !id ? id.error()
                           : !type ? type.error()
                           : !entities ? entities.error()
                           : !enabled ? enabled.error()
                                      : parameter.error();
        return std::unexpected(error);
    }
    sketch::Constraint constraint{.id = ConstraintId::fromValue(*id), .type = *type};
    const std::string entitiesPath = childPath(path, "entities");
    for (std::size_t i = 0; i < (*entities)->size(); ++i) {
        auto entity = readId((**entities)[i], indexPath(entitiesPath, i));
        if (!entity) {
            return std::unexpected(entity.error());
        }
        constraint.entities.push_back(EntityId::fromValue(*entity));
    }
    if (value.contains("value")) {
        auto number = readNumber(value, "value", path);
        if (!number) {
            return std::unexpected(number.error());
        }
        constraint.value = Length::fromSi(*number);
    }
    if (*parameter) {
        constraint.parameter = ParameterId::fromValue(**parameter);
    }
    constraint.enabled = *enabled;
    return constraint;
}

} // namespace

Json pointToJson(const Point3D& point) {
    return Json::array({point.x.si(), point.y.si(), point.z.si()});
}

Result<Point3D> pointFromJson(const Json& object, std::string_view key, std::string_view path) {
    auto values = readNumbers(object, key, path, 3);
    if (!values) {
        return std::unexpected(values.error());
    }
    return Point3D{Length::fromSi((*values)[0]), Length::fromSi((*values)[1]), Length::fromSi((*values)[2])};
}

Json directionToJson(const Direction3D& direction) {
    return Json::array({direction.x(), direction.y(), direction.z()});
}

Result<Direction3D> directionFromJson(const Json& object, std::string_view key, std::string_view path) {
    auto values = readNumbers(object, key, path, 3);
    if (!values) {
        return std::unexpected(values.error());
    }
    const auto direction = Direction3D::fromUnitComponents((*values)[0], (*values)[1], (*values)[2]);
    if (!direction) {
        return parseError(childPath(path, key), "expected a unit vector");
    }
    return *direction;
}

Json frameToJson(const Frame3D& frame) {
    Json json = Json::object();
    json["origin"] = Json::array({frame.origin().x.si(), frame.origin().y.si(), frame.origin().z.si()});
    json["x_axis"] = directionToJson(frame.xAxis());
    json["y_axis"] = directionToJson(frame.yAxis());
    json["normal"] = directionToJson(frame.normal());
    return json;
}

Result<Frame3D> frameFromJson(const Json& value, std::string_view path) {
    if (auto object = requireObject(value, path, {"origin", "x_axis", "y_axis", "normal"}); !object) {
        return std::unexpected(object.error());
    }
    auto origin = readNumbers(value, "origin", path, 3);
    auto x = directionFromJson(value, "x_axis", path);
    auto y = directionFromJson(value, "y_axis", path);
    auto normal = directionFromJson(value, "normal", path);
    if (!origin || !x || !y || !normal) {
        return std::unexpected(!origin ? origin.error() : !x ? x.error() : !y ? y.error() : normal.error());
    }
    auto frame = Frame3D::fromAxes(Point3D{Length::fromSi((*origin)[0]), Length::fromSi((*origin)[1]),
                                           Length::fromSi((*origin)[2])},
                                   *x, *y, *normal);
    if (!frame) {
        return atPath(path, frame.error());
    }
    return frame;
}

Json sketchToJson(const sketch::Sketch& sketch) {
    Json json = Json::object();
    json["placement"] = frameToJson(sketch.placement());
    Json entities = Json::array();
    for (const sketch::Entity& entity : sketch.entities()) {
        entities.push_back(entityToJson(entity));
    }
    json["entities"] = std::move(entities);
    Json constraints = Json::array();
    for (const sketch::Constraint& constraint : sketch.constraints()) {
        constraints.push_back(constraintToJson(constraint));
    }
    json["constraints"] = std::move(constraints);
    json["last_entity_id"] = sketch.lastAllocatedEntityId();
    json["last_constraint_id"] = sketch.lastAllocatedConstraintId();
    return json;
}

Result<std::unique_ptr<sketch::Sketch>> sketchFromJson(const Json& data, std::string name,
                                                      std::string_view path) {
    if (auto object = requireObject(
            data, path, {"placement", "entities", "constraints", "last_entity_id", "last_constraint_id"});
        !object) {
        return std::unexpected(object.error());
    }
    auto placementField = requireField(data, "placement", path);
    if (!placementField) {
        return std::unexpected(placementField.error());
    }
    auto placement = frameFromJson(**placementField, childPath(path, "placement"));
    auto entities = requireArray(data, "entities", path);
    auto constraints = requireArray(data, "constraints", path);
    auto lastEntity = readId(data, "last_entity_id", path);
    auto lastConstraint = readId(data, "last_constraint_id", path);
    if (!placement || !entities || !constraints || !lastEntity || !lastConstraint) {
        const Error& error = !placement ? placement.error()
                           : !entities ? entities.error()
                           : !constraints ? constraints.error()
                           : !lastEntity ? lastEntity.error()
                                         : lastConstraint.error();
        return std::unexpected(error);
    }

    auto sketch = std::make_unique<sketch::Sketch>(std::move(name), *placement);
    // Points first, so every reference resolves regardless of file order.
    const std::string entitiesPath = childPath(path, "entities");
    std::vector<std::pair<std::size_t, sketch::Entity>> parsed;
    for (std::size_t i = 0; i < (*entities)->size(); ++i) {
        auto entity = entityFromJson((**entities)[i], indexPath(entitiesPath, i));
        if (!entity) {
            return std::unexpected(entity.error());
        }
        parsed.emplace_back(i, std::move(*entity));
    }
    [[maybe_unused]] const auto firstNonPoint = std::stable_partition(
        parsed.begin(), parsed.end(),
        [](const auto& item) { return item.second.type() == sketch::EntityType::Point; });
    for (const auto& [index, entity] : parsed) {
        if (auto inserted = sketch->insertEntity(entity); !inserted) {
            return atPath(indexPath(entitiesPath, index), inserted.error());
        }
    }
    const std::string constraintsPath = childPath(path, "constraints");
    for (std::size_t i = 0; i < (*constraints)->size(); ++i) {
        auto constraint = constraintFromJson((**constraints)[i], indexPath(constraintsPath, i));
        if (!constraint) {
            return std::unexpected(constraint.error());
        }
        if (auto inserted = sketch->insertConstraint(std::move(*constraint)); !inserted) {
            return atPath(indexPath(constraintsPath, i), inserted.error());
        }
    }
    // The counters record IDs handed out before (including deleted items), so
    // they can never be below an ID in use.
    if (*lastEntity < sketch->lastAllocatedEntityId()) {
        return parseError(childPath(path, "last_entity_id"), "less than an entity ID in use");
    }
    if (*lastConstraint < sketch->lastAllocatedConstraintId()) {
        return parseError(childPath(path, "last_constraint_id"), "less than a constraint ID in use");
    }
    sketch->reserveIds(*lastEntity, *lastConstraint);
    return sketch;
}

} // namespace bettercad::io::detail
