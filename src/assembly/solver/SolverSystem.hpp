#pragma once

// The equation system of an assembly: unknowns, residual equations and their
// analytic Jacobian. Private to the solver.
//
// Unknowns are six per component that is free to move: a translation and a
// rotation vector, both increments from the component's CURRENT transform
// rather than absolute angles. That is what keeps the parameterization out of
// trouble -- the Jacobian is always evaluated at a zero increment, so there
// is no gimbal lock to reach and no quaternion to renormalize. After a step
// is accepted the increment is folded into the base transform and reset.
//
// Every residual is a length in metres, including the ones that are really
// angles, which are multiplied by the assembly's characteristic size. The
// sketch solver takes the same care for the same reason: "all residuals are
// lengths (metres), so the Jacobian is well scaled".

#include <bettercad/assembly/Mate.hpp>
#include <bettercad/assembly/Solver.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/math/RigidTransform.hpp>

#include <Eigen/Dense>

#include <array>
#include <map>
#include <vector>

namespace bettercad {
class Document;
}

namespace bettercad::assembly::detail {

/// One side of a mate, resolved to geometry in its part's own frame.
///
/// A plane and a face are both a point and a normal; an axis is a point and a
/// direction. Reducing both to the same pair is what lets one set of equations
/// serve every target kind.
struct TargetGeometry {
    ComponentId component{};
    bool planar = false;
    Point3D origin{};
    Direction3D direction = Direction3D::unitZ();
};

/// What an equation measures. Every kind yields metres.
enum class EquationKind {
    /// One component of cross(Da, Db), projected onto a basis of Da's
    /// complement. Two rows say the directions are parallel; three would say
    /// the same thing with one row always dependent, which would make the
    /// mate look redundant forever.
    Parallel,
    /// dot(Da, Db) -- the directions are perpendicular.
    Perpendicular,
    /// dot(Da, Db) - cos(target) -- a given angle between the directions.
    Angle,
    /// dot(Pb - Pa, Da) - target -- the signed offset of b from a's plane,
    /// along a's normal. Negative means the other side.
    OffsetAlong,
    /// One component of (Pb - Pa), projected onto a basis of Da's
    /// complement. Two rows say the axes meet.
    OffsetPerpendicular,
    /// |(Pb - Pa) perpendicular to Da| - target -- the separation of two
    /// axes, which has no side. The norm is not differentiable at zero, the
    /// same property the sketch solver's Length equation has and handles.
    SeparationPerpendicular,
};

/// One scalar equation.
struct Equation {
    EquationKind kind = EquationKind::Parallel;
    /// The mate it comes from; every equation here has one.
    MateId source{};
    /// Index into the system's targets, for each side.
    std::size_t a = 0;
    std::size_t b = 0;
    /// Which of the two complement directions this row projects onto.
    int axis = 0;
    /// The two directions perpendicular to a's direction, frozen at the base
    /// so that the equation is linear in them for one evaluation.
    std::array<Eigen::Vector3d, 2> complement{};
    /// A Parallel row normally projects cross(Da, Db) onto a basis of Da's
    /// complement. A slide's roll row projects it onto a third direction
    /// instead -- the slide axis -- and this is the target that direction
    /// comes from, or -1 for the usual complement basis.
    ///
    /// The mechanism is the same either way: the projection vector is frozen
    /// at the base and refreshed on rebase, so the analytic derivative is
    /// exact where it is taken. Nothing else about the row changes, which is
    /// why the roll needs no equation kind and no gradient of its own.
    int projectFrom = -1;
    /// Metres for OffsetAlong and SeparationPerpendicular, the cosine for
    /// Angle, unused otherwise.
    double target = 0.0;
};

/// The assembly's equations and the transforms they are evaluated at.
// Exported because the derivative tests link against it. That is the second
// cost of the white-box exception recorded in
// docs/verification/P13-SOLVE-001/: in a shared build the class's symbols
// have to leave the DLL, or the tests cannot link at all -- which is how the
// debug-shared preset found this. No public header declares System, so it is
// an exported implementation detail and not public API: reaching it still
// means including this private header on purpose.
class BETTERCAD_ASSEMBLY_EXPORT System {
public:
    /// Builds the system from the document's components and enabled mates.
    ///
    /// Fails when a mate names a component that is gone, a reference that
    /// does not resolve, or a face with no body to resolve it against: those
    /// are not solver outcomes.
    [[nodiscard]] static Result<System> build(const Document& document, const BodyLookup& bodies);

    [[nodiscard]] Eigen::Index unknowns() const noexcept { return 6 * static_cast<Eigen::Index>(free_.size()); }
    [[nodiscard]] Eigen::Index equationCount() const noexcept {
        return static_cast<Eigen::Index>(equations_.size());
    }
    [[nodiscard]] const std::vector<Equation>& equations() const noexcept { return equations_; }
    [[nodiscard]] double characteristicLength() const noexcept { return characteristic_; }

    /// Residuals at the increment @p x from the current base transforms.
    void evaluate(const Eigen::VectorXd& x, Eigen::VectorXd& residuals) const;

    /// dF/dx at the base, which is where x is zero.
    ///
    /// Split from evaluate() on purpose. Each equation's complement basis is
    /// frozen at the base, so the derivative is exact there and a finite
    /// difference about zero agrees with it exactly. Recomputing the basis
    /// inside an evaluation would make a correct Jacobian disagree with a
    /// correct finite difference, and the verification would fail on working
    /// code.
    void jacobianAtBase(Eigen::MatrixXd& jacobian) const;

    /// Folds @p x into the base transforms, refreshes the frozen bases and
    /// returns to a zero increment.
    void rebase(const Eigen::VectorXd& x);

    /// The transform of every component, grounded ones included.
    [[nodiscard]] std::map<ComponentId, RigidTransform3D> transforms() const;

private:
    /// A component's transform under increment @p x.
    [[nodiscard]] RigidTransform3D transformOf(const Eigen::VectorXd& x, ComponentId component) const;
    /// Where a target's geometry is, under increment @p x.
    void placeTarget(const Eigen::VectorXd& x, std::size_t target, Point3D& origin, Direction3D& direction) const;

    std::vector<TargetGeometry> targets_;
    std::vector<Equation> equations_;
    /// Components that may move, in ascending ID order; the index into this
    /// is the block of six unknowns.
    std::vector<ComponentId> free_;
    /// Every component's transform, updated by rebase().
    std::map<ComponentId, RigidTransform3D> base_;
    /// Index into free_, or -1 for a grounded component.
    std::map<ComponentId, Eigen::Index> slot_;
    /// The size the angular residuals are scaled by.
    double characteristic_ = 1.0;
};

} // namespace bettercad::assembly::detail
