#include <bettercad/core/math/BSpline.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace bettercad {

namespace {

// Poles closer than this in total form no curve (the sketch tolerance).
constexpr double kLengthTolerance = 1e-10; // metres

} // namespace

Result<void> UniformBSpline::checkStructure(std::size_t poleCount, int degree) {
    if (degree < kMinBSplineDegree || degree > kMaxBSplineDegree) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("a spline's degree must be {} to {}, got {}", kMinBSplineDegree,
                                     kMaxBSplineDegree, degree));
    }
    if (poleCount < static_cast<std::size_t>(degree) + 1) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("a spline of degree {} needs at least {} poles, got {}", degree, degree + 1,
                                     poleCount));
    }
    return {};
}

Result<UniformBSpline> UniformBSpline::create(std::span<const Point2D> poles, int degree, bool periodic) {
    if (auto valid = checkStructure(poles.size(), degree); !valid) {
        return std::unexpected(valid.error());
    }
    const auto d = static_cast<std::size_t>(degree);
    const std::size_t n = poles.size();
    double polygon = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        if (!isFinite(poles[i].x) || !isFinite(poles[i].y)) {
            return makeError(ErrorCode::InvalidArgument, std::format("pole {} of a spline is not finite", i + 1));
        }
        if (i + 1 < n || periodic) {
            polygon += distance(poles[i], poles[(i + 1) % n]).si();
        }
    }
    if (!(polygon > kLengthTolerance)) {
        return makeError(ErrorCode::InvalidArgument, "a spline's poles all coincide");
    }

    UniformBSpline spline;
    spline.degree_ = degree;
    spline.periodic_ = periodic;
    spline.poleCount_ = n;
    const std::size_t count = periodic ? n + d : n;
    spline.poleX_.reserve(count);
    spline.poleY_.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        spline.poleX_.push_back(poles[i % n].x.si());
        spline.poleY_.push_back(poles[i % n].y.si());
    }
    if (periodic) {
        for (std::size_t k = 0; k <= count + d; ++k) {
            spline.knots_.push_back(static_cast<double>(k));
        }
        spline.first_ = static_cast<double>(d);
        spline.last_ = static_cast<double>(n + d);
    } else {
        const std::size_t spans = n - d;
        spline.knots_.assign(d + 1, 0.0);
        for (std::size_t k = 1; k < spans; ++k) {
            spline.knots_.push_back(static_cast<double>(k));
        }
        spline.knots_.insert(spline.knots_.end(), d + 1, static_cast<double>(spans));
        spline.first_ = 0.0;
        spline.last_ = static_cast<double>(spans);
    }
    return spline;
}

UniformBSpline::Sample UniformBSpline::evaluate(double u) const {
    const auto d = static_cast<std::size_t>(degree_);
    const std::size_t count = poleX_.size();
    u = std::clamp(u, first_, last_);
    // The span k with knots[k] <= u < knots[k + 1], d <= k < count.
    std::size_t k = d;
    while (k + 1 < count && knots_[k + 1] <= u) {
        ++k;
    }
    std::array<double, kMaxBSplineDegree + 1> x{};
    std::array<double, kMaxBSplineDegree + 1> y{};
    for (std::size_t j = 0; j <= d; ++j) {
        x[j] = poleX_[k - d + j];
        y[j] = poleY_[k - d + j];
    }
    Sample sample;
    for (std::size_t r = 1; r <= d; ++r) {
        if (r == d) {
            // Before the last step the two points span the curve's tangent:
            // C'(u) = d (x[d] - x[d-1]) / (knots[k+1] - knots[k]).
            const double span = knots_[k + 1] - knots_[k];
            sample.dx = static_cast<double>(d) * (x[d] - x[d - 1]) / span;
            sample.dy = static_cast<double>(d) * (y[d] - y[d - 1]) / span;
        }
        for (std::size_t j = d; j >= r; --j) {
            const double left = knots_[k - d + j];
            const double right = knots_[k + 1 + j - r];
            const double alpha = (u - left) / (right - left);
            x[j] = (1.0 - alpha) * x[j - 1] + alpha * x[j];
            y[j] = (1.0 - alpha) * y[j - 1] + alpha * y[j];
        }
    }
    sample.x = x[d];
    sample.y = y[d];
    return sample;
}

