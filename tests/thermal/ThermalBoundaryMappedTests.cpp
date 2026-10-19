// P18-BC-001: boundary conditions against REAL P16 meshes.
//
// These are the AUTHORITY tests: a canonical FaceName is resolved through P16's
// mapping to current facets, nodes and unknowns; an unresolved face is refused
// rather than bound to a neighbour; two prescriptions that meet along an edge
// either agree or are refused; a prepared boundary dies with its mesh even when
// every count and every numeric id is unchanged; and nothing about preparation
// changes the canonical conditions, the CAD or the mesh.
//
// The arithmetic is checked in ThermalBoundaryTests.cpp against hand values. Here
// the expected dense systems are formed from the mesh coordinates by the test's own
// cross product and the literal formulas h A / 12 and h T_inf A / 3, never by a
// production helper.
//
// TWO TOLERANCE CLASSES, KEPT APART. `sum(Q) == h T_inf * sum(facet areas)` is an
// ALGEBRAIC identity of the integration and is held to a few ulps. `sum(facet
// areas) == A_CAD` is a statement about the MESH and carries P16's geometric
// tolerance (exact for a planar face, a polygon-in-circle defect for a cylinder's
// cap). Conflating them would blame the boundary conversion for meshing's
// approximation.

#include "ThermalBoundaryTestSupport.hpp"

#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <bit>
#include <format>
#include <iterator>
#include <numbers>

using namespace bctest;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using thermal::BoundaryFailure;
using thermal::BoundaryProblem;
using thermal::BoundaryStaleReason;
using thermal::ConvectionFacet;
using thermal::PreparedThermalBoundary;
using thermal::ResultCurrency;
using thermal::StaleReason;
using thermal::ThermalResult;
using thermal::ThermalResultSource;

namespace {

[[nodiscard]] std::vector<ThermalDofIndex> dofsOfHandles(const ThermalDofMap& map, const std::vector<NodeId::ValueType>& handles) {
    std::vector<ThermalDofIndex> out;
    for (const NodeId::ValueType handle : handles) {
        const Result<ThermalDofIndex> dof = map.dofOf(NodeId::fromValue(handle));
        REQUIRE(dof.has_value());
        out.push_back(*dof);
    }
    std::ranges::sort(out, [](ThermalDofIndex a, ThermalDofIndex b) { return a.value() < b.value(); });
    return out;
}

[[nodiscard]] std::vector<std::uint64_t> raw(const std::vector<ThermalDofIndex>& dofs) {
    std::vector<std::uint64_t> out;
    for (const ThermalDofIndex dof : dofs) {
        out.push_back(dof.value());
    }
    return out;
}

[[nodiscard]] std::vector<NodeId::ValueType> intersection(std::vector<NodeId::ValueType> a, std::vector<NodeId::ValueType> b) {
    std::vector<NodeId::ValueType> out;
    std::ranges::set_intersection(a, b, std::back_inserter(out));
    return out;
}

[[nodiscard]] std::vector<NodeId::ValueType> unionOf(std::vector<NodeId::ValueType> a, std::vector<NodeId::ValueType> b) {
    std::vector<NodeId::ValueType> out;
    std::ranges::set_union(a, b, std::back_inserter(out));
    return out;
}

} // namespace

// ===========================================================================
// Resolution through GeometryReference
// ===========================================================================

TEST_CASE("ThermalBC_GeometryReferenceResolvesCurrentBoundary", "[thermal][bc][mapping]") {
    BoundaryStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const std::vector<ThermalBoundaryCondition> bcs{temperatureBc(1, study.endCap(), 350.0),
                                                    convectionBc(2, study.startCap(), 25.0, 293.15)};
    const PreparedThermalBoundary prepared = prepareOn(model, map, bcs);

    // The Dirichlet side: exactly P16's facets, their unique nodes, their unknowns.
    const std::vector<ElementId> capFacets = facetsOf(model, study.endCap());
    const std::vector<NodeId::ValueType> capNodes = uniqueNodeHandles(model.mesh().mesh(), capFacets);
    REQUIRE(prepared.temperatures().size() == 1);
    CHECK(prepared.temperatures()[0].condition == bid(1));
    CHECK(prepared.temperatures()[0].facetCount == capFacets.size());
    CHECK(raw(prepared.temperatures()[0].dofs) == raw(dofsOfHandles(map, capNodes)));
    CHECK(prepared.temperatures()[0].dofs.size() == capNodes.size());
    // The convection side: one term per facet of the OTHER face, in P16's order.
    const std::vector<ElementId> startFacets = facetsOf(model, study.startCap());
    REQUIRE(prepared.convections().size() == 1);
    REQUIRE(prepared.convections()[0].facets.size() == startFacets.size());
    for (std::size_t i = 0; i < startFacets.size(); ++i) {
        CHECK(prepared.convections()[0].facets[i].facet == startFacets[i]);
    }
    CHECK(prepared.describes(model.mesh().mesh()));
    CHECK(prepared.describes(map));
}

TEST_CASE("ThermalBC_MappedFaceDirichletMapsUniqueNodesOnce", "[thermal][bc][dirichlet]") {
    BoundaryStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const PreparedThermalBoundary prepared = prepareOn(model, map, {temperatureBc(1, study.endCap(), 350.0)});

    const std::vector<ElementId> facets = facetsOf(model, study.endCap());
    // A cap is two triangles sharing an edge: six corners, four nodes.
    std::size_t corners = 0;
    for (const ElementId id : facets) {
        REQUIRE(model.mesh().mesh().findTriangle(id) != nullptr);
        corners += 3;
    }
    const std::vector<NodeId::ValueType> nodes = uniqueNodeHandles(model.mesh().mesh(), facets);
    CHECK(corners > nodes.size());                    // nodes ARE shared between facets
    CHECK(prepared.constraints().size() == nodes.size());  // and constrained once
    // Every constraint is 350 K, exactly, and attributed to BC 1.
    for (std::size_t i = 0; i < prepared.constraints().size(); ++i) {
        CHECK(prepared.constraints().constrained()[i].temperature.si() == 350.0);
        REQUIRE(prepared.constraintSources(i).size() == 1);
        CHECK(prepared.constraintSources(i)[0] == bid(1));
    }
    // Ascending by unknown.
    for (std::size_t i = 1; i < prepared.constraints().size(); ++i) {
        CHECK(prepared.constraints().constrained()[i - 1].dof.value() < prepared.constraints().constrained()[i].dof.value());
    }
    // Everything else on the body is free: no implicit 0 K anywhere.
    std::size_t freeNodes = 0;
    for (std::uint64_t d = 0; d < map.dofCount(); ++d) {
        if (!prepared.constraints().isConstrained(ThermalDofIndex::fromValue(d))) {
            CHECK_FALSE(prepared.constraints().prescribedTemperature(ThermalDofIndex::fromValue(d)).has_value());
            ++freeNodes;
        }
    }
    CHECK(freeNodes == map.dofCount() - nodes.size());
    WARN(std::format("end cap: {} facets, {} corners, {} unique nodes of {} -> {} constraints at 350 K",
                     facets.size(), corners, nodes.size(), map.dofCount(), prepared.constraints().size()));
}

TEST_CASE("ThermalBC_MappedFaceConvectionConservesTheExchange", "[thermal][bc][robin]") {
    BoundaryStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const double h = 25.0;
    const double ambient = 293.15;
    const PreparedThermalBoundary prepared = prepareOn(model, map, {convectionBc(1, study.endCap(), h, ambient)});

    const std::vector<ElementId> facets = facetsOf(model, study.endCap());
    const double meshArea = areaOfFacets(model.mesh().mesh(), facets);
    // MESH FIDELITY, a planar face: the facets tile the cap exactly.
    CHECK_THAT(meshArea, WithinRel(kCapArea, 1e-12));

    const thermal::PreparedConvectionBoundary& convection = prepared.convections()[0];
    CHECK(convection.condition == bid(1));
    CHECK(convection.coefficient.si() == h);
    CHECK(convection.ambient.si() == ambient);
    // INTEGRATION identities, algebraic: a few ulps.
    CHECK_THAT(convection.area().si(), WithinRel(meshArea, 1e-13));
    CHECK_THAT(convection.conductance().si(), WithinRel(h * meshArea, 1e-13));
    CHECK_THAT(convection.source().si(), WithinRel(h * ambient * meshArea, 1e-13));
    CHECK_THAT(prepared.convectionConductance().si(), WithinRel(h * kCapArea, 1e-12));
    CHECK_THAT(prepared.convectionSource().si(), WithinRel(h * ambient * kCapArea, 1e-12));
    CHECK_THAT(prepared.convectionArea().si(), WithinRel(kCapArea, 1e-12));

    // The whole system against the test's own expectation, entry by entry.
    const std::size_t n = static_cast<std::size_t>(map.dofCount());
    const Dense actual = denseOf(prepared, n);
    const Dense expected = expectedDense(model.mesh().mesh(), map, facets, h, ambient);
    CHECK(worstDifference(actual, expected) <= 1e-13 * largestEntry(expected));
    CHECK_THAT(actual.sumQ(), WithinRel(h * ambient * meshArea, 1e-13));
    CHECK_THAT(sum(actual.k), WithinRel(h * meshArea, 1e-13));

    // Uniform boundary temperature: the resultant is h A (T_b - T_inf), with the
    // sign of the frozen convention.
    for (const double tb : {ambient, 350.0, 250.0}) {
        const double total = sum(actual.residual(std::vector<double>(n, tb)));
        const double wanted = h * meshArea * (tb - ambient);
        CHECK_THAT(total, WithinAbs(wanted, 1e-12 * h * meshArea * 100.0));
        WARN(std::format("end cap, h = 25, T_inf = 293.15: T_b = {} K -> sum(Kconv T - Qconv) = {:.9g} W "
                         "(expected {:.9g})",
                         tb, total, wanted));
    }
}

