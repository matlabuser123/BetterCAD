#include "assembly/solver/SolverSystem.hpp"

#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Configurations.hpp>
#include <bettercad/assembly/Resolution.hpp>
#include <bettercad/assembly/Mates.hpp>
#include <bettercad/assembly/Placement.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Datums.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <set>

namespace bettercad::assembly::detail {
namespace {

using Eigen::Index;
using Eigen::Matrix3d;
using Eigen::MatrixXd;
using Eigen::Vector3d;
using Eigen::VectorXd;

/// Below this the assembly is treated as sitting at the origin, and the
/// characteristic size falls back to a millimetre so angular residuals are
/// still scaled by something.
constexpr double kMinimumCharacteristic = 1e-3;
/// The separation of two axes is not differentiable where they meet.
constexpr double kSeparationFloor = 1e-12;

[[nodiscard]] Vector3d vec(const Point3D& p) noexcept { return {p.x.si(), p.y.si(), p.z.si()}; }
[[nodiscard]] Vector3d vec(const Direction3D& d) noexcept { return {d.x(), d.y(), d.z()}; }
[[nodiscard]] Vector3d vec(const Translation3D& t) noexcept { return {t.x.si(), t.y.si(), t.z.si()}; }

[[nodiscard]] Point3D point(const Vector3d& v) noexcept {
    return {Length::fromSi(v.x()), Length::fromSi(v.y()), Length::fromSi(v.z())};
}

/// Two orthonormal directions perpendicular to @p d, chosen from its
/// smallest component so the choice is deterministic and the cross product
/// is well conditioned.
[[nodiscard]] std::array<Vector3d, 2> complementBasis(const Vector3d& d) {
    Index smallest = 0;
    for (Index i = 1; i < 3; ++i) {
        if (std::abs(d[i]) < std::abs(d[smallest])) {
            smallest = i;
        }
    }
    const Vector3d axis = Vector3d::Unit(smallest);
    const Vector3d u = d.cross(axis).normalized();
    return {u, d.cross(u)};
}

} // namespace

Result<System> System::build(const Document& document, const BodyLookup& bodies) {
    System system;

    // Grounded components come from Fixed mates. A component needs no flag of
    // its own: P13-XFORM-001 deferred one as meaningless without a solver, and
    // P13-MATE-001 supplied the mate instead.
    std::set<ComponentId> grounded;
    for (const MateId id : activeMates(document)) {
        const Mate* mate = findMate(document, id);
        if (mate == nullptr) {
            continue;
        }
        if (mate->definition().type == MateType::Fixed) {
            if (findComponent(document, mate->definition().component) == nullptr) {
                return makeError(ErrorCode::NotFound,
                                 std::format("{} holds {}, which is not a component of this document", id,
                                             mate->definition().component));
            }
            grounded.insert(mate->definition().component);
        }
    }

    // The starting configuration is the placement intent and nothing else.
    // ADR-005 rejected seeding from a previous solution, so there is no other
    // state that could make two identical documents solve differently.
    // Only the components in force: a suppressed one is not in this build, so
    // it contributes no unknowns and no degrees of freedom. The solver asks
    // which are active and never learns that configurations exist -- the same
    // way expressions read effectiveParameterValue() (ADR-007).
    for (const ComponentId id : activeComponents(document)) {
        auto placement = placementOf(document, id);
        if (!placement) {
            return std::unexpected(placement.error());
        }
        system.base_.emplace(id, *placement);
        if (grounded.contains(id)) {
            system.slot_.emplace(id, -1);
        } else {
            system.slot_.emplace(id, static_cast<Index>(system.free_.size()));
            system.free_.push_back(id);
        }
    }

    // One resolution path, shared with unresolvedMateTargets(): the question
    // "does this target resolve" is answered by the same code that answers
    // "to what", so a report and a solve can never disagree (P13-STREF-001).
    const auto addTarget = [&](const MateTarget& target) -> Result<std::size_t> {
        auto resolved = resolveMateTarget(document, target, bodies);
        if (!resolved) {
            return std::unexpected(resolved.error());
        }
        system.targets_.push_back({.component = target.component,
                                   .planar = resolved->planar,
                                   .origin = resolved->origin,
                                   .direction = resolved->direction});
        return system.targets_.size() - 1;
    };

    for (const MateId id : activeMates(document)) {
        const Mate* mate = findMate(document, id);
        if (mate == nullptr) {
            continue;
        }
        const MateDefinition& d = mate->definition();
        if (d.type == MateType::Fixed) {
            continue; // grounding, not an equation
        }
        std::vector<const MateTarget*> named{&*d.a, &*d.b};
        if (d.a2) {
            named.push_back(&*d.a2);
            named.push_back(&*d.b2);
        }
        for (const MateTarget* target : named) {
            if (findComponent(document, target->component) == nullptr) {
                return makeError(ErrorCode::NotFound,
                                 std::format("{} names {}, which is not a component of this document", id,
                                             target->component));
            }
        }
        auto a = addTarget(*d.a);
        if (!a) {
            return std::unexpected(a.error());
        }
        auto b = addTarget(*d.b);
        if (!b) {
            return std::unexpected(b.error());
        }
        const Equation base{.source = id, .a = *a, .b = *b};

        const auto parallel = [&] {
            for (int axis = 0; axis < 2; ++axis) {
                Equation equation = base;
                equation.kind = EquationKind::Parallel;
                equation.axis = axis;
                system.equations_.push_back(equation);
            }
        };
        const auto offsetPerpendicular = [&] {
            for (int axis = 0; axis < 2; ++axis) {
                Equation equation = base;
                equation.kind = EquationKind::OffsetPerpendicular;
                equation.axis = axis;
                system.equations_.push_back(equation);
            }
        };
        const auto offsetAlong = [&] {
            Equation equation = base;
            equation.kind = EquationKind::OffsetAlong;
            system.equations_.push_back(equation);
        };
        // Two axes on one line: parallel, and meeting. Four equations of
        // rank four, leaving the slide along the axis and the turn about it
        // -- which is a cylindrical joint exactly, and what a revolute and a
        // slider each remove one of.
        const auto axesCollinear = [&] {
            parallel();
            offsetPerpendicular();
        };
        switch (d.type) {
        case MateType::Fixed:
            break;
        case MateType::Coincident:
        case MateType::Concentric: {
            // Concentric and an axis-to-axis Coincident are the same
            // equations. They differ in what the engineer meant, which the
            // model records and the solver does not need.
            parallel();
            if (system.targets_[*a].planar) {
                Equation offset = base;
                offset.kind = EquationKind::OffsetAlong;
                system.equations_.push_back(offset);
            } else {
                for (int axis = 0; axis < 2; ++axis) {
                    Equation offset = base;
                    offset.kind = EquationKind::OffsetPerpendicular;
                    offset.axis = axis;
                    system.equations_.push_back(offset);
                }
            }
            break;
        }
        case MateType::Parallel:
            parallel();
            break;
        case MateType::Perpendicular: {
            Equation equation = base;
            equation.kind = EquationKind::Perpendicular;
            system.equations_.push_back(equation);
            break;
        }
        case MateType::Distance: {
            Equation equation = base;
            equation.kind = system.targets_[*a].planar ? EquationKind::OffsetAlong
                                                       : EquationKind::SeparationPerpendicular;
            equation.target = d.distance->si();
            system.equations_.push_back(equation);
            break;
        }
        case MateType::Angle: {
            Equation equation = base;
            equation.kind = EquationKind::Angle;
            equation.target = std::cos(d.angle->si());
            system.equations_.push_back(equation);
            break;
        }

        // --- the joints ------------------------------------------------
        case MateType::Cylindrical:
            axesCollinear(); // 4 equations -> 2 DOF
            break;
        case MateType::Revolute:
            // 5 equations -> 1 rotational DOF. OffsetAlong on axis targets
            // is the axial position: dot(Pb - Pa, Da), with Da the axis
            // direction rather than a plane normal. Same equation, same
            // gradient; only the geometry it is asked about differs.
            axesCollinear();
            offsetAlong();
            break;
        case MateType::Slider: {
            // 5 equations -> 1 translational DOF. The slide keeps the axial
            // freedom the cylindrical joint has and gives up the turn, which
            // is the one thing the axis pair cannot express on its own.
            axesCollinear();
            auto a2 = addTarget(*d.a2);
            if (!a2) {
                return std::unexpected(a2.error());
            }
            auto b2 = addTarget(*d.b2);
            if (!b2) {
                return std::unexpected(b2.error());
            }
            // cross(Ra, Rb) . D, one row: the roll references and the slide
            // axis are coplanar. Written as a Parallel row whose projection
            // vector is the axis instead of a complement basis, so it is the
            // Parallel gradient unchanged. Its derivative does not vanish
            // where the references line up, which dot(Ra, Rb) - 1 would.
            Equation roll;
            roll.kind = EquationKind::Parallel;
            roll.source = id;
            roll.a = *a2;
            roll.b = *b2;
            roll.axis = 0;
            roll.projectFrom = static_cast<int>(*a);
            system.equations_.push_back(roll);
            break;
        }
        case MateType::Planar:
            // 3 equations -> 2 in-plane translations and the turn about the
            // normal. The same set a plane-to-plane Coincident produces: the
            // difference is what the engineer meant, which is what the model
            // is for.
            parallel();
            offsetAlong();
            break;
        }
    }

    // The size the angular residuals are scaled by, so that a radian of
    // misalignment weighs about as much as the length it sweeps at the edge
    // of the assembly. Without it the Jacobian mixes metres and radians and
    // its conditioning depends on the unit the model happens to use.
    double extent = 0.0;
    const VectorXd zero = VectorXd::Zero(system.unknowns());
    for (std::size_t i = 0; i < system.targets_.size(); ++i) {
        Point3D origin;
        Direction3D direction = Direction3D::unitZ();
        system.placeTarget(zero, i, origin, direction);
        extent = std::max(extent, vec(origin).norm());
    }
    for (const auto& [id, transform] : system.base_) {
        extent = std::max(extent, vec(transform.translationPart()).norm());
    }
    system.characteristic_ = std::max(kMinimumCharacteristic, extent);

    system.rebase(zero);
    return system;
}

RigidTransform3D System::transformOf(const VectorXd& x, ComponentId component) const {
    const RigidTransform3D& base = base_.at(component);
    const Index slot = slot_.at(component);
    if (slot < 0 || x.size() == 0) {
        return base;
    }
    const Vector3d translation = x.segment(6 * slot, 3);
    const Vector3d rotation = x.segment(6 * slot + 3, 3);

    // The increment turns the component about its own origin, so a component
    // far from the model origin does not swing wildly for a small angle and
    // the Jacobian stays well conditioned.
    RigidTransform3D motion = base;
    const double angle = rotation.norm();
    if (angle > 0.0) {
        const auto direction = Direction3D::fromComponents(rotation.x(), rotation.y(), rotation.z());
        if (direction) {
            const Point3D origin = point(vec(base.translationPart()));
            motion = RigidTransform3D::rotation(Axis3D{origin, *direction}, Angle::fromSi(angle)).after(motion);
        }
    }
    return RigidTransform3D::translation({Length::fromSi(translation.x()), Length::fromSi(translation.y()),
                                          Length::fromSi(translation.z())})
        .after(motion);
}

void System::placeTarget(const VectorXd& x, std::size_t target, Point3D& origin, Direction3D& direction) const {
    const TargetGeometry& geometry = targets_[target];
    const RigidTransform3D transform = transformOf(x, geometry.component);
    origin = transform.apply(geometry.origin);
    direction = transform.apply(geometry.direction);
}

void System::evaluate(const VectorXd& x, VectorXd& residuals) const {
    residuals.resize(equationCount());
    for (std::size_t i = 0; i < equations_.size(); ++i) {
        const Equation& equation = equations_[i];
        Point3D pa;
        Point3D pb;
        Direction3D da = Direction3D::unitZ();
        Direction3D db = Direction3D::unitZ();
        placeTarget(x, equation.a, pa, da);
        placeTarget(x, equation.b, pb, db);
        const Vector3d Pa = vec(pa);
        const Vector3d Pb = vec(pb);
        const Vector3d Da = vec(da);
        const Vector3d Db = vec(db);
        const Vector3d delta = Pb - Pa;
        const double L = characteristic_;

        double residual = 0.0;
        switch (equation.kind) {
        case EquationKind::Parallel:
            residual = L * Da.cross(Db).dot(equation.complement[static_cast<std::size_t>(equation.axis)]);
            break;
        case EquationKind::Perpendicular:
            residual = L * Da.dot(Db);
            break;
        case EquationKind::Angle:
            residual = L * (Da.dot(Db) - equation.target);
            break;
        case EquationKind::OffsetAlong:
            residual = delta.dot(Da) - equation.target;
            break;
        case EquationKind::OffsetPerpendicular:
            residual = delta.dot(equation.complement[static_cast<std::size_t>(equation.axis)]);
            break;
        case EquationKind::SeparationPerpendicular: {
            const Vector3d perpendicular = delta - delta.dot(Da) * Da;
            residual = perpendicular.norm() - equation.target;
            break;
        }
        }
        residuals[static_cast<Index>(i)] = residual;
    }
}

void System::jacobianAtBase(MatrixXd& jacobian) const {
    const Index n = unknowns();
    const Index m = equationCount();
    jacobian.setZero(m, n);
    const VectorXd zero = VectorXd::Zero(n);

    for (std::size_t i = 0; i < equations_.size(); ++i) {
        const Equation& equation = equations_[i];
        Point3D pa;
        Point3D pb;
        Direction3D da = Direction3D::unitZ();
        Direction3D db = Direction3D::unitZ();
        placeTarget(zero, equation.a, pa, da);
        placeTarget(zero, equation.b, pb, db);
        const Vector3d Pa = vec(pa);
        const Vector3d Pb = vec(pb);
        const Vector3d Da = vec(da);
        const Vector3d Db = vec(db);
        const Vector3d delta = Pb - Pa;
        const double L = characteristic_;

        // The gradient of the residual with respect to each piece of world
        // geometry it is built from. Everything after this is the chain rule
        // onto the six unknowns of each component, which is the same for
        // every equation kind.
        Vector3d gPa = Vector3d::Zero();
        Vector3d gPb = Vector3d::Zero();
        Vector3d gDa = Vector3d::Zero();
        Vector3d gDb = Vector3d::Zero();

        switch (equation.kind) {
        case EquationKind::Parallel: {
            const Vector3d& e = equation.complement[static_cast<std::size_t>(equation.axis)];
            // (Da x Db) . e  =  Da . (Db x e)  =  Db . (e x Da)
            gDa = L * Db.cross(e);
            gDb = L * e.cross(Da);
            break;
        }
        case EquationKind::Perpendicular:
        case EquationKind::Angle:
            gDa = L * Db;
            gDb = L * Da;
            break;
        case EquationKind::OffsetAlong:
            gPa = -Da;
            gPb = Da;
            gDa = delta;
            break;
        case EquationKind::OffsetPerpendicular: {
            const Vector3d& e = equation.complement[static_cast<std::size_t>(equation.axis)];
            gPa = -e;
            gPb = e;
            break;
        }
        case EquationKind::SeparationPerpendicular: {
            const Vector3d perpendicular = delta - delta.dot(Da) * Da;
            const double norm = perpendicular.norm();
            if (norm <= kSeparationFloor) {
                break; // not differentiable where the axes meet
            }
            const Vector3d unit = perpendicular / norm;
            gPa = -unit;
            gPb = unit;
            // u = delta - (delta . Da) Da, and unit is perpendicular to Da,
            // so the (unit . Da) term drops out.
            gDa = -delta.dot(Da) * unit;
            break;
        }
        }

        const auto contribute = [&](std::size_t target, const Vector3d& gP, const Vector3d& gD) {
            const TargetGeometry& geometry = targets_[target];
            const Index slot = slot_.at(geometry.component);
            if (slot < 0) {
                return; // grounded: nothing can move it
            }
            Point3D origin;
            Direction3D direction = Direction3D::unitZ();
            placeTarget(zero, target, origin, direction);
            const Vector3d centre = vec(base_.at(geometry.component).translationPart());
            // dP/dv = I, dP/dw = -[P - c]x, dD/dw = -[D]x, so the row picks up
            // (P - c) x gP + D x gD for the rotation block.
            const Vector3d rotation = (vec(origin) - centre).cross(gP) + vec(direction).cross(gD);
            for (Index k = 0; k < 3; ++k) {
                jacobian(static_cast<Index>(i), 6 * slot + k) += gP[k];
                jacobian(static_cast<Index>(i), 6 * slot + 3 + k) += rotation[k];
            }
        };
        contribute(equation.a, gPa, gDa);
        contribute(equation.b, gPb, gDb);
    }
}

void System::rebase(const VectorXd& x) {
    if (x.size() != 0) {
        std::map<ComponentId, RigidTransform3D> moved;
        for (const auto& [id, transform] : base_) {
            moved.emplace(id, transformOf(x, id));
        }
        base_ = std::move(moved);
    }
    // Refresh each equation's frozen complement basis at the new base, so the
    // derivative is exact here and a finite difference about zero agrees.
    const VectorXd zero = VectorXd::Zero(unknowns());
    for (Equation& equation : equations_) {
        Point3D origin;
        Direction3D direction = Direction3D::unitZ();
        if (equation.projectFrom >= 0) {
            // A slide's roll row: the projection vector is the slide axis
            // itself, not a complement basis of the roll reference.
            placeTarget(zero, static_cast<std::size_t>(equation.projectFrom), origin, direction);
            equation.complement[0] = vec(direction);
            equation.complement[1] = Eigen::Vector3d::Zero();
            continue;
        }
        placeTarget(zero, equation.a, origin, direction);
        equation.complement = complementBasis(vec(direction));
    }
}

std::map<ComponentId, RigidTransform3D> System::transforms() const {
    return base_;
}

} // namespace bettercad::assembly::detail
