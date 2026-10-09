// P17-POST-001 against the meshing reference models.
//
// WHY THESE ARE SEPARATE from tests/structural/StructuralPostTests.cpp. Those
// tests validate the formulas against written-down fields and closed-form
// tensors. These do the four things a synthetic fixture cannot:
//
//   an INDEPENDENT ROUTE TO B over a real solved field -- the linear
//     displacement field of each tetrahedron is recovered by solving a 4x4
//     Vandermonde-like system for its coefficients and then differentiated,
//     which shares no code and no algorithm with the production shape-gradient
//   the CONTINUUM check -- axial stress against F/A on a real meshed prism,
//     one-sided because a constant-strain Tet4 is stiffer than the continuum
//   the SCALE claims -- a mesh an order of magnitude larger still recovers,
//     with storage growing linearly in the two counts
//   the ROTATED MODEL -- the same body, mesh, load and restraint expressed on
//     a rotated and translated frame, where the invariants must not move
//
// THE ROTATED CASE USES A PRESSURE LOAD ON PURPOSE. A `SurfaceTractionLoad`
// carries GLOBAL components, so rotating the model would not rotate the load
// and the two problems would not be equivalent. A `PressureLoad` acts along
// the current surface normal, so it rotates with the face -- which makes
// "the same problem, expressed on a rotated frame" an actual statement rather
// than an approximation. P17-LOAD-001 froze that distinction and this is the
// first milestone to depend on it.
//
// THE LARGE MESH IS RM-MESH-02, A CYLINDER, AND NOT A BLOCK. A plane is
// exactly representable, so a box's surface mesh stays at two triangles per
// face however fine the deflection control: refining a block gives 9 nodes and
// measures nothing. Only a curved wall responds. The same trap cost
// P17-ASSEMBLY-001 a fixture and it is recorded here so it costs nothing next
// time.

#include "reference/MeshTestSupport.hpp"

#include <MeshReferenceModels.hpp>

#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/meshing/MeshSizing.hpp>
#include <bettercad/structural/StructuralConstraints.hpp>
#include <bettercad/structural/StructuralLoadVector.hpp>
#include <bettercad/structural/StructuralPost.hpp>
#include <bettercad/structural/StructuralSolve.hpp>
#include <bettercad/structural/StructuralSystem.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace bettercad;
using namespace bettercad::test;
using namespace bettercad::test::meshref;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using structural::ConstraintSet;
using structural::ElementFields;
using structural::GlobalStructuralSystem;
using structural::kTet4Nodes;
using structural::MeshDofMap;
using structural::NodalDisplacement;
using structural::PreparedLoads;
using structural::PrincipalStresses;
using structural::RecoveredFields;
using structural::SolvedSystem;
using structural::Strain6;
using structural::Stress6;
using structural::StructuralAnalysisMode;
using structural::StructuralLoad;
using structural::StructuralMaterial;
using structural::StructuralModel;
using structural::StructuralRestraint;

constexpr double kYoungs = 210.0e9;
constexpr double kPoisson = 0.3;
constexpr double kMu = kYoungs / (2.0 * (1.0 + kPoisson));
constexpr double kLambda = kYoungs * kPoisson / ((1.0 + kPoisson) * (1.0 - 2.0 * kPoisson));

void assignSteel(Document& document) {
    features::MaterialDefinition definition;
    definition.designation = "Steel";
    definition.mechanical.youngsModulus =
        materials::MaterialProperty<ElasticModulus>::known(ElasticModulus::fromSi(kYoungs));
    definition.mechanical.poissonRatio =
        materials::MaterialProperty<bettercad::PoissonRatio>::known(PoissonRatio::of(kPoisson));
    definition.mechanical.density =
        materials::MaterialProperty<Density>::known(Density::fromSi(7850.0));
    const Result<MaterialId> id = features::createMaterial(document, "Steel", definition);
    REQUIRE(id.has_value());
    REQUIRE(features::assignMaterial(document, *id).has_value());
}

[[nodiscard]] StructuralModel modelOf(MeshedReference& reference) {
    Result<StructuralModel> model = structural::requireStructuralModel(
        reference.document(), reference.regenerator(), reference.mesher(), reference.control());
    INFO((model.has_value() ? std::string{} : model.error().message));
    REQUIRE(model.has_value());
    return std::move(*model);
}

[[nodiscard]] StructuralMaterial materialOf(MeshedReference& reference) {
    Result<StructuralMaterial> material = structural::resolveStructuralMaterial(
        reference.document(), reference.body(), StructuralAnalysisMode::LinearStatic);
    INFO((material.has_value() ? std::string{} : material.error().message));
    REQUIRE(material.has_value());
    return *material;
}

[[nodiscard]] GlobalStructuralSystem assemble(MeshedReference& reference,
                                              const std::vector<StructuralLoad>& loads) {
    const StructuralModel model = modelOf(reference);
    const StructuralMaterial material = materialOf(reference);
    Result<PreparedLoads> prepared = structural::prepareStructuralLoads(model, material, loads);
    INFO((prepared.has_value() ? std::string{} : prepared.error().message));
    REQUIRE(prepared.has_value());
    Result<GlobalStructuralSystem> system =
        structural::assembleStructuralSystem(model, material, *prepared);
    INFO((system.has_value() ? std::string{} : system.error().message));
    REQUIRE(system.has_value());
    return std::move(*system);
}

