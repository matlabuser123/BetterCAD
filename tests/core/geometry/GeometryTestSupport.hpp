#pragma once

#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Body.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// Tolerances for comparing kernel results with analytic solutions
// (measurements: examples/geometry_accuracy, docs/verification/P3):
//  - kRelTight: primitives and booleans whose intersection curves are exact
//    lines or circles. Measured <= 5e-15.
//  - kRelCurvedIntersection: the bicylinder, whose intersection curves are
//    ellipses. Measured 1.9e-13.
//  - kRelApproximatedIntersection: curved surfaces meeting in general curves
//    (e.g. an off-axis sphere and cylinder), which the kernel approximates to
//    its 1e-7 mm precision; for bodies of ~10 mm that bounds the volume error
//    near 1e-9. Measured 3.1e-12.
namespace bettercad::test {

inline constexpr double kRelTight = 1e-12;
inline constexpr double kRelCurvedIntersection = 1e-11;
inline constexpr double kRelApproximatedIntersection = 1e-9;
/// Absolute tolerance for positions, in millimetres.
inline constexpr double kPositionToleranceMm = 1e-9;

inline geometry::MassProperties requireProperties(const geometry::Body& body) {
    const auto properties = body.massProperties();
    REQUIRE(properties.has_value());
    return *properties;
}

inline void checkPoint(const Point3D& actual, double xMm, double yMm, double zMm) {
    using Catch::Matchers::WithinAbs;
    CHECK_THAT(actual.x.in(units::mm), WithinAbs(xMm, kPositionToleranceMm));
    CHECK_THAT(actual.y.in(units::mm), WithinAbs(yMm, kPositionToleranceMm));
    CHECK_THAT(actual.z.in(units::mm), WithinAbs(zMm, kPositionToleranceMm));
}

} // namespace bettercad::test
