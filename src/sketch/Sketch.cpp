#include <bettercad/core/units/Format.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <utility>

namespace bettercad::sketch {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

std::unexpected<Error> notFound(EntityId id) {
    return makeError(ErrorCode::NotFound, std::format("{} does not exist in this sketch", id));
}

std::unexpected<Error> wrongType(const Entity& entity, std::string_view expected) {
    return makeError(ErrorCode::InvalidArgument,
                     std::format("{} is a {}, expected {}", entity.id, toString(entity.type()), expected));
}

Result<void> requireFinite(const Point2D& point) {
    if (!isFinite(point.x) || !isFinite(point.y)) {
        return makeError(ErrorCode::InvalidArgument, "sketch coordinates must be finite");
    }
    return {};
}

Result<void> requireRadius(Length radius) {
    if (!isFinite(radius) || radius <= Sketch::kLengthTolerance) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("radius must be finite and larger than {}, got {}",
                                     toString(Sketch::kLengthTolerance, units::mm),
                                     toString(radius, units::mm)));
    }
    return {};
}

/// Angle in [0, 2 pi).
double normalizeAngle(double angle) {
    double result = std::fmod(angle, kTwoPi);
    if (result < 0.0) {
        result += kTwoPi;
    }
    return result >= kTwoPi ? 0.0 : result;
}

/// Angle of the direction from @p from to @p to, in (-pi, pi].
double directionAngle(const Point2D& from, const Point2D& to) {
    return std::atan2((to.y - from.y).si(), (to.x - from.x).si());
}

Point2D onCircle(const Point2D& center, Length radius, Angle angle) {
    return {center.x + radius * cos(angle), center.y + radius * sin(angle)};
}

/// Calls @p fn for each point entity referenced by @p geometry.
template <typename Fn>
void forEachReferencedPoint(const EntityGeometry& geometry, Fn&& fn) {
    if (const auto* line = std::get_if<LineEntity>(&geometry)) {
        fn(line->start);
        fn(line->end);
    } else if (const auto* circle = std::get_if<CircleEntity>(&geometry)) {
        fn(circle->center);
    } else if (const auto* arc = std::get_if<ArcEntity>(&geometry)) {
        fn(arc->center);
        fn(arc->start);
        fn(arc->end);
    }
}

} // namespace

Sketch::Sketch(std::string name, const Frame3D& placement)
    : DocumentObject(std::move(name)), placement_(placement) {}

std::unique_ptr<DocumentObject> Sketch::clone() const {
    return std::make_unique<Sketch>(*this);
}

bool Sketch::contentEquals(const DocumentObject& other) const {
    const auto& sketch = static_cast<const Sketch&>(other);
    return placement_ == sketch.placement_ && entities_ == sketch.entities_ &&
           constraints_ == sketch.constraints_;
}

std::vector<ObjectId> Sketch::dependencies() const {
    std::vector<ObjectId> parameters;
    for (const Constraint& constraint : constraints()) {
        if (constraint.parameter) {
            parameters.push_back(ObjectId{*constraint.parameter});
        }
    }
    std::ranges::sort(parameters);
    const auto duplicates = std::ranges::unique(parameters);
    parameters.erase(duplicates.begin(), duplicates.end());
    return parameters;
}

Result<bool> Sketch::adoptSolution(const Sketch& solved) {
    const bool sameStructure =
        std::ranges::equal(entities(), solved.entities(),
                           [](const Entity& a, const Entity& b) { return a.id == b.id && a.type() == b.type(); }) &&
        std::ranges::equal(constraints(), solved.constraints(),
                           [](const Constraint& a, const Constraint& b) {
                               return a.id == b.id && a.type == b.type && a.entities == b.entities;
                           });
    if (!sameStructure) {
        return makeError(ErrorCode::FailedPrecondition,
                         "cannot adopt a solution from a sketch with different entities or constraints");
    }
    bool changed = false;
    for (auto& [id, entity] : entities_) {
        const Entity& source = *solved.findEntity(id);
        if (auto* point = std::get_if<PointEntity>(&entity.geometry)) {
            const Point2D position = std::get<PointEntity>(source.geometry).position;
            changed |= point->position != position;
            point->position = position;
        } else if (auto* circle = std::get_if<CircleEntity>(&entity.geometry)) {
            const Length radius = std::get<CircleEntity>(source.geometry).radius;
            changed |= circle->radius != radius;
            circle->radius = radius;
        }
    }
    for (auto& [id, constraint] : constraints_) {
        const Constraint& source = *solved.findConstraint(id);
        changed |= constraint.value != source.value || constraint.angle != source.angle;
        constraint.value = source.value;
        constraint.angle = source.angle;
    }
    return changed;
}

