#include "core/geometry/RadiusLaw.hpp"

#include <algorithm>
#include <cmath>

namespace bettercad::geometry::detail {

RadiusLaw::RadiusLaw(const std::vector<RadiusStation>& stations) {
    knots_.push_back(-0.5);
    radii_.push_back(stations.front().radius.si());
    for (const RadiusStation& station : stations) {
        knots_.push_back(station.position);
        radii_.push_back(station.radius.si());
    }
    knots_.push_back(1.5);
    radii_.push_back(stations.back().radius.si());

    // The second derivatives M: for each inner knot
    //   h[i-1] M[i-1] + 2 (h[i-1] + h[i]) M[i] + h[i] M[i+1] = 6 (d[i] - d[i-1]),
    // and zero slope at both ends:
    //   2 h[0] M[0] + h[0] M[1] = 6 d[0],  h[n-2] M[n-2] + 2 h[n-2] M[n-1] = -6 d[n-2],
    // with h the knot spacing and d the slope of each span's chord. The
    // system is diagonally dominant; solved without pivoting.
    const std::size_t n = knots_.size();
    std::vector<double> lower(n, 0.0);
    std::vector<double> diagonal(n, 0.0);
    std::vector<double> upper(n, 0.0);
    std::vector<double> rhs(n, 0.0);
    const auto h = [&](std::size_t i) { return knots_[i + 1] - knots_[i]; };
    const auto d = [&](std::size_t i) { return (radii_[i + 1] - radii_[i]) / h(i); };
    diagonal[0] = 2.0 * h(0);
    upper[0] = h(0);
    rhs[0] = 6.0 * d(0);
    for (std::size_t i = 1; i + 1 < n; ++i) {
        lower[i] = h(i - 1);
        diagonal[i] = 2.0 * (h(i - 1) + h(i));
        upper[i] = h(i);
        rhs[i] = 6.0 * (d(i) - d(i - 1));
    }
    lower[n - 1] = h(n - 2);
    diagonal[n - 1] = 2.0 * h(n - 2);
    rhs[n - 1] = -6.0 * d(n - 2);
    for (std::size_t i = 1; i < n; ++i) {
        const double factor = lower[i] / diagonal[i - 1];
        diagonal[i] -= factor * upper[i - 1];
        rhs[i] -= factor * rhs[i - 1];
    }
    moments_.assign(n, 0.0);
    moments_[n - 1] = rhs[n - 1] / diagonal[n - 1];
    for (std::size_t i = n - 1; i-- > 0;) {
        moments_[i] = (rhs[i] - upper[i] * moments_[i + 1]) / diagonal[i];
    }
}

double RadiusLaw::operator()(double position) const {
    std::size_t i = 0;
    while (i + 2 < knots_.size() && position > knots_[i + 1]) {
        ++i;
    }
    const double h = knots_[i + 1] - knots_[i];
    const double a = (knots_[i + 1] - position) / h;
    const double b = (position - knots_[i]) / h;
    return a * radii_[i] + b * radii_[i + 1] +
           ((a * a * a - a) * moments_[i] + (b * b * b - b) * moments_[i + 1]) * h * h / 6.0;
}

std::vector<LawExtreme> RadiusLaw::candidates(std::size_t i) const {
    const double h = knots_[i + 1] - knots_[i];
    std::vector<LawExtreme> result{{knots_[i], radii_[i]}, {knots_[i + 1], radii_[i + 1]}};
    // On the span, with t in [0, 1] from knot i:
    //   r(t) = c0 + c1 t + c2 t^2 + c3 t^3
    // where c0 = r[i], c1 = r[i+1] - r[i] - h^2 (2 M[i] + M[i+1]) / 6,
    // c2 = h^2 M[i] / 2 and c3 = h^2 (M[i+1] - M[i]) / 6. Its slope is zero
    // where 3 c3 t^2 + 2 c2 t + c1 = 0.
    const double c0 = radii_[i];
    const double c1 = radii_[i + 1] - radii_[i] - h * h * (2.0 * moments_[i] + moments_[i + 1]) / 6.0;
    const double c2 = h * h * moments_[i] / 2.0;
    const double c3 = h * h * (moments_[i + 1] - moments_[i]) / 6.0;
    const auto add = [&](double t) {
        if (std::isfinite(t) && t > 0.0 && t < 1.0) {
            result.push_back({knots_[i] + t * h, c0 + t * (c1 + t * (c2 + t * c3))});
        }
    };
    const double qa = 3.0 * c3;
    const double qb = 2.0 * c2;
    const double qc = c1;
    if (qa == 0.0) {
        if (qb != 0.0) {
            add(-qc / qb);
        }
        return result;
    }
    const double discriminant = qb * qb - 4.0 * qa * qc;
    if (discriminant < 0.0) {
        return result;
    }
    // The root pair without cancellation: q / qa and qc / q.
    const double q = -0.5 * (qb + std::copysign(std::sqrt(discriminant), qb));
    add(q / qa);
    if (q != 0.0) {
        add(qc / q);
    }
    return result;
}

LawExtreme RadiusLaw::lowest(std::size_t span) const {
    const std::vector<LawExtreme> found = candidates(span + 1);
    return *std::ranges::min_element(found, {}, &LawExtreme::radius);
}

LawExtreme RadiusLaw::highest(std::size_t span) const {
    const std::vector<LawExtreme> found = candidates(span + 1);
    return *std::ranges::max_element(found, {}, &LawExtreme::radius);
}

double RadiusLaw::largest() const {
    double result = 0.0;
    for (std::size_t span = 0; span < stationSpans(); ++span) {
        result = std::max(result, highest(span).radius);
    }
    return result;
}

} // namespace bettercad::geometry::detail
