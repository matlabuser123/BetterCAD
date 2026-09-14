#pragma once

// The equation system of a sketch: unknowns, residual equations and their
// analytic Jacobian. Private to the solver.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <Eigen/Dense>

#include <array>
#include <vector>

namespace bettercad::sketch::detail {

/// A point of the system: variable coordinates (indices into x) or, for a
/// fixed point, constant coordinates.
struct PointRef {
    EntityId entity{};
    Eigen::Index ix = -1;
    Eigen::Index iy = -1;
    double fx = 0.0;
    double fy = 0.0;
};

/// A circle radius (always a variable).
struct RadiusRef {
    EntityId entity{};
    Eigen::Index index = -1;
};

enum class EquationKind {
    DiffX,            ///< p0.x - p1.x
    DiffY,            ///< p0.y - p1.y
    Length,           ///< |p1 - p0| - target
    RadiusValue,      ///< r0 - target
    LengthDiff,       ///< |p1 - p0| - |p3 - p2|
    RadiusDiff,       ///< r0 - r1
    LengthRadiusDiff, ///< |p1 - p0| - r0
    PointLine,        ///< signed distance of p0 from line p1 -> p2, minus sign * target
    Parallel,         ///< cross(p1 - p0, p3 - p2) / |p3 - p2|
    Perpendicular,    ///< dot(p1 - p0, p3 - p2) / |p3 - p2|
};

struct Equation {
    EquationKind kind = EquationKind::DiffX;
    /// Constraint the equation comes from; invalid for internal arc equations.
    ConstraintId source{};
    std::array<PointRef, 4> p{};
    std::array<RadiusRef, 2> r{};
    double target = 0.0;
    double sign = 1.0;
};

class System {
public:
    /// Builds the system from the sketch's entities and enabled constraints.
    [[nodiscard]] static Result<System> build(const Sketch& sketch);

    [[nodiscard]] Eigen::Index unknowns() const noexcept { return initial_.size(); }
    [[nodiscard]] Eigen::Index equationCount() const noexcept {
        return static_cast<Eigen::Index>(equations_.size());
    }
    [[nodiscard]] const Eigen::VectorXd& initial() const noexcept { return initial_; }
    [[nodiscard]] const std::vector<Equation>& equations() const noexcept { return equations_; }

    /// Residuals F(x) and, if @p jacobian is not null, dF/dx.
    void evaluate(const Eigen::VectorXd& x, Eigen::VectorXd& residuals,
                  Eigen::MatrixXd* jacobian) const;

    /// Fails if a line, arc or circle of the solution has (near) zero size.
    [[nodiscard]] Result<void> checkNonDegenerate(const Eigen::VectorXd& x, double tolerance) const;

    /// Writes the solution into the sketch; returns whether anything changed.
    [[nodiscard]] Result<bool> apply(const Eigen::VectorXd& x, Sketch& sketch) const;

private:
    struct Segment {
        EntityId entity{};
        PointRef a{};
        PointRef b{};
    };

    std::vector<Equation> equations_;
    Eigen::VectorXd initial_;
    std::vector<PointRef> points_;
    std::vector<RadiusRef> radii_;
    std::vector<Segment> segments_; ///< lines (start, end) and arc radii (centre, start)
};

} // namespace bettercad::sketch::detail