Result<bool> Sketch::restoreContent(const Sketch& state) {
    const bool changed = placement_ != state.placement_ || entities_ != state.entities_ ||
                         constraints_ != state.constraints_ ||
                         entityIds_.lastValue() != state.entityIds_.lastValue() ||
                         constraintIds_.lastValue() != state.constraintIds_.lastValue();
    if (!changed) {
        return false;
    }
    // Copy first, so a failed allocation leaves this sketch unchanged.
    std::map<EntityId, Entity> entities = state.entities_;
    std::map<ConstraintId, Constraint> constraints = state.constraints_;
    placement_ = state.placement_;
    entities_.swap(entities);
    constraints_.swap(constraints);
    entityIds_ = state.entityIds_;
    constraintIds_ = state.constraintIds_;
    return true;
}

Result<bool> Sketch::setPlacement(const Frame3D& placement) {
    if (placement == placement_) {
        return false;
    }
    placement_ = placement;
    return true;
}

// --- Lookup helpers ------------------------------------------------------------------

Result<const Entity*> Sketch::require(EntityId id) const {
    const Entity* entity = findEntity(id);
    if (entity == nullptr) {
        return notFound(id);
    }
    return entity;
}

Result<Point2D> Sketch::requirePoint(EntityId id) const {
    auto entity = require(id);
    if (!entity) {
        return std::unexpected(entity.error());
    }
    const auto* point = std::get_if<PointEntity>(&(*entity)->geometry);
    if (point == nullptr) {
        return wrongType(**entity, "a point");
    }
    return point->position;
}

EntityId Sketch::insert(EntityGeometry geometry) {
    const auto id = entityIds_.allocate<EntityId>();
    entities_.emplace(id, Entity{id, std::move(geometry), false});
    return id;
}

// --- Creating entities -------------------------------------------------------------------

Result<EntityId> Sketch::addPoint(const Point2D& position) {
    if (auto finite = requireFinite(position); !finite) {
        return std::unexpected(finite.error());
    }
    return insert(PointEntity{position});
}

Result<EntityId> Sketch::addLine(const Point2D& start, const Point2D& end) {
    if (auto finite = requireFinite(start); !finite) {
        return std::unexpected(finite.error());
    }
    if (auto finite = requireFinite(end); !finite) {
        return std::unexpected(finite.error());
    }
    if (distance(start, end) <= kLengthTolerance) {
        return makeError(ErrorCode::InvalidArgument, "a line must have non-zero length");
    }
    const EntityId startPoint = insert(PointEntity{start});
    const EntityId endPoint = insert(PointEntity{end});
    return insert(LineEntity{startPoint, endPoint});
}

Result<EntityId> Sketch::addLine(EntityId startPoint, EntityId endPoint) {
    if (startPoint == endPoint) {
        return makeError(ErrorCode::InvalidArgument, "a line needs two different points");
    }
    auto start = requirePoint(startPoint);
    if (!start) {
        return std::unexpected(start.error());
    }
    auto end = requirePoint(endPoint);
    if (!end) {
        return std::unexpected(end.error());
    }
    if (distance(*start, *end) <= kLengthTolerance) {
        return makeError(ErrorCode::InvalidArgument, "a line must have non-zero length");
    }
    return insert(LineEntity{startPoint, endPoint});
}

Result<EntityId> Sketch::addCircle(const Point2D& center, Length radius) {
    if (auto finite = requireFinite(center); !finite) {
        return std::unexpected(finite.error());
    }
    if (auto valid = requireRadius(radius); !valid) {
        return std::unexpected(valid.error());
    }
    const EntityId centerPoint = insert(PointEntity{center});
    return insert(CircleEntity{centerPoint, radius});
}

Result<EntityId> Sketch::addCircle(EntityId centerPoint, Length radius) {
    if (auto center = requirePoint(centerPoint); !center) {
        return std::unexpected(center.error());
    }
    if (auto valid = requireRadius(radius); !valid) {
        return std::unexpected(valid.error());
    }
    return insert(CircleEntity{centerPoint, radius});
}

