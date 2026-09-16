#include <bettercad/core/math/BSpline.hpp>
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

Result<void> requireSemiAxis(Length semiAxis) {
    if (!isFinite(semiAxis) || semiAxis <= Sketch::kLengthTolerance) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("an ellipse's semi-axes must be finite and longer than {}, got {}",
                                     toString(Sketch::kLengthTolerance, units::mm), toString(semiAxis, units::mm)));
    }
    return {};
}

/// Checks the vertices of an ellipse: both off the centre, on perpendicular
/// axes (see Sketch::addEllipse).
Result<void> checkEllipse(const Point2D& center, const Point2D& xVertex, const Point2D& yVertex) {
    const Length a = distance(center, xVertex);
    const Length b = distance(center, yVertex);
    for (const Length semiAxis : {a, b}) {
        if (auto valid = requireSemiAxis(semiAxis); !valid) {
            return valid;
        }
    }
    const double dot = (xVertex.x - center.x).si() * (yVertex.x - center.x).si() +
                       (xVertex.y - center.y).si() * (yVertex.y - center.y).si();
    // |dot| / a is the Y vertex's offset along the X axis, |dot| / b the X
    // vertex's along the Y axis.
    if (std::abs(dot) / std::min(a, b).si() > Sketch::kLengthTolerance.si()) {
        const double apart = std::acos(std::clamp(dot / (a.si() * b.si()), -1.0, 1.0));
        return makeError(ErrorCode::InvalidArgument,
                         std::format("an ellipse's axes must be perpendicular, but they are {:.10g} deg apart",
                                     apart * 180.0 / std::numbers::pi));
    }
    return {};
}

/// Circumference of the ellipse with semi-axes @p a and @p b, by the
/// arithmetic-geometric mean: C = 2 pi / M(a, b) (a^2 - sum 2^(n-1) c_n^2)
/// with c_0^2 = a^2 - b^2 and c_(n+1) = (a_n - b_n) / 2.
double ellipseCircumference(double a, double b) {
    double an = a;
    double bn = b;
    double weight = 0.5; // 2^(n-1)
    double sum = weight * std::abs(a * a - b * b);
    for (int n = 0; n < 64 && std::abs(an - bn) > 1e-15 * an; ++n) {
        const double c = 0.5 * (an - bn);
        const double next = std::sqrt(an * bn);
        an = 0.5 * (an + bn);
        bn = next;
        weight *= 2.0;
        sum += weight * c * c;
    }
    return 2.0 * std::numbers::pi / an * (std::max(a, b) * std::max(a, b) - sum);
}