[[nodiscard]] ConstraintSet fixedSupportOn(MeshedReference& reference, const FaceName& face) {
    const StructuralModel model = modelOf(reference);
    Result<MeshDofMap> numbering = structural::buildMeshDofMap(model.mesh().mesh());
    REQUIRE(numbering.has_value());
    Result<structural::PreparedRestraints> prepared = structural::prepareStructuralRestraints(
        model, *numbering,
        std::vector{StructuralRestraint::fixedSupport(RestraintId::fromValue(1), face)});
    INFO((prepared.has_value() ? std::string{} : prepared.error().message));
    REQUIRE(prepared.has_value());
    return prepared->constraints();
}

/// Every node of @p face pushed along @p axis, sharing @p total between them.
[[nodiscard]] std::vector<StructuralLoad> spreadLoad(MeshedReference& reference,
                                                     const FaceName& face, std::size_t axis,
                                                     double total) {
    const StructuralModel model = modelOf(reference);
    Result<meshing::BoundaryFacetSet> facets = meshing::boundaryFacetsOf(model.map(), face);
    REQUIRE(facets.has_value());
    Result<std::vector<meshing::NodeId>> nodes =
        meshing::boundaryNodesOf(model.map(), model.mesh().mesh(), facets->facets);
    REQUIRE(nodes.has_value());
    REQUIRE_FALSE(nodes->empty());

    const double each = total / static_cast<double>(nodes->size());
    std::array<double, 3> components{};
    components[axis] = each;
    std::vector<StructuralLoad> loads;
    std::uint64_t id = 1;
    for (const meshing::NodeId node : *nodes) {
        loads.push_back(StructuralLoad{
            LoadId::fromValue(id++),
            structural::NodalForceLoad{
                .mesh = model.mesh().mesh().stamp(),
                .node = node,
                .force = Force3D{Force::fromSi(components[0]), Force::fromSi(components[1]),
                                 Force::fromSi(components[2])}}});
    }
    return loads;
}

[[nodiscard]] SolvedSystem solveOf(const GlobalStructuralSystem& system,
                                   const ConstraintSet& constraints) {
    Result<SolvedSystem> solved = structural::solveStructuralSystem(system, constraints, {});
    INFO((solved.has_value() ? std::string{} : solved.error().message));
    REQUIRE(solved.has_value());
    return std::move(*solved);
}

[[nodiscard]] RecoveredFields recoverOf(MeshedReference& reference,
                                        const GlobalStructuralSystem& system,
                                        const SolvedSystem& solution) {
    Result<RecoveredFields> fields = structural::recoverFields(
        modelOf(reference), materialOf(reference), system, solution);
    INFO((fields.has_value() ? std::string{} : fields.error().message));
    REQUIRE(fields.has_value());
    return std::move(*fields);
}

// -----------------------------------------------------------------------
// The independent route to B: fit the linear field, then differentiate
// -----------------------------------------------------------------------

/// Solves a 4x4 system by Gaussian elimination with partial pivoting.
///
/// Written out here rather than taken from a library, so there is no shared
/// assumption with production to inherit.
[[nodiscard]] std::array<double, 4> solve4(std::array<std::array<double, 5>, 4> rows) {
    for (std::size_t column = 0; column < 4; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1; row < 4; ++row) {
            if (std::abs(rows[row][column]) > std::abs(rows[pivot][column])) {
                pivot = row;
            }
        }
        REQUIRE(std::abs(rows[pivot][column]) > 0.0);
        std::swap(rows[column], rows[pivot]);
        for (std::size_t row = column + 1; row < 4; ++row) {
            const double factor = rows[row][column] / rows[column][column];
            for (std::size_t k = column; k < 5; ++k) {
                rows[row][k] -= factor * rows[column][k];
            }
        }
    }
    std::array<double, 4> x{};
    for (std::size_t i = 4; i-- > 0;) {
        double sum = rows[i][4];
        for (std::size_t k = i + 1; k < 4; ++k) {
            sum -= rows[i][k] * x[k];
        }
        x[i] = sum / rows[i][i];
    }
    return x;
}