Result<EntityId> Sketch::addArc(const Point2D& center, Length radius, Angle startAngle,
                                Angle endAngle) {
    if (auto finite = requireFinite(center); !finite) {
        return std::unexpected(finite.error());
    }
    if (auto valid = requireRadius(radius); !valid) {
        return std::unexpected(valid.error());
    }
    if (!isFinite(startAngle) || !isFinite(endAngle)) {
        return makeError(ErrorCode::InvalidArgument, "arc angles must be finite");
    }
    const double sweep = normalizeAngle((endAngle - startAngle).si());
    // Both the arc and its complement must be longer than the tolerance.
    if (radius * sweep <= kLengthTolerance || radius * (kTwoPi - sweep) <= kLengthTolerance) {
        return makeError(ErrorCode::InvalidArgument,
                         "an arc needs a sweep between zero and a full turn; use a circle for a full turn");
    }
    const EntityId centerPoint = insert(PointEntity{center});
    const EntityId startPoint = insert(PointEntity{onCircle(center, radius, startAngle)});
    const EntityId endPoint = insert(PointEntity{onCircle(center, radius, endAngle)});
    return insert(ArcEntity{centerPoint, startPoint, endPoint});
}

Result<EntityId> Sketch::addArc(EntityId centerPoint, EntityId startPoint, EntityId endPoint) {
    if (centerPoint == startPoint || centerPoint == endPoint || startPoint == endPoint) {
        return makeError(ErrorCode::InvalidArgument, "an arc needs three different points");
    }
    auto center = requirePoint(centerPoint);
    if (!center) {
        return std::unexpected(center.error());
    }
    auto start = requirePoint(startPoint);
    if (!start) {
        return std::unexpected(start.error());
    }
    auto end = requirePoint(endPoint);
    if (!end) {
        return std::unexpected(end.error());
    }
    const Length startRadius = distance(*center, *start);
    if (auto valid = requireRadius(startRadius); !valid) {
        return std::unexpected(valid.error());
    }
    const Length endRadius = distance(*center, *end);
    if (abs(endRadius - startRadius) > kLengthTolerance) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("arc start and end are at different distances from the centre "
                                     "({} and {})",
                                     toString(startRadius, units::mm), toString(endRadius, units::mm)));
    }
    if (distance(*start, *end) <= kLengthTolerance) {
        return makeError(ErrorCode::InvalidArgument, "arc start and end must differ; use a circle");
    }
    return insert(ArcEntity{centerPoint, startPoint, endPoint});
}

// --- Editing ------------------------------------------------------------------------------

Result<bool> Sketch::setPointPosition(EntityId point, const Point2D& position) {
    if (auto current = requirePoint(point); !current) {
        return std::unexpected(current.error());
    }
    if (auto finite = requireFinite(position); !finite) {
        return std::unexpected(finite.error());
    }
    auto& stored = std::get<PointEntity>(entities_.at(point).geometry);
    if (stored.position == position) {
        return false;
    }
    stored.position = position;
    return true;
}

Result<bool> Sketch::setCircleRadius(EntityId circle, Length radius) {
    auto entity = require(circle);
    if (!entity) {
        return std::unexpected(entity.error());
    }
    if (!std::holds_alternative<CircleEntity>((*entity)->geometry)) {
        return wrongType(**entity, "a circle");
    }
    if (auto valid = requireRadius(radius); !valid) {
        return std::unexpected(valid.error());
    }
    auto& stored = std::get<CircleEntity>(entities_.at(circle).geometry);
    if (stored.radius == radius) {
        return false;
    }
    stored.radius = radius;
    return true;
}

Result<bool> Sketch::setConstruction(EntityId entity, bool construction) {
    const auto it = entities_.find(entity);
    if (it == entities_.end()) {
        return notFound(entity);
    }
    if (it->second.construction == construction) {
        return false;
    }
    it->second.construction = construction;
    return true;
}

