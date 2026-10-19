#pragma once

// Shared fixtures for the P18-ASSEMBLY-001 tests.
//
// THE DENSE ORACLE IS INDEPENDENT BY CONSTRUCTION. `DenseOracle` scatters known
// local matrices into a dense array with nested loops written here, and it never
// calls a production scatter, a production sparse helper or the assembler. The
// local matrices it is fed are written out by hand in the tests (for the unit and
// the second tetrahedron they are derived from the shape functions in the test's
// comments), or come from the qualified element kernels where a mesh is too large
// to write down; in that second case what is independent is the SCATTER, which is
// what this milestone owns.

#include "ThermalBoundaryTestSupport.hpp"

#include <bettercad/thermal/ThermalSystem.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <vector>

namespace actest {

using namespace bctest;
using thermal::AssemblyFailure;
using thermal::AssemblyProblem;
using thermal::CapacityMatrix;
using thermal::ConductanceMatrix;
using thermal::ThermalAssemblyData;
using thermal::ThermalCapacity;
using thermal::ThermalSystem;
using thermal::ThermalSystemSource;

/// A hand-built mesh with tetrahedra and triangles, and its numbering.
struct AsmHand {
    meshing::Mesh mesh;
    std::vector<ElementId> triangles;
    std::vector<ElementId> tets;
    ThermalDofMap dofs;
};

using Nodes = std::vector<std::pair<NodeId::ValueType, Point3D>>;

/// Nodes (strictly increasing handles, as MeshBuilder requires), triangles and
/// tetrahedra given as indices into @p nodes.
[[nodiscard]] inline AsmHand makeAsmHand(const Nodes& nodes, const std::vector<std::array<std::size_t, 3>>& triangles,
                                         const std::vector<std::array<std::size_t, 4>>& tets) {
    meshing::MeshBuilder builder;
    std::vector<NodeId> ids;
    for (const auto& [handle, position] : nodes) {
        const Result<NodeId> added = builder.addNode(NodeId::fromValue(handle), position);
        INFO((added.has_value() ? std::string{} : added.error().message));
        REQUIRE(added.has_value());
        ids.push_back(*added);
    }
    std::vector<ElementId> triangleIds;
    for (const auto& c : triangles) {
        const Result<ElementId> added = builder.addTriangle({ids.at(c[0]), ids.at(c[1]), ids.at(c[2])}, RegionId::fromValue(1));
        INFO((added.has_value() ? std::string{} : added.error().message));
        REQUIRE(added.has_value());
        triangleIds.push_back(*added);
    }
    std::vector<ElementId> tetIds;
    for (const auto& c : tets) {
        const Result<ElementId> added =
            builder.addTetrahedron({ids.at(c[0]), ids.at(c[1]), ids.at(c[2]), ids.at(c[3])}, RegionId::fromValue(1));
        INFO((added.has_value() ? std::string{} : added.error().message));
        REQUIRE(added.has_value());
        tetIds.push_back(*added);
    }
    meshing::Mesh mesh = builder.build();
    Result<ThermalDofMap> map = thermal::buildThermalDofMap(mesh);
    INFO((map.has_value() ? std::string{} : map.error().message));
    REQUIRE(map.has_value());
    return AsmHand{std::move(mesh), std::move(triangleIds), std::move(tetIds), std::move(*map)};
}

/// The unit tetrahedron (0,0,0) (1,0,0) (0,1,0) (0,0,1): V = 1/6, positively oriented.
[[nodiscard]] inline Nodes unitTetNodes(NodeId::ValueType first = 1) {
    return {{first, at(0, 0, 0)}, {first + 1, at(1, 0, 0)}, {first + 2, at(0, 1, 0)}, {first + 3, at(0, 0, 1)}};
}

/// Two tetrahedra sharing the face (1,0,0) (0,1,0) (0,0,1): the unit one, and one
/// with fifth node (1,1,1), V = 1/3. Five unknowns, global 0..4.
[[nodiscard]] inline Nodes twoTetNodes() {
    return {{1, at(0, 0, 0)}, {2, at(1, 0, 0)}, {3, at(0, 1, 0)}, {4, at(0, 0, 1)}, {5, at(1, 1, 1)}};
}

[[nodiscard]] inline AsmHand makeTwoTet() {
    // A = nodes [0 1 2 3]; B = nodes [1 2 3 4], both positively oriented.
    return makeAsmHand(twoTetNodes(), {{0, 1, 2}}, {{0, 1, 2, 3}, {1, 2, 3, 4}});
}

[[nodiscard]] inline ThermalDofIndex dofIx(std::uint64_t v) { return ThermalDofIndex::fromValue(v); }

/// The heat input a constant generation produces in every tetrahedron, from the
/// qualified P18-LOAD-001 seam. That conversion is upstream's; this fixture only
/// asks for it.
[[nodiscard]] inline std::vector<Power> generationHeat(const AsmHand& hand, double generation) {
    std::vector<Power> q(static_cast<std::size_t>(hand.dofs.dofCount()), Power{});
    const auto done = thermal::integrateVolumetricHeatSource(hand.mesh, hand.dofs, hand.tets,
                                                            VolumetricHeatSource::fromSi(generation), q);
    INFO((done.has_value() ? std::string{} : done.error().error.message));
    REQUIRE(done.has_value());
    return q;
}

[[nodiscard]] inline std::vector<Power> noHeat(const AsmHand& hand) {
    return std::vector<Power>(static_cast<std::size_t>(hand.dofs.dofCount()), Power{});
}

/// A prepared convection condition over @p facets of @p hand, from the qualified
/// P18-BC-001 seam. (A plain struct, so a test can build it by hand.)
[[nodiscard]] inline thermal::PreparedConvectionBoundary convectionOver(const AsmHand& hand, std::uint64_t id,
                                                                        const std::vector<ElementId>& facets,
                                                                        double h, double ambient) {
    auto done = thermal::integrateConvection(hand.mesh, hand.dofs, facets, HeatTransferCoefficient::fromSi(h),
                                            Temperature::fromSi(ambient));
    INFO((done.has_value() ? std::string{} : done.error().error.message));
    REQUIRE(done.has_value());
    return thermal::PreparedConvectionBoundary{.condition = bid(id),
                                               .coefficient = HeatTransferCoefficient::fromSi(h),
                                               .ambient = Temperature::fromSi(ambient),
                                               .facets = std::move(*done)};
}

/// Assembles through the seam, REQUIRING success.
[[nodiscard]] inline ThermalSystem assemble(const AsmHand& hand, double k, std::optional<ThermalCapacity> capacity,
                                            const std::vector<Power>& heat,
                                            const std::vector<thermal::PreparedConvectionBoundary>& convections = {}) {
    const ThermalAssemblyData data{.mesh = hand.mesh,
                                   .map = hand.dofs,
                                   .conductivity = ThermalConductivity::fromSi(k),
                                   .capacity = capacity,
                                   .heatInput = heat,
                                   .convections = convections};
    auto built = thermal::assembleThermalSystemFrom(data, ThermalSystemSource{});
    INFO((built.has_value() ? std::string{} : built.error().error.message));
    REQUIRE(built.has_value());
    return std::move(*built);
}

[[nodiscard]] inline ThermalCapacity capacityOf(double density, double specificHeat) {
    return ThermalCapacity{.density = Density::fromSi(density), .specificHeatCapacity = SpecificHeatCapacity::fromSi(specificHeat)};
}

// -----------------------------------------------------------------------
// The independent dense oracle. Test-only.
// -----------------------------------------------------------------------

struct DenseOracle {
    std::size_t n;
    std::vector<double> k;
    std::vector<double> c;
    std::vector<double> q;

