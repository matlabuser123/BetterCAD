#include "sketch/solver/SolverSystem.hpp"

#include <bettercad/core/units/Format.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace bettercad::sketch::detail {

namespace {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

Vec2 valueOf(const PointRef& p, const Eigen::VectorXd& x) {
    return {p.ix >= 0 ? x[p.ix] : p.fx, p.iy >= 0 ? x[p.iy] : p.fy};
}

double valueOf(const RadiusRef& r, const Eigen::VectorXd& x) {
    return x[r.index];
}

/// Length of b - a and its unit direction; (1, 0) for coincident points so the
/// derivative stays finite.
struct LengthTerm {
    double length = 0.0;
    Vec2 unit{1.0, 0.0};
};

LengthTerm lengthOf(Vec2 a, Vec2 b) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length = std::hypot(dx, dy);
    if (length > 0.0) {
        return {length, {dx / length, dy / length}};
    }
    return {0.0, {1.0, 0.0}};
}

// Accumulates dF/d(point) into the Jacobian row, skipping constant coordinates.
void addGradient(Eigen::MatrixXd* jacobian, Eigen::Index row, const PointRef& p, double gx,
                 double gy) {
    if (jacobian == nullptr) {
        return;
    }
    if (p.ix >= 0) {
        (*jacobian)(row, p.ix) += gx;
    }
    if (p.iy >= 0) {
        (*jacobian)(row, p.iy) += gy;
    }
}

void addGradient(Eigen::MatrixXd* jacobian, Eigen::Index row, const RadiusRef& r, double g) {
    if (jacobian != nullptr) {
        (*jacobian)(row, r.index) += g;
    }
}

double signedDistance(Vec2 p, Vec2 a, Vec2 b) {
    const double ux = b.x - a.x;
    const double uy = b.y - a.y;
    const double length = std::max(std::hypot(ux, uy), 1e-300);
    return (ux * (p.y - a.y) - uy * (p.x - a.x)) / length;
}

/// Signed distance of p from the line a -> b; with a Jacobian, adds
/// @p scale times its gradient.
double addSignedDistance(const PointRef& p, const PointRef& a, const PointRef& b, const Eigen::VectorXd& x,
                         Eigen::MatrixXd* jacobian, Eigen::Index row, double scale) {
    const Vec2 pv = valueOf(p, x);
    const Vec2 av = valueOf(a, x);
    const Vec2 bv = valueOf(b, x);
    const double ux = bv.x - av.x;
    const double uy = bv.y - av.y;
    const double wx = pv.x - av.x;
    const double wy = pv.y - av.y;
    const double length = std::max(std::hypot(ux, uy), 1e-300);
    const double s = (ux * wy - uy * wx) / length;
    const double dLbx = ux / length;
    const double dLby = uy / length;
    addGradient(jacobian, row, p, scale * -uy / length, scale * ux / length);
    addGradient(jacobian, row, b, scale * (wy / length - s * dLbx / length),
                scale * (-wx / length - s * dLby / length));
    addGradient(jacobian, row, a, scale * ((uy - wy) / length + s * dLbx / length),
                scale * ((wx - ux) / length + s * dLby / length));
    return s;
}

/// Radius term @p term of an equation (see Equation); with a Jacobian, adds
/// @p scale times its gradient.
double addRadiusTerm(const Equation& e, std::size_t term, const PointRef& centre, const PointRef& start,
                     const Eigen::VectorXd& x, Eigen::MatrixXd* jacobian, Eigen::Index row, double scale) {
    if (e.arcRadius[term]) {
        const LengthTerm t = lengthOf(valueOf(centre, x), valueOf(start, x));
        addGradient(jacobian, row, start, scale * t.unit.x, scale * t.unit.y);
        addGradient(jacobian, row, centre, -scale * t.unit.x, -scale * t.unit.y);
        return t.length;
    }
    addGradient(jacobian, row, e.r[term], scale);
    return valueOf(e.r[term], x);
}

} // namespace