std::vector<std::vector<std::array<double, 2>>> UniformBSpline::bezierPieces() const {
    const auto d = static_cast<std::size_t>(degree_);
    const std::size_t count = poleX_.size();
    std::vector<std::vector<std::array<double, 2>>> pieces;
    // Every span k in [d, count) of the knot vector is non-empty (see create()).
    for (std::size_t k = d; k < count; ++k) {
        std::vector<std::array<double, 2>> control;
        control.reserve(d + 1);
        for (std::size_t i = 0; i <= d; ++i) {
            // Control point i is the blossom at (knots[k] d - i times,
            // knots[k + 1] i times): de Boor's algorithm with that parameter
            // at each level.
            std::array<double, kMaxBSplineDegree + 1> x{};
            std::array<double, kMaxBSplineDegree + 1> y{};
            for (std::size_t j = 0; j <= d; ++j) {
                x[j] = poleX_[k - d + j];
                y[j] = poleY_[k - d + j];
            }
            for (std::size_t r = 1; r <= d; ++r) {
                const double t = r <= d - i ? knots_[k] : knots_[k + 1];
                for (std::size_t j = d; j >= r; --j) {
                    const double left = knots_[k - d + j];
                    const double right = knots_[k + 1 + j - r];
                    const double alpha = (t - left) / (right - left);
                    x[j] = (1.0 - alpha) * x[j - 1] + alpha * x[j];
                    y[j] = (1.0 - alpha) * y[j - 1] + alpha * y[j];
                }
            }
            control.push_back({x[d], y[d]});
        }
        pieces.push_back(std::move(control));
    }
    return pieces;
}

std::vector<double> UniformBSpline::knotValues() const {
    std::vector<double> values;
    const std::size_t distinct = periodic_ ? poleCount_ + 1 : poleCount_ - static_cast<std::size_t>(degree_) + 1;
    for (std::size_t k = 0; k < distinct; ++k) {
        values.push_back(static_cast<double>(k));
    }
    return values;
}

std::vector<int> UniformBSpline::knotMultiplicities() const {
    const std::size_t distinct = knotValues().size();
    std::vector<int> multiplicities(distinct, 1);
    if (!periodic_) {
        multiplicities.front() = degree_ + 1;
        multiplicities.back() = degree_ + 1;
    }
    return multiplicities;
}

const GaussLegendreRule& gaussLegendreRule() {
    static const GaussLegendreRule rule = [] {
        GaussLegendreRule result;
        const auto n = static_cast<int>(GaussLegendreRule::kPoints);
        // The Legendre polynomial P_n and its derivative at x.
        const auto legendre = [n](double x) {
            double p0 = 1.0;
            double p1 = x;
            for (int k = 2; k <= n; ++k) {
                const double p2 = ((2.0 * k - 1.0) * x * p1 - (k - 1.0) * p0) / k;
                p0 = p1;
                p1 = p2;
            }
            return std::array<double, 2>{p1, n * (x * p1 - p0) / (x * x - 1.0)};
        };
        for (int i = 0; i < n; ++i) {
            // Newton's method from the classical estimate of the i-th root; a
            // fixed number of steps keeps the result deterministic.
            double x = std::cos(std::numbers::pi * (i + 0.75) / (n + 0.5));
            for (int step = 0; step < 20; ++step) {
                const auto [p, dp] = legendre(x);
                x -= p / dp;
            }
            const double dp = legendre(x)[1];
            const auto index = static_cast<std::size_t>(i);
            result.nodes[index] = 0.5 * (1.0 + x);
            result.weights[index] = 1.0 / ((1.0 - x * x) * dp * dp);
        }
        return result;
    }();
    return rule;
}

} // namespace bettercad
