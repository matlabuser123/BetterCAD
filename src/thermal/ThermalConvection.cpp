// The local convection (Robin) boundary term of one linear triangle
// (P18-BC-001).
//
// Kconv = (h A / 12) [2 1 1; 1 2 1; 1 1 2]   Qconv = (h T_inf A / 3) [1 1 1].
//
// CLOSED FORM, NOT QUADRATURE. The integrands are constant times a product of
// linear shape functions, so the integrals are exact: the diagonal of
// `integral(N^T N dA)` is A/6 and the off-diagonal A/12, and each `integral(N dA)`
// is A/3. A one-point centroid rule would put h A / 9 in every entry; that is
// not an approximation of this matrix, it is a different one.
//
// NO CONDUCTIVITY, NO NORMAL, NO ABSOLUTE VALUE. The terms are functions of h,
// T_inf and the unsigned area. Nothing here is ordered by anything but the
// triangle's own corner order, and nothing is static.

#include <bettercad/thermal/ThermalConvection.hpp>

#include <bettercad/meshing/Mesh.hpp>

#include <cmath>
#include <format>
#include <utility>

namespace bettercad::thermal {

std::string_view toString(ConvectionProblem problem) noexcept {
    switch (problem) {
    case ConvectionProblem::InvalidArea:
        return "invalid_area";
    case ConvectionProblem::InvalidCoefficient:
        return "invalid_coefficient";
    case ConvectionProblem::InvalidAmbient:
        return "invalid_ambient";
    case ConvectionProblem::NonFiniteResult:
        return "non_finite_result";
    }
    return "unknown";
}

ThermalConductance Tri3Convection::operator()(std::size_t row, std::size_t column) const noexcept {
    if (row >= kTri3Nodes || column >= kTri3Nodes) {
        return ThermalConductance{};
    }
    return ThermalConductance::fromSi(entries_[row * kTri3Nodes + column]);
}

Power Tri3Convection::source(std::size_t node) const noexcept {
    if (node >= kTri3Nodes) {
        return Power{};
    }
    return Power::fromSi(source_[node]);
}

std::array<Power, kTri3Nodes> Tri3Convection::residualFrom(const Tri3Temperatures& temperatures) const noexcept {
    // Kconv T - Qconv, row by row in the stored order: one serial sum per row.
    std::array<Power, kTri3Nodes> loss{};
    for (std::size_t row = 0; row < kTri3Nodes; ++row) {
        double sum = 0.0;
        for (std::size_t column = 0; column < kTri3Nodes; ++column) {
            sum = sum + entries_[row * kTri3Nodes + column] * temperatures[column].si();
        }
        loss[row] = Power::fromSi(sum - source_[row]);
    }
    return loss;
}

std::optional<ConvectionProblem> convectionProblem(Area area, HeatTransferCoefficient coefficient,
                                                   Temperature ambient) noexcept {
    // `!(x > 0)` and `!(x >= 0)` rather than `x <= 0` and `x < 0`: a NaN makes
    // every ordered comparison false, so the negated forms refuse it and the
    // plain ones would let it through.
    if (!std::isfinite(area.si()) || !(area.si() > 0.0)) {
        return ConvectionProblem::InvalidArea;
    }
    if (!std::isfinite(coefficient.si()) || !(coefficient.si() >= 0.0)) {
        return ConvectionProblem::InvalidCoefficient;
    }
    if (!isAbsoluteTemperature(ambient)) {
        return ConvectionProblem::InvalidAmbient;
    }
    return std::nullopt;
}

Result<Tri3Convection> computeTri3Convection(Area area, HeatTransferCoefficient coefficient, Temperature ambient) {
    if (const std::optional<ConvectionProblem> problem = convectionProblem(area, coefficient, ambient);
        problem.has_value()) {
        switch (*problem) {
        case ConvectionProblem::InvalidArea:
            return makeError(ErrorCode::InvalidArgument,
                             std::format("a boundary triangle of area {} m^2 cannot carry a convection "
                                         "term",
                                         area.si()));
        case ConvectionProblem::InvalidCoefficient:
            return makeError(ErrorCode::InvalidArgument,
                             std::format("a convection coefficient of {} W/(m^2 K) is not finite and "
                                         "non-negative",
                                         coefficient.si()));
        case ConvectionProblem::InvalidAmbient:
            return makeError(ErrorCode::InvalidArgument,
                             std::format("an ambient of {} K is not a finite absolute temperature",
                                         ambient.si()));
        case ConvectionProblem::NonFiniteResult:
            break;
        }
    }

    // h A / 12, formed as h * (A / 12). The division comes first so that the
    // product overflows only when the entry itself would: (h A) / 12 overflows
    // whenever h A does, even where h A / 12 is representable. The same ordering
    // P18-LOAD-001 chose for its shares, for the same reason.
    const double unit = coefficient.si() * (area.si() / 12.0);
    const double diagonal = 2.0 * unit;
    const double off = unit;
    // h T_inf A / 3, formed as (h * (A / 3)) * T_inf.
    const double perNode = (coefficient.si() * (area.si() / 3.0)) * ambient.si();

    Tri3Convection out;
    for (std::size_t row = 0; row < kTri3Nodes; ++row) {
        for (std::size_t column = 0; column < kTri3Nodes; ++column) {
            out.entries_[row * kTri3Nodes + column] = (row == column) ? diagonal : off;
        }
        out.source_[row] = perNode;
    }
    for (const double entry : out.entries_) {
        if (!std::isfinite(entry)) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("the convection matrix of h = {} W/(m^2 K) over {} m^2 is not "
                                         "finite",
                                         coefficient.si(), area.si()));
        }
    }
    for (const double entry : out.source_) {
        if (!std::isfinite(entry)) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("the convection source of h = {} W/(m^2 K) and T_inf = {} K "
                                         "over {} m^2 is not finite",
                                         coefficient.si(), ambient.si(), area.si()));
        }
    }
    out.area_ = area;
    out.coefficient_ = coefficient;
    out.ambient_ = ambient;
    return out;
}

Result<Tri3Convection> computeTri3Convection(const Tri3Corners& corners, HeatTransferCoefficient coefficient,
                                             Temperature ambient) {
    return computeTri3Convection(meshing::triangleArea(corners[0], corners[1], corners[2]), coefficient, ambient);
}

} // namespace bettercad::thermal