/// The strain of one tetrahedron, recovered WITHOUT the production `B`.
///
/// A Tet4's displacement field is linear, so each component is
///
/// ```text
///     u = a0 + a1 x + a2 y + a3 z
/// ```
///
/// and its four nodal values determine the four coefficients exactly. Fitting
/// that system and reading off `a1 a2 a3` gives the gradient by a completely
/// different route from a shape-function derivative: no `1/(6V)`, no cofactor
/// determinant, no reference element and no transpose. The engineering strain
/// then follows from the continuum definitions.
[[nodiscard]] Strain6 fittedStrain(const std::array<Point3D, kTet4Nodes>& corners,
                                   const std::array<Translation3D, kTet4Nodes>& u) {
    std::array<std::array<double, 3>, 3> gradient{};
    for (std::size_t component = 0; component < 3; ++component) {
        std::array<std::array<double, 5>, 4> rows{};
        for (std::size_t node = 0; node < kTet4Nodes; ++node) {
            rows[node][0] = 1.0;
            rows[node][1] = corners[node].x.si();
            rows[node][2] = corners[node].y.si();
            rows[node][3] = corners[node].z.si();
            rows[node][4] = component == 0 ? u[node].x.si()
                                           : (component == 1 ? u[node].y.si() : u[node].z.si());
        }
        const std::array<double, 4> fit = solve4(rows);
        gradient[component] = {fit[1], fit[2], fit[3]};
    }

    // exx = du_x/dx, gxy = du_x/dy + du_y/dx, and so on -- the continuum
    // definitions, with ENGINEERING shear.
    return Strain6{.xx = gradient[0][0],
                   .yy = gradient[1][1],
                   .zz = gradient[2][2],
                   .gammaXy = gradient[0][1] + gradient[1][0],
                   .gammaYz = gradient[1][2] + gradient[2][1],
                   .gammaZx = gradient[2][0] + gradient[0][2]};
}

/// `sigma = Dref eps`, from the continuum relations rather than production's D.
[[nodiscard]] Stress6 referenceStress(const Strain6& e) {
    const double trace = e.xx + e.yy + e.zz;
    return Stress6{.xx = Stress::fromSi(kLambda * trace + 2.0 * kMu * e.xx),
                   .yy = Stress::fromSi(kLambda * trace + 2.0 * kMu * e.yy),
                   .zz = Stress::fromSi(kLambda * trace + 2.0 * kMu * e.zz),
                   .xy = Stress::fromSi(kMu * e.gammaXy),
                   .yz = Stress::fromSi(kMu * e.gammaYz),
                   .zx = Stress::fromSi(kMu * e.gammaZx)};
}

/// Von Mises through the deviatoric invariant, which production does not use.
[[nodiscard]] double deviatoricVonMises(const Stress6& s) {
    const double mean = (s.xx.si() + s.yy.si() + s.zz.si()) / 3.0;
    const double dxx = s.xx.si() - mean;
    const double dyy = s.yy.si() - mean;
    const double dzz = s.zz.si() - mean;
    const double contraction =
        dxx * dxx + dyy * dyy + dzz * dzz +
        2.0 * (s.xy.si() * s.xy.si() + s.yz.si() * s.yz.si() + s.zx.si() * s.zx.si());
    return std::sqrt(1.5 * contraction);
}

} // namespace