    explicit DenseOracle(std::size_t unknowns) : n(unknowns), k(unknowns * unknowns, 0.0), c(unknowns * unknowns, 0.0), q(unknowns, 0.0) {}

    /// `target(g[i], g[j]) += local[i][j]` -- the whole scatter, written out.
    static void addLocal(std::vector<double>& target, std::size_t n, const std::vector<std::size_t>& global,
                         const std::vector<std::vector<double>>& local) {
        for (std::size_t i = 0; i < global.size(); ++i) {
            for (std::size_t j = 0; j < global.size(); ++j) {
                target[global[i] * n + global[j]] += local[i][j];
            }
        }
    }
    void addK(const std::vector<std::size_t>& global, const std::vector<std::vector<double>>& local) { addLocal(k, n, global, local); }
    void addC(const std::vector<std::size_t>& global, const std::vector<std::vector<double>>& local) { addLocal(c, n, global, local); }
    void addQ(const std::vector<std::size_t>& global, const std::vector<double>& local) {
        for (std::size_t i = 0; i < global.size(); ++i) {
            q[global[i]] += local[i];
        }
    }
};

/// A sparse matrix as a dense array, TEST-SIDE ONLY, built from `coeffSi` rather
/// than from the CSR arrays so that it also exercises the lookup.
[[nodiscard]] inline std::vector<double> toDense(const thermal::ThermalSparseMatrix& m) {
    std::vector<double> out(m.rows() * m.columns(), 0.0);
    for (std::size_t r = 0; r < m.rows(); ++r) {
        for (std::size_t c = 0; c < m.columns(); ++c) {
            out[r * m.columns() + c] = m.coeffSi(r, c);
        }
    }
    return out;
}

[[nodiscard]] inline double maxAbsDifference(const std::vector<double>& a, const std::vector<double>& b) {
    REQUIRE(a.size() == b.size());
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        worst = std::max(worst, std::abs(a[i] - b[i]));
    }
    return worst;
}

[[nodiscard]] inline double maxAbs(const std::vector<double>& a) {
    double biggest = 0.0;
    for (const double v : a) {
        biggest = std::max(biggest, std::abs(v));
    }
    return biggest;
}

[[nodiscard]] inline std::vector<double> qDense(const ThermalSystem& s) {
    return std::vector<double>(s.heat().values().begin(), s.heat().values().end());
}

[[nodiscard]] inline double sumOf(const std::vector<double>& a) {
    double s = 0.0;
    for (const double v : a) {
        s += v;
    }
    return s;
}

/// `x^T A y` for a dense array, written out.
[[nodiscard]] inline double bilinear(const std::vector<double>& a, std::size_t n, const std::vector<double>& x,
                                     const std::vector<double>& y) {
    double s = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            s += x[i] * a[i * n + j] * y[j];
        }
    }
    return s;
}

} // namespace actest
