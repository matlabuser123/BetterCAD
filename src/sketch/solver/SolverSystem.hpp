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
    // P12-SKETCH-001
    /// (cross(d1, d2) cos t - dot(d1, d2) sin t) / |d2| = |d1| sin(angle - t),
    /// d1 = p1 - p0, d2 = p3 - p2, t = target
    Angle,
    /// signed distance of p0 (a centre) from line p1 -> p2, minus sign * R0.
    /// A tangent at a joint (a shared end point) is instead Perpendicular
    /// (the line against the radius there), or Parallel for two arcs (their
    /// radii there): this form is second order at a joint.
    LineTangent,
    /// |p1 - p0| - (k0 R0 + k1 R1), with p0 and p1 the centres
    CircleTangent,
    MidX,           ///< p0.x - (p1.x + p2.x) / 2
    MidY,           ///< p0.y - (p1.y + p2.y) / 2
    MidpointOnLine, ///< signed distance of (p0 + p1) / 2 from line p2 -> p3
};

/// An equation. Radius terms Ri (the tangents) are the radius variable r[i]
/// of a circle, or, for an arc (arcRadius[i]), the distance from its centre to
/// its start point: p[3] from p[0] for LineTangent, p[2 + i] from p[i] for
/// CircleTangent.
struct Equation {
    EquationKind kind = EquationKind::DiffX;
    /// Constraint the equation comes from; invalid for internal arc equations.
    ConstraintId source{};
    std::array<PointRef, 4> p{};
    std::array<RadiusRef, 2> r{};
    double target = 0.0;
    double sign = 1.0;
    std::array<bool, 2> arcRadius{};
    /// k0 and k1 of CircleTangent: (1, 1) for external contact, (1, -1) or
    /// (-1, 1) for internal contact.
    std::array<double, 2> coefficient{1.0, 1.0};
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