TEST_CASE("ThermalBC_PreparedBoundaryKeepsTheAttributionAMenderNeeds", "[thermal][bc][mapping]") {
    BoundaryStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const std::vector<ThermalBoundaryCondition> bcs{convectionBc(5, study.endCap(), 25.0, 293.15),
                                                    temperatureBc(2, study.startCap(), 400.0),
                                                    convectionBc(9, study.side(0), 10.0, 280.0)};
    const PreparedThermalBoundary prepared = prepareOn(model, map, bcs);
    // In the order given, with the identity of each.
    REQUIRE(prepared.convections().size() == 2);
    CHECK(prepared.convections()[0].condition == bid(5));
    CHECK(prepared.convections()[1].condition == bid(9));
    CHECK(prepared.convections()[1].coefficient.si() == 10.0);
    CHECK(prepared.convections()[1].ambient.si() == 280.0);
    CHECK(prepared.temperatures()[0].condition == bid(2));
    CHECK_THAT(prepared.convections()[1].area().si(), WithinRel(areaOfFacets(model.mesh().mesh(), facetsOf(model, study.side(0))), 1e-13));
}

TEST_CASE("ThermalBC_DirichletAndRobinOverlapPolicy", "[thermal][bc][mapping]") {
    // POLICY: a prescribed temperature and a convection on the SAME face are both
    // kept. The temperature is imposed exactly by the later solve; the convection
    // is still part of the heat exchange and of the reaction balance, and dropping
    // it would silently change the heat that leaves the body.
    BoundaryStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const PreparedThermalBoundary both = prepareOn(
        model, map, {temperatureBc(1, study.endCap(), 350.0), convectionBc(2, study.endCap(), 25.0, 293.15)});
    const PreparedThermalBoundary convectionOnly = prepareOn(model, map, {convectionBc(2, study.endCap(), 25.0, 293.15)});
    const PreparedThermalBoundary temperatureOnly = prepareOn(model, map, {temperatureBc(1, study.endCap(), 350.0)});

    CHECK(both.constraints() == temperatureOnly.constraints());
    REQUIRE(both.convections().size() == 1);
    // The convection terms are the same, bit for bit, with or without the temperature.
    CHECK(both.convections() .front() == convectionOnly.convections().front());
    CHECK(both.constraints().size() > 0);
    CHECK(both.convectionConductance().si() > 0.0);
}

TEST_CASE("ThermalBC_NoDirichletBoundarySetIsValid", "[thermal][bc][mapping]") {
    // A convection face with h > 0 removes the null mode of the conductivity
    // matrix by itself, so a set with no prescribed temperature must not be
    // refused at the boundary-condition layer.
    BoundaryStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const PreparedThermalBoundary convectionOnly = prepareOn(model, map, {convectionBc(1, study.endCap(), 25.0, 293.15)});
    CHECK(convectionOnly.constraints().isEmpty());
    CHECK(convectionOnly.convections().size() == 1);
    CHECK(convectionOnly.temperatures().empty());

    // And the empty set is valid too: insulation everywhere is the natural
    // condition and contributes nothing.
    const PreparedThermalBoundary none = prepareOn(model, map, {});
    CHECK(none.constraints().isEmpty());
    CHECK(none.convections().empty());
    CHECK(none.temperatures().empty());
    CHECK(none.convectionConductance().si() == 0.0);
    CHECK(none.convectionSource().si() == 0.0);
    CHECK(thermal::isCurrent(none, model, std::vector<ThermalBoundaryCondition>{}));
    // Whether such a system is solvable is the solver's question, not asked here.
}

// ===========================================================================
// Duplicates and conflicts, on real faces
// ===========================================================================

TEST_CASE("ThermalBC_DuplicateIdenticalDirichletOnTheSameFace", "[thermal][bc][conflict]") {
    BoundaryStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const PreparedThermalBoundary prepared = prepareOn(
        model, map, {temperatureBc(1, study.endCap(), 350.0), temperatureBc(2, study.endCap(), 350.0)});
    const PreparedThermalBoundary single = prepareOn(model, map, {temperatureBc(1, study.endCap(), 350.0)});
    // One mathematical constraint per unknown: the same map as with one condition.
    CHECK(prepared.constraints() == single.constraints());
    // Both conditions are recorded as having asked.
    CHECK(prepared.temperatures().size() == 2);
    for (std::size_t i = 0; i < prepared.constraints().size(); ++i) {
        const auto asked = prepared.constraintSources(i);
        CHECK(std::ranges::equal(asked, std::vector{bid(1), bid(2)}));
    }
}

TEST_CASE("ThermalBC_PartialSurfaceOverlapConflictRejected", "[thermal][bc][conflict]") {
    // The end cap and a side wall meet along an edge. Two different temperatures
    // disagree at the nodes of that edge, though each face is fine on its own.
    BoundaryStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const std::vector<NodeId::ValueType> capNodes = uniqueNodeHandles(model.mesh().mesh(), facetsOf(model, study.endCap()));
    const std::vector<NodeId::ValueType> sideNodes = uniqueNodeHandles(model.mesh().mesh(), facetsOf(model, study.side(0)));
    const std::vector<NodeId::ValueType> shared = intersection(capNodes, sideNodes);
    REQUIRE_FALSE(shared.empty());  // the faces do overlap
    REQUIRE(shared.size() < capNodes.size());  // and not completely

    const std::vector<ThermalBoundaryCondition> bcs{temperatureBc(1, study.endCap(), 350.0),
                                                    temperatureBc(2, study.side(0), 300.0)};
    const Result<PreparedThermalBoundary> refused = thermal::prepareThermalBoundary(model, map, bcs);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::InvalidArgument);
    const std::optional<BoundaryFailure> failure = thermal::thermalBoundaryFailure(model, map, bcs);
    REQUIRE(failure.has_value());
    CHECK(failure->problem == BoundaryProblem::ConflictingTemperature);
    REQUIRE(failure->conflicts.size() == 1);
    CHECK(failure->conflicts[0].first == bid(1));
    CHECK(failure->conflicts[0].second == bid(2));
    CHECK(failure->conflicts[0].firstValue.si() == 350.0);
    CHECK(failure->conflicts[0].secondValue.si() == 300.0);
    // EVERY shared unknown is counted -- the summary hides none.
    CHECK(failure->conflicts[0].unknownCount == shared.size());
    CHECK_THAT(failure->error.message, ContainsSubstring("none chosen over another"));
    WARN(std::format("conflict: end cap 350 K and side wall 300 K share {} unknowns of {}; set refused", shared.size(),
                     map.dofCount()));

    // Faces that do NOT meet never conflict, whatever their values.
    const PreparedThermalBoundary apart = prepareOn(
        model, map, {temperatureBc(1, study.endCap(), 350.0), temperatureBc(2, study.startCap(), 300.0)});
    CHECK(apart.constraints().size() == 2 * capNodes.size());
}

TEST_CASE("ThermalBC_IdenticalOverlapAlongAnEdgeDeduplicates", "[thermal][bc][conflict]") {
    BoundaryStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const std::vector<NodeId::ValueType> capNodes = uniqueNodeHandles(model.mesh().mesh(), facetsOf(model, study.endCap()));
    const std::vector<NodeId::ValueType> sideNodes = uniqueNodeHandles(model.mesh().mesh(), facetsOf(model, study.side(0)));
    const std::vector<NodeId::ValueType> shared = intersection(capNodes, sideNodes);
    REQUIRE_FALSE(shared.empty());

    const PreparedThermalBoundary prepared = prepareOn(
        model, map, {temperatureBc(1, study.endCap(), 300.0), temperatureBc(2, study.side(0), 300.0)});
    // The union of the two node sets, each constrained once.
    CHECK(prepared.constraints().size() == unionOf(capNodes, sideNodes).size());
    CHECK(prepared.constraints().size() < capNodes.size() + sideNodes.size());
    // Shared nodes are attributed to both; the others to one.
    const std::vector<ThermalDofIndex> sharedDofs = dofsOfHandles(map, shared);
    for (std::size_t i = 0; i < prepared.constraints().size(); ++i) {
        const ThermalDofIndex dof = prepared.constraints().constrained()[i].dof;
        const bool isShared = std::ranges::any_of(sharedDofs, [&](ThermalDofIndex s) { return s.value() == dof.value(); });
        CHECK(prepared.constraintSources(i).size() == (isShared ? 2U : 1U));
        CHECK(prepared.constraints().constrained()[i].temperature.si() == 300.0);
    }
}