Result<System> System::build(const Sketch& sketch) {
    System system;
    std::set<EntityId> fixed;
    for (const Constraint& constraint : sketch.constraints()) {
        if (constraint.enabled && constraint.type == ConstraintType::Fixed) {
            fixed.insert(constraint.entities.front());
        }
    }

    // Unknowns: free point coordinates and circle radii, in entity ID order.
    // Points that an enabled coincident constraint joins, as ordered pairs.
    std::set<std::pair<EntityId, EntityId>> coincident;
    for (const Constraint& constraint : sketch.constraints()) {
        if (constraint.enabled && constraint.type == ConstraintType::Coincident) {
            const auto [low, high] = std::minmax(constraint.entities[0], constraint.entities[1]);
            coincident.emplace(low, high);
        }
    }
    // The first pair (a, b) of end points, a from @p aEnds and b from
    // @p bEnds, that are one point or joined by a coincident constraint.
    const auto jointOf = [&](std::array<EntityId, 2> aEnds,
                             std::array<EntityId, 2> bEnds) -> std::optional<std::pair<EntityId, EntityId>> {
        for (const EntityId a : aEnds) {
            for (const EntityId b : bEnds) {
                const auto [low, high] = std::minmax(a, b);
                if (a == b || coincident.contains({low, high})) {
                    return std::pair{a, b};
                }
            }
        }
        return std::nullopt;
    };

    std::map<EntityId, PointRef> points;
    std::map<EntityId, RadiusRef> radii;
    std::vector<double> initial;
    for (const Entity& entity : sketch.entities()) {
        if (const auto* point = std::get_if<PointEntity>(&entity.geometry)) {
            PointRef ref{.entity = entity.id};
            if (fixed.contains(entity.id)) {
                ref.fx = point->position.x.si();
                ref.fy = point->position.y.si();
            } else {
                ref.ix = static_cast<Eigen::Index>(initial.size());
                ref.iy = ref.ix + 1;
                initial.push_back(point->position.x.si());
                initial.push_back(point->position.y.si());
            }
            points.emplace(entity.id, ref);
            system.points_.push_back(ref);
        } else if (const auto* circle = std::get_if<CircleEntity>(&entity.geometry)) {
            const RadiusRef ref{entity.id, static_cast<Eigen::Index>(initial.size())};
            initial.push_back(circle->radius.si());
            radii.emplace(entity.id, ref);
            system.radii_.push_back(ref);
        }
    }
    system.initial_ = Eigen::Map<const Eigen::VectorXd>(initial.data(), static_cast<Eigen::Index>(initial.size()));

    const auto point = [&](EntityId id) -> const PointRef& { return points.at(id); };
    const Eigen::VectorXd& x0 = system.initial_;

    // Internal arc and ellipse equations, then bookkeeping for degeneracy
    // checks.
    for (const Entity& entity : sketch.entities()) {
        if (const auto* line = std::get_if<LineEntity>(&entity.geometry)) {
            system.segments_.push_back({entity.id, point(line->start), point(line->end)});
        } else if (const auto* arc = std::get_if<ArcEntity>(&entity.geometry)) {
            Equation equation{.kind = EquationKind::LengthDiff};
            equation.p = {point(arc->center), point(arc->end), point(arc->center), point(arc->start)};
            system.equations_.push_back(equation);
            system.segments_.push_back({entity.id, point(arc->center), point(arc->start)});
        } else if (const auto* ellipse = std::get_if<EllipseEntity>(&entity.geometry)) {
            Equation equation{.kind = EquationKind::Perpendicular};
            equation.p = {point(ellipse->center), point(ellipse->xVertex), point(ellipse->center),
                          point(ellipse->yVertex)};
            system.equations_.push_back(equation);
            system.segments_.push_back({entity.id, point(ellipse->center), point(ellipse->xVertex)});
            system.segments_.push_back({entity.id, point(ellipse->center), point(ellipse->yVertex)});
        } else if (const auto* spline = std::get_if<SplineEntity>(&entity.geometry)) {
            Polygon polygon{.entity = entity.id, .points = {}, .closed = spline->periodic};
            for (const EntityId pole : spline->poles) {
                polygon.points.push_back(point(pole));
            }
            system.polygons_.push_back(std::move(polygon));
        }
    }

    // Constraint equations in constraint ID order.
    for (const Constraint& c : sketch.constraints()) {
        if (!c.enabled || c.type == ConstraintType::Fixed) {
            continue;
        }
        const auto endpoints = [&](EntityId id) {
            const auto& line = std::get<LineEntity>(sketch.findEntity(id)->geometry);
            return std::array<PointRef, 2>{point(line.start), point(line.end)};
        };
        const auto type = [&](std::size_t i) { return sketch.findEntity(c.entities[i])->type(); };
        const auto centreOf = [&](EntityId id) -> const PointRef& {
            const EntityGeometry& geometry = sketch.findEntity(id)->geometry;
            if (const auto* circle = std::get_if<CircleEntity>(&geometry)) {
                return point(circle->center);
            }
            if (const auto* ellipse = std::get_if<EllipseEntity>(&geometry)) {
                return point(ellipse->center);
            }
            return point(std::get<ArcEntity>(geometry).center);
        };
        Equation e{.source = c.id};
        // Radius term @p term of a circle (its variable) or an arc (|start - centre|).
        const auto setRadiusTerm = [&](EntityId id, std::size_t term, std::size_t startSlot) {
            const EntityGeometry& geometry = sketch.findEntity(id)->geometry;
            if (const auto* arc = std::get_if<ArcEntity>(&geometry)) {
                e.arcRadius[term] = true;
                e.p[startSlot] = point(arc->start);
            } else {
                e.r[term] = radii.at(id);
            }
        };
        const auto push = [&](EquationKind kind) {
            e.kind = kind;
            system.equations_.push_back(e);
        };

        switch (c.type) {
        case ConstraintType::Coincident:
            e.p[0] = point(c.entities[0]);
            e.p[1] = point(c.entities[1]);
            push(EquationKind::DiffX);
            push(EquationKind::DiffY);
            break;
        case ConstraintType::Horizontal:
        case ConstraintType::Vertical: {
            const EquationKind kind =
                c.type == ConstraintType::Horizontal ? EquationKind::DiffY : EquationKind::DiffX;
            if (type(0) == EntityType::Line) {
                const auto ends = endpoints(c.entities[0]);
                e.p[0] = ends[0];
                e.p[1] = ends[1];
            } else {
                e.p[0] = point(c.entities[0]);
                e.p[1] = point(c.entities[1]);
            }
            push(kind);
            break;
        }
        case ConstraintType::Parallel:
        case ConstraintType::Perpendicular: {
            const auto first = endpoints(c.entities[0]);
            const auto second = endpoints(c.entities[1]);
            e.p = {first[0], first[1], second[0], second[1]};
            push(c.type == ConstraintType::Parallel ? EquationKind::Parallel : EquationKind::Perpendicular);
            break;
        }
        case ConstraintType::Distance:
            e.target = c.value->si();
            if (c.entities.size() == 1) {
                const auto ends = endpoints(c.entities[0]);
                e.p[0] = ends[0];
                e.p[1] = ends[1];
                push(EquationKind::Length);
            } else if (type(1) == EntityType::Point) {
                e.p[0] = point(c.entities[0]);
                e.p[1] = point(c.entities[1]);
                push(EquationKind::Length);
            } else {
                const auto line = endpoints(c.entities[1]);
                e.p[0] = point(c.entities[0]);
                e.p[1] = line[0];
                e.p[2] = line[1];
                // Keep the point on the side of the line where it starts.
                e.sign = signedDistance(valueOf(e.p[0], x0), valueOf(e.p[1], x0), valueOf(e.p[2], x0)) < 0.0 ? -1.0 : 1.0;
                push(EquationKind::PointLine);
            }
            break;
        case ConstraintType::Radius:
        case ConstraintType::Diameter:
            e.target = c.type == ConstraintType::Diameter ? c.value->si() / 2.0 : c.value->si();
            if (type(0) == EntityType::Circle) {
                e.r[0] = radii.at(c.entities[0]);
                push(EquationKind::RadiusValue);
            } else {
                const auto& arc = std::get<ArcEntity>(sketch.findEntity(c.entities[0])->geometry);
                e.p[0] = point(arc.center);
                e.p[1] = point(arc.start);
                push(EquationKind::Length);
            }
            break;
        case ConstraintType::Equal: {
            if (type(0) == EntityType::Line) {
                const auto first = endpoints(c.entities[0]);
                const auto second = endpoints(c.entities[1]);
                e.p = {first[0], first[1], second[0], second[1]};
                push(EquationKind::LengthDiff);
                break;
            }
            // Radii: a circle contributes its radius variable, an arc |start - centre|.
            const auto arcRadius = [&](std::size_t i) {
                const auto& arc = std::get<ArcEntity>(sketch.findEntity(c.entities[i])->geometry);
                return std::array<PointRef, 2>{point(arc.center), point(arc.start)};
            };
            const bool circle0 = type(0) == EntityType::Circle;
            const bool circle1 = type(1) == EntityType::Circle;
            if (circle0 && circle1) {
                e.r = {radii.at(c.entities[0]), radii.at(c.entities[1])};
                push(EquationKind::RadiusDiff);
            } else if (!circle0 && !circle1) {
                const auto a = arcRadius(0);
                const auto b = arcRadius(1);
                e.p = {a[0], a[1], b[0], b[1]};
                push(EquationKind::LengthDiff);
            } else {
                const std::size_t arcIndex = circle0 ? 1 : 0;
                const auto a = arcRadius(arcIndex);
                e.p[0] = a[0];
                e.p[1] = a[1];
                e.r[0] = radii.at(c.entities[circle0 ? 0 : 1]);
                push(EquationKind::LengthRadiusDiff);
            }
            break;
        }
        case ConstraintType::Fixed:
            break;
        case ConstraintType::Angle: {
            const auto first = endpoints(c.entities[0]);
            const auto second = endpoints(c.entities[1]);
            e.p = {first[0], first[1], second[0], second[1]};
            e.target = c.angle->si();
            push(EquationKind::Angle);
            break;
        }
        case ConstraintType::Tangent: {
            // At a joint (the two entities end at one point, or at points a
            // coincident constraint joins) tangency is the line perpendicular
            // to the arc's radius there, or the two radii parallel. The
            // distance form below is second order at a joint: it would place
            // the joint only to the square root of the tolerance.
            const Entity* first = sketch.findEntity(c.entities[0]);
            const Entity* second = sketch.findEntity(c.entities[1]);
            if (const auto* spline = std::get_if<SplineEntity>(&second->geometry)) {
                // A spline is last (canonical order) and tangent only at a
                // joint, where its tangent is its end leg.
                const auto firstEnds = endPointIds(*first);
                const auto secondEnds = endPointIds(*second);
                const auto joint =
                    firstEnds && secondEnds ? jointOf(*firstEnds, *secondEnds) : std::nullopt;
                if (!joint) {
                    return makeError(ErrorCode::FailedPrecondition,
                                     std::format("{} needs {} and {} to share an end point", c.id, c.entities[0],
                                                 c.entities[1]));
                }
                const auto legAt = [&](const SplineEntity& curve, EntityId end) {
                    const std::size_t n = curve.poles.size();
                    return end == curve.poles.front()
                               ? std::array{point(curve.poles[0]), point(curve.poles[1])}
                               : std::array{point(curve.poles[n - 2]), point(curve.poles[n - 1])};
                };
                const auto leg = legAt(*spline, joint->second);
                if (const auto* line = std::get_if<LineEntity>(&first->geometry)) {
                    e.p = {leg[0], leg[1], point(line->start), point(line->end)};
                    push(EquationKind::Parallel);
                } else if (const auto* arc = std::get_if<ArcEntity>(&first->geometry)) {
                    e.p = {leg[0], leg[1], point(arc->center), point(joint->first)};
                    push(EquationKind::Perpendicular);
                } else {
                    const auto other = legAt(std::get<SplineEntity>(first->geometry), joint->first);
                    e.p = {other[0], other[1], leg[0], leg[1]};
                    push(EquationKind::Parallel);
                }
                break;
            }
            const auto* secondArc = std::get_if<ArcEntity>(&second->geometry);
            if (secondArc != nullptr) {
                if (const auto* line = std::get_if<LineEntity>(&first->geometry)) {
                    if (const auto joint = jointOf({line->start, line->end}, {secondArc->start, secondArc->end})) {
                        const auto ends = endpoints(c.entities[0]);
                        e.p = {ends[0], ends[1], point(secondArc->center), point(joint->second)};
                        push(EquationKind::Perpendicular);
                        break;
                    }
                } else if (const auto* firstArc = std::get_if<ArcEntity>(&first->geometry)) {
                    if (const auto joint = jointOf({firstArc->start, firstArc->end}, {secondArc->start, secondArc->end})) {
                        e.p = {point(firstArc->center), point(joint->first), point(secondArc->center),
                               point(joint->second)};
                        push(EquationKind::Parallel);
                        break;
                    }
                }
            }
            if (type(0) == EntityType::Line) {
                const auto line = endpoints(c.entities[0]);
                e.p[0] = centreOf(c.entities[1]);
                e.p[1] = line[0];
                e.p[2] = line[1];
                setRadiusTerm(c.entities[1], 0, 3);
                // Keep the centre on the side of the line where it starts.
                e.sign = signedDistance(valueOf(e.p[0], x0), valueOf(e.p[1], x0), valueOf(e.p[2], x0)) < 0.0 ? -1.0
                                                                                                             : 1.0;
                push(EquationKind::LineTangent);
            } else {
                e.p[0] = centreOf(c.entities[0]);
                e.p[1] = centreOf(c.entities[1]);
                setRadiusTerm(c.entities[0], 0, 2);
                setRadiusTerm(c.entities[1], 1, 3);
                // External or internal contact, whichever the start is nearer
                // (external on a tie).
                const double d = lengthOf(valueOf(e.p[0], x0), valueOf(e.p[1], x0)).length;
                const double r0 = addRadiusTerm(e, 0, e.p[0], e.p[2], x0, nullptr, 0, 0.0);
                const double r1 = addRadiusTerm(e, 1, e.p[1], e.p[3], x0, nullptr, 0, 0.0);
                if (std::abs(d - (r0 + r1)) <= std::abs(d - std::abs(r0 - r1))) {
                    e.coefficient = {1.0, 1.0};
                } else if (r0 >= r1) {
                    e.coefficient = {1.0, -1.0};
                } else {
                    e.coefficient = {-1.0, 1.0};
                }
                push(EquationKind::CircleTangent);
            }
            break;
        }
        case ConstraintType::Concentric:
            e.p[0] = centreOf(c.entities[0]);
            e.p[1] = centreOf(c.entities[1]);
            push(EquationKind::DiffX);
            push(EquationKind::DiffY);
            break;
        case ConstraintType::Midpoint: {
            const auto line = endpoints(c.entities[1]);
            e.p[0] = point(c.entities[0]);
            e.p[1] = line[0];
            e.p[2] = line[1];
            push(EquationKind::MidX);
            push(EquationKind::MidY);
            break;
        }
        case ConstraintType::Symmetric: {
            // The midpoint lies on the axis, and the points' segment is
            // perpendicular to it.
            const auto axis = endpoints(c.entities[2]);
            e.p = {point(c.entities[0]), point(c.entities[1]), axis[0], axis[1]};
            push(EquationKind::MidpointOnLine);
            push(EquationKind::Perpendicular);
            break;
        }
        }
    }
    return system;
}

