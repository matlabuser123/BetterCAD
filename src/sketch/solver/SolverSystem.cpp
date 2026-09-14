#include "sketch/solver/SolverSystem.hpp"

#include <bettercad/core/units/Format.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <set>

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

    // Internal arc equations, then segment bookkeeping for degeneracy checks.
    for (const Entity& entity : sketch.entities()) {
        if (const auto* line = std::get_if<LineEntity>(&entity.geometry)) {
            system.segments_.push_back({entity.id, point(line->start), point(line->end)});
        } else if (const auto* arc = std::get_if<ArcEntity>(&entity.geometry)) {
            Equation equation{.kind = EquationKind::LengthDiff};
            equation.p = {point(arc->center), point(arc->end), point(arc->center), point(arc->start)};
            system.equations_.push_back(equation);
            system.segments_.push_back({entity.id, point(arc->center), point(arc->start)});
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
        Equation e{.source = c.id};
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
            e.target = c.value->si();
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