// ===========================================================================
// Convection superposition on a mapped face
// ===========================================================================

TEST_CASE("ThermalBC_ConvectionSuperpositionOnAMappedFace", "[thermal][bc][superposition]") {
    BoundaryStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const std::size_t n = static_cast<std::size_t>(map.dofCount());
    const std::vector<ElementId> facets = facetsOf(model, study.endCap());

    // Two DIFFERENT convections on one face add.
    const PreparedThermalBoundary two = prepareOn(
        model, map, {convectionBc(1, study.endCap(), 25.0, 293.15), convectionBc(2, study.endCap(), 10.0, 350.0)});
    REQUIRE(two.convections().size() == 2);
    const Dense sumOfExpected = [&] {
        Dense a = expectedDense(model.mesh().mesh(), map, facets, 25.0, 293.15);
        const Dense b = expectedDense(model.mesh().mesh(), map, facets, 10.0, 350.0);
        for (std::size_t i = 0; i < a.k.size(); ++i) {
            a.k[i] += b.k[i];
        }
        for (std::size_t i = 0; i < a.q.size(); ++i) {
            a.q[i] += b.q[i];
        }
        return a;
    }();
    CHECK(worstDifference(denseOf(two, n), sumOfExpected) <= 1e-13 * largestEntry(sumOfExpected));

    // The single equivalent (h1 + h2, T_eff) gives the same system.
    const double teff = (25.0 * 293.15 + 10.0 * 350.0) / 35.0;
    const PreparedThermalBoundary equivalent = prepareOn(model, map, {convectionBc(1, study.endCap(), 35.0, teff)});
    CHECK(worstDifference(denseOf(two, n), denseOf(equivalent, n)) <= 1e-12 * largestEntry(sumOfExpected));

    // IDENTICAL convections on one face are DOUBLE: two physical exchanges, never
    // deduplicated the way equal prescriptions are.
    const PreparedThermalBoundary twice = prepareOn(
        model, map, {convectionBc(1, study.endCap(), 25.0, 293.15), convectionBc(2, study.endCap(), 25.0, 293.15)});
    const PreparedThermalBoundary once = prepareOn(model, map, {convectionBc(1, study.endCap(), 25.0, 293.15)});
    REQUIRE(twice.convections().size() == 2);
    const Dense dTwice = denseOf(twice, n);
    const Dense dOnce = denseOf(once, n);
    for (std::size_t i = 0; i < dOnce.k.size(); ++i) {
        CHECK(dTwice.k[i] == 2.0 * dOnce.k[i]);
    }
    for (std::size_t i = 0; i < dOnce.q.size(); ++i) {
        CHECK(dTwice.q[i] == 2.0 * dOnce.q[i]);
    }
    CHECK(twice.convectionConductance().si() == 2.0 * once.convectionConductance().si());
    CHECK(twice.convectionSource().si() == 2.0 * once.convectionSource().si());
    CHECK(twice.convectionArea().si() == 2.0 * once.convectionArea().si());  // a face carrying two exchanges counts twice
}

TEST_CASE("ThermalBC_ConvectionIsIndependentOfTheMaterial", "[thermal][bc][scaling]") {
    // The boundary terms are functions of h, T_inf and the facet. A different
    // conductivity cannot change them, bit for bit.
    BoundaryStudy a;
    BoundaryStudy b;
    materials::ThermalProperties properties = features::findMaterial(b.document, b.material)->definition().thermal;
    properties.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(400_W_per_m_K);
    REQUIRE(features::setMaterialThermal(b.document, b.material, properties).has_value());
    const std::vector<ThermalBoundaryCondition> bcs{convectionBc(1, a.endCap(), 25.0, 293.15), temperatureBc(2, a.startCap(), 350.0)};
    const ThermalModel ma = a.model();
    const ThermalModel mb = b.model();
    const PreparedThermalBoundary pa = prepareOn(ma, dofMapOf(ma), bcs);
    const PreparedThermalBoundary pb = prepareOn(mb, dofMapOf(mb), bcs);
    CHECK(pa.convections() .front().facets.size() == pb.convections().front().facets.size());
    for (std::size_t i = 0; i < pa.convections().front().facets.size(); ++i) {
        CHECK(pa.convections().front().facets[i].term == pb.convections().front().facets[i].term);
    }
    CHECK(pa.convectionConductance().si() == pb.convectionConductance().si());
}

// ===========================================================================
// Refusals through the whole chain
// ===========================================================================

TEST_CASE("ThermalBC_UnresolvedGeometryReferenceFails", "[thermal][bc][mapping]") {
    BoundaryStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const FaceName hole{study.feature, FaceSelector{.role = FaceRole::HoleBottom}};
    const FaceName unknownObject{ObjectId::fromValue(9999), FaceSelector{.role = FaceRole::EndCap}};
    for (const FaceName& face : {hole, unknownObject}) {
        for (const ThermalBoundaryCondition& bc : {temperatureBc(1, face, 350.0), convectionBc(1, face, 25.0, 293.15)}) {
            const std::vector<ThermalBoundaryCondition> bcs{bc};
            const std::optional<BoundaryFailure> failure = thermal::thermalBoundaryFailure(model, map, bcs);
            REQUIRE(failure.has_value());
            CHECK(failure->problem == BoundaryProblem::TargetUnresolved);
            CHECK(failure->error.code == ErrorCode::NotFound);
            CHECK_THAT(failure->error.message, ContainsSubstring("Nothing nearby is substituted"));
            CHECK_FALSE(thermal::prepareThermalBoundary(model, map, bcs).has_value());
        }
    }
}

TEST_CASE("ThermalBC_UnsupportedTargetFails", "[thermal][bc][mapping]") {
    // A Side face needs the entity that names which side; without it the selector
    // is malformed and nothing is guessed. Edges, vertices and points have no
    // FaceRole at all, so they cannot be written (the compile-fail group pins
    // that no such member exists).
    BoundaryStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const FaceName malformed{study.feature, FaceSelector{.role = FaceRole::Side}};
    for (const ThermalBoundaryCondition& bc : {temperatureBc(1, malformed, 350.0), convectionBc(1, malformed, 25.0, 293.15)}) {
        const std::vector<ThermalBoundaryCondition> bcs{bc};
        CHECK(thermal::thermalBoundaryProblem(model, map, bcs) == std::optional<BoundaryProblem>{BoundaryProblem::TargetInvalid});
        const Result<PreparedThermalBoundary> refused = thermal::prepareThermalBoundary(model, map, bcs);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == ErrorCode::InvalidArgument);
    }
    WARN("a Side face with no entity is refused as target_invalid for both condition kinds");
}

TEST_CASE("ThermalBC_NoNearestFaceRebind", "[thermal][bc][mapping]") {
    // The body has both caps and four sides; a hole bottom it does not have is
    // refused, not bound to any of them. Prepared against the faces that DO
    // resolve, the same conditions succeed -- so the refusal is about the missing
    // face and about nothing else.
    BoundaryStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const FaceName missing{study.feature, FaceSelector{.role = FaceRole::HoleBottom}};
    const FaceName counterbore{study.feature, FaceSelector{.role = FaceRole::CounterboreFloor}};
    const FaceName spotface{study.feature, FaceSelector{.role = FaceRole::SpotfaceFloor}};
    for (const FaceName& face : {missing, counterbore, spotface}) {
        CHECK(thermal::thermalBoundaryProblem(model, map, std::vector{temperatureBc(1, face, 350.0)}) ==
              std::optional<BoundaryProblem>{BoundaryProblem::TargetUnresolved});
        CHECK(thermal::thermalBoundaryProblem(model, map, std::vector{convectionBc(1, face, 25.0, 293.15)}) ==
              std::optional<BoundaryProblem>{BoundaryProblem::TargetUnresolved});
    }
    CHECK_FALSE(thermal::thermalBoundaryProblem(model, map, std::vector{temperatureBc(1, study.endCap(), 350.0)}).has_value());
}