void System::evaluate(const Eigen::VectorXd& x, Eigen::VectorXd& residuals,
                      Eigen::MatrixXd* jacobian) const {
    const auto m = equationCount();
    residuals.resize(m);
    if (jacobian != nullptr) {
        jacobian->setZero(m, unknowns());
    }

    for (Eigen::Index row = 0; row < m; ++row) {
        const Equation& e = equations_[static_cast<std::size_t>(row)];
        const Vec2 p0 = valueOf(e.p[0], x);
        const Vec2 p1 = valueOf(e.p[1], x);
        switch (e.kind) {
        case EquationKind::DiffX:
            residuals[row] = p0.x - p1.x;
            addGradient(jacobian, row, e.p[0], 1.0, 0.0);
            addGradient(jacobian, row, e.p[1], -1.0, 0.0);
            break;
        case EquationKind::DiffY:
            residuals[row] = p0.y - p1.y;
            addGradient(jacobian, row, e.p[0], 0.0, 1.0);
            addGradient(jacobian, row, e.p[1], 0.0, -1.0);
            break;
        case EquationKind::Length: {
            const LengthTerm t = lengthOf(p0, p1);
            residuals[row] = t.length - e.target;
            addGradient(jacobian, row, e.p[1], t.unit.x, t.unit.y);
            addGradient(jacobian, row, e.p[0], -t.unit.x, -t.unit.y);
            break;
        }
        case EquationKind::RadiusValue:
            residuals[row] = valueOf(e.r[0], x) - e.target;
            addGradient(jacobian, row, e.r[0], 1.0);
            break;
        case EquationKind::LengthDiff: {
            const LengthTerm t1 = lengthOf(p0, p1);
            const LengthTerm t2 = lengthOf(valueOf(e.p[2], x), valueOf(e.p[3], x));
            residuals[row] = t1.length - t2.length;
            addGradient(jacobian, row, e.p[1], t1.unit.x, t1.unit.y);
            addGradient(jacobian, row, e.p[0], -t1.unit.x, -t1.unit.y);
            addGradient(jacobian, row, e.p[3], -t2.unit.x, -t2.unit.y);
            addGradient(jacobian, row, e.p[2], t2.unit.x, t2.unit.y);
            break;
        }
        case EquationKind::RadiusDiff:
            residuals[row] = valueOf(e.r[0], x) - valueOf(e.r[1], x);
            addGradient(jacobian, row, e.r[0], 1.0);
            addGradient(jacobian, row, e.r[1], -1.0);
            break;
        case EquationKind::LengthRadiusDiff: {
            const LengthTerm t = lengthOf(p0, p1);
            residuals[row] = t.length - valueOf(e.r[0], x);
            addGradient(jacobian, row, e.p[1], t.unit.x, t.unit.y);
            addGradient(jacobian, row, e.p[0], -t.unit.x, -t.unit.y);
            addGradient(jacobian, row, e.r[0], -1.0);
            break;
        }
        case EquationKind::PointLine: {
            // p = p0, line a = p1 -> b = p2.
            const Vec2 b = valueOf(e.p[2], x);
            const double ux = b.x - p1.x;
            const double uy = b.y - p1.y;
            const double wx = p0.x - p1.x;
            const double wy = p0.y - p1.y;
            const double length = std::max(std::hypot(ux, uy), 1e-300);
            const double cross = ux * wy - uy * wx;
            const double s = cross / length;
            residuals[row] = s - e.sign * e.target;
            // ds/dX = (dC/dX) / L - s * (dL/dX) / L
            const double dLbx = ux / length;
            const double dLby = uy / length;
            addGradient(jacobian, row, e.p[0], -uy / length, ux / length);
            addGradient(jacobian, row, e.p[2], wy / length - s * dLbx / length,
                        -wx / length - s * dLby / length);
            addGradient(jacobian, row, e.p[1], (uy - wy) / length + s * dLbx / length,
                        (wx - ux) / length + s * dLby / length);
            break;
        }
        case EquationKind::Parallel:
        case EquationKind::Perpendicular: {
            const Vec2 p2 = valueOf(e.p[2], x);
            const Vec2 p3 = valueOf(e.p[3], x);
            const double d1x = p1.x - p0.x;
            const double d1y = p1.y - p0.y;
            const double d2x = p3.x - p2.x;
            const double d2y = p3.y - p2.y;
            const double l2 = std::max(std::hypot(d2x, d2y), 1e-300);
            double value = 0.0;
            double g1x = 0.0; // dF/d(d1)
            double g1y = 0.0;
            double gcx = 0.0; // d(numerator)/d(d2)
            double gcy = 0.0;
            if (e.kind == EquationKind::Parallel) {
                value = d1x * d2y - d1y * d2x;
                g1x = d2y / l2;
                g1y = -d2x / l2;
                gcx = -d1y;
                gcy = d1x;
            } else {
                value = d1x * d2x + d1y * d2y;
                g1x = d2x / l2;
                g1y = d2y / l2;
                gcx = d1x;
                gcy = d1y;
            }
            const double f = value / l2;
            residuals[row] = f;
            // dF/d(d2) = (dN/d(d2) - F * d2 / L2) / L2
            const double g2x = (gcx - f * d2x / l2) / l2;
            const double g2y = (gcy - f * d2y / l2) / l2;
            addGradient(jacobian, row, e.p[1], g1x, g1y);
            addGradient(jacobian, row, e.p[0], -g1x, -g1y);
            addGradient(jacobian, row, e.p[3], g2x, g2y);
            addGradient(jacobian, row, e.p[2], -g2x, -g2y);
            break;
        }
        case EquationKind::Angle: {
            const Vec2 p2 = valueOf(e.p[2], x);
            const Vec2 p3 = valueOf(e.p[3], x);
            const double d1x = p1.x - p0.x;
            const double d1y = p1.y - p0.y;
            const double d2x = p3.x - p2.x;
            const double d2y = p3.y - p2.y;
            const double l2 = std::max(std::hypot(d2x, d2y), 1e-300);
            const double ct = std::cos(e.target);
            const double st = std::sin(e.target);
            const double cross = d1x * d2y - d1y * d2x;
            const double dot = d1x * d2x + d1y * d2y;
            const double f = (cross * ct - dot * st) / l2;
            residuals[row] = f;
            // dF/d(d1) = dN/d(d1) / L2; dF/d(d2) = (dN/d(d2) - F d2 / L2) / L2
            const double g1x = (d2y * ct - d2x * st) / l2;
            const double g1y = (-d2x * ct - d2y * st) / l2;
            const double gcx = -d1y * ct - d1x * st;
            const double gcy = d1x * ct - d1y * st;
            const double g2x = (gcx - f * d2x / l2) / l2;
            const double g2y = (gcy - f * d2y / l2) / l2;
            addGradient(jacobian, row, e.p[1], g1x, g1y);
            addGradient(jacobian, row, e.p[0], -g1x, -g1y);
            addGradient(jacobian, row, e.p[3], g2x, g2y);
            addGradient(jacobian, row, e.p[2], -g2x, -g2y);
            break;
        }
        case EquationKind::LineTangent: {
            const double s = addSignedDistance(e.p[0], e.p[1], e.p[2], x, jacobian, row, 1.0);
            const double radius = addRadiusTerm(e, 0, e.p[0], e.p[3], x, jacobian, row, -e.sign);
            residuals[row] = s - e.sign * radius;
            break;
        }
        case EquationKind::CircleTangent: {
            const LengthTerm t = lengthOf(p0, p1);
            addGradient(jacobian, row, e.p[1], t.unit.x, t.unit.y);
            addGradient(jacobian, row, e.p[0], -t.unit.x, -t.unit.y);
            const double r0 = addRadiusTerm(e, 0, e.p[0], e.p[2], x, jacobian, row, -e.coefficient[0]);
            const double r1 = addRadiusTerm(e, 1, e.p[1], e.p[3], x, jacobian, row, -e.coefficient[1]);
            residuals[row] = t.length - (e.coefficient[0] * r0 + e.coefficient[1] * r1);
            break;
        }
        case EquationKind::MidX:
        case EquationKind::MidY: {
            const bool alongX = e.kind == EquationKind::MidX;
            const Vec2 p2 = valueOf(e.p[2], x);
            residuals[row] = alongX ? p0.x - 0.5 * (p1.x + p2.x) : p0.y - 0.5 * (p1.y + p2.y);
            addGradient(jacobian, row, e.p[0], alongX ? 1.0 : 0.0, alongX ? 0.0 : 1.0);
            addGradient(jacobian, row, e.p[1], alongX ? -0.5 : 0.0, alongX ? 0.0 : -0.5);
            addGradient(jacobian, row, e.p[2], alongX ? -0.5 : 0.0, alongX ? 0.0 : -0.5);
            break;
        }
        case EquationKind::MidpointOnLine: {
            // m = (p0 + p1) / 2 against the line p2 -> p3; the gradient with
            // respect to m goes half to each point.
            const Vec2 a = valueOf(e.p[2], x);
            const Vec2 b = valueOf(e.p[3], x);
            const double ux = b.x - a.x;
            const double uy = b.y - a.y;
            const double wx = 0.5 * (p0.x + p1.x) - a.x;
            const double wy = 0.5 * (p0.y + p1.y) - a.y;
            const double length = std::max(std::hypot(ux, uy), 1e-300);
            const double s = (ux * wy - uy * wx) / length;
            residuals[row] = s;
            const double dLbx = ux / length;
            const double dLby = uy / length;
            addGradient(jacobian, row, e.p[0], -0.5 * uy / length, 0.5 * ux / length);
            addGradient(jacobian, row, e.p[1], -0.5 * uy / length, 0.5 * ux / length);
            addGradient(jacobian, row, e.p[3], wy / length - s * dLbx / length, -wx / length - s * dLby / length);
            addGradient(jacobian, row, e.p[2], (uy - wy) / length + s * dLbx / length,
                        (wx - ux) / length + s * dLby / length);
            break;
        }
        }
    }
}