TEST_CASE("StructuralPost_MatchesAnIndependentlyFittedStrainOnEveryElement",
          "[structural][post][reference]") {
    // RM-MESH-01, THE SMALLEST REFERENCE MODEL: a 120 x 70 x 35 mm block at a
    // 20 mm global target, 8 nodes and 6 tetrahedra. Small enough to fit a
    // linear field per element and compare every one, and a real structural
    // problem produced by the whole qualified chain.
    auto built = reference::buildMeshBlockReferenceModel();
    REQUIRE(built.has_value());
    const FaceName bottom = built->bottom();
    const FaceName top = built->top();
    MeshedReference reference(std::move(built->document));
    assignSteel(reference.document());
    const meshing::VolumeMesh& volume = reference.require();

    const GlobalStructuralSystem system = assemble(reference, spreadLoad(reference, top, 2, 5000.0));
    const SolvedSystem solution = solveOf(system, fixedSupportOn(reference, bottom));
    const RecoveredFields fields = recoverOf(reference, system, solution);

    const meshing::Mesh& mesh = volume.mesh();
    REQUIRE(fields.elements().size() == mesh.tetrahedra().size());
    INFO("nodes " << mesh.nodes().size() << ", tets " << mesh.tetrahedra().size());

    std::size_t compared = 0;
    double worstStrain = 0.0;
    double worstStress = 0.0;
    double worstVonMises = 0.0;
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        Result<ElementFields> stored = fields.fieldsOf(mesh, tet.id);
        REQUIRE(stored.has_value());

        std::array<Point3D, kTet4Nodes> corners{};
        std::array<Translation3D, kTet4Nodes> u{};
        for (std::size_t corner = 0; corner < kTet4Nodes; ++corner) {
            const meshing::Node* node = mesh.findNode(tet.nodes[corner]);
            REQUIRE(node != nullptr);
            corners[corner] = node->position;
            // THE NODAL CHANNEL, not the solver vector: this also ties the
            // element gather to the per-node result.
            Result<NodalDisplacement> nodal = fields.displacementOf(mesh, tet.nodes[corner]);
            REQUIRE(nodal.has_value());
            u[corner] = nodal->displacement;
        }

        const Strain6 fitted = fittedStrain(corners, u);
        const Stress6 expected = referenceStress(fitted);

        // THE SCALE THE ERRORS ARE JUDGED AGAINST, so a relative bound means
        // something on a near-zero component.
        const double strainScale =
            std::max({std::abs(fitted.xx), std::abs(fitted.yy), std::abs(fitted.zz),
                      std::abs(fitted.gammaXy), std::abs(fitted.gammaYz),
                      std::abs(fitted.gammaZx), 1e-30});
        const double stressScale =
            std::max({std::abs(expected.xx.si()), std::abs(expected.yy.si()),
                      std::abs(expected.zz.si()), std::abs(expected.xy.si()),
                      std::abs(expected.yz.si()), std::abs(expected.zx.si()), 1e-30});

        const auto strainError = [&](double a, double b) {
            return std::abs(a - b) / strainScale;
        };
        const auto stressError = [&](Stress a, Stress b) {
            return std::abs(a.si() - b.si()) / stressScale;
        };

        worstStrain = std::max(
            {worstStrain, strainError(stored->strain.xx, fitted.xx),
             strainError(stored->strain.yy, fitted.yy), strainError(stored->strain.zz, fitted.zz),
             strainError(stored->strain.gammaXy, fitted.gammaXy),
             strainError(stored->strain.gammaYz, fitted.gammaYz),
             strainError(stored->strain.gammaZx, fitted.gammaZx)});
        worstStress = std::max({worstStress, stressError(stored->stress.xx, expected.xx),
                                stressError(stored->stress.yy, expected.yy),
                                stressError(stored->stress.zz, expected.zz),
                                stressError(stored->stress.xy, expected.xy),
                                stressError(stored->stress.yz, expected.yz),
                                stressError(stored->stress.zx, expected.zx)});
        worstVonMises =
            std::max(worstVonMises, std::abs(stored->vonMises.si() - deviatoricVonMises(expected)) /
                                        std::max(deviatoricVonMises(expected), 1e-30));
        ++compared;
    }

    INFO("worst relative strain error " << worstStrain << ", stress " << worstStress
                                        << ", von Mises " << worstVonMises);
    WARN("RM-MESH-01 independent fit | " << compared << " elements | worst relative error: strain "
                                         << worstStrain << " | stress " << worstStress
                                         << " | von Mises " << worstVonMises);
    // 1e-9 is the geometric-accumulation band: the fit inverts a 4x4
    // coordinate matrix and production forms cofactors, so the two differ by
    // conditioning rather than by formula.
    REQUIRE(worstStrain < 1e-9);
    REQUIRE(worstStress < 1e-9);
    REQUIRE(worstVonMises < 1e-9);
    REQUIRE(compared == mesh.tetrahedra().size());
    REQUIRE(compared > 0);

    SECTION("and the strains are not all zero, so the comparison measured something") {
        // A VACUOUS-INSTRUMENT GUARD. A rigid-body solution would make every
        // error above exactly zero and prove nothing.
        bool deformed = false;
        for (const ElementFields& e : fields.elements()) {
            if (e.vonMises.si() > 1.0) {
                deformed = true;
                break;
            }
        }
        REQUIRE(deformed);
        REQUIRE(fields.largestVonMises().si() > 1.0);
        REQUIRE(fields.largestDisplacementMagnitude().si() > 0.0);
    }
}

