// Constraint part of Sketch: creation, reference validation and editing.
#include <bettercad/core/units/Format.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <algorithm>
#include <format>
#include <string>
#include <utility>

namespace bettercad::sketch {

namespace {

std::unexpected<Error> constraintNotFound(ConstraintId id) {
    return makeError(ErrorCode::NotFound, std::format("{} does not exist in this sketch", id));
}

std::string describeTypes(const std::vector<EntityType>& types) {
    std::string text;
    for (const EntityType type : types) {
        if (!text.empty()) {
            text += ", ";
        }
        text += toString(type);
    }
    return text.empty() ? std::string{"nothing"} : text;
}

bool isRound(EntityType type) noexcept {
    return type == EntityType::Circle || type == EntityType::Arc;
}

using Types = std::vector<EntityType>;

} // namespace

std::string_view toString(ConstraintType type) noexcept {
    switch (type) {
    case ConstraintType::Coincident:
        return "coincident";
    case ConstraintType::Horizontal:
        return "horizontal";
    case ConstraintType::Vertical:
        return "vertical";
    case ConstraintType::Parallel:
        return "parallel";
    case ConstraintType::Perpendicular:
        return "perpendicular";
    case ConstraintType::Distance:
        return "distance";
    case ConstraintType::Radius:
        return "radius";
    case ConstraintType::Equal:
        return "equal";
    case ConstraintType::Fixed:
        return "fixed";
    }
    return "unknown";
}

Result<void> Sketch::checkConstraint(const Constraint& constraint) const {
    const std::string_view name = toString(constraint.type);

    // Value and driving parameter.
    if (hasValue(constraint.type)) {
        if (!constraint.value || !isFinite(*constraint.value)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("a {} constraint needs a finite value", name));
        }
    } else if (constraint.value || constraint.parameter) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("a {} constraint does not take a value", name));
    }

    // References exist and are distinct.
    Types types;
    for (std::size_t i = 0; i < constraint.entities.size(); ++i) {
        const EntityId id = constraint.entities[i];
        const Entity* entity = findEntity(id);
        if (entity == nullptr) {
            return makeError(ErrorCode::NotFound,
                             std::format("{} constraint references {}, which does not exist", name, id));
        }
        if (std::find(constraint.entities.begin(), constraint.entities.begin() + static_cast<std::ptrdiff_t>(i), id) !=
            constraint.entities.begin() + static_cast<std::ptrdiff_t>(i)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} constraint references {} twice", name, id));
        }
        types.push_back(entity->type());
    }

    // Signature and value range per type.
    const auto signatureError = [&](std::string_view expected) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("a {} constraint takes {}, got {}", name, expected, describeTypes(types)));
    };
    const Types point2{EntityType::Point, EntityType::Point};
    const Types line1{EntityType::Line};
    const Types line2{EntityType::Line, EntityType::Line};

    switch (constraint.type) {
    case ConstraintType::Coincident:
        if (types != point2) {
            return signatureError("two points");
        }
        break;
    case ConstraintType::Horizontal:
    case ConstraintType::Vertical:
        if (types != line1 && types != point2) {
            return signatureError("a line or two points");
        }
        break;
    case ConstraintType::Parallel:
    case ConstraintType::Perpendicular:
        if (types != line2) {
            return signatureError("two lines");
        }
        break;
    case ConstraintType::Distance: {
        const bool pointLine = types == Types{EntityType::Point, EntityType::Line};
        if (types != point2 && types != line1 && !pointLine) {
            return signatureError("two points, a line, or a point and a line");
        }
        // A point may lie on a line (distance 0); coincident points and
        // zero-length lines are expressed by other means.
        const Length minimum = pointLine ? Length{} : kLengthTolerance;
        if (pointLine ? *constraint.value < minimum : *constraint.value <= minimum) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("distance {} is out of range", toString(*constraint.value, units::mm)));
        }
        break;
    }
    case ConstraintType::Radius:
        if (types.size() != 1 || !isRound(types.front())) {
            return signatureError("a circle or an arc");
        }
        if (*constraint.value <= kLengthTolerance) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("radius {} must be positive", toString(*constraint.value, units::mm)));
        }
        break;
    case ConstraintType::Equal:
        if (types != line2 && !(types.size() == 2 && isRound(types[0]) && isRound(types[1]))) {
            return signatureError("two lines or two circles/arcs");
        }
        break;
    case ConstraintType::Fixed:
        if (types != Types{EntityType::Point}) {
            return signatureError("a point");
        }
        break;
    }
    return {};
}

Result<ConstraintId> Sketch::addConstraint(ConstraintType type, std::vector<EntityId> entities,
                                           std::optional<Length> value) {
    // Canonical order for point-line distances: (point, line).
    if (type == ConstraintType::Distance && entities.size() == 2 &&
        entityType(entities[0]) == EntityType::Line && entityType(entities[1]) == EntityType::Point) {
        std::swap(entities[0], entities[1]);
    }
    Constraint constraint{.id = {},
                          .type = type,
                          .entities = std::move(entities),
                          .value = value,
                          .parameter = std::nullopt,
                          .enabled = true};
    if (auto valid = checkConstraint(constraint); !valid) {
        return std::unexpected(valid.error());
    }
    constraint.id = constraintIds_.allocate<ConstraintId>();
    const ConstraintId id = constraint.id;
    constraints_.emplace(id, std::move(constraint));
    return id;
}

