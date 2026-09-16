// Constraint part of Sketch: creation, reference validation and editing.
#include <bettercad/core/units/Format.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <algorithm>
#include <format>
#include <numbers>
#include <string>
#include <utility>

namespace bettercad::sketch {

namespace {

// The largest angle an Angle constraint takes (exclusive): lines have no
// direction, so 180 deg is 0 deg.
constexpr Angle kHalfTurn = Angle::fromSi(std::numbers::pi);

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

/// "a distance constraint", "an angle constraint".
std::string constraintNoun(ConstraintType type) {
    const std::string_view name = toString(type);
    const bool vowel = !name.empty() && std::string_view{"aeiou"}.find(name.front()) != std::string_view::npos;
    return std::format("{} {} constraint", vowel ? "an" : "a", name);
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
    case ConstraintType::Angle:
        return "angle";
    case ConstraintType::Tangent:
        return "tangent";
    case ConstraintType::Concentric:
        return "concentric";
    case ConstraintType::Midpoint:
        return "midpoint";
    case ConstraintType::Symmetric:
        return "symmetric";
    case ConstraintType::Diameter:
        return "diameter";
    }
    return "unknown";
}

void Sketch::canonicalize(Constraint& constraint) const {
    std::vector<EntityId>& entities = constraint.entities;
    const auto is = [&](std::size_t i, EntityType type) { return entityType(entities[i]) == type; };
    const auto round = [&](std::size_t i) {
        const auto type = entityType(entities[i]);
        return type && isRound(*type);
    };
    switch (constraint.type) {
    case ConstraintType::Distance:
    case ConstraintType::Midpoint:
        // (line, point) -> (point, line)
        if (entities.size() == 2 && is(0, EntityType::Line) && is(1, EntityType::Point)) {
            std::swap(entities[0], entities[1]);
        }
        break;
    case ConstraintType::Tangent:
        // (circle/arc, line) -> (line, circle/arc)
        if (entities.size() == 2 && round(0) && is(1, EntityType::Line)) {
            std::swap(entities[0], entities[1]);
        }
        break;
    case ConstraintType::Symmetric: {
        // One line and two points: the line goes last, the points keep their order.
        if (entities.size() != 3) {
            break;
        }
        const auto lines = std::ranges::count_if(entities, [&](EntityId id) {
            return entityType(id) == EntityType::Line;
        });
        const auto points = std::ranges::count_if(entities, [&](EntityId id) {
            return entityType(id) == EntityType::Point;
        });
        if (lines == 1 && points == 2) {
            const auto line = std::ranges::find_if(entities, [&](EntityId id) {
                return entityType(id) == EntityType::Line;
            });
            const EntityId axis = *line;
            entities.erase(line);
            entities.push_back(axis);
        }
        break;
    }
    default:
        break;
    }
}

Result<void> Sketch::checkConstraint(const Constraint& constraint) const {
    const std::string noun = constraintNoun(constraint.type);

    // Value or angle, and the driving parameter.
    if (hasValue(constraint.type)) {
        if (constraint.angle) {
            return makeError(ErrorCode::InvalidArgument, std::format("{} does not take an angle", noun));
        }
        if (!constraint.value || !isFinite(*constraint.value)) {
            return makeError(ErrorCode::InvalidArgument, std::format("{} needs a finite value", noun));
        }
    } else if (hasAngle(constraint.type)) {
        if (constraint.value) {
            return makeError(ErrorCode::InvalidArgument, std::format("{} takes an angle, not a length", noun));
        }
        if (!constraint.angle || !(*constraint.angle > Angle{}) || !(*constraint.angle < kHalfTurn)) {
            const std::string got =
                constraint.angle ? std::format("{:.10g} deg", constraint.angle->in(units::deg)) : std::string{"none"};
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} needs an angle between 0 and 180 deg (exclusive), got {}", noun, got));
        }
    } else {
        if (constraint.angle) {
            return makeError(ErrorCode::InvalidArgument, std::format("{} does not take an angle", noun));
        }
        if (constraint.value || constraint.parameter) {
            return makeError(ErrorCode::InvalidArgument, std::format("{} does not take a value", noun));
        }
    }
    return checkReferences(constraint);
}