Result<void> System::checkNonDegenerate(const Eigen::VectorXd& x, double tolerance) const {
    for (const Segment& segment : segments_) {
        if (lengthOf(valueOf(segment.a, x), valueOf(segment.b, x)).length <= tolerance) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("the solution collapses {} to zero size", segment.entity));
        }
    }
    for (const Polygon& polygon : polygons_) {
        double length = 0.0;
        const std::size_t n = polygon.points.size();
        for (std::size_t i = 0; i + 1 < n + (polygon.closed ? 1 : 0); ++i) {
            length += lengthOf(valueOf(polygon.points[i], x), valueOf(polygon.points[(i + 1) % n], x)).length;
        }
        if (length <= tolerance) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("the solution collapses {} to zero size", polygon.entity));
        }
    }
    for (const RadiusRef& radius : radii_) {
        if (valueOf(radius, x) <= tolerance) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("the solution gives {} a non-positive radius", radius.entity));
        }
    }
    return {};
}

Result<bool> System::apply(const Eigen::VectorXd& x, Sketch& sketch) const {
    bool changed = false;
    for (const PointRef& point : points_) {
        if (point.ix < 0) {
            continue;
        }
        const Point2D position{Length::fromSi(x[point.ix]), Length::fromSi(x[point.iy])};
        auto result = sketch.setPointPosition(point.entity, position);
        if (!result) {
            return std::unexpected(result.error());
        }
        changed |= *result;
    }
    for (const RadiusRef& radius : radii_) {
        auto result = sketch.setCircleRadius(radius.entity, Length::fromSi(x[radius.index]));
        if (!result) {
            return std::unexpected(result.error());
        }
        changed |= *result;
    }
    return changed;
}

} // namespace bettercad::sketch::detail