TEST_CASE("StructuralPost_ValidatesAxialStressAgainstTheContinuumRelations",
          "[structural][post][reference]") {
    // RM-MESH-01 as an axial prism: fixed at z = 0, pulled along +Z at
    // z = 35 mm. The continuum answers are
    //
    //     sigma_zz = F / A        A = 120 x 70 mm
    //     eps_zz   = sigma / E
    //
    // and a CONSTANT-STRAIN TET4 IS STIFFER THAN THE CONTINUUM, so the check
    // is one-sided on the displacement and bounded on the mean stress. Nothing
    // here demands beam or bar theory from a six-element mesh.
    auto built = reference::buildMeshBlockReferenceModel();
    REQUIRE(built.has_value());
    const FaceName bottom = built->bottom();
    const FaceName top = built->top();
    MeshedReference reference(std::move(built->document));
    assignSteel(reference.document());
    const meshing::VolumeMesh& volume = reference.require();

    constexpr double kForce = 50000.0; // N, along +Z
    constexpr double kArea = 0.120 * 0.070;
    constexpr double kLength = 0.035;
    const GlobalStructuralSystem system =
        assemble(reference, spreadLoad(reference, top, 2, kForce));
    const SolvedSystem solution = solveOf(system, fixedSupportOn(reference, bottom));
    const RecoveredFields fields = recoverOf(reference, system, solution);

    const double nominalStress = kForce / kArea;
    const double nominalStrain = nominalStress / kYoungs;
    const double nominalDelta = kForce * kLength / (kArea * kYoungs);

    SECTION("the volume-averaged axial stress is near F/A") {
        // VOLUME-WEIGHTED, because element stresses are constant per element
        // and the elements are not equal in size.
        //
        // AND THIS IS AN EQUALITY, NOT A BAND. For a body in equilibrium whose
        // only axial traction is on the two end faces,
        //
        //     (1/V) integral sigma_zz dV = F L / V = F / A
        //
        // holds EXACTLY -- it is a statement of equilibrium, which the
        // assembled discrete system satisfies exactly, so it does not depend on
        // the mesh being fine. The bound is therefore the floating-point
        // accumulation band and not a tuned tolerance. Measured ratio: 1.0 to
        // within 1e-14.
        double weighted = 0.0;
        double total = 0.0;
        const meshing::Mesh& mesh = volume.mesh();
        for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
            std::array<Point3D, kTet4Nodes> corners{};
            for (std::size_t corner = 0; corner < kTet4Nodes; ++corner) {
                const meshing::Node* node = mesh.findNode(tet.nodes[corner]);
                REQUIRE(node != nullptr);
                corners[corner] = node->position;
            }
            const Volume v = meshing::signedVolume(corners[0], corners[1], corners[2], corners[3]);
            Result<ElementFields> e = fields.fieldsOf(mesh, tet.id);
            REQUIRE(e.has_value());
            weighted += v.si() * e->stress.zz.si();
            total += v.si();
        }
        REQUIRE(total > 0.0);
        const double mean = weighted / total;
        INFO("mean sigma_zz " << mean << " vs F/A " << nominalStress);
        WARN("RM-MESH-01 axial stress | volume-weighted mean sigma_zz " << mean << " Pa | F/A "
                                                                       << nominalStress
                                                                       << " Pa | ratio "
                                                                       << mean / nominalStress);
        REQUIRE_THAT(mean, WithinRel(nominalStress, 1e-9));
        // And the sign is right: a tensile load gives tensile stress.
        REQUIRE(mean > 0.0);
    }

    SECTION("the MEAN axial displacement of the loaded face is positive and below FL/(AE)") {
        // THE ONE-SIDED BOUND, which is known a priori and cannot be tuned --
        // a constant-strain Tet4 is stiffer than the continuum, so the
        // compliance must come out lower.
        //
        // THE MEAN OF THE LOADED FACE, NOT A POINT MAXIMUM, and the distinction
        // is not cosmetic: the load is lumped equally onto the face's NODES,
        // which on a coarse mesh is not a uniform traction, so a single node
        // can displace further than the uniform-strain value. Measured here:
        // the largest u_z is 1.384e-06 m against FL/(AE) = 9.92e-07 m, which
        // would have failed a maximum-based bound while the mean passes
        // comfortably. The compliance bound is a statement about the average
        // elongation and only the average is bounded.
        //
        // READ THROUGH THE RECOVERED CHANNEL, which is what makes this
        // P17-POST's test rather than a copy of P17-SOLVE's: it also proves the
        // recovered per-node displacement agrees with the solved field on the
        // quantity the continuum bound is about.
        const StructuralModel model = modelOf(reference);
        Result<meshing::BoundaryFacetSet> facets = meshing::boundaryFacetsOf(model.map(), top);
        REQUIRE(facets.has_value());
        Result<std::vector<meshing::NodeId>> loaded =
            meshing::boundaryNodesOf(model.map(), model.mesh().mesh(), facets->facets);
        REQUIRE(loaded.has_value());
        REQUIRE_FALSE(loaded->empty());

        double total = 0.0;
        double largest = 0.0;
        for (const meshing::NodeId node : *loaded) {
            Result<NodalDisplacement> d =
                fields.displacementOf(model.mesh().mesh(), node);
            REQUIRE(d.has_value());
            total += d->displacement.z.si();
            largest = std::max(largest, d->displacement.z.si());
        }
        const double mean = total / static_cast<double>(loaded->size());

        INFO("mean u_z " << mean << ", largest " << largest << ", FL/(AE) " << nominalDelta);
        WARN("RM-MESH-01 axial deflection | mean u_z " << mean << " m | largest u_z " << largest
                                                       << " m | FL/(AE) " << nominalDelta
                                                       << " m | mean ratio " << mean / nominalDelta
                                                       << " | largest ratio "
                                                       << largest / nominalDelta);
        REQUIRE(mean > 0.0);
        REQUIRE(mean < nominalDelta);
        // Not absurdly small either: a coarse Tet4 prism is stiff, not rigid.
        REQUIRE(mean > 0.5 * nominalDelta);
    }

    SECTION("and the axial strain is of the order sigma/E") {
        double largestStrain = 0.0;
        for (const ElementFields& e : fields.elements()) {
            largestStrain = std::max(largestStrain, std::abs(e.strain.zz));
        }
        INFO("largest |eps_zz| " << largestStrain << " vs sigma/E " << nominalStrain);
        REQUIRE(largestStrain > 0.2 * nominalStrain);
        REQUIRE(largestStrain < 5.0 * nominalStrain);
    }

    SECTION("the magnitude of each nodal displacement is its own norm") {
        for (const NodalDisplacement& d : fields.displacements()) {
            REQUIRE_THAT(d.magnitude.si(),
                         WithinAbs(std::hypot(d.displacement.x.si(), d.displacement.y.si(),
                                              d.displacement.z.si()),
                                   1e-18));
        }
    }
}