TEST_CASE("ThermalBC_OneInvalidConditionRefusesTheWholeSet", "[thermal][bc][mapping]") {
    BoundaryStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const FaceName missing{study.feature, FaceSelector{.role = FaceRole::HoleBottom}};
    // The first and last are fine; the middle one cannot resolve. Nothing is
    // prepared and nothing is dropped.
    const std::vector<ThermalBoundaryCondition> bcs{temperatureBc(1, study.endCap(), 350.0), convectionBc(2, missing, 25.0, 293.15),
                                                    convectionBc(3, study.startCap(), 10.0, 280.0)};
    const Result<PreparedThermalBoundary> refused = thermal::prepareThermalBoundary(model, map, bcs);
    REQUIRE_FALSE(refused.has_value());
    CHECK_THAT(refused.error().message, ContainsSubstring(std::format("{}", bid(2))));
    CHECK(thermal::thermalBoundaryProblem(model, map, bcs) == std::optional<BoundaryProblem>{BoundaryProblem::TargetUnresolved});
}

TEST_CASE("ThermalBC_IdentitiesAreCheckedFirst", "[thermal][bc][invalid]") {
    BoundaryStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    // The same id twice: one record would be silently ignored.
    CHECK(thermal::thermalBoundaryProblem(model, map, std::vector{temperatureBc(1, study.endCap(), 350.0), convectionBc(1, study.startCap(), 25.0, 293.15)}) ==
          std::optional<BoundaryProblem>{BoundaryProblem::DuplicateConditionId});
    // An invalid (zero) id.
    CHECK(thermal::thermalBoundaryProblem(model, map, std::vector{temperatureBc(0, study.endCap(), 350.0)}) ==
          std::optional<BoundaryProblem>{BoundaryProblem::InvalidIdentity});
    // Identity is checked before values: a duplicate with an invalid value reports the duplicate.
    CHECK(thermal::thermalBoundaryProblem(model, map, std::vector{temperatureBc(1, study.endCap(), kNaN), temperatureBc(1, study.endCap(), 350.0)}) ==
          std::optional<BoundaryProblem>{BoundaryProblem::DuplicateConditionId});
}

TEST_CASE("ThermalBC_ValuesAreCheckedBeforeTargetsAreResolved", "[thermal][bc][invalid]") {
    BoundaryStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const FaceName missing{study.feature, FaceSelector{.role = FaceRole::HoleBottom}};
    // A bad number is reported as a bad number, not as a bad face -- even on a face that cannot resolve.
    using Problem = std::optional<BoundaryProblem>;
    CHECK(thermal::thermalBoundaryProblem(model, map, std::vector{temperatureBc(1, missing, kNaN)}) == Problem{BoundaryProblem::TemperatureInvalid});
    CHECK(thermal::thermalBoundaryProblem(model, map, std::vector{temperatureBc(1, missing, kInf)}) == Problem{BoundaryProblem::TemperatureInvalid});
    CHECK(thermal::thermalBoundaryProblem(model, map, std::vector{temperatureBc(1, missing, -5.0)}) == Problem{BoundaryProblem::TemperatureInvalid});
    CHECK(thermal::thermalBoundaryProblem(model, map, std::vector{convectionBc(1, missing, -1.0, 300.0)}) == Problem{BoundaryProblem::CoefficientInvalid});
    CHECK(thermal::thermalBoundaryProblem(model, map, std::vector{convectionBc(1, missing, kNaN, 300.0)}) == Problem{BoundaryProblem::CoefficientInvalid});
    CHECK(thermal::thermalBoundaryProblem(model, map, std::vector{convectionBc(1, missing, kInf, 300.0)}) == Problem{BoundaryProblem::CoefficientInvalid});
    CHECK(thermal::thermalBoundaryProblem(model, map, std::vector{convectionBc(1, missing, 25.0, kNaN)}) == Problem{BoundaryProblem::AmbientInvalid});
    CHECK(thermal::thermalBoundaryProblem(model, map, std::vector{convectionBc(1, missing, 25.0, -1.0)}) == Problem{BoundaryProblem::AmbientInvalid});
    // A canonical record cannot hold h = 0 (P18-DATA-001): preparation refuses it by the same rule.
    CHECK(thermal::thermalBoundaryProblem(model, map, std::vector{convectionBc(1, study.endCap(), 0.0, 300.0)}) == Problem{BoundaryProblem::CoefficientInvalid});
    // And an invalid ambient is refused even beside an h that would multiply it by nearly nothing.
    CHECK(thermal::thermalBoundaryProblem(model, map, std::vector{convectionBc(1, study.endCap(), 1.0e-300, kNaN)}) == Problem{BoundaryProblem::AmbientInvalid});
    // 0 K is a valid temperature and a valid ambient.
    CHECK_FALSE(thermal::thermalBoundaryProblem(model, map, std::vector{temperatureBc(1, study.endCap(), 0.0)}).has_value());
    CHECK_FALSE(thermal::thermalBoundaryProblem(model, map, std::vector{convectionBc(1, study.endCap(), 25.0, 0.0)}).has_value());
}

TEST_CASE("ThermalBC_ANumberingForAnotherMeshIsRefused", "[thermal][bc][invalid]") {
    BoundaryStudy study;
    BoundaryStudy other;
    const ThermalModel model = study.model();
    const ThermalModel otherModel = other.model();
    const ThermalDofMap foreign = dofMapOf(otherModel);
    CHECK(thermal::thermalBoundaryProblem(model, foreign, std::vector{temperatureBc(1, study.endCap(), 350.0)}) ==
          std::optional<BoundaryProblem>{BoundaryProblem::DofMapMismatch});
    CHECK_FALSE(thermal::prepareThermalBoundary(model, foreign, std::vector{convectionBc(1, study.endCap(), 25.0, 293.15)}).has_value());
}

TEST_CASE("ThermalBC_OverflowingConvectionFailsTheWholeSet", "[thermal][bc][invalid]") {
    BoundaryStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    // h and T_inf so large that the source h T_inf A / 3 overflows although each is finite: refused, naming the condition.
    const std::optional<BoundaryFailure> failure =
        thermal::thermalBoundaryFailure(model, map, std::vector{convectionBc(7, study.endCap(), 1.0e300, 1.0e300)});
    REQUIRE(failure.has_value());
    CHECK(failure->problem == BoundaryProblem::NonFiniteResult);
    CHECK_THAT(failure->error.message, ContainsSubstring(std::format("{}", bid(7))));
}

TEST_CASE("ThermalBC_AFailedPreparationPublishesNothing", "[thermal][bc][purity]") {
    BoundaryStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const FaceName missing{study.feature, FaceSelector{.role = FaceRole::HoleBottom}};
    const Result<PreparedThermalBoundary> refused = thermal::prepareThermalBoundary(
        model, map, std::vector{temperatureBc(1, study.endCap(), 350.0), convectionBc(2, missing, 25.0, 293.15)});
    // A Result either holds a whole prepared boundary or an error: the valid
    // first condition leaves no trace.
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::NotFound);
}

// ===========================================================================
// Remesh and currentness
// ===========================================================================

TEST_CASE("ThermalBC_PreparedBCBindsExactMeshSource", "[thermal][bc][currentness]") {
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> bcs{temperatureBc(1, study.endCap(), 350.0),
                                                    convectionBc(2, study.startCap(), 25.0, 293.15)};
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const PreparedThermalBoundary prepared = prepareOn(model, map, bcs);

    CHECK(prepared.describes(model.mesh().mesh()));
    CHECK(prepared.describes(map));
    CHECK(prepared.constraints().stamp() == map.stamp());
    CHECK(prepared.constraints().dofCount() == map.dofCount());
    CHECK(prepared.source().body == model.body());
    CHECK(prepared.source().mesh == model.mesh().mesh().stamp());
    CHECK(prepared.source().nodeCount == model.mesh().nodeCount());
    CHECK(prepared.source().geometry == model.geometryRevision());
    CHECK(prepared.source().conditions == bcs);
    CHECK(thermal::isCurrent(prepared, model, bcs));
    CHECK(thermal::requireCurrent(prepared, model, bcs).has_value());
    CHECK(thermal::staleReasons(prepared.source(), thermal::currentBoundarySource(model, bcs)).empty());
}