Result<void> Sketch::removeEntity(EntityId entity) {
    if (!entities_.contains(entity)) {
        return notFound(entity);
    }
    const std::vector<EntityId> users = dependentsOf(entity);
    if (!users.empty()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{} cannot be removed: it is used by {}", entity, users.front()));
    }
    const std::vector<ConstraintId> constrainedBy = constraintsReferencing(entity);
    if (!constrainedBy.empty()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{} cannot be removed: it is referenced by {}", entity,
                                     constrainedBy.front()));
    }
    entities_.erase(entity);
    return {};
}

Result<void> Sketch::insertEntity(const Entity& entity) {
    if (!entity.id.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "an inserted entity needs a valid ID");
    }
    if (entities_.contains(entity.id)) {
        return makeError(ErrorCode::AlreadyExists, std::format("{} already exists", entity.id));
    }
    std::vector<EntityId> references;
    forEachReferencedPoint(entity.geometry, [&](EntityId id) { references.push_back(id); });
    for (std::size_t i = 0; i < references.size(); ++i) {
        if (auto point = requirePoint(references[i]); !point) {
            return std::unexpected(point.error());
        }
        if (std::find(references.begin(), references.begin() + static_cast<std::ptrdiff_t>(i), references[i]) !=
            references.begin() + static_cast<std::ptrdiff_t>(i)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} references {} twice", entity.id, references[i]));
        }
    }
    if (const auto* point = std::get_if<PointEntity>(&entity.geometry)) {
        if (auto finite = requireFinite(point->position); !finite) {
            return finite;
        }
    } else if (const auto* circle = std::get_if<CircleEntity>(&entity.geometry)) {
        if (auto valid = requireRadius(circle->radius); !valid) {
            return valid;
        }
    }
    entities_.emplace(entity.id, entity);
    entityIds_.reserveThrough(entity.id.value());
    return {};
}

// --- Queries --------------------------------------------------------------------------------

const Entity* Sketch::findEntity(EntityId id) const noexcept {
    const auto it = entities_.find(id);
    return it == entities_.end() ? nullptr : &it->second;
}

std::optional<EntityType> Sketch::entityType(EntityId id) const noexcept {
    const Entity* entity = findEntity(id);
    return entity == nullptr ? std::nullopt : std::optional<EntityType>{entity->type()};
}

std::vector<EntityId> Sketch::dependentsOf(EntityId point) const {
    std::vector<EntityId> users;
    for (const Entity& entity : entities()) {
        bool uses = false;
        forEachReferencedPoint(entity.geometry, [&](EntityId referenced) { uses |= referenced == point; });
        if (uses) {
            users.push_back(entity.id);
        }
    }
    return users;
}

Result<Point2D> Sketch::position(EntityId point) const {
    return requirePoint(point);
}

Result<Endpoints> Sketch::endpoints(EntityId lineOrArc) const {
    auto entity = require(lineOrArc);
    if (!entity) {
        return std::unexpected(entity.error());
    }
    EntityId startId;
    EntityId endId;
    if (const auto* line = std::get_if<LineEntity>(&(*entity)->geometry)) {
        startId = line->start;
        endId = line->end;
    } else if (const auto* arc = std::get_if<ArcEntity>(&(*entity)->geometry)) {
        startId = arc->start;
        endId = arc->end;
    } else {
        return wrongType(**entity, "a line or an arc");
    }
    auto start = requirePoint(startId);
    auto end = requirePoint(endId);
    if (!start || !end) {
        return makeError(ErrorCode::Internal, std::format("{} references a missing point", lineOrArc));
    }
    return Endpoints{*start, *end};
}

Result<Length> Sketch::radius(EntityId circleOrArc) const {
    auto entity = require(circleOrArc);
    if (!entity) {
        return std::unexpected(entity.error());
    }
    if (const auto* circle = std::get_if<CircleEntity>(&(*entity)->geometry)) {
        return circle->radius;
    }
    if (const auto* arc = std::get_if<ArcEntity>(&(*entity)->geometry)) {
        auto center = requirePoint(arc->center);
        auto start = requirePoint(arc->start);
        if (!center || !start) {
            return makeError(ErrorCode::Internal, std::format("{} references a missing point", circleOrArc));
        }
        return distance(*center, *start);
    }
    return wrongType(**entity, "a circle or an arc");
}

Result<Point2D> Sketch::center(EntityId circleOrArc) const {
    auto entity = require(circleOrArc);
    if (!entity) {
        return std::unexpected(entity.error());
    }
    if (const auto* circle = std::get_if<CircleEntity>(&(*entity)->geometry)) {
        return requirePoint(circle->center);
    }
    if (const auto* arc = std::get_if<ArcEntity>(&(*entity)->geometry)) {
        return requirePoint(arc->center);
    }
    return wrongType(**entity, "a circle or an arc");
}