TEST_CASE("StructuralPost_RecoversALargeMeshWithLinearStorageGrowth",
          "[structural][post][reference]") {
    // RM-MESH-02, A CYLINDER, at two refinements. A block cannot be used here:
    // a plane is exactly representable, so its surface mesh stays at two
    // triangles per face however fine the deflection control. Only a curved
    // wall responds, which is why P17-ASSEMBLY-001 settled on this model.
    //
    // 2.5e-5 deflection over a 6 mm target is its sizing, chosen after Netgen
    // refused a finer one with "Illegal position in Geomsearch".
    //
    // NOT A PERFORMANCE TEST. No wall-clock bound is asserted and no timing is
    // a gate.
    struct Level {
        std::size_t nodes = 0;
        std::size_t elements = 0;
        std::size_t nodeResults = 0;
        std::size_t elementResults = 0;
    };
    std::array<Level, 2> levels{};

    const std::array<double, 2> deflections{2.5e-4, 2.5e-5};
    const std::array<double, 2> targets{12.0e-3, 6.0e-3};

    for (std::size_t level = 0; level < 2; ++level) {
        auto built = reference::buildMeshCylinderReferenceModel();
        REQUIRE(built.has_value());
        const FaceName bottom = built->bottomCap();
        const FaceName top = built->topCap();
        MeshedReference reference(std::move(built->document));
        assignSteel(reference.document());

        meshing::MeshControlDefinition definition = reference.definition()->definition();
        definition.mesh.surface.linearDeflection = Length::fromSi(deflections[level]);
        definition.mesh.sizing.globalTargetSize = Length::fromSi(targets[level]);
        REQUIRE(reference.document()
                    .modifyObject<meshing::MeshControl>(
                        reference.controlObject(),
                        [&definition](meshing::MeshControl& control) {
                            return control.setDefinition(definition);
                        })
                    .has_value());
        const meshing::VolumeMesh& volume = reference.require();

        const GlobalStructuralSystem system =
            assemble(reference, spreadLoad(reference, top, 2, 10000.0));
        const SolvedSystem solution = solveOf(system, fixedSupportOn(reference, bottom));
        const RecoveredFields fields = recoverOf(reference, system, solution);

        levels[level] = Level{.nodes = volume.mesh().nodes().size(),
                              .elements = volume.mesh().tetrahedra().size(),
                              .nodeResults = fields.displacements().size(),
                              .elementResults = fields.elements().size()};

        // EVERY VALUE FINITE, over the whole mesh, in every channel.
        for (const NodalDisplacement& d : fields.displacements()) {
            REQUIRE(isFinite(d.displacement));
            REQUIRE(isFinite(d.magnitude));
        }
        double worst = 0.0;
        for (const ElementFields& e : fields.elements()) {
            REQUIRE(structural::isFinite(e.strain));
            REQUIRE(structural::isFinite(e.stress));
            REQUIRE(isFinite(e.vonMises));
            REQUIRE(e.vonMises.si() >= 0.0);
            REQUIRE(e.principalStress.sigma1.si() >= e.principalStress.sigma3.si());
            // The invariant cross-check, on every element of a large mesh.
            const double reference_ = deviatoricVonMises(e.stress);
            if (reference_ > 1.0) {
                worst = std::max(worst, std::abs(e.vonMises.si() - reference_) / reference_);
            }
        }
        INFO("level " << level << ": worst von Mises deviation " << worst);
        REQUIRE(worst < 1e-9);

        REQUIRE(fields.describes(volume.mesh()));
        REQUIRE(fields.largestVonMises().si() > 0.0);
        REQUIRE(fields.largestDisplacementMagnitude().si() > 0.0);

        WARN("RM-MESH-02 level " << level << " | " << levels[level].nodes << " nodes | "
                                 << levels[level].elements << " tets | " << levels[level].nodeResults
                                 << " nodal results | " << levels[level].elementResults
                                 << " element results | peak |u| "
                                 << fields.largestDisplacementMagnitude().si() << " m | peak vm "
                                 << fields.largestVonMises().si() << " Pa");
    }

    SECTION("the finer level really is an order of magnitude larger") {
        // A VACUOUS-INSTRUMENT GUARD, and the one P17-ASSEMBLY-001 needed:
        // two sizings that produce the same mesh would make the growth claim
        // below meaningless.
        REQUIRE(levels[1].nodes > 5 * levels[0].nodes);
        REQUIRE(levels[1].elements > 5 * levels[0].elements);
        REQUIRE(levels[1].nodes > 400);
        REQUIRE(levels[1].elements > 1500);
    }

    SECTION("storage is exactly one entry per node and one per element, at both levels") {
        // THE O(nodes + elements) CLAIM, MEASURED rather than asserted: the
        // result counts track the mesh counts exactly across a tenfold growth,
        // so nothing in the result scales with their product.
        for (const Level& level : levels) {
            REQUIRE(level.nodeResults == level.nodes);
            REQUIRE(level.elementResults == level.elements);
        }
    }
}