Result<void> Sketch::checkReferences(const Constraint& constraint) const {
    const std::string noun = constraintNoun(constraint.type);

    // References exist and are distinct.
    Types types;
    for (std::size_t i = 0; i < constraint.entities.size(); ++i) {
        const EntityId id = constraint.entities[i];
        const Entity* entity = findEntity(id);
        if (entity == nullptr) {
            return makeError(ErrorCode::NotFound,
                             std::format("{} constraint references {}, which does not exist",
                                         toString(constraint.type), id));
        }
        if (std::find(constraint.entities.begin(), constraint.entities.begin() + static_cast<std::ptrdiff_t>(i), id) !=
            constraint.entities.begin() + static_cast<std::ptrdiff_t>(i)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} constraint references {} twice", toString(constraint.type), id));
        }
        types.push_back(entity->type());
    }

    // Signature and value range per type.
    const auto signatureError = [&](std::string_view expected) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} takes {}, got {}", noun, expected, describeTypes(types)));
    };
    const auto centreOf = [&](EntityId id) {
        const EntityGeometry& geometry = findEntity(id)->geometry;
        if (const auto* circle = std::get_if<CircleEntity>(&geometry)) {
            return circle->center;
        }
        return std::get<ArcEntity>(geometry).center;
    };
    const Types point2{EntityType::Point, EntityType::Point};
    const Types line1{EntityType::Line};
    const Types line2{EntityType::Line, EntityType::Line};
    const bool twoRound = types.size() == 2 && isRound(types[0]) && isRound(types[1]);

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
        if (types != line2 && !twoRound) {
            return signatureError("two lines or two circles/arcs");
        }
        break;
    case ConstraintType::Fixed:
        if (types != Types{EntityType::Point}) {
            return signatureError("a point");
        }
        break;
    case ConstraintType::Angle:
        if (types != line2) {
            return signatureError("two lines");
        }
        break;
    case ConstraintType::Tangent: {
        const bool lineRound = types.size() == 2 && types[0] == EntityType::Line && isRound(types[1]);
        if (!lineRound && !twoRound) {
            return signatureError("a line and a circle or arc, or two circles or arcs");
        }
        break;
    }
    case ConstraintType::Concentric: {
        if (!twoRound) {
            return signatureError("two circles or arcs");
        }
        const EntityId a = centreOf(constraint.entities[0]);
        if (a == centreOf(constraint.entities[1])) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} and {} already share their centre point {}", constraint.entities[0],
                                         constraint.entities[1], a));
        }
        break;
    }
    case ConstraintType::Midpoint: {
        if (types != Types{EntityType::Point, EntityType::Line}) {
            return signatureError("a point and a line");
        }
        const auto& line = std::get<LineEntity>(findEntity(constraint.entities[1])->geometry);
        if (line.start == constraint.entities[0] || line.end == constraint.entities[0]) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} is an end point of {}; it cannot also be its midpoint",
                                         constraint.entities[0], constraint.entities[1]));
        }
        break;
    }
    case ConstraintType::Symmetric:
        if (types != Types{EntityType::Point, EntityType::Point, EntityType::Line}) {
            return signatureError("two points and a line");
        }
        break;
    case ConstraintType::Diameter:
        if (types.size() != 1 || !isRound(types.front())) {
            return signatureError("a circle or an arc");
        }
        if (*constraint.value <= 2.0 * kLengthTolerance) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("diameter {} must be positive", toString(*constraint.value, units::mm)));
        }
        break;
    }
    return {};
}

Result<ConstraintId> Sketch::addConstraint(ConstraintType type, std::vector<EntityId> entities,
                                           std::optional<Length> value, std::optional<Angle> angle) {
    Constraint constraint{.id = {},
                          .type = type,
                          .entities = std::move(entities),
                          .value = value,
                          .angle = angle,
                          .parameter = std::nullopt,
                          .enabled = true};
    canonicalize(constraint);
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
Result<ConstraintId> Sketch::addAngle(EntityId lineA, EntityId lineB, Angle value) {
    return addConstraint(ConstraintType::Angle, {lineA, lineB}, std::nullopt, value);
}
Result<ConstraintId> Sketch::addTangent(EntityId a, EntityId b) {
    return addConstraint(ConstraintType::Tangent, {a, b});
}
Result<ConstraintId> Sketch::addConcentric(EntityId circleOrArcA, EntityId circleOrArcB) {
    return addConstraint(ConstraintType::Concentric, {circleOrArcA, circleOrArcB});
}
Result<ConstraintId> Sketch::addMidpoint(EntityId a, EntityId b) {
    return addConstraint(ConstraintType::Midpoint, {a, b});
}
Result<ConstraintId> Sketch::addSymmetric(EntityId pointA, EntityId pointB, EntityId line) {
    return addConstraint(ConstraintType::Symmetric, {pointA, pointB, line});
}
Result<ConstraintId> Sketch::addDiameter(EntityId circleOrArc, Length value) {
    return addConstraint(ConstraintType::Diameter, {circleOrArc}, value);
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

Result<bool> Sketch::setConstraintAngle(ConstraintId id, Angle value) {
    auto constraint = requireConstraint(id);
    if (!constraint) {
        return std::unexpected(constraint.error());
    }
    Constraint updated = **constraint;
    updated.angle = value;
    if (auto valid = checkConstraint(updated); !valid) {
        return std::unexpected(valid.error());
    }
    if ((*constraint)->angle == value) {
        return false;
    }
    (*constraint)->angle = value;
    return true;
}

Result<bool> Sketch::setConstraintParameter(ConstraintId id, std::optional<ParameterId> parameter) {
    auto constraint = requireConstraint(id);
    if (!constraint) {
        return std::unexpected(constraint.error());
    }
    if (!isDrivable((*constraint)->type)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} has no value to drive", constraintNoun((*constraint)->type)));
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