TEST_CASE("ThermalBC_RemeshInvalidatesPreparedBC", "[thermal][bc][currentness]") {
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> bcs{temperatureBc(1, study.endCap(), 350.0),
                                                    convectionBc(2, study.startCap(), 25.0, 293.15)};
    // A ThermalModel BORROWS the mesher's mesh and must not outlive a remesh: it
    // would then see the NEW mesh. So everything about generation 1 that is needed
    // afterwards is captured by VALUE first, and the old model goes out of scope.
    meshing::MeshStamp stamp1{};
    std::size_t nodes1 = 0;
    std::optional<ThermalDofMap> oldMap;
    std::optional<PreparedThermalBoundary> p1Holder;
    {
        const ThermalModel m1 = study.model();
        oldMap = dofMapOf(m1);
        p1Holder = prepareOn(m1, *oldMap, bcs);
        stamp1 = m1.mesh().mesh().stamp();
        nodes1 = m1.mesh().nodeCount();
    }
    const PreparedThermalBoundary& p1 = *p1Holder;

    study.remesh();  // same body, same controls, a new generation
    const ThermalModel m2 = study.model();
    const ThermalDofMap map2 = dofMapOf(m2);

    CHECK_FALSE(m2.mesh().mesh().stamp() == stamp1);
    CHECK_FALSE(p1.describes(m2.mesh().mesh()));
    CHECK_FALSE(p1.describes(map2));
    CHECK_FALSE(thermal::isCurrent(p1, m2, bcs));
    const std::vector<BoundaryStaleReason> reasons = thermal::staleReasons(p1.source(), thermal::currentBoundarySource(m2, bcs));
    CHECK(std::ranges::find(reasons, BoundaryStaleReason::Mesh) != reasons.end());
    const Result<void> required = thermal::requireCurrent(p1, m2, bcs);
    REQUIRE_FALSE(required.has_value());
    CHECK(required.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(required.error().message, ContainsSubstring("mesh"));

    // Re-preparing against the new mesh gives a fresh, current boundary.
    const PreparedThermalBoundary p2 = prepareOn(m2, map2, bcs);
    CHECK(thermal::isCurrent(p2, m2, bcs));
    // A prepared boundary cannot be applied to the wrong numbering either.
    CHECK_FALSE(thermal::prepareThermalBoundary(m2, *oldMap, bcs).has_value());
    WARN(std::format("remesh: stamp changed, nodes {} -> {}; P1 stale (reason mesh), P2 current", nodes1,
                     m2.mesh().nodeCount()));
}

TEST_CASE("ThermalBC_SameCountsNewMeshIsStale", "[thermal][bc][currentness]") {
    // THE ADVERSARIAL CASE: a remesh with identical node and element counts. The
    // constraint map is exactly as long as the new mesh needs, and it is still
    // stale, because currentness is the source and not the size.
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> bcs{temperatureBc(1, study.endCap(), 350.0),
                                                    convectionBc(2, study.startCap(), 25.0, 293.15)};
    std::optional<PreparedThermalBoundary> p1Holder;
    std::size_t nodes1 = 0;
    std::size_t tets1 = 0;
    std::size_t facets1 = 0;
    {
        const ThermalModel m1 = study.model();
        p1Holder = prepareOn(m1, dofMapOf(m1), bcs);
        nodes1 = m1.mesh().nodeCount();
        tets1 = m1.mesh().tetrahedronCount();
        facets1 = m1.mesh().boundaryTriangleCount();
    }
    const PreparedThermalBoundary& p1 = *p1Holder;

    study.remesh();
    const ThermalModel m2 = study.model();
    REQUIRE(m2.mesh().nodeCount() == nodes1);
    REQUIRE(m2.mesh().tetrahedronCount() == tets1);
    REQUIRE(m2.mesh().boundaryTriangleCount() == facets1);
    CHECK(p1.constraints().dofCount() == m2.mesh().nodeCount());
    CHECK_FALSE(p1.describes(m2.mesh().mesh()));
    CHECK_FALSE(thermal::isCurrent(p1, m2, bcs));
    WARN(std::format("same counts: {} nodes, {} tets, {} boundary facets in both generations; constraint map "
                     "dimension {} matches M2 and P1 is still stale",
                     nodes1, tets1, facets1, p1.constraints().dofCount()));
}

TEST_CASE("ThermalBC_SameIdsNewMeshIsStale", "[thermal][bc][currentness]") {
    // Identical numeric NodeIds AND ElementIds as well as identical counts.
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> bcs{temperatureBc(1, study.endCap(), 350.0)};
    std::optional<PreparedThermalBoundary> p1Holder;
    meshing::MeshStamp stamp1{};
    std::vector<NodeId> nodeIds1;
    std::vector<ElementId> tetIds1;
    std::vector<ElementId> facets1;
    {
        const ThermalModel m1 = study.model();
        p1Holder = prepareOn(m1, dofMapOf(m1), bcs);
        stamp1 = m1.mesh().mesh().stamp();
        for (const meshing::Node& node : m1.mesh().mesh().nodes()) {
            nodeIds1.push_back(node.id);
        }
        for (const meshing::Tetrahedron& tet : m1.mesh().mesh().tetrahedra()) {
            tetIds1.push_back(tet.id);
        }
        facets1 = facetsOf(m1, study.endCap());
    }
    const PreparedThermalBoundary& p1 = *p1Holder;

    study.remesh();
    const ThermalModel m2 = study.model();
    std::vector<NodeId> nodeIds2;
    for (const meshing::Node& node : m2.mesh().mesh().nodes()) {
        nodeIds2.push_back(node.id);
    }
    std::vector<ElementId> tetIds2;
    for (const meshing::Tetrahedron& tet : m2.mesh().mesh().tetrahedra()) {
        tetIds2.push_back(tet.id);
    }
    REQUIRE(nodeIds1 == nodeIds2);
    REQUIRE(tetIds1 == tetIds2);
    CHECK(facetsOf(m2, study.endCap()) == facets1);
    CHECK_FALSE(stamp1 == m2.mesh().mesh().stamp());
    CHECK_FALSE(thermal::isCurrent(p1, m2, bcs));
    CHECK_FALSE(p1.describes(m2.mesh().mesh()));
    WARN("same ids: every NodeId, ElementId and cap facet handle identical across the remesh; P1 stale by stamp alone");
}

TEST_CASE("ThermalBC_RemeshReResolvesGeometryReference", "[thermal][bc][currentness]") {
    // Coarse and fine meshes of ONE cylinder, genuinely different. The canonical
    // FaceName and the physical numbers are the same values throughout; the
    // facets, nodes and unknowns are not; and the mesh-independent totals agree
    // to P16's tolerance.
    RoundStudy part;
    const FaceName cap = part.endCap();
    const FaceName capBefore = cap;
    const double h = 25.0;
    const double ambient = 293.15;
    const double tbar = 350.0;
    const std::vector<ThermalBoundaryCondition> bcs{temperatureBc(1, cap, tbar), convectionBc(2, cap, h, ambient)};
    const std::vector<ThermalBoundaryCondition> bcsBefore = bcs;

    const ThermalModel coarse = part.model(part.coarse);
    const ThermalModel fine = part.model(part.fine);
    const ThermalDofMap coarseMap = dofMapOf(coarse);
    const ThermalDofMap fineMap = dofMapOf(fine);

    // GENUINELY DIFFERENT, before anything is compared.
    REQUIRE(fine.mesh().tetrahedronCount() > coarse.mesh().tetrahedronCount());
    REQUIRE(fine.mesh().nodeCount() > coarse.mesh().nodeCount());
    REQUIRE(fine.mesh().boundaryTriangleCount() > coarse.mesh().boundaryTriangleCount());
    CHECK_FALSE(coarse.mesh().mesh().stamp() == fine.mesh().mesh().stamp());

    const PreparedThermalBoundary pc = prepareOn(coarse, coarseMap, bcs);
    const PreparedThermalBoundary pf = prepareOn(fine, fineMap, bcs);

    // The canonical intent is untouched by resolution.
    CHECK(cap == capBefore);
    CHECK(bcs == bcsBefore);

    const std::vector<ElementId> coarseFacets = facetsOf(coarse, cap);
    const std::vector<ElementId> fineFacets = facetsOf(fine, cap);
    CHECK(fineFacets.size() > coarseFacets.size());
    const std::vector<NodeId::ValueType> coarseNodes = uniqueNodeHandles(coarse.mesh().mesh(), coarseFacets);
    const std::vector<NodeId::ValueType> fineNodes = uniqueNodeHandles(fine.mesh().mesh(), fineFacets);
    CHECK(fineNodes.size() > coarseNodes.size());

    // PRESCRIBED TEMPERATURE: a different number of nodes, the SAME value on each,
    // no averaging and no interpolation from the other mesh.
    CHECK(pc.constraints().size() == coarseNodes.size());
    CHECK(pf.constraints().size() == fineNodes.size());
    CHECK(pf.constraints().size() > pc.constraints().size());
    for (const auto& constraint : pc.constraints().constrained()) {
        CHECK(constraint.temperature.si() == tbar);
    }
    for (const auto& constraint : pf.constraints().constrained()) {
        CHECK(constraint.temperature.si() == tbar);
    }

    CHECK_FALSE(thermal::isCurrent(pc, fine, bcs));
    CHECK_FALSE(thermal::isCurrent(pf, coarse, bcs));

    // CONVECTION: mesh-independent totals. The cap is a disc of radius 6 mm; a
    // polygon inscribed in it has slightly less area, less so as the mesh refines.
    const double capCad = std::numbers::pi * RoundStudy::kRadius * RoundStudy::kRadius;
    const double areaCoarse = areaOfFacets(coarse.mesh().mesh(), coarseFacets);
    const double areaFine = areaOfFacets(fine.mesh().mesh(), fineFacets);
    // Conversion identities, algebraic.
    CHECK_THAT(pc.convectionConductance().si(), WithinRel(h * areaCoarse, 1e-13));
    CHECK_THAT(pf.convectionConductance().si(), WithinRel(h * areaFine, 1e-13));
    CHECK_THAT(pc.convectionSource().si(), WithinRel(h * ambient * areaCoarse, 1e-13));
    CHECK_THAT(pf.convectionSource().si(), WithinRel(h * ambient * areaFine, 1e-13));
    // Mesh fidelity, P16's geometric tolerance.
    const double errorCoarse = relativeError(areaCoarse, capCad);
    const double errorFine = relativeError(areaFine, capCad);
    CHECK(errorFine <= errorCoarse);
    CHECK(errorCoarse < 0.1);
    CHECK(relativeError(pf.convectionConductance().si(), pc.convectionConductance().si()) <= errorCoarse + 1e-12);
    // The constant-temperature resultant is mesh independent in the same sense.
    const double tb = 350.0;
    const double totalCoarse = sum(denseOf(pc, static_cast<std::size_t>(coarseMap.dofCount())).residual(std::vector<double>(static_cast<std::size_t>(coarseMap.dofCount()), tb)));
    const double totalFine = sum(denseOf(pf, static_cast<std::size_t>(fineMap.dofCount())).residual(std::vector<double>(static_cast<std::size_t>(fineMap.dofCount()), tb)));
    CHECK_THAT(totalCoarse, WithinRel(h * areaCoarse * (tb - ambient), 1e-12));
    CHECK_THAT(totalFine, WithinRel(h * areaFine * (tb - ambient), 1e-12));
    CHECK(relativeError(totalFine, totalCoarse) <= errorCoarse + 1e-12);

    WARN(std::format("coarse: {} nodes {} boundary facets, cap {} facets {} nodes, mesh cap area {:.9g}; fine: {} nodes "
                     "{} boundary facets, cap {} facets {} nodes, cap area {:.9g}; CAD cap {:.9g} m^2",
                     coarse.mesh().nodeCount(), coarse.mesh().boundaryTriangleCount(), coarseFacets.size(),
                     coarseNodes.size(), areaCoarse, fine.mesh().nodeCount(), fine.mesh().boundaryTriangleCount(),
                     fineFacets.size(), fineNodes.size(), areaFine, capCad));
    WARN(std::format("convection at T_b = 350 K: coarse {:.9g} W, fine {:.9g} W, CAD h A (T_b - T_inf) = {:.9g} W",
                     totalCoarse, totalFine, h * capCad * (tb - ambient)));
}

TEST_CASE("ThermalBC_ADifferentBodyIsStale", "[thermal][bc][currentness]") {
    RoundStudy part;
    const std::vector<ThermalBoundaryCondition> bcs{convectionBc(1, part.endCap(), 25.0, 293.15)};
    const ThermalModel cylinder = part.model(part.coarse);
    const PreparedThermalBoundary prepared = prepareOn(cylinder, dofMapOf(cylinder), bcs);
    const ThermalModel block = part.model(part.otherControl);
    REQUIRE_FALSE(cylinder.body() == block.body());
    const std::vector<BoundaryStaleReason> reasons =
        thermal::staleReasons(prepared.source(), thermal::currentBoundarySource(block, bcs));
    CHECK(std::ranges::find(reasons, BoundaryStaleReason::Body) != reasons.end());
    CHECK(std::ranges::find(reasons, BoundaryStaleReason::Mesh) != reasons.end());
    CHECK_FALSE(thermal::isCurrent(prepared, block, bcs));
    // And a boundary is never applied to the other body's numbering.
    CHECK(thermal::thermalBoundaryProblem(block, dofMapOf(cylinder), bcs) ==
          std::optional<BoundaryProblem>{BoundaryProblem::DofMapMismatch});
}

TEST_CASE("ThermalBC_EditStalesPreparedBC", "[thermal][bc][currentness]") {
    // Every kind of edit to the canonical conditions makes a prepared boundary
    // stale, even though the mesh, the geometry and the facet set are unchanged.
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> original{convectionBc(1, study.endCap(), 10.0, 300.0),
                                                         temperatureBc(2, study.startCap(), 350.0)};
    const ThermalModel model = study.model();
    const PreparedThermalBoundary prepared = prepareOn(model, dofMapOf(model), original);
    REQUIRE(thermal::isCurrent(prepared, model, original));

    const auto reasonsFor = [&](const std::vector<ThermalBoundaryCondition>& edited) {
        return thermal::staleReasons(prepared.source(), thermal::currentBoundarySource(model, edited));
    };
    const std::vector<BoundaryStaleReason> conditionsOnly{BoundaryStaleReason::Conditions};

    SECTION("h: 10 -> 20, the target and ambient unchanged") {
        std::vector<ThermalBoundaryCondition> edited = original;
        edited[0] = convectionBc(1, study.endCap(), 20.0, 300.0);
        CHECK(reasonsFor(edited) == conditionsOnly);
        CHECK_FALSE(thermal::isCurrent(prepared, model, edited));
    }
    SECTION("T_inf: 300 -> 310 K") {
        std::vector<ThermalBoundaryCondition> edited = original;
        edited[0] = convectionBc(1, study.endCap(), 10.0, 310.0);
        CHECK(reasonsFor(edited) == conditionsOnly);
    }
    SECTION("T_bar: 350 -> 351 K") {
        std::vector<ThermalBoundaryCondition> edited = original;
        edited[1] = temperatureBc(2, study.startCap(), 351.0);
        CHECK(reasonsFor(edited) == conditionsOnly);
    }
    SECTION("the target: end cap -> a side wall") {
        std::vector<ThermalBoundaryCondition> edited = original;
        edited[0] = convectionBc(1, study.side(1), 10.0, 300.0);
        CHECK(reasonsFor(edited) == conditionsOnly);
    }
    SECTION("the type: the convection becomes a prescribed temperature") {
        std::vector<ThermalBoundaryCondition> edited = original;
        edited[0] = temperatureBc(1, study.endCap(), 300.0);
        CHECK(reasonsFor(edited) == conditionsOnly);
    }
    SECTION("a condition added, removed or reordered") {
        std::vector<ThermalBoundaryCondition> added = original;
        added.push_back(convectionBc(3, study.side(0), 5.0, 290.0));
        CHECK(reasonsFor(added) == conditionsOnly);
        CHECK(reasonsFor({original[0]}) == conditionsOnly);
        CHECK(reasonsFor({original[1], original[0]}) == conditionsOnly);
    }
    SECTION("an identity change with the same physics") {
        std::vector<ThermalBoundaryCondition> edited = original;
        edited[0] = convectionBc(9, study.endCap(), 10.0, 300.0);
        CHECK(reasonsFor(edited) == conditionsOnly);
    }
}

TEST_CASE("ThermalBC_EditStalesThermalResult", "[thermal][bc][currentness]") {
    // P18-DATA's mechanism, exercised by a boundary-condition edit: a result
    // computed under h = 10 is stale after the definition's h becomes 20, for
    // reason Analysis, with the mesh still current. No solve is needed.
    BoundaryStudy base;
    REQUIRE(base.setDefinition(ThermalAnalysisDefinition{.mesh = base.control,
                                                         .boundaryConditions = {convectionBc(1, base.endCap(), 10.0, 300.0)}})
                .has_value());
    const ThermalResultSource before = base.currentSource();
    const meshing::VolumeMesh& held = base.volume();
    const Result<ThermalResult> created = ThermalResult::create(
        before, held, std::vector<Temperature>(held.nodeCount(), 293.15_K),
        std::vector<thermal::TemperatureGradient3>(held.tetrahedronCount()),
        std::vector<thermal::HeatFlux3>(held.tetrahedronCount()), thermal::HeatBalance{}, std::nullopt);
    REQUIRE(created.has_value());
    CHECK(thermal::resultCurrency(&*created, before) == ResultCurrency::Current);

    ThermalAnalysisDefinition edited = base.definition();
    edited.boundaryConditions[0] = convectionBc(1, base.endCap(), 20.0, 300.0);
    REQUIRE(base.setDefinition(edited).has_value());

    const ThermalResultSource after = base.currentSource();
    CHECK(after.mesh == before.mesh);
    CHECK(after.analysisRevision != before.analysisRevision);
    CHECK(thermal::resultCurrency(&*created, after) == ResultCurrency::Stale);
    CHECK(thermal::staleReasons(created->source(), after) == std::vector{StaleReason::Analysis});
}

TEST_CASE("ThermalBC_MaterialEditKeepsPreparedBoundaryMappingCurrent", "[thermal][bc][currentness]") {
    // A conductivity edit leaves the mesh, the mapping and the conditions alone,
    // so the prepared boundary is still exactly right -- while the thermal RESULT
    // goes stale on its own material provenance. Two gates, two dependencies. And
    // the mesh is NOT regenerated.
    BoundaryStudy base;
    const std::vector<ThermalBoundaryCondition> bcs{temperatureBc(1, base.endCap(), 350.0),
                                                    convectionBc(2, base.startCap(), 25.0, 293.15)};
    REQUIRE(base.setDefinition(ThermalAnalysisDefinition{.mesh = base.control, .boundaryConditions = bcs}).has_value());
    const ThermalModel m1 = base.model();
    const PreparedThermalBoundary prepared = prepareOn(m1, dofMapOf(m1), bcs);
    const ThermalResultSource sourceBefore = base.currentSource();

    materials::ThermalProperties thermalProperties =
        features::findMaterial(base.document, base.material)->definition().thermal;
    thermalProperties.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(50_W_per_m_K);
    REQUIRE(features::setMaterialThermal(base.document, base.material, thermalProperties).has_value());

    // P16 is NOT asked to remesh: the mesh is still current.
    CHECK(base.mesher.currency(base.document, base.control) == meshing::MeshCurrency::Current);
    const ThermalModel m2 = base.model();
    CHECK(thermal::isCurrent(prepared, m2, bcs));
    CHECK(thermal::requireCurrent(prepared, m2, bcs).has_value());
    // Bit-identical when prepared again.
    CHECK(prepareOn(m2, dofMapOf(m2), bcs) == prepared);
    CHECK(thermal::staleReasons(prepared.source(), thermal::currentBoundarySource(m2, bcs)).empty());
    // The result's own gate does see it.
    const ThermalResultSource sourceAfter = base.currentSource();
    CHECK(thermal::staleReasons(sourceBefore, sourceAfter) == std::vector{StaleReason::Material});
    WARN("k 45 -> 50 W/(m K): mesh still current, prepared boundary still current and bit-identical; a result "
         "would be stale for reason material only");
}

TEST_CASE("ThermalBC_LoadSolverAndTransientEditsDoNotInvalidate", "[thermal][bc][currentness]") {
    // A load, a solver tolerance, the mode, the initial condition and the transient
    // settings are not inputs of a constraint or a convection term. Editing them
    // leaves a prepared boundary current and the definition overload's answer
    // unchanged. (They DO move the analysis revision, which stales a RESULT; that
    // is a different gate.)
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> bcs{temperatureBc(1, study.endCap(), 350.0),
                                                    convectionBc(2, study.startCap(), 25.0, 293.15)};
    REQUIRE(study.setDefinition(ThermalAnalysisDefinition{.mesh = study.control, .boundaryConditions = bcs}).has_value());
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const PreparedThermalBoundary before = prepareOn(model, map, bcs);
    const std::uint64_t analysisRevision = thermal::findThermalAnalysis(study.document, study.analysis)->revision();

    ThermalAnalysisDefinition edited = study.definition();
    edited.solver.relativeResidualTolerance = 1.0e-6;
    edited.loads.push_back(thermal::ThermalLoad{
        ThermalLoadId::fromValue(1), thermal::SurfaceHeatFluxLoad{.face = study.side(0), .flux = HeatFlux::fromSi(100.0)}});
    edited.mode = thermal::ThermalAnalysisMode::Transient;
    edited.initialCondition = thermal::UniformInitialTemperature{.value = 350_K};
    edited.transient = thermal::TransientSettings{.timeStep = 0.1_s, .endTime = 5_s};
    REQUIRE(study.setDefinition(edited).has_value());
    CHECK(thermal::findThermalAnalysis(study.document, study.analysis)->revision() != analysisRevision);

    CHECK(thermal::isCurrent(before, model, edited.boundaryConditions));
    const Result<PreparedThermalBoundary> viaDefinition = thermal::prepareThermalBoundary(model, map, edited);
    REQUIRE(viaDefinition.has_value());
    CHECK(*viaDefinition == before);
    CHECK(viaDefinition->source() == before.source());
}

TEST_CASE("ThermalBC_GeometryEditStalesPreparedBC", "[thermal][bc][currentness]") {
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> bcs{temperatureBc(1, study.endCap(), 350.0),
                                                    convectionBc(2, study.startCap(), 25.0, 293.15)};
    const ThermalModel m1 = study.model();
    const PreparedThermalBoundary p1 = prepareOn(m1, dofMapOf(m1), bcs);
    const meshing::GeometryRevision g1 = m1.geometryRevision();

    REQUIRE(study.document
                .modifyObject<features::ExtrudeFeature>(
                    study.feature,
                    [](features::ExtrudeFeature& extrude) -> Result<bool> {
                        auto definition = extrude.definition();
                        definition.depth = 35_mm;
                        return extrude.setDefinition(definition).has_value();
                    })
                .has_value());
    requireReport(study.regenerator, study.document);
    // The gate: a stale mesh yields no model, so nothing can be prepared on it.
    CHECK_FALSE(study.tryModel().has_value());

    study.remesh();
    const ThermalModel m2 = study.model();
    CHECK_FALSE(m2.geometryRevision() == g1);
    const std::vector<BoundaryStaleReason> reasons = thermal::staleReasons(p1.source(), thermal::currentBoundarySource(m2, bcs));
    CHECK(std::ranges::find(reasons, BoundaryStaleReason::Geometry) != reasons.end());
    CHECK(std::ranges::find(reasons, BoundaryStaleReason::Mesh) != reasons.end());
    // The same canonical FaceName still resolves on the edited body.
    const PreparedThermalBoundary p2 = prepareOn(m2, dofMapOf(m2), bcs);
    CHECK_THAT(p2.convectionArea().si(), WithinRel(kCapArea, 1e-12));
}

TEST_CASE("ThermalBC_DocumentBoundaryConditionsOnlyAreRead", "[thermal][bc][currentness]") {
    BoundaryStudy study;
    const std::vector<ThermalBoundaryCondition> bcs{convectionBc(1, study.endCap(), 25.0, 293.15)};
    REQUIRE(study.setDefinition(ThermalAnalysisDefinition{.mesh = study.control, .boundaryConditions = bcs}).has_value());
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const Result<PreparedThermalBoundary> viaDefinition = thermal::prepareThermalBoundary(model, map, study.definition());
    REQUIRE(viaDefinition.has_value());
    CHECK(*viaDefinition == prepareOn(model, map, bcs));
}

// ===========================================================================
// Purity and determinism
// ===========================================================================

TEST_CASE("ThermalBC_PreparationDoesNotMutateCanonicalBCCadOrMesh", "[thermal][bc][purity]") {
    BoundaryStudy base;
    const std::vector<ThermalBoundaryCondition> bcs{temperatureBc(1, base.endCap(), 350.0),
                                                    convectionBc(2, base.startCap(), 25.0, 293.15)};
    REQUIRE(base.setDefinition(ThermalAnalysisDefinition{.mesh = base.control, .boundaryConditions = bcs}).has_value());
    const ThermalModel model = base.model();
    const ThermalDofMap map = dofMapOf(model);

    const std::vector<ThermalBoundaryCondition> bcsBefore = bcs;
    const ThermalAnalysisDefinition definitionBefore = base.definition();
    const std::uint64_t analysisRevision = thermal::findThermalAnalysis(base.document, base.analysis)->revision();
    const std::uint64_t featureRevision = base.document.findObject(base.feature)->revision();
    const std::uint64_t documentRevision = base.document.revision();
    const meshing::MeshStamp stampBefore = model.mesh().mesh().stamp();
    const std::vector<meshing::Node> nodesBefore(model.mesh().mesh().nodes().begin(), model.mesh().mesh().nodes().end());
    const std::vector<meshing::Triangle> trianglesBefore(model.mesh().mesh().triangles().begin(),
                                                         model.mesh().mesh().triangles().end());
    const std::vector<meshing::Tetrahedron> tetsBefore(model.mesh().mesh().tetrahedra().begin(),
                                                       model.mesh().mesh().tetrahedra().end());
    const meshing::GeometryMeshMap mapBefore = model.map();

    for (int i = 0; i < 3; ++i) {
        (void)prepareOn(model, map, bcs);
    }
    (void)thermal::prepareThermalBoundary(model, map, base.definition());
    (void)thermal::thermalBoundaryProblem(model, map, bcs);

    // THE CANONICAL CONDITIONS: the same values, no facet, node or unknown written back.
    CHECK(bcs == bcsBefore);
    CHECK(base.definition() == definitionBefore);
    CHECK(thermal::findThermalAnalysis(base.document, base.analysis)->revision() == analysisRevision);
    CHECK(bcs[0].face() == base.endCap());
    // THE CAD.
    CHECK(base.document.findObject(base.feature)->revision() == featureRevision);
    CHECK(base.document.revision() == documentRevision);
    // THE MESH AND ITS MAPPING.
    CHECK(model.mesh().mesh().stamp() == stampBefore);
    CHECK(std::ranges::equal(model.mesh().mesh().nodes(), nodesBefore));
    CHECK(std::ranges::equal(model.mesh().mesh().triangles(), trianglesBefore));
    CHECK(std::ranges::equal(model.mesh().mesh().tetrahedra(), tetsBefore));
    CHECK(model.map() == mapBefore);
    CHECK(base.mesher.currency(base.document, base.control) == meshing::MeshCurrency::Current);
}

TEST_CASE("ThermalBC_RepeatedPreparationIsBitIdentical", "[thermal][bc][determinism]") {
    BoundaryStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const std::vector<ThermalBoundaryCondition> bcs{
        temperatureBc(1, study.endCap(), 350.123), convectionBc(2, study.startCap(), 25.5, 293.15),
        convectionBc(3, study.side(0), 7.25, 280.0), temperatureBc(4, study.side(2), 350.123),
        convectionBc(5, study.startCap(), 11.0, 301.0)};
    const PreparedThermalBoundary first = prepareOn(model, map, bcs);
    int differences = 0;
    for (int i = 0; i < 50; ++i) {
        if (!(prepareOn(model, map, bcs) == first)) {
            ++differences;
        }
    }
    CHECK(differences == 0);
    CHECK(first.source() == thermal::currentBoundarySource(model, bcs));
    WARN(std::format("determinism: 50 repeated preparations of a 5-condition mixed set, {} bitwise differences in the "
                     "constraints, the convection terms or the provenance",
                     differences));
}

TEST_CASE("ThermalBC_DiagnosticsAreDeterministic", "[thermal][bc][determinism]") {
    BoundaryStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const FaceName missing{study.feature, FaceSelector{.role = FaceRole::HoleBottom}};
    const std::vector<ThermalBoundaryCondition> bcs{convectionBc(1, study.endCap(), 25.0, 293.15), convectionBc(2, missing, 25.0, 293.15),
                                                    convectionBc(3, missing, 5.0, 300.0)};
    const Result<PreparedThermalBoundary> first = thermal::prepareThermalBoundary(model, map, bcs);
    REQUIRE_FALSE(first.has_value());
    for (int i = 0; i < 20; ++i) {
        const Result<PreparedThermalBoundary> again = thermal::prepareThermalBoundary(model, map, bcs);
        REQUIRE_FALSE(again.has_value());
        CHECK(again.error().code == first.error().code);
        CHECK(again.error().message == first.error().message);
        CHECK(thermal::thermalBoundaryProblem(model, map, bcs) == std::optional<BoundaryProblem>{BoundaryProblem::TargetUnresolved});
    }
    // The FIRST bad condition in canonical order is the one named.
    CHECK_THAT(first.error().message, ContainsSubstring(std::format("{}", bid(2))));

    // A conflict is reported the same way every time, and in a stable order.
    const std::vector<ThermalBoundaryCondition> clash{temperatureBc(7, study.endCap(), 350.0), temperatureBc(3, study.side(0), 300.0)};
    const auto one = thermal::thermalBoundaryFailure(model, map, clash);
    REQUIRE(one.has_value());
    for (int i = 0; i < 20; ++i) {
        const auto again = thermal::thermalBoundaryFailure(model, map, clash);
        REQUIRE(again.has_value());
        CHECK(again->conflicts == one->conflicts);
        CHECK(again->error.message == one->error.message);
    }
    // Ordered by condition id, whatever the list order.
    REQUIRE(one->conflicts.size() == 1);
    CHECK(one->conflicts[0].first == bid(3));
    CHECK(one->conflicts[0].second == bid(7));
    const auto swapped = thermal::thermalBoundaryFailure(model, map, std::vector{clash[1], clash[0]});
    REQUIRE(swapped.has_value());
    CHECK(swapped->conflicts == one->conflicts);
}

TEST_CASE("ThermalBC_BitFingerprintForCrossPresetComparison", "[thermal][bc][determinism]") {
    // A 64-bit FNV-1a hash over the bit patterns of the hand Robin terms and the
    // real block's prepared boundary, printed so the three presets can be compared
    // exactly rather than to a tolerance. Equality across presets is MEASURED, not
    // promised: the test asserts only reproducibility.
    const auto hashOf = [](auto&& fill) {
        std::uint64_t hash = 14695981039346656037ULL;
        const auto mix = [&hash](double value) {
            const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
            for (int i = 0; i < 8; ++i) {
                hash ^= (bits >> (8 * i)) & 0xFFU;
                hash *= 1099511628211ULL;
            }
        };
        fill(mix);
        return hash;
    };
    const auto handHash = [&] {
        return hashOf([](auto&& mix) {
            const Hand tri = makeHand({{10, at(0.3, -0.2, 0.5)}, {20, at(1.7, 0.4, 0.9)}, {30, at(0.1, 1.3, -0.4)}}, {{0, 1, 2}});
            for (const ConvectionFacet& f : convect(tri, 17.5, 288.15)) {
                for (std::size_t r = 0; r < 3; ++r) {
                    mix(f.term.source(r).si());
                    for (std::size_t c = 0; c < 3; ++c) {
                        mix(f.term(r, c).si());
                    }
                }
                mix(f.term.area().si());
            }
        });
    };
    const std::uint64_t hand1 = handHash();
    CHECK(hand1 == handHash());

    BoundaryStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const std::vector<ThermalBoundaryCondition> bcs{temperatureBc(1, study.endCap(), 350.123),
                                                    convectionBc(2, study.startCap(), 25.5, 293.15),
                                                    convectionBc(3, study.side(0), 7.25, 280.0)};
    const auto blockHash = [&] {
        const PreparedThermalBoundary prepared = prepareOn(model, map, bcs);
        return hashOf([&prepared, &map](auto&& mix) {
            for (const auto& constraint : prepared.constraints().constrained()) {
                mix(static_cast<double>(constraint.dof.value()));
                mix(constraint.temperature.si());
            }
            const Dense dense = denseOf(prepared, static_cast<std::size_t>(map.dofCount()));
            for (const double v : dense.k) {
                mix(v);
            }
            for (const double v : dense.q) {
                mix(v);
            }
        });
    };
    const std::uint64_t block1 = blockHash();
    CHECK(block1 == blockHash());
    WARN(std::format("bit fingerprint, hand Robin triangle: {:016x}", hand1));
    WARN(std::format("bit fingerprint, real block, {} nodes, Dirichlet + two convections: {:016x}", model.mesh().nodeCount(), block1));
}

TEST_CASE("ThermalBC_PreparedBoundaryHasNoPenaltyAssemblyOrSolve", "[thermal][bc][scope]") {
    // The prepared type exposes constraints, convection terms and their
    // attribution and nothing else: no stiffness matrix, no right-hand side, no
    // penalty factor, no reduced system, no solution. The interface is the
    // evidence; the source audit is in SCOPE.md.
    STATIC_CHECK(std::is_same_v<decltype(std::declval<const PreparedThermalBoundary&>().constraints()),
                                const thermal::ThermalConstraintMap&>);
    STATIC_CHECK(std::is_same_v<decltype(std::declval<const PreparedThermalBoundary&>().convectionSource()), Power>);
    STATIC_CHECK(std::is_same_v<decltype(std::declval<const PreparedThermalBoundary&>().convectionConductance()),
                                ThermalConductance>);
    // A constrained unknown's value is the prescribed temperature itself: the
    // map holds a Temperature, not a "large number" or a force.
    STATIC_CHECK(std::is_same_v<decltype(thermal::ThermalTemperatureConstraint::temperature), Temperature>);
    // Nothing was added to a diagonal: the convection terms of a face carrying a
    // prescribed temperature are the convection terms of the face alone.
    BoundaryStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const PreparedThermalBoundary both = prepareOn(model, map, {temperatureBc(1, study.endCap(), 350.0), convectionBc(2, study.endCap(), 25.0, 293.15)});
    const PreparedThermalBoundary convectionOnly = prepareOn(model, map, {convectionBc(2, study.endCap(), 25.0, 293.15)});
    const Dense a = denseOf(both, static_cast<std::size_t>(map.dofCount()));
    const Dense b = denseOf(convectionOnly, static_cast<std::size_t>(map.dofCount()));
    CHECK(a.k == b.k);
    CHECK(a.q == b.q);
}