/// Length of a spline: an 8-point Gauss-Legendre rule on 64 pieces of each
/// knot span.
double splineLength(const UniformBSpline& spline) {
    constexpr int kPieces = 64;
    const GaussLegendreRule& rule = gaussLegendreRule();
    double length = 0.0;
    const auto first = static_cast<int>(spline.first());
    const auto last = static_cast<int>(spline.last());
    for (int span = first; span < last; ++span) {
        for (int piece = 0; piece < kPieces; ++piece) {
            const double u0 = span + static_cast<double>(piece) / kPieces;
            for (std::size_t k = 0; k < GaussLegendreRule::kPoints; ++k) {
                const auto sample = spline.evaluate(u0 + rule.nodes[k] / kPieces);
                length += rule.weights[k] / kPieces * std::hypot(sample.dx, sample.dy);
            }
        }
    }
    return length;
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
    } else if (const auto* ellipse = std::get_if<EllipseEntity>(&geometry)) {
        fn(ellipse->center);
        fn(ellipse->xVertex);
        fn(ellipse->yVertex);
    } else if (const auto* spline = std::get_if<SplineEntity>(&geometry)) {
        for (const EntityId pole : spline->poles) {
            fn(pole);
        }
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

Result<std::vector<Point2D>> Sketch::requirePoints(std::span<const EntityId> ids) const {
    std::vector<Point2D> positions;
    positions.reserve(ids.size());
    for (const EntityId id : ids) {
        auto position = requirePoint(id);
        if (!position) {
            return std::unexpected(position.error());
        }
        positions.push_back(*position);
    }
    return positions;
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

Result<EntityId> Sketch::addEllipse(const Point2D& center, Length radiusX, Length radiusY, Angle rotation) {
    if (auto finite = requireFinite(center); !finite) {
        return std::unexpected(finite.error());
    }
    for (const Length semiAxis : {radiusX, radiusY}) {
        if (auto valid = requireSemiAxis(semiAxis); !valid) {
            return std::unexpected(valid.error());
        }
    }
    if (!isFinite(rotation)) {
        return makeError(ErrorCode::InvalidArgument, "an ellipse's rotation must be finite");
    }
    // The Y axis is the X axis turned by exactly 90 degrees: (-sin, cos).
    const Point2D xVertex{center.x + radiusX * cos(rotation), center.y + radiusX * sin(rotation)};
    const Point2D yVertex{center.x - radiusY * sin(rotation), center.y + radiusY * cos(rotation)};
    const EntityId centerPoint = insert(PointEntity{center});
    const EntityId xPoint = insert(PointEntity{xVertex});
    const EntityId yPoint = insert(PointEntity{yVertex});
    return insert(EllipseEntity{centerPoint, xPoint, yPoint});
}

Result<EntityId> Sketch::addEllipse(EntityId centerPoint, EntityId xVertex, EntityId yVertex) {
    if (centerPoint == xVertex || centerPoint == yVertex || xVertex == yVertex) {
        return makeError(ErrorCode::InvalidArgument, "an ellipse needs three different points");
    }
    const std::array ids{centerPoint, xVertex, yVertex};
    auto positions = requirePoints(ids);
    if (!positions) {
        return std::unexpected(positions.error());
    }
    if (auto valid = checkEllipse((*positions)[0], (*positions)[1], (*positions)[2]); !valid) {
        return std::unexpected(valid.error());
    }
    return insert(EllipseEntity{centerPoint, xVertex, yVertex});
}

Result<EntityId> Sketch::addSpline(const std::vector<Point2D>& poles, int degree, bool periodic) {
    if (auto valid = UniformBSpline::create(poles, degree, periodic); !valid) {
        return std::unexpected(valid.error());
    }
    SplineEntity spline{.poles = {}, .degree = degree, .periodic = periodic};
    spline.poles.reserve(poles.size());
    for (const Point2D& pole : poles) {
        spline.poles.push_back(insert(PointEntity{pole}));
    }
    return insert(std::move(spline));
}

Result<EntityId> Sketch::addSpline(std::vector<EntityId> poles, int degree, bool periodic) {
    std::vector<EntityId> sorted = poles;
    std::ranges::sort(sorted);
    if (const auto repeated = std::ranges::adjacent_find(sorted); repeated != sorted.end()) {
        return makeError(ErrorCode::InvalidArgument, std::format("a spline cannot use {} twice", *repeated));
    }
    auto positions = requirePoints(poles);
    if (!positions) {
        return std::unexpected(positions.error());
    }
    if (auto valid = UniformBSpline::create(*positions, degree, periodic); !valid) {
        return std::unexpected(valid.error());
    }
    return insert(SplineEntity{std::move(poles), degree, periodic});
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
    } else if (const auto* spline = std::get_if<SplineEntity>(&entity.geometry)) {
        if (auto valid = UniformBSpline::checkStructure(spline->poles.size(), spline->degree); !valid) {
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

Result<Endpoints> Sketch::endpoints(EntityId id) const {
    auto entity = require(id);
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
    } else if (const auto* spline = std::get_if<SplineEntity>(&(*entity)->geometry)) {
        if (spline->periodic) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} is a periodic spline, which has no end points", id));
        }
        startId = spline->poles.front();
        endId = spline->poles.back();
    } else {
        return wrongType(**entity, "a line, an arc or an open spline");
    }
    auto start = requirePoint(startId);
    auto end = requirePoint(endId);
    if (!start || !end) {
        return makeError(ErrorCode::Internal, std::format("{} references a missing point", id));
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

Result<Point2D> Sketch::center(EntityId id) const {
    auto entity = require(id);
    if (!entity) {
        return std::unexpected(entity.error());
    }
    if (const auto* circle = std::get_if<CircleEntity>(&(*entity)->geometry)) {
        return requirePoint(circle->center);
    }
    if (const auto* arc = std::get_if<ArcEntity>(&(*entity)->geometry)) {
        return requirePoint(arc->center);
    }
    if (const auto* ellipse = std::get_if<EllipseEntity>(&(*entity)->geometry)) {
        return requirePoint(ellipse->center);
    }
    return wrongType(**entity, "a circle, an arc or an ellipse");
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
    case EntityType::Ellipse: {
        const auto& ellipse = std::get<EllipseEntity>(found->geometry);
        const std::array ids{ellipse.center, ellipse.xVertex, ellipse.yVertex};
        auto points = requirePoints(ids);
        if (!points) {
            return makeError(ErrorCode::Internal, std::format("{} references a missing point", entity));
        }
        return Length::fromSi(ellipseCircumference(distance((*points)[0], (*points)[1]).si(),
                                                   distance((*points)[0], (*points)[2]).si()));
    }
    case EntityType::Spline: {
        const auto& geometry = std::get<SplineEntity>(found->geometry);
        auto poles = requirePoints(geometry.poles);
        if (!poles) {
            return makeError(ErrorCode::Internal, std::format("{} references a missing point", entity));
        }
        auto spline = UniformBSpline::create(*poles, geometry.degree, geometry.periodic);
        if (!spline) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("{} is not a valid spline: {}", entity, spline.error().message));
        }
        return Length::fromSi(splineLength(*spline));
    }
    case EntityType::Point:
        break;
    }
    return wrongType(*found, "a line, an arc, a circle, an ellipse or a spline");
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
    case EntityType::Ellipse: {
        const auto& ellipse = std::get<EllipseEntity>(found->geometry);
        const std::array ids{ellipse.center, ellipse.xVertex, ellipse.yVertex};
        auto points = requirePoints(ids);
        if (!points) {
            return makeError(ErrorCode::Internal, std::format("{} references a missing point", entity));
        }
        const Point2D& c = (*points)[0];
        // Half extents of c + (X - c) cos t + (b / a) perp(X - c) sin t.
        const double dx = ((*points)[1].x - c.x).si();
        const double dy = ((*points)[1].y - c.y).si();
        const double ratio = distance(c, (*points)[2]).si() / std::max(std::hypot(dx, dy), 1e-300);
        const Length hx = Length::fromSi(std::hypot(dx, ratio * dy));
        const Length hy = Length::fromSi(std::hypot(dy, ratio * dx));
        return BoundingBox2D{{c.x - hx, c.y - hy}, {c.x + hx, c.y + hy}};
    }
    case EntityType::Spline: {
        auto poles = requirePoints(std::get<SplineEntity>(found->geometry).poles);
        if (!poles || poles->empty()) {
            return makeError(ErrorCode::Internal, std::format("{} references a missing point", entity));
        }
        BoundingBox2D box = BoundingBox2D::around(poles->front());
        for (const Point2D& pole : *poles) {
            box.include(pole);
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