TEST_CASE("StructuralPost_KeepsTheInvariantsWhenTheWholeModelIsRotated",
          "[structural][post][reference]") {
    // RM-MESH-06: the SAME 90 x 55 x 24 mm block, built once at the origin on
    // the XY plane and once on a rotated and translated frame.
    //
    // THE LOAD IS A PRESSURE, NOT A TRACTION, AND THAT IS WHAT MAKES THE TWO
    // PROBLEMS EQUIVALENT. A pressure acts along the current surface normal, so
    // it rotates with the face; a traction carries global components and would
    // not. P17-LOAD-001 froze that distinction and this is the first milestone
    // to depend on it.
    //
    // WHAT IS CLAIMED: the SCALAR INVARIANTS -- von Mises, the principal
    // stresses, the displacement magnitude -- are properties of the physical
    // state and must not move. The stress COMPONENTS are expressed on global
    // axes and therefore must, so they are not compared.
    //
    // WHAT IS NOT CLAIMED: equality to machine precision. The two documents are
    // meshed independently on differently-oriented geometry, so they are not
    // the same mesh and the discretisation differs. The band is wide and the
    // claim is still strong: a convention error moves these by a factor.
    constexpr double kPressure = 20.0e6; // Pa, inward on the far face

    struct Outcome {
        double peakVonMises = 0.0;
        double peakDisplacement = 0.0;
        /// NOT INITIALISED TO ZERO. Under a purely inward pressure every
        /// `sigma1` is negative, so a `std::max` against 0.0 would report 0.0
        /// -- the initialiser rather than a measurement -- and an invariance
        /// comparison of 0 against 0 holds whatever the code does. Seeded from
        /// the first element instead.
        double peakPrincipal = 0.0;
        double leastPrincipal = 0.0;
        bool seeded = false;
        std::size_t elements = 0;
        /// A corner of the mesh, so the caller can prove the two documents
        /// really are differently placed.
        Point3D firstNode{};
    };

    const auto run = [](bool placed) {
        auto built = placed ? reference::buildMeshTransformedPlacedReferenceModel()
                            : reference::buildMeshTransformedBaseReferenceModel();
        REQUIRE(built.has_value());
        const FaceName datum = built->datumFace();
        const FaceName opposite = built->oppositeFace();
        MeshedReference reference(std::move(built->document));
        assignSteel(reference.document());
        reference.require();

        const std::vector<StructuralLoad> loads{
            StructuralLoad{LoadId::fromValue(1),
                           structural::PressureLoad{.face = opposite,
                                                    .magnitude = Pressure::fromSi(kPressure)}}};
        const GlobalStructuralSystem system = assemble(reference, loads);
        const SolvedSystem solution = solveOf(system, fixedSupportOn(reference, datum));
        const RecoveredFields fields = recoverOf(reference, system, solution);

        Outcome out;
        out.elements = fields.elements().size();
        out.peakVonMises = fields.largestVonMises().si();
        out.peakDisplacement = fields.largestDisplacementMagnitude().si();
        out.firstNode = modelOf(reference).mesh().mesh().nodes().front().position;
        for (const ElementFields& e : fields.elements()) {
            if (!out.seeded) {
                out.peakPrincipal = e.principalStress.sigma1.si();
                out.leastPrincipal = e.principalStress.sigma3.si();
                out.seeded = true;
            } else {
                out.peakPrincipal = std::max(out.peakPrincipal, e.principalStress.sigma1.si());
                out.leastPrincipal = std::min(out.leastPrincipal, e.principalStress.sigma3.si());
            }
            REQUIRE(structural::isFinite(e.stress));
            REQUIRE(isFinite(e.vonMises));
        }
        REQUIRE(out.seeded);
        return out;
    };

    const Outcome base = run(false);
    const Outcome turned = run(true);

    INFO("base: vm " << base.peakVonMises << ", |u| " << base.peakDisplacement << ", s1 "
                     << base.peakPrincipal << ", s3 " << base.leastPrincipal << ", "
                     << base.elements << " elements");
    INFO("turned: vm " << turned.peakVonMises << ", |u| " << turned.peakDisplacement << ", s1 "
                       << turned.peakPrincipal << ", s3 " << turned.leastPrincipal << ", "
                       << turned.elements << " elements");

    WARN("RM-MESH-06 rotated | base vm " << base.peakVonMises << " Pa, |u| "
                                         << base.peakDisplacement << " m, s1 "
                                         << base.peakPrincipal << ", s3 " << base.leastPrincipal
                                         << ", " << base.elements << " elements | turned vm "
                                         << turned.peakVonMises << " Pa, |u| "
                                         << turned.peakDisplacement << " m, s1 "
                                         << turned.peakPrincipal << ", s3 "
                                         << turned.leastPrincipal << ", " << turned.elements
                                         << " elements | vm ratio "
                                         << turned.peakVonMises / base.peakVonMises);

    SECTION("the two documents really are differently placed") {
        // THE VACUOUS-INSTRUMENT GUARD, AND IT IS NOT OPTIONAL HERE. The two
        // models return BIT-IDENTICAL invariants -- which is the ideal outcome,
        // because a box meshed at one sizing on a rigidly rotated frame gives
        // the rigidly rotated mesh -- but it is also exactly what two copies of
        // the SAME document would give. Without this assertion the invariance
        // claim below would hold even if `placed` did nothing.
        const reference::RigidPlacement placement = reference::meshTransformedPlacement();
        INFO("placement origin " << placement.origin[0] << ", " << placement.origin[1] << ", "
                                 << placement.origin[2] << " mm");
        const double moved = std::hypot(turned.firstNode.x.si() - base.firstNode.x.si(),
                                        turned.firstNode.y.si() - base.firstNode.y.si(),
                                        turned.firstNode.z.si() - base.firstNode.z.si());
        INFO("first node moved by " << moved << " m");
        // The declared translation is tens of millimetres, so the meshes sit in
        // genuinely different places.
        REQUIRE(moved > 1.0e-3);
    }

    SECTION("both solve and recover, and neither is trivial") {
        REQUIRE(base.elements > 0);
        REQUIRE(turned.elements > 0);
        REQUIRE(base.peakVonMises > 0.0);
        REQUIRE(turned.peakVonMises > 0.0);
        REQUIRE(base.peakDisplacement > 0.0);
        REQUIRE(turned.peakDisplacement > 0.0);
        // AND THE PRINCIPAL STRESSES ARE A MEASUREMENT, not an initialiser:
        // a pure inward pressure puts the body in compression, so the largest
        // sigma1 is itself negative.
        REQUIRE(base.leastPrincipal < 0.0);
        REQUIRE(base.peakPrincipal < 0.0);
        REQUIRE(turned.peakPrincipal < 0.0);
    }

    SECTION("the scalar invariants agree within the discretisation difference") {
        // 25% is the band, set by the two meshes being different meshes rather
        // than by the physics. A missing shear term or a halved engineering
        // shear would move these by sqrt(3) or by 2, far outside it.
        REQUIRE_THAT(turned.peakVonMises, WithinRel(base.peakVonMises, 0.25));
        REQUIRE_THAT(turned.peakDisplacement, WithinRel(base.peakDisplacement, 0.25));
        REQUIRE_THAT(turned.peakPrincipal, WithinRel(base.peakPrincipal, 0.25));
        REQUIRE_THAT(turned.leastPrincipal, WithinRel(base.leastPrincipal, 0.25));
    }
}

