#pragma once

#include "reference/ThermalReferenceSupport.hpp"
#include "thermal/ThermalPostTestSupport.hpp"

#include <bettercad/thermal/ThermalSolve.hpp>

#include <functional>

// The affine PATCH on a real mesh (P18-REFMOD-001): the temperature of every BOUNDARY node is imposed from a given field, every
// interior node is left free, and the production chain (assembly, constraint map, steady solve, recovery) is run on the mesh
// through the algebraic seams. For an affine field a linear tetrahedron is exact, so the free nodes must land on the field and
// every element's gradient must be the field's gradient -- whatever the mesh, however its interior nodes happen to be placed.
//
// No canonical boundary condition can say "a different temperature at each node", which is why this goes through the seam, and
// why it is the one way to impose a field that is NOT aligned with the body's own axes.
namespace bettercad::test::thermalref {

struct PatchSolution {
    std::vector<double> temperature;                 ///< per node, in mesh order
    std::vector<Vec3> gradient;                      ///< per tetrahedron
    std::vector<Vec3> flux;                          ///< per tetrahedron
    std::vector<double> reaction;                    ///< per node: the heat the constraint supplies INTO the domain (0 at a free node)
    std::vector<bool> boundary;                      ///< per node: on the boundary (a boundary triangle uses it)
};

[[nodiscard]] inline PatchSolution solveAffinePatch(const meshing::Mesh& mesh, double conductivity,
                                                    const std::function<double(const Vec3&)>& field) {
    std::vector<bool> onBoundary(mesh.nodes().size(), false);
    std::unordered_map<std::uint64_t, std::size_t> index;
    for (std::size_t i = 0; i < mesh.nodes().size(); ++i) {
        index.emplace(mesh.nodes()[i].id.value(), i);
    }
    for (const meshing::Triangle& tri : mesh.triangles()) {
        for (const meshing::NodeId id : tri.nodes) {
            onBoundary[index.at(id.value())] = true;
        }
    }

    Result<thermal::ThermalDofMap> dofs = thermal::buildThermalDofMap(mesh);
    REQUIRE(dofs.has_value());
    std::vector<meshing::ElementId> tetIds;
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        tetIds.push_back(tet.id);
    }
    actest::AsmHand hand{.mesh = mesh, .triangles = {}, .tets = tetIds, .dofs = std::move(*dofs)};
    const thermal::ThermalSystem system = actest::assemble(hand, conductivity, std::nullopt, actest::noHeat(hand));

    std::vector<thermal::ThermalTemperatureConstraint> list;
    for (std::size_t i = 0; i < mesh.nodes().size(); ++i) {
        if (onBoundary[i]) {
            list.push_back({actest::dofIx(i), Temperature::fromSi(field(positionOf(mesh.nodes()[i].position)))});
        }
    }
    const auto constraints = thermal::buildThermalConstraintMap(hand.dofs, list);
    REQUIRE(constraints.has_value());
    const auto solved = thermal::solveSteadyThermalSystem(system, hand.dofs, *constraints, thermal::ThermalSolverSettings{});
    REQUIRE(solved.has_value());
    auto post = thermal::recoverSteadyFromSolution(hand.mesh, hand.dofs, ThermalConductivity::fromSi(conductivity), *solved);
    REQUIRE(post.has_value());

    PatchSolution out;
    out.boundary = onBoundary;
    out.temperature = finalTemperatures(*post);
    for (std::size_t e = 0; e < mesh.tetrahedra().size(); ++e) {
        out.gradient.push_back(vec(post->finalState().fields.gradients[e]));
        out.flux.push_back(vec(post->finalState().fields.heatFluxes[e]));
    }
    const auto residual = thermal::steadyFullResidual(system.conductance(), system.heat(), post->finalState().temperatures);
    REQUIRE(residual.has_value());
    out.reaction.assign(residual->begin(), residual->end());
    return out;
}

} // namespace bettercad::test::thermalref