Result<ConstraintId> Sketch::addCoincident(EntityId pointA, EntityId pointB) {
    return addConstraint(ConstraintType::Coincident, {pointA, pointB});
}
Result<ConstraintId> Sketch::addHorizontal(EntityId line) {
    return addConstraint(ConstraintType::Horizontal, {line});
}
Result<ConstraintId> Sketch::addHorizontal(EntityId pointA, EntityId pointB) {
    return addConstraint(ConstraintType::Horizontal, {pointA, pointB});
}
Result<ConstraintId> Sketch::addVertical(EntityId line) {
    return addConstraint(ConstraintType::Vertical, {line});
}
Result<ConstraintId> Sketch::addVertical(EntityId pointA, EntityId pointB) {
    return addConstraint(ConstraintType::Vertical, {pointA, pointB});
}
Result<ConstraintId> Sketch::addParallel(EntityId lineA, EntityId lineB) {
    return addConstraint(ConstraintType::Parallel, {lineA, lineB});
}
Result<ConstraintId> Sketch::addPerpendicular(EntityId lineA, EntityId lineB) {
    return addConstraint(ConstraintType::Perpendicular, {lineA, lineB});
}
Result<ConstraintId> Sketch::addDistance(EntityId line, Length value) {
    return addConstraint(ConstraintType::Distance, {line}, value);
}
Result<ConstraintId> Sketch::addDistance(EntityId a, EntityId b, Length value) {
    return addConstraint(ConstraintType::Distance, {a, b}, value);
}
Result<ConstraintId> Sketch::addRadius(EntityId circleOrArc, Length value) {
    return addConstraint(ConstraintType::Radius, {circleOrArc}, value);
}
Result<ConstraintId> Sketch::addEqual(EntityId a, EntityId b) {
    return addConstraint(ConstraintType::Equal, {a, b});
}
Result<ConstraintId> Sketch::addFixed(EntityId point) {
    return addConstraint(ConstraintType::Fixed, {point});
}

Result<void> Sketch::insertConstraint(Constraint constraint) {
    if (!constraint.id.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "an inserted constraint needs a valid ID");
    }
    if (constraints_.contains(constraint.id)) {
        return makeError(ErrorCode::AlreadyExists, std::format("{} already exists", constraint.id));
    }
    if (auto valid = checkConstraint(constraint); !valid) {
        return valid;
    }
    constraintIds_.reserveThrough(constraint.id.value());
    const ConstraintId id = constraint.id;
    constraints_.emplace(id, std::move(constraint));
    return {};
}

Result<Constraint*> Sketch::requireConstraint(ConstraintId id) {
    const auto it = constraints_.find(id);
    if (it == constraints_.end()) {
        return constraintNotFound(id);
    }
    return &it->second;
}

Result<Constraint> Sketch::removeConstraint(ConstraintId id) {
    auto node = constraints_.extract(id);
    if (node.empty()) {
        return constraintNotFound(id);
    }
    return std::move(node.mapped());
}

Result<bool> Sketch::setConstraintEnabled(ConstraintId id, bool enabled) {
    auto constraint = requireConstraint(id);
    if (!constraint) {
        return std::unexpected(constraint.error());
    }
    if ((*constraint)->enabled == enabled) {
        return false;
    }
    (*constraint)->enabled = enabled;
    return true;
}

Result<bool> Sketch::setConstraintValue(ConstraintId id, Length value) {
    auto constraint = requireConstraint(id);
    if (!constraint) {
        return std::unexpected(constraint.error());
    }
    Constraint updated = **constraint;
    updated.value = value;
    if (auto valid = checkConstraint(updated); !valid) {
        return std::unexpected(valid.error());
    }
    if ((*constraint)->value == value) {
        return false;
    }
    (*constraint)->value = value;
    return true;
}

Result<bool> Sketch::setConstraintParameter(ConstraintId id, std::optional<ParameterId> parameter) {
    auto constraint = requireConstraint(id);
    if (!constraint) {
        return std::unexpected(constraint.error());
    }
    if (!hasValue((*constraint)->type)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("a {} constraint has no value to drive", toString((*constraint)->type)));
    }
    if (parameter && !parameter->isValid()) {
        return makeError(ErrorCode::InvalidArgument, "the driving parameter ID must be valid");
    }
    if ((*constraint)->parameter == parameter) {
        return false;
    }
    (*constraint)->parameter = parameter;
    return true;
}

const Constraint* Sketch::findConstraint(ConstraintId id) const noexcept {
    const auto it = constraints_.find(id);
    return it == constraints_.end() ? nullptr : &it->second;
}

std::vector<ConstraintId> Sketch::constraintsReferencing(EntityId entity) const {
    std::vector<ConstraintId> result;
    for (const Constraint& constraint : constraints()) {
        if (std::ranges::find(constraint.entities, entity) != constraint.entities.end()) {
            result.push_back(constraint.id);
        }
    }
    return result;
}

} // namespace bettercad::sketch