TEST_CASE("StructuralPost_IsDeterministicOnAReferenceModel",
          "[structural][post][reference]") {
    // A FINGERPRINT OVER EVERY ORDERED CHANNEL, repeated five times. It is a
    // test-side encoding and is never persisted as engineering authority.
    auto built = reference::buildMeshBlockReferenceModel();
    REQUIRE(built.has_value());
    const FaceName bottom = built->bottom();
    const FaceName top = built->top();
    MeshedReference reference(std::move(built->document));
    assignSteel(reference.document());
    reference.require();

    const GlobalStructuralSystem system =
        assemble(reference, spreadLoad(reference, top, 2, 5000.0));
    const SolvedSystem solution = solveOf(system, fixedSupportOn(reference, bottom));

    // THE CANONICAL ORDER: NodeId then four displacement values, ElementId then
    // six strains, six stresses, three principal stresses, von Mises,
    // hydrostatic stress and three principal strains. Bit-exact, because it
    // hashes each double's own representation rather than a rounded form.
    const auto fingerprint = [](const RecoveredFields& fields) {
        std::uint64_t hash = 1469598103934665603ULL;
        const auto mix = [&hash](double value) {
            std::uint64_t bits = 0;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(bits));
            hash ^= bits;
            hash *= 1099511628211ULL;
        };
        for (const NodalDisplacement& d : fields.displacements()) {
            mix(static_cast<double>(d.node.value()));
            mix(d.displacement.x.si());
            mix(d.displacement.y.si());
            mix(d.displacement.z.si());
            mix(d.magnitude.si());
        }
        for (const ElementFields& e : fields.elements()) {
            mix(static_cast<double>(e.element.value()));
            for (const structural::TensorComponent which : structural::kTensorComponents) {
                mix(e.strain.component(which));
                mix(e.stress.component(which).si());
            }
            for (std::size_t i = 0; i < 3; ++i) {
                mix(e.principalStress.at(i).si());
                mix(e.principalStrain.at(i));
            }
            mix(e.vonMises.si());
            mix(e.hydrostatic.si());
        }
        return hash;
    };

    const RecoveredFields first = recoverOf(reference, system, solution);
    const std::uint64_t expected = fingerprint(first);
    for (int run = 0; run < 5; ++run) {
        INFO("run " << run);
        const RecoveredFields again = recoverOf(reference, system, solution);
        REQUIRE(fingerprint(again) == expected);
    }
    WARN("RM-MESH-01 recovery fingerprint 0x" << std::hex << expected << std::dec << " over "
                                              << first.displacements().size() << " nodes and "
                                              << first.elements().size() << " elements");

    SECTION("and the fingerprint is not a constant") {
        // A VACUOUS-INSTRUMENT GUARD: a different load must give a different
        // fingerprint, or the equality above would hold for any encoding.
        const GlobalStructuralSystem other =
            assemble(reference, spreadLoad(reference, top, 2, 9000.0));
        const RecoveredFields moved =
            recoverOf(reference, other, solveOf(other, fixedSupportOn(reference, bottom)));
        REQUIRE(fingerprint(moved) != expected);
    }
}