Result<Angle> Sketch::sweep(EntityId arc) const {
    auto entity = require(arc);
    if (!entity) {
        return std::unexpected(entity.error());
    }
    const auto* geometry = std::get_if<ArcEntity>(&(*entity)->geometry);
    if (geometry == nullptr) {
        return wrongType(**entity, "an arc");
    }
    auto center = requirePoint(geometry->center);
    auto start = requirePoint(geometry->start);
    auto end = requirePoint(geometry->end);
    if (!center || !start || !end) {
        return makeError(ErrorCode::Internal, std::format("{} references a missing point", arc));
    }
    return Angle::fromSi(
        normalizeAngle(directionAngle(*center, *end) - directionAngle(*center, *start)));
}

Result<Length> Sketch::length(EntityId entity) const {
    const Entity* found = findEntity(entity);
    if (found == nullptr) {
        return notFound(entity);
    }
    switch (found->type()) {
    case EntityType::Line: {
        auto ends = endpoints(entity);
        if (!ends) {
            return std::unexpected(ends.error());
        }
        return distance(ends->start, ends->end);
    }
    case EntityType::Circle:
        return kTwoPi * std::get<CircleEntity>(found->geometry).radius;
    case EntityType::Arc: {
        auto r = radius(entity);
        auto angle = sweep(entity);
        if (!r || !angle) {
            return std::unexpected(!r ? r.error() : angle.error());
        }
        return *r * angle->si();
    }
    case EntityType::Point:
        break;
    }
    return wrongType(*found, "a line, an arc or a circle");
}

Result<BoundingBox2D> Sketch::boundingBox(EntityId entity) const {
    const Entity* found = findEntity(entity);
    if (found == nullptr) {
        return notFound(entity);
    }
    switch (found->type()) {
    case EntityType::Point:
        return BoundingBox2D::around(std::get<PointEntity>(found->geometry).position);
    case EntityType::Line: {
        auto ends = endpoints(entity);
        if (!ends) {
            return std::unexpected(ends.error());
        }
        BoundingBox2D box = BoundingBox2D::around(ends->start);
        box.include(ends->end);
        return box;
    }
    case EntityType::Circle: {
        const auto& circle = std::get<CircleEntity>(found->geometry);
        auto c = requirePoint(circle.center);
        if (!c) {
            return std::unexpected(c.error());
        }
        return BoundingBox2D{{c->x - circle.radius, c->y - circle.radius},
                             {c->x + circle.radius, c->y + circle.radius}};
    }
    case EntityType::Arc: {
        auto ends = endpoints(entity);
        auto c = center(entity);
        auto r = radius(entity);
        auto angle = sweep(entity);
        if (!ends || !c || !r || !angle) {
            return makeError(ErrorCode::Internal, std::format("{} references a missing point", entity));
        }
        BoundingBox2D box = BoundingBox2D::around(ends->start);
        box.include(ends->end);
        // Add the axis-extreme points (0, 90, 180, 270 degrees) the arc passes.
        const double startAngle = directionAngle(*c, ends->start);
        const std::array<Point2D, 4> extremes{Point2D{c->x + *r, c->y}, Point2D{c->x, c->y + *r},
                                              Point2D{c->x - *r, c->y}, Point2D{c->x, c->y - *r}};
        for (std::size_t k = 0; k < extremes.size(); ++k) {
            const double offset =
                normalizeAngle(static_cast<double>(k) * std::numbers::pi / 2.0 - startAngle);
            if (offset > 0.0 && offset < angle->si()) {
                box.include(extremes[k]);
            }
        }
        return box;
    }
    }
    return makeError(ErrorCode::Internal, "unknown entity type");
}

std::optional<BoundingBox2D> Sketch::boundingBox() const {
    std::optional<BoundingBox2D> bounds;
    for (const Entity& entity : entities()) {
        auto box = boundingBox(entity.id);
        if (!box) {
            continue;
        }
        if (bounds) {
            bounds->include(*box);
        } else {
            bounds = *box;
        }
    }
    return bounds;
}

} // namespace bettercad::sketch
