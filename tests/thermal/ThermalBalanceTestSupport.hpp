#pragma once

// Shared fixtures for the P18-BALANCE-001 tests.
//
// THE ORACLES ARE INDEPENDENT BY CONSTRUCTION. Every expected heat rate is typed in from a
// hand derivation (`g (T_hot - T_cold)`, `h A (T - Tinf)`, `1^T C dT`), comes from the closed
// form of a one-dimensional problem, or from the test's own dense arithmetic. Nothing here calls
// the production residual, reaction, storage or convection routines to learn what they should say:
// the production path is the thing under test, and a test that asked it for its own answer would
// be a mirror.
//
// SMALL NETWORKS HAVE NO MESH. A conductance network of two or three unknowns is a matrix and a
// vector, with the numbering of a hand mesh of `n` nodes on a line (as the solve's fixtures), and
// the loads and convection conditions a balance reads are the plain structs P18-LOAD and P18-BC
// publish, built by hand.

#include "ThermalTransientTestSupport.hpp"

#include <bettercad/thermal/ThermalBalance.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace batest {

using namespace trtest;
using thermal::BalanceAlgebra;
using thermal::BalanceFailure;
using thermal::BalanceProblem;
using thermal::BalanceStatus;
using thermal::ConvectionFacet;
using thermal::ConvectionHeat;
using thermal::IntegratedHeat;
using thermal::LoadHeat;
using thermal::PreparedConvectionBoundary;
using thermal::ReactionHeat;
using thermal::ThermalBalanceResult;
using thermal::ThermalHeatBalance;
using thermal::ThermalLoadContribution;
using thermal::ThermalLoadKind;

/// A network as matrices: `K`, optionally `C`, the heat vector and the prescribed unknowns.
struct Net {
    Algebra base;
    thermal::ConductanceMatrix k;
    std::optional<thermal::CapacityMatrix> c;
    Dense2 denseK;
    Dense2 denseC;
};

[[nodiscard]] inline Net makeNet(const Dense2& dk, const std::vector<double>& q,
                                 const std::vector<std::pair<std::size_t, double>>& prescribed = {},
                                 const std::optional<Dense2>& dc = std::nullopt) {
    Algebra base = makeAlgebra(dk, q, prescribed);
    thermal::ConductanceMatrix k{base.k};
    std::optional<thermal::CapacityMatrix> c;
    if (dc.has_value()) {
        c.emplace(sparseOf(*dc));
    }
    return Net{std::move(base), std::move(k), std::move(c), dk, dc.value_or(Dense2{})};
}

[[nodiscard]] inline BalanceAlgebra algebraOf(const Net& net, std::span<const ThermalLoadContribution> loads = {},
                                              std::span<const PreparedConvectionBoundary> convections = {}) {
    return BalanceAlgebra{.conductance = net.k,
                          .capacity = net.c.has_value() ? &*net.c : nullptr,
                          .heat = net.base.q,
                          .constraints = net.base.constraints,
                          .loads = loads,
                          .convections = convections};
}

/// What P18-LOAD would report for one load: an id, a kind and the heat rate it put in.
[[nodiscard]] inline ThermalLoadContribution loadHeat(std::uint64_t id, ThermalLoadKind kind, double watts) {
    return ThermalLoadContribution{.load = ThermalLoadId::fromValue(id),
                                   .kind = kind,
                                   .integrated = IntegratedHeat{.entities = 1, .heatRate = Power::fromSi(watts)}};
}
[[nodiscard]] inline ThermalLoadContribution surfaceContribution(std::uint64_t id, double watts) {
    return loadHeat(id, ThermalLoadKind::SurfaceHeatFlux, watts);
}
[[nodiscard]] inline ThermalLoadContribution generationContribution(std::uint64_t id, double watts) {
    return loadHeat(id, ThermalLoadKind::VolumetricHeatSource, watts);
}

/// A prepared convection condition of ONE triangle of area @p area over the three unknowns 0, 1, 2,
/// from the qualified P18-BC-001 kernel `computeTri3Convection`.
[[nodiscard]] inline PreparedConvectionBoundary oneTriangle(std::uint64_t id, double area, double h, double ambient,
                                                            std::array<std::uint64_t, 3> dofs = {0, 1, 2}) {
    auto term = thermal::computeTri3Convection(Area::fromSi(area), HeatTransferCoefficient::fromSi(h),
                                               Temperature::fromSi(ambient));
    INFO((term.has_value() ? std::string{} : term.error().message));
    REQUIRE(term.has_value());
    ConvectionFacet facet{.facet = ElementId::fromValue(static_cast<ElementId::ValueType>(id)),
                          .dofs = {ThermalDofIndex::fromValue(dofs[0]), ThermalDofIndex::fromValue(dofs[1]),
                                   ThermalDofIndex::fromValue(dofs[2])},
                          .term = *term};
    return PreparedConvectionBoundary{.condition = bid(id),
                                      .coefficient = HeatTransferCoefficient::fromSi(h),
                                      .ambient = Temperature::fromSi(ambient),
                                      .facets = {facet}};
}

/// The dense 3 x 3 `Kconv` of @p condition's single facet (from the typed accessors), and its `Qconv`.
[[nodiscard]] inline Dense2 denseKconv(const PreparedConvectionBoundary& condition) {
    Dense2 out(3, std::vector<double>(3, 0.0));
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            out[i][j] = condition.facets.at(0).term(i, j).si();
        }
    }
    return out;
}
[[nodiscard]] inline std::vector<double> denseQconv(const PreparedConvectionBoundary& condition) {
    std::vector<double> out(3, 0.0);
    for (std::size_t i = 0; i < 3; ++i) {
        out[i] = condition.facets.at(0).term.source(i).si();
    }
    return out;
}

/// `a + b` entrywise.
[[nodiscard]] inline Dense2 added(const Dense2& a, const Dense2& b) {
    Dense2 out = a;
    for (std::size_t i = 0; i < a.size(); ++i) {
        for (std::size_t j = 0; j < a[i].size(); ++j) {
            out[i][j] += b[i][j];
        }
    }
    return out;
}

[[nodiscard]] inline std::vector<Temperature> kelvin(const std::vector<double>& values) { return stateOf(values); }

/// The requirement that a balance was computed, returning it.
[[nodiscard]] inline ThermalHeatBalance mustBalance(const std::expected<ThermalHeatBalance, BalanceFailure>& result) {
    INFO((result.has_value() ? std::string{} : result.error().error.message));
    REQUIRE(result.has_value());
    return *result;
}

template <typename T>
[[nodiscard]] std::optional<BalanceProblem> balanceProblemOf(const std::expected<T, BalanceFailure>& result) {
    if (result.has_value()) {
        return std::nullopt;
    }
    return result.error().problem;
}

[[nodiscard]] inline double reactionOf(const ThermalHeatBalance& balance, std::size_t dof) {
    for (const ReactionHeat& reaction : balance.reactions) {
        if (reaction.dof.value() == dof) {
            return reaction.heatInto.si();
        }
    }
    FAIL("no reaction at unknown " << dof);
    return 0.0;
}

/// A sum over a vector in the order given, written here.
[[nodiscard]] inline double sumOf(const std::vector<double>& values) {
    double sum = 0.0;
    for (const double v : values) {
        sum += v;
    }
    return sum;
}

} // namespace batest
