#pragma once

// The radius law of a variable-radius fillet (see VariableFillet.hpp),
// computed without the kernel.

#include <bettercad/core/geometry/VariableFillet.hpp>

#include <cstddef>
#include <vector>

namespace bettercad::geometry::detail {

/// Where and how far the law goes on one span.
struct LawExtreme {
    double position = 0.0;
    double radius = 0.0; // metres
};

/// The clamped cubic spline through (-1/2, r_first), the stations and
/// (3/2, r_last), with zero slope at both ends. Radii in metres.
class RadiusLaw {
public:
    /// @p stations must have valid positions (validate(), WithoutLaw).
    explicit RadiusLaw(const std::vector<RadiusStation>& stations);

    /// The radius at @p position; positions outside [-1/2, 3/2] take the
    /// nearest end span's cubic.
    [[nodiscard]] double operator()(double position) const;

    /// The number of spans between stations (the stations less one).
    [[nodiscard]] std::size_t stationSpans() const noexcept { return knots_.size() - 3; }
    /// The lowest and highest radius between station @p span and the next,
    /// both ends included.
    [[nodiscard]] LawExtreme lowest(std::size_t span) const;
    [[nodiscard]] LawExtreme highest(std::size_t span) const;
    /// The highest radius on the edge, positions 0 to 1.
    [[nodiscard]] double largest() const;

private:
    /// Values at the ends of knot span @p i and at every point where the
    /// cubic's slope is zero inside it.
    [[nodiscard]] std::vector<LawExtreme> candidates(std::size_t i) const;

    std::vector<double> knots_;
    std::vector<double> radii_;
    /// Second derivatives at the knots.
    std::vector<double> moments_;
};

} // namespace bettercad::geometry::detail
