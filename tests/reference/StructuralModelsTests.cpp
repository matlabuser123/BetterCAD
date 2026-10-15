// The structural reference models (P17-REFMOD-001): ten model IDs in twelve
// committed documents, each exercising the whole P17 stack from a CAD body
// with a material, a meshing control and a structural analysis to a
// validated, solved, recovered and balanced result -- or to an explicit
// refusal with nothing published.
//
// EVERY EXPECTED VALUE COMES FROM A CLOSED FORM THAT CANNOT CALL BETTERCAD.
// `tests/reference/Analytic.hpp` has no BetterCAD include, so the strain of an
// affine field, Hooke's law, von Mises, the axial bar, the cantilever, the
// pressure resultant and the self-weight are all computed from the models'
// own dimensions and the document's own material by arithmetic the production
// code does not share. Production's B, D, von Mises, facet integration and
// equilibrium check are the things under test, and nothing here asks them
// what the answer should be.
//
// Two conventions from the predecessor suites are kept because they were
// earned there:
//
//   ASSERT THE LEVELS DIFFER BEFORE COMPARING ERRORS. P16-REFMOD-001's
//     convergence table once passed on one unit in the last place because two
//     deflections produced the same boundary. RM-FEA-03 requires strictly
//     increasing node and element counts before it looks at a deflection.
//   JUDGE THE TWO NODE POPULATIONS SEPARATELY. A boundary node is where the
//     kernel put it and follows R x + t to rounding; an interior node is the
//     mesher's own choice and is not equivariant under rotation. RM-FEA-06
//     measures each with its own tolerance rather than one over both.

#include "reference/StructuralTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <StructuralReferenceModels.hpp>

#include <bettercad/core/Units.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/parameters/Parameter.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/MeshSizing.hpp>
#include <bettercad/structural/StructuralLoad.hpp>
#include <bettercad/structural/StructuralRestraint.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::reference;
using namespace bettercad::test;
using namespace bettercad::test::structref;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using structural::DofComponent;
using structural::ElementFields;
using structural::NodalDisplacement;
using structural::RestraintComponents;
using structural::StructuralAnalysisDefinition;
using structural::StructuralAnalysisMode;
using structural::StructuralLoad;
using structural::StructuralRestraint;
using structural::StructuralSolveOutcome;
using structural::StructuralValidationReport;
using structural::ValidationCode;
using structural::ValidationStatus;

/// The band for a quantity the discrete solution reproduces EXACTLY in exact
/// arithmetic: an affine field on a linear element, a resultant through a
/// consistent nodal integration, a mean stress that is a statement of
/// discrete equilibrium. Only floating-point accumulation separates the
/// solver's number from the closed form, and the repository's figure for
/// that is 1e-9. Measured on these models the worst is recorded beside each
/// gate in ANALYTICAL_VALIDATION.md, orders inside this.
constexpr double kExact = 1e-9;

/// The band for a quantity that is exact in exact arithmetic and involves no
/// factorisation: a nodal load sum, a facet area, a cross product.
constexpr double kAlgebraic = 1e-12;

/// A length parameter's value in mm, read from the document. The suite's
/// oracle reads DIMENSIONS and multiplies them; it never reads a volume, an
/// area or a result.
[[nodiscard]] double mm(const Document& document, std::string_view name) {
    const std::optional<ObjectId> object = document.findByName(name);
    REQUIRE(object.has_value());
    const std::optional<ParameterId> id = document.asParameter(*object);
    REQUIRE(id.has_value());
    const Parameter* parameter = document.parameters().find(*id);
    REQUIRE(parameter != nullptr);
    return Length::fromSi(parameter->siValue()).in(units::mm);
}

/// The same, in metres, for the SI closed forms.
[[nodiscard]] double metres(const Document& document, std::string_view name) {
    return mm(document, name) / kMmPerM;
}

void setMm(Document& document, std::string_view name, double value) {
    const std::optional<ObjectId> object = document.findByName(name);
    REQUIRE(object.has_value());
    const std::optional<ParameterId> id = document.asParameter(*object);
    REQUIRE(id.has_value());
    REQUIRE(document.setParameterValue(*id, value * units::mm).has_value());
}

[[nodiscard]] bool carries(const StructuralValidationReport& report, ValidationCode code) {
    return std::ranges::any_of(report.findings,
                               [code](const structural::ValidationFinding& f) { return f.code == code; });
}

[[nodiscard]] const StructuralReferenceModelInfo& infoOf(StructuralReferenceModelKind kind) {
    for (const StructuralReferenceModelInfo& info : kStructuralReferenceModels) {
        if (info.kind == kind) {
            return info;
        }
    }
    FAIL("no catalog entry for the kind");
    return kStructuralReferenceModels.front();
}

/// The affine field the patch tests impose: nine DISTINCT gradient entries
/// and three distinct offsets, so a transposed B, a dropped shear or a
/// swapped axis cannot pass. The strains are of the order 1e-4, the size a
/// steel part really sees.
constexpr analytic::Gradient3 kGradient{{{3.1e-4, -1.2e-4, 2.4e-4},
                                         {0.7e-4, -2.6e-4, 1.9e-4},
                                         {-1.5e-4, 0.8e-4, 4.3e-4}}};
constexpr std::array<double, 3> kOffset{1.1e-5, -2.2e-5, 3.3e-5};

[[nodiscard]] Translation3D displacementAt(const Point3D& p, const analytic::Gradient3& a,
                                           const std::array<double, 3>& b) {
    const std::array<double, 3> u = analytic::affineDisplacement(a, b, positionSi(p));
    return Translation3D{Length::fromSi(u[0]), Length::fromSi(u[1]), Length::fromSi(u[2])};
}

[[nodiscard]] materials::LinearElasticConstants constantsOf(double youngs, double poisson) {
    materials::LinearElasticConstants c;
    c.youngsModulus = ElasticModulus::fromSi(youngs);
    c.poissonRatio = PoissonRatio::of(poisson);
    c.shearModulus = ElasticModulus::fromSi(youngs / (2.0 * (1.0 + poisson)));
    c.bulkModulus = ElasticModulus::fromSi(youngs / (3.0 * (1.0 - 2.0 * poisson)));
    return c;
}

/// Clapeyron's theorem, computed on the test side: with zero prescribed
/// displacement the strain energy is half the work of the applied loads,
/// `U = (1/2) sum F_i . u_i` over the LOADED nodes. The reactions do no work.
[[nodiscard]] double halfExternalWork(const meshing::Mesh& mesh, const structural::PreparedLoads& loads,
                                      const structural::RecoveredFields& fields) {
    double work = 0.0;
    for (const structural::NodalLoad& load : loads.nodal()) {
        Result<NodalDisplacement> u = fields.displacementOf(mesh, load.node);
        REQUIRE(u.has_value());
        work += load.force.x.si() * u->displacement.x.si() + load.force.y.si() * u->displacement.y.si() +
                load.force.z.si() * u->displacement.z.si();
    }
    return 0.5 * work;
}

/// The applied resultant, summed on the test side over the prepared nodal
/// forces in handle order.
[[nodiscard]] Si3 appliedResultant(const structural::PreparedLoads& loads) {
    Si3 sum{};
    for (const structural::NodalLoad& load : loads.nodal()) {
        sum = plus(sum, Si3{load.force.x.si(), load.force.y.si(), load.force.z.si()});
    }
    return sum;
}

/// The moment of the prepared nodal forces about @p origin, on the test side.
[[nodiscard]] Si3 appliedMoment(const meshing::Mesh& mesh, const structural::PreparedLoads& loads,
                                const Si3& origin) {
    Si3 sum{};
    for (const structural::NodalLoad& load : loads.nodal()) {
        const meshing::Node* node = mesh.findNode(load.node);
        REQUIRE(node != nullptr);
        const Si3 lever = minus(positionSi(node->position), origin);
        sum = plus(sum, cross(lever, Si3{load.force.x.si(), load.force.y.si(), load.force.z.si()}));
    }
    return sum;
}

[[nodiscard]] Si3 si3(const Force3D& f) {
    return {f.x.si(), f.y.si(), f.z.si()};
}
[[nodiscard]] Si3 si3(const Translation3D& t) {
    return {t.x.si(), t.y.si(), t.z.si()};
}
[[nodiscard]] Si3 si3(const Moment3D& m) {
    return {m.x.si(), m.y.si(), m.z.si()};
}

} // namespace

// ---------------------------------------------------------------------------
// The suite as a whole
// ---------------------------------------------------------------------------

TEST_CASE("StructuralReferenceSuite_DeclaresTenModelsInTwelveDocuments",
          "[refmod][structural][suite]") {
    // TWELVE ENTRIES FOR TEN IDS: RM-FEA-06 and RM-FEA-07 are pairs, and
    // comparing the halves is the case in both.
    REQUIRE(kStructuralReferenceModels.size() == 12);
    std::set<std::string_view> ids;
    std::set<std::string_view> names;
    std::set<std::string_view> stems;
    std::set<StructuralReferenceModelKind> kinds;
    std::size_t refusals = 0;
    for (const StructuralReferenceModelInfo& info : kStructuralReferenceModels) {
        ids.insert(info.id);
        REQUIRE(names.insert(info.name).second);
        REQUIRE(stems.insert(info.fileStem).second);
        REQUIRE(kinds.insert(info.kind).second);
        REQUIRE_FALSE(info.purpose.empty());
        REQUIRE_FALSE(info.mainParameter.empty());
        REQUIRE(info.mainParameterMm > 0.0);
        if (!info.expectSolve) {
            ++refusals;
        }
    }
    REQUIRE(ids.size() == kStructuralReferenceModelIdCount);
    REQUIRE(ids.size() == 10);
    for (int n = 1; n <= 10; ++n) {
        REQUIRE(ids.contains(std::format("RM-FEA-{:02}", n)));
    }
    // Exactly two committed refusals: the under-constrained model and the
    // unresolved reference. The stale-mesh model's COMMITTED state is valid;
    // its refusal is what the suite makes happen afterwards.
    REQUIRE(refusals == 2);
    REQUIRE_FALSE(infoOf(StructuralReferenceModelKind::UnderConstrained).expectSolve);
    REQUIRE_FALSE(infoOf(StructuralReferenceModelKind::Unresolved).expectSolve);
    REQUIRE(infoOf(StructuralReferenceModelKind::StaleMesh).expectSolve);
}

TEST_CASE("StructuralReferenceSuite_EveryModelCarriesItsMaterialMeshAndAnalysis",
          "[refmod][structural][suite]") {
    for (const StructuralReferenceModelInfo& info : kStructuralReferenceModels) {
        INFO(info.id << " " << info.name);
        Result<Document> document = buildStructuralReferenceModel(info.kind);
        REQUIRE(document.has_value());
        REQUIRE(document->name() == info.name);
        REQUIRE(document->findByName("Mesh").has_value());
        REQUIRE(document->findByName(std::string{kStructuralStudyName}).has_value());
        REQUIRE(document->findByName(info.mainParameter).has_value());

        // ONE analysis, naming the one control, with at least one load and
        // one restraint -- a model with none would be refused for the wrong
        // reason.
        const std::vector<AnalysisId> analyses = structural::structuralAnalysisIds(*document);
        REQUIRE(analyses.size() == 1);
        const structural::StructuralAnalysis* study =
            structural::findStructuralAnalysis(*document, analyses.front());
        REQUIRE(study != nullptr);
        REQUIRE(study->definition().mesh.isValid());
        REQUIRE_FALSE(study->definition().loads.empty());
        REQUIRE_FALSE(study->definition().restraints.empty());
        const auto* control = document->findObjectAs<meshing::MeshControl>(
            ObjectId::fromValue(study->definition().mesh.value()));
        REQUIRE(control != nullptr);
        REQUIRE(control->name() == "Mesh");

        // THE MATERIAL THE DOCUMENT RESOLVES IS THE ONE THE HEADER DECLARES,
        // exactly: a builder that quietly used another steel would make every
        // closed form below disagree with the model it describes.
        REQUIRE(features::materialAssignment(*document).state ==
                features::MaterialAssignmentState::Resolved);
        const ResolvedSteel steel = steelOf(*document, control->definition().body, study->definition().mode);
        REQUIRE(steel.youngs == kStructuralYoungsModulusPa);
        REQUIRE(steel.poisson == kStructuralPoissonRatio);
        REQUIRE_THAT(steel.shear,
                     WithinRel(analytic::lame(kStructuralYoungsModulusPa, kStructuralPoissonRatio).mu,
                               kAlgebraic));
        if (study->definition().mode == StructuralAnalysisMode::LinearStaticWithGravity) {
            REQUIRE(steel.density.has_value());
            REQUIRE(*steel.density == kStructuralDensityKgM3);
        } else {
            REQUIRE_FALSE(steel.density.has_value());
        }
    }
}

TEST_CASE("StructuralReferenceSuite_EveryModelSolvesOrIsRefusedAsDeclared",
          "[refmod][structural][suite]") {
    // The declared outcome of every document, through the production entry
    // and nothing else -- plus, on every solve, the gates a reader of the
    // evidence tables expects to have been checked: the residual, the pivot
    // ratio, both equilibrium balances, a published result that describes
    // the mesh it was solved on, and Clapeyron's theorem computed on the test
    // side.
    for (const StructuralReferenceModelInfo& info : kStructuralReferenceModels) {
        INFO(info.id << " " << info.name);
        Result<Document> document = buildStructuralReferenceModel(info.kind);
        REQUIRE(document.has_value());
        StructuralReference reference(std::move(*document));
        const meshing::VolumeMesh& volume = reference.require();
        const StructuralValidationReport validation = reference.validate();
        Result<StructuralSolveOutcome> outcome = reference.solve();

        REQUIRE(outcome.has_value() == info.expectSolve);
        if (!info.expectSolve) {
            WARN(info.id << " " << info.name << " | " << volume.nodeCount() << " nodes | "
                         << volume.tetrahedronCount() << " tets | validation "
                         << structural::toString(validation.status()) << " at "
                         << structural::toString(validation.reachedStage) << " | refused: "
                         << outcome.error().message);
            REQUIRE_FALSE(outcome.error().message.empty());
            continue;
        }

        const StructuralSolveOutcome& solved = *outcome;
        REQUIRE(validation.acceptable());
        REQUIRE(solved.validation.acceptable());
        REQUIRE(solved.residual.normalized <= 1e-9);
        REQUIRE(solved.pivotRatio >= 1e-12);
        REQUIRE(solved.forceBalance.normalized <= 1e-12);
        REQUIRE(solved.momentBalance.normalized <= 1e-12);
        REQUIRE(solved.result.describes(volume));
        REQUIRE(solved.fields.describes(volume.mesh()));
        REQUIRE(solved.reactions.describes(volume.mesh()));
        REQUIRE(solved.strainEnergy.si() > 0.0);
        REQUIRE(solved.largestDisplacement.si() > 0.0);
        REQUIRE(solved.largestVonMises.si() > 0.0);
        REQUIRE(std::isfinite(solved.strainEnergy.si()));
        REQUIRE(solved.freeEquations > 0);
        REQUIRE(solved.freeEquations < 3 * volume.nodeCount());

        // CLAPEYRON, on the test side: U = (1/2) sum F . u over the loaded
        // nodes, with the loads re-prepared here and the displacements read
        // through the per-node channel.
        const structural::PreparedLoads loads = reference.prepareLoads();
        const double half = halfExternalWork(volume.mesh(), loads, solved.fields);
        INFO("strain energy " << solved.strainEnergy.si() << " J, half the external work " << half);
        REQUIRE_THAT(solved.strainEnergy.si(), WithinRel(half, kExact));

        WARN(info.id << " " << info.name << " | " << volume.nodeCount() << " nodes | "
                     << volume.tetrahedronCount() << " tets | " << solved.freeEquations
                     << " free equations | residual " << solved.residual.normalized << " | pivot "
                     << solved.pivotRatio << " | U " << solved.strainEnergy.si() << " J | max |u| "
                     << solved.largestDisplacement.si() << " m | max vm " << solved.largestVonMises.si()
                     << " Pa | eta_F " << solved.forceBalance.normalized << " | eta_M "
                     << solved.momentBalance.normalized);
    }
}

// ---------------------------------------------------------------------------
// RM-FEA-01: the patch test
// ---------------------------------------------------------------------------

TEST_CASE("StructuralPatch_SingleTetReproducesAnAffineFieldToRoundoff",
          "[refmod][structural][rm01][analytic]") {
    // ONE TETRAHEDRON, handed its corner displacements from an affine field
    // and asked for everything the element kernel and the recovery produce.
    // The oracle is the field's own gradient: the strain of u = A x + b is
    // sym(A), and a linear element must reproduce it to floating point.
    //
    // The tetrahedron is deliberately skew, with no edge along an axis and no
    // two coordinates equal, so an axis swap in B cannot survive.
    const std::array<Point3D, 4> corners{
        Point3D{Length::fromSi(0.000), Length::fromSi(0.000), Length::fromSi(0.000)},
        Point3D{Length::fromSi(0.021), Length::fromSi(0.004), Length::fromSi(-0.003)},
        Point3D{Length::fromSi(0.005), Length::fromSi(0.017), Length::fromSi(0.002)},
        Point3D{Length::fromSi(0.004), Length::fromSi(0.006), Length::fromSi(0.019)}};
    const std::array<Si3, 4> p{positionSi(corners[0]), positionSi(corners[1]), positionSi(corners[2]),
                               positionSi(corners[3])};
    const double volume = signedVolumeSi(p[0], p[1], p[2], p[3]);
    INFO("signed volume " << volume << " m^3");
    REQUIRE(volume > 0.0);

    const materials::LinearElasticConstants constants =
        constantsOf(kStructuralYoungsModulusPa, kStructuralPoissonRatio);
    const analytic::Lame lame = analytic::lame(kStructuralYoungsModulusPa, kStructuralPoissonRatio);
    Result<structural::ElasticityMatrix> elasticity = structural::isotropicElasticity(constants);
    REQUIRE(elasticity.has_value());

    std::array<Translation3D, 4> u{};
    for (std::size_t i = 0; i < 4; ++i) {
        u[i] = displacementAt(corners[i], kGradient, kOffset);
    }
    const analytic::Voigt6 strain = analytic::affineStrain(kGradient);
    const analytic::Voigt6 stress = analytic::isotropicStress(strain, lame);

    SECTION("the recovered strain and stress are the field's own, component for component") {
        Result<ElementFields> fields = structural::recoverElementFields(
            meshing::ElementId::fromValue(1), corners, u, *elasticity);
        REQUIRE(fields.has_value());
        const double strainError = worstRelative(voigtOf(fields->strain), strain, 1e-30);
        const double stressError = worstRelative(voigtOf(fields->stress), stress, 1e-30);
        INFO("worst relative strain error " << strainError << ", stress " << stressError);
        WARN("RM-FEA-01 single tet | worst relative error: strain " << strainError << " | stress "
                                                                    << stressError);
        REQUIRE(strainError < 1e-12);
        REQUIRE(stressError < 1e-12);

        // The derived invariants, each against its own closed form.
        REQUIRE_THAT(fields->vonMises.si(), WithinRel(analytic::vonMises(stress), 1e-12));
        REQUIRE_THAT(fields->hydrostatic.si(), WithinRel((stress[0] + stress[1] + stress[2]) / 3.0, 1e-12));
        REQUIRE_THAT(fields->principalStress.sum().si(), WithinRel(stress[0] + stress[1] + stress[2], 1e-12));
        REQUIRE_THAT(fields->principalStrain.sum(), WithinRel(strain[0] + strain[1] + strain[2], 1e-12));
        REQUIRE(fields->principalStress.sigma1.si() >= fields->principalStress.sigma2.si());
        REQUIRE(fields->principalStress.sigma2.si() >= fields->principalStress.sigma3.si());
        // And the state is a real three-dimensional one, so every term of the
        // von Mises formula was exercised.
        for (const double component : stress) {
            REQUIRE(std::abs(component) > 1.0e6);
        }
    }

    SECTION("the element's internal forces are the tractions of the constant stress on its faces") {
        // f = Ke u must equal the consistent nodal forces of sigma on the
        // four faces: each face transmits sigma S_f, and a constant traction
        // on a linear triangle splits equally between its three corners. The
        // face area vectors are the suite's own cross products, oriented
        // outward by the face centroid, and sigma is the closed form's.
        Result<structural::Tet4Stiffness> stiffness = structural::computeTet4Stiffness(corners, constants);
        REQUIRE(stiffness.has_value());
        const std::array<Force3D, 4> internal = stiffness->forceFrom(u);

        const Si3 centroid = scaled(plus(plus(p[0], p[1]), plus(p[2], p[3])), 0.25);
        std::array<Si3, 4> expected{};
        constexpr std::array<std::array<std::size_t, 3>, 4> kFaces{{{0, 1, 2}, {0, 1, 3}, {0, 2, 3}, {1, 2, 3}}};
        for (const std::array<std::size_t, 3>& face : kFaces) {
            Si3 area = areaVector(p[face[0]], p[face[1]], p[face[2]]);
            const Si3 faceCentroid = scaled(plus(plus(p[face[0]], p[face[1]]), p[face[2]]), 1.0 / 3.0);
            if (dot(area, minus(faceCentroid, centroid)) < 0.0) {
                area = scaled(area, -1.0);
            }
            const std::array<double, 3> traction = analytic::tractionForce(stress, area);
            for (const std::size_t corner : face) {
                expected[corner] = plus(expected[corner], scaled(Si3{traction[0], traction[1], traction[2]}, 1.0 / 3.0));
            }
        }
        double scale = 0.0;
        for (const Si3& f : expected) {
            scale = std::max(scale, norm(f));
        }
        REQUIRE(scale > 0.0);
        double worst = 0.0;
        Si3 sum{};
        for (std::size_t i = 0; i < 4; ++i) {
            worst = std::max(worst, norm(minus(si3(internal[i]), expected[i])) / scale);
            sum = plus(sum, si3(internal[i]));
        }
        INFO("worst relative nodal force error " << worst);
        WARN("RM-FEA-01 single tet | worst relative internal force error " << worst
                                                                           << " | force scale " << scale << " N");
        REQUIRE(worst < 1e-10);
        // Self-equilibrated: a constant stress state has no net force.
        REQUIRE(norm(sum) < 1e-10 * scale);

        // And the energy is the energy density times the volume.
        const double density = analytic::strainEnergyDensity(stress, strain);
        REQUIRE_THAT(stiffness->energyFrom(u).si(), WithinRel(density * volume, 1e-12));
    }

    SECTION("a rigid-body motion gives no strain, no stress and no energy") {
        // The complementary half of the patch test: an ANTISYMMETRIC gradient
        // plus a translation is an infinitesimal rotation, and a linear
        // element must see nothing in it.
        constexpr analytic::Gradient3 kSpin{{{0.0, -2.0e-4, 1.5e-4}, {2.0e-4, 0.0, -0.9e-4}, {-1.5e-4, 0.9e-4, 0.0}}};
        std::array<Translation3D, 4> rigid{};
        for (std::size_t i = 0; i < 4; ++i) {
            rigid[i] = displacementAt(corners[i], kSpin, kOffset);
        }
        Result<ElementFields> fields = structural::recoverElementFields(
            meshing::ElementId::fromValue(1), corners, rigid, *elasticity);
        REQUIRE(fields.has_value());
        const double strainScale = 2.0e-4;
        for (const double component : voigtOf(fields->strain)) {
            REQUIRE(std::abs(component) < 1e-12 * strainScale);
        }
        REQUIRE(fields->vonMises.si() < 1e-12 * kStructuralYoungsModulusPa * strainScale);
        Result<structural::Tet4Stiffness> stiffness = structural::computeTet4Stiffness(corners, constants);
        REQUIRE(stiffness.has_value());
        REQUIRE(stiffness->energyFrom(rigid).si() <
                1e-20 * kStructuralYoungsModulusPa * strainScale * strainScale * volume);
    }
}

TEST_CASE("StructuralPatch_ReproducesConstantStrainOverEveryElementOfTheMeshedBlock",
          "[refmod][structural][rm01][analytic]") {
    // THE PATCH TEST PROPER, on the mesh the production chain built from
    // RM-FEA-01's CAD body. An affine field is imposed on EVERY node -- the
    // interior one included -- and three things must follow exactly:
    //
    //   every element recovers the same constant strain, sym(A)
    //   every element recovers the same constant stress, D sym(A)
    //   the assembled K times that field equals the consistent nodal forces
    //     of the constant stress on the BOUNDARY, and is ZERO at the interior
    //     node, because a constant stress state has no body force
    //
    // The third is the internal-force consistency the brief names, and it is
    // only a test because the mesh has an interior node. That is asserted
    // first.
    auto built = buildStructuralPatchReferenceModel();
    REQUIRE(built.has_value());
    StructuralReference reference(std::move(built->document));
    const meshing::VolumeMesh& volume = reference.require();
    const meshing::Mesh& mesh = volume.mesh();

    const std::vector<meshing::NodeId> interior = interiorNodes(mesh);
    INFO(mesh.nodeCount() << " nodes, " << mesh.tetrahedra().size() << " tets, " << interior.size()
                          << " interior");
    REQUIRE_FALSE(interior.empty());
    // And the boundary triangles face OUT of the material, which the force
    // check below depends on: the suite's own divergence sum is positive.
    REQUIRE(meshref::enclosedVolumeMm3(mesh) > 0.0);

    const ResolvedSteel steel = reference.steel();
    const analytic::Lame lame = analytic::lame(steel.youngs, steel.poisson);
    const analytic::Voigt6 strain = analytic::affineStrain(kGradient);
    const analytic::Voigt6 stress = analytic::isotropicStress(strain, lame);
    Result<structural::ElasticityMatrix> elasticity = structural::isotropicElasticity(reference.material().elastic());
    REQUIRE(elasticity.has_value());

    // The field, both per node and as a flat vector in the frozen DofIndex
    // order: node ordinal k, component c -> entry 3 k + c.
    Result<structural::MeshDofMap> numbering = structural::buildMeshDofMap(mesh);
    REQUIRE(numbering.has_value());
    std::map<meshing::NodeId::ValueType, Translation3D> field;
    std::vector<double> flat(numbering->dofCount(), 0.0);
    for (std::size_t k = 0; k < mesh.nodes().size(); ++k) {
        const meshing::Node& node = mesh.nodes()[k];
        const Translation3D u = displacementAt(node.position, kGradient, kOffset);
        field.emplace(node.id.value(), u);
        REQUIRE(numbering->nodeAt(k) == node.id);
        flat[3 * k + 0] = u.x.si();
        flat[3 * k + 1] = u.y.si();
        flat[3 * k + 2] = u.z.si();
    }

    SECTION("every element recovers the same constant strain and stress") {
        double worstStrain = 0.0;
        double worstStress = 0.0;
        double worstVonMises = 0.0;
        for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
            const std::array<Point3D, 4> corners = cornersOf(mesh, tet);
            std::array<Translation3D, 4> u{};
            for (std::size_t i = 0; i < 4; ++i) {
                u[i] = field.at(tet.nodes[i].value());
            }
            Result<ElementFields> fields = structural::recoverElementFields(tet.id, corners, u, *elasticity);
            REQUIRE(fields.has_value());
            worstStrain = std::max(worstStrain, worstRelative(voigtOf(fields->strain), strain, 1e-30));
            worstStress = std::max(worstStress, worstRelative(voigtOf(fields->stress), stress, 1e-30));
            worstVonMises = std::max(worstVonMises, std::abs(fields->vonMises.si() - analytic::vonMises(stress)) /
                                                        analytic::vonMises(stress));
        }
        INFO("worst relative error: strain " << worstStrain << ", stress " << worstStress << ", von Mises "
                                             << worstVonMises);
        WARN("RM-FEA-01 patch | " << mesh.tetrahedra().size() << " elements | worst relative error: strain "
                                  << worstStrain << " | stress " << worstStress << " | von Mises "
                                  << worstVonMises);
        REQUIRE(worstStrain < 1e-11);
        REQUIRE(worstStress < 1e-11);
        REQUIRE(worstVonMises < 1e-11);
    }

    SECTION("K u is the boundary traction of the constant stress, and zero at the interior node") {
        const structural::GlobalStructuralSystem system = reference.assemble();
        REQUIRE(system.numbering() == *numbering);
        const std::vector<double> internal = multiply(system.stiffness(), flat);

        // The expected nodal forces: sigma S over every boundary triangle, in
        // the mesh's own outward winding, a third to each corner.
        const std::map<meshing::NodeId::ValueType, Si3> positions = nodePositionsSi(mesh);
        std::map<meshing::NodeId::ValueType, Si3> expected;
        for (const meshing::Triangle& triangle : mesh.triangles()) {
            const Si3 area = areaVector(positions.at(triangle.nodes[0].value()),
                                        positions.at(triangle.nodes[1].value()),
                                        positions.at(triangle.nodes[2].value()));
            const std::array<double, 3> traction = analytic::tractionForce(stress, area);
            for (const meshing::NodeId corner : triangle.nodes) {
                expected[corner.value()] =
                    plus(expected[corner.value()], scaled(Si3{traction[0], traction[1], traction[2]}, 1.0 / 3.0));
            }
        }
        double scale = 0.0;
        for (const auto& [handle, force] : expected) {
            scale = std::max(scale, norm(force));
        }
        REQUIRE(scale > 0.0);

        double worstBoundary = 0.0;
        double worstInterior = 0.0;
        Si3 total{};
        for (std::size_t k = 0; k < mesh.nodes().size(); ++k) {
            const meshing::Node& node = mesh.nodes()[k];
            const Si3 actual{internal[3 * k], internal[3 * k + 1], internal[3 * k + 2]};
            total = plus(total, actual);
            const auto found = expected.find(node.id.value());
            if (found == expected.end()) {
                worstInterior = std::max(worstInterior, norm(actual) / scale);
            } else {
                worstBoundary = std::max(worstBoundary, norm(minus(actual, found->second)) / scale);
            }
        }
        INFO("worst relative force error: boundary " << worstBoundary << ", interior " << worstInterior
                                                     << ", net " << norm(total) / scale);
        WARN("RM-FEA-01 patch | K u against sigma S / 3 | worst relative error: boundary " << worstBoundary
                                                                                           << " | interior "
                                                                                           << worstInterior
                                                                                           << " | net force "
                                                                                           << norm(total) / scale);
        REQUIRE(worstBoundary < 1e-10);
        REQUIRE(worstInterior < 1e-10);
        REQUIRE(norm(total) < 1e-10 * scale);
    }
}

// ---------------------------------------------------------------------------
// RM-FEA-02: the axial bar
// ---------------------------------------------------------------------------

TEST_CASE("StructuralAxialBar_MatchesTheClosedFormToRoundoff", "[refmod][structural][rm02][analytic]") {
    auto built = buildStructuralAxialBarReferenceModel();
    REQUIRE(built.has_value());
    const FaceName tip = built->tip();
    StructuralReference reference(std::move(built->document));

    // THE ORACLE, from the model's own dimensions, the document's own material
    // and the analysis's own traction -- never from a result.
    const double a = metres(reference.document(), "bar_a");
    const double b = metres(reference.document(), "bar_b");
    const double length = metres(reference.document(), "bar_l");
    const ResolvedSteel steel = reference.steel();
    const StructuralAnalysisDefinition definition = reference.definition();
    REQUIRE(definition.loads.size() == 1);
    REQUIRE(definition.loads[0].traction() != nullptr);
    const double traction = definition.loads[0].traction()->traction.z.si();
    REQUIRE(traction == kAxialTractionPa);
    const double area = a * b;
    const double force = traction * area;
    const analytic::AxialBar bar = analytic::axialBar(force, length, area, steel.youngs, steel.poisson);

    const StructuralSolveOutcome solved = reference.requireSolve();
    const meshing::Mesh& mesh = reference.model().mesh().mesh();
    REQUIRE(solved.freeEquations > 0);

    SECTION("the whole displacement field is the affine uniaxial state") {
        // u = (-nu eps x, -nu eps y, eps z) at EVERY node, not only at the
        // tip: the rollers leave the Poisson contraction free, so that is the
        // exact solution and a linear element carries it.
        double worst = 0.0;
        for (const NodalDisplacement& d : solved.fields.displacements()) {
            const meshing::Node* node = mesh.findNode(d.node);
            REQUIRE(node != nullptr);
            const Si3 x = positionSi(node->position);
            const Si3 expected{bar.lateralStrain * x[0], bar.lateralStrain * x[1], bar.strain * x[2]};
            worst = std::max(worst, norm(minus(si3(d.displacement), expected)) / bar.elongation);
        }
        INFO("worst displacement error, relative to delta " << worst);
        WARN("RM-FEA-02 axial bar | delta = FL/AE " << bar.elongation << " m | worst nodal error / delta " << worst);
        REQUIRE(worst < kExact);

        // And every tip node has moved by exactly delta.
        const std::vector<meshing::NodeId> tipNodes = reference.nodesOf(tip);
        for (const meshing::NodeId node : tipNodes) {
            Result<NodalDisplacement> d = solved.fields.displacementOf(mesh, node);
            REQUIRE(d.has_value());
            REQUIRE_THAT(d->displacement.z.si(), WithinRel(bar.elongation, kExact));
        }
        // The largest magnitude is at the tip corner (a, b, L).
        const double corner = std::hypot(bar.lateralStrain * a, bar.lateralStrain * b, bar.elongation);
        REQUIRE_THAT(solved.largestDisplacement.si(), WithinRel(corner, kExact));
    }

    SECTION("every element carries sigma = F / A, eps = sigma / E and nothing else") {
        const analytic::Voigt6 strain{bar.lateralStrain, bar.lateralStrain, bar.strain, 0.0, 0.0, 0.0};
        const analytic::Voigt6 stress{0.0, 0.0, bar.stress, 0.0, 0.0, 0.0};
        double worstStrain = 0.0;
        double worstStress = 0.0;
        double worstVonMises = 0.0;
        for (const ElementFields& e : solved.fields.elements()) {
            worstStrain = std::max(worstStrain, worstRelative(voigtOf(e.strain), strain, 1e-30));
            worstStress = std::max(worstStress, worstRelative(voigtOf(e.stress), stress, 1e-30));
            worstVonMises = std::max(worstVonMises, std::abs(e.vonMises.si() - bar.stress) / bar.stress);
        }
        INFO("worst relative error: strain " << worstStrain << ", stress " << worstStress << ", von Mises "
                                             << worstVonMises);
        WARN("RM-FEA-02 axial bar | sigma = F/A " << bar.stress << " Pa | eps " << bar.strain
                                                  << " | worst relative error: strain " << worstStrain
                                                  << " | stress " << worstStress << " | von Mises "
                                                  << worstVonMises);
        REQUIRE(worstStrain < kExact);
        REQUIRE(worstStress < kExact);
        REQUIRE(worstVonMises < kExact);
        REQUIRE_THAT(solved.largestVonMises.si(), WithinRel(bar.stress, kExact));
        REQUIRE(solved.fields.elements().size() == mesh.tetrahedra().size());
    }

    SECTION("the base reaction is -F, carried by the roller that holds Uz") {
        const Si3 total = si3(solved.reactions.totalForce());
        INFO("total reaction " << total[0] << ", " << total[1] << ", " << total[2] << " N against -F = " << -force);
        WARN("RM-FEA-02 axial bar | F " << force << " N | reaction " << total[2] << " N | relative "
                                        << std::abs(total[2] + force) / force);
        REQUIRE_THAT(total[2], WithinRel(-force, kExact));
        REQUIRE(std::abs(total[0]) < kExact * force);
        REQUIRE(std::abs(total[1]) < kExact * force);
        // Per restraint: the Uz roller carries all of it, and the two
        // in-plane rollers carry nothing in their own direction.
        Result<structural::RestraintReaction> uz = solved.reactions.of(RestraintId::fromValue(1));
        REQUIRE(uz.has_value());
        REQUIRE_THAT(uz->ownedForce.z.si() + uz->sharedForce.z.si(), WithinRel(-force, kExact));
        Result<structural::RestraintReaction> ux = solved.reactions.of(RestraintId::fromValue(2));
        REQUIRE(ux.has_value());
        REQUIRE(std::abs(ux->ownedForce.x.si() + ux->sharedForce.x.si()) < kExact * force);
        Result<structural::RestraintReaction> uy = solved.reactions.of(RestraintId::fromValue(3));
        REQUIRE(uy.has_value());
        REQUIRE(std::abs(uy->ownedForce.y.si() + uy->sharedForce.y.si()) < kExact * force);
    }

    SECTION("the strain energy is (1/2) F delta") {
        WARN("RM-FEA-02 axial bar | U " << solved.strainEnergy.si() << " J | (1/2) F delta " << bar.energy << " J");
        REQUIRE_THAT(solved.strainEnergy.si(), WithinRel(bar.energy, kExact));
    }
}

// ---------------------------------------------------------------------------
// RM-FEA-03: the cantilever
// ---------------------------------------------------------------------------

TEST_CASE("StructuralCantilever_TrendsTowardBeamTheoryAcrossThreeMeshesAndMeasuresWhyItCannotReachIt",
          "[refmod][structural][rm03][convergence]") {
    // THREE MESHES OF THE SAME CANTILEVER, refined through the document's own
    // control, and beam theory as the TARGET of the trend -- never as a point
    // value on one mesh. What is claimed is the TREND: the deflection rises
    // with every refinement and the distance to beam theory falls.
    //
    // AND WHAT THE STUDY FOUND IS WHY THE TREND IS SLOW, which is finding F3
    // of this suite and is asserted below so that it stays measured: OCCT
    // triangulates a developable wall WITHOUT subdividing along its ruling,
    // so every boundary triangle on the cylinder's wall spans the FULL 100 mm
    // length at every deflection, and every tetrahedron owning one is a
    // needle with a 100 mm edge -- at the outer fibre, where bending stress
    // lives. Refinement through the global target adds interior points and
    // cannot shorten those edges, so the measured tip deflection is a few per
    // cent of the beam value and climbs slowly (3.8%, 4.7%, 4.8% of
    // Timoshenko, measured). That is the P16 surface pipeline's planar-face
    // limitation ("two triangles whatever the deflection") in its curved
    // form, and it is recorded for the owner rather than hidden behind a
    // tolerance.
    //
    // WHAT IS EXACT AT EVERY LEVEL, and is asserted as such: the root
    // reaction balances the applied load, and the root moment is the applied
    // force times the lever arm -- both statements of equilibrium that the
    // discrete system satisfies whatever the mesh.
    //
    // THE EQUILIBRIUM TOLERANCE THE LEVELS ARE SOLVED UNDER IS STATED, NOT
    // DEFAULTED, and the reason is a measured finding of this suite. The
    // production default refuses a moment imbalance above 1e-12 normalized;
    // the finest level here measured 1.15e-12 and was refused -- not because
    // anything is out of balance, but because the forward error of a
    // bending-dominated factorisation (pivot ratio 5e-4 and falling with
    // refinement) reaches the floor P17-REACTION-001 set from well-conditioned
    // blocks. So the levels are solved under BetterCAD's documented 1e-9 band
    // for accumulated algebra, the measured eta_M is recorded at every level,
    // and the crossing is reported as finding F1 for the owner. The committed
    // coarse document is solved under the production default everywhere else
    // in this suite, in the runner and in the CLI, and passes it.
    constexpr structural::EquilibriumTolerance kStudyTolerance{.force = 1.0e-9, .moment = 1.0e-9};

    auto built = buildStructuralCantileverReferenceModel();
    REQUIRE(built.has_value());
    const FaceName tip = built->tip();
    StructuralReference reference(std::move(built->document));

    const double radius = metres(reference.document(), "cant_r");
    const double length = metres(reference.document(), "cant_l");
    const ResolvedSteel steel = reference.steel();
    const StructuralAnalysisDefinition definition = reference.definition();
    REQUIRE(definition.loads.size() == 1);
    REQUIRE(definition.loads[0].traction() != nullptr);
    const double traction = definition.loads[0].traction()->traction.y.si();
    REQUIRE(traction < 0.0);
    const double area = analytic::pi * radius * radius;
    const double force = -traction * area;
    REQUIRE_THAT(force, WithinRel(kCantileverTipForceN, 1e-12));
    const analytic::Cantilever beam = analytic::cantilever(
        force, length, steel.youngs, analytic::secondMomentCircle(radius), steel.shear,
        analytic::shearCoefficientCircle(steel.poisson), area, radius);

    struct Level {
        double globalMm = 0.0;
        double deflectionMm = 0.0;
        std::size_t nodes = 0;
        std::size_t tets = 0;
        std::size_t freeEquations = 0;
        /// The mean of -u_y over the tip nodes: the tip deflection in the
        /// load's direction.
        double tipDeflection = 0.0;
        /// The work-conjugate deflection, 2 U / F.
        double conjugateDeflection = 0.0;
        double appliedForce = 0.0;
        double reactionY = 0.0;
        double reactionMomentX = 0.0;
        double largestVonMises = 0.0;
        /// The bending moment the element stresses carry across the mid-span
        /// slab, integrated on the test side.
        double midSpanMoment = 0.0;
        double pivotRatio = 0.0;
        double forceEta = 0.0;
        double momentEta = 0.0;
        /// The longest edge of any boundary triangle, in metres: the
        /// instrument that measures finding F3.
        double longestBoundaryEdge = 0.0;
        /// The fraction of tetrahedra with an edge at least half the length.
        double needleFraction = 0.0;
    };
    std::vector<Level> levels;

    for (const auto [globalMm, deflectionMm] :
         std::array<std::pair<double, double>, 3>{{{10.0, 0.25}, {5.0, 0.10}, {2.5, 0.05}}}) {
        meshing::MeshControlDefinition intent = reference.meshed().definition()->definition();
        intent.mesh.sizing.globalTargetSize = Length::fromSi(globalMm / kMmPerM);
        intent.mesh.surface.linearDeflection = Length::fromSi(deflectionMm / kMmPerM);
        reference.setMeshDefinition(intent);
        const meshing::VolumeMesh& volume = reference.require();
        const meshing::Mesh& mesh = volume.mesh();
        const StructuralSolveOutcome solved = reference.requireSolve(Point3D{}, kStudyTolerance);

        Level level{.globalMm = globalMm, .deflectionMm = deflectionMm, .nodes = mesh.nodeCount(),
                    .tets = mesh.tetrahedra().size(), .freeEquations = solved.freeEquations};
        level.pivotRatio = solved.pivotRatio;
        level.forceEta = solved.forceBalance.normalized;
        level.momentEta = solved.momentBalance.normalized;

        // F3's instrument: the longest boundary-triangle edge, and how many
        // tetrahedra carry an edge of at least half the beam's length.
        {
            const std::map<meshing::NodeId::ValueType, Si3> positions = nodePositionsSi(mesh);
            for (const meshing::Triangle& triangle : mesh.triangles()) {
                for (std::size_t i = 0; i < 3; ++i) {
                    const double edge = norm(minus(positions.at(triangle.nodes[i].value()),
                                                   positions.at(triangle.nodes[(i + 1) % 3].value())));
                    level.longestBoundaryEdge = std::max(level.longestBoundaryEdge, edge);
                }
            }
            std::size_t needles = 0;
            for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
                bool needle = false;
                for (const std::array<std::size_t, 2>& edge : meshref::kTetEdges) {
                    if (norm(minus(positions.at(tet.nodes[edge[0]].value()),
                                   positions.at(tet.nodes[edge[1]].value()))) >= 0.5 * length) {
                        needle = true;
                    }
                }
                needles += needle ? 1 : 0;
            }
            level.needleFraction = static_cast<double>(needles) / static_cast<double>(mesh.tetrahedra().size());
        }

        const std::vector<meshing::NodeId> tipNodes = reference.nodesOf(tip);
        double sum = 0.0;
        for (const meshing::NodeId node : tipNodes) {
            Result<NodalDisplacement> d = solved.fields.displacementOf(mesh, node);
            REQUIRE(d.has_value());
            sum += -d->displacement.y.si();
        }
        level.tipDeflection = sum / static_cast<double>(tipNodes.size());

        const structural::PreparedLoads loads = reference.prepareLoads();
        const Si3 applied = appliedResultant(loads);
        level.appliedForce = -applied[1];
        level.conjugateDeflection = 2.0 * solved.strainEnergy.si() / level.appliedForce;
        level.reactionY = solved.reactions.totalForce().y.si();
        level.reactionMomentX = solved.momentBalance.reaction.x.si();
        level.largestVonMises = solved.largestVonMises.si();

        // The moment carried across the slab z in [L/2 - h, L/2 + h]:
        // integral of sigma_zz y dA = sum_e sigma_zz,e y_c,e V_e / (2 h). The
        // slab is a whole element layer wide at the coarsest level.
        const double half = 5.0e-3;
        double moment = 0.0;
        for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
            const Si3 c = centroidSi(mesh, tet);
            if (std::abs(c[2] - length / 2.0) > half) {
                continue;
            }
            const std::array<Point3D, 4> corners = cornersOf(mesh, tet);
            const double v = signedVolumeSi(positionSi(corners[0]), positionSi(corners[1]),
                                            positionSi(corners[2]), positionSi(corners[3]));
            Result<ElementFields> e = solved.fields.fieldsOf(mesh, tet.id);
            REQUIRE(e.has_value());
            moment += e->stress.zz.si() * c[1] * v;
        }
        level.midSpanMoment = moment / (2.0 * half);
        levels.push_back(level);

        WARN("RM-FEA-03 cantilever | global " << globalMm << " mm, deflection " << deflectionMm << " mm | "
                                              << level.nodes << " nodes | " << level.tets << " tets | "
                                              << level.freeEquations << " free | tip " << level.tipDeflection
                                              << " m | conjugate " << level.conjugateDeflection << " m | EB "
                                              << beam.eulerBernoulli << " | Timoshenko " << beam.timoshenko
                                              << " | ratio " << level.tipDeflection / beam.timoshenko
                                              << " | applied " << level.appliedForce << " N | R_y "
                                              << level.reactionY << " | M_x " << level.reactionMomentX
                                              << " N m | mid-span M " << level.midSpanMoment << " vs "
                                              << beam.rootMoment / 2.0 << " | max vm " << level.largestVonMises
                                              << " | pivot " << level.pivotRatio << " | eta_F " << level.forceEta
                                              << " | eta_M " << level.momentEta << " | longest boundary edge "
                                              << level.longestBoundaryEdge << " m | needle fraction "
                                              << level.needleFraction);
    }
    REQUIRE(levels.size() == 3);

    SECTION("the three levels really are different discretisations") {
        // ASSERTED FIRST. Every claim below is meaningless if the mesh did
        // not change, which is exactly how P16-REFMOD-001's first convergence
        // table passed.
        for (std::size_t i = 1; i < levels.size(); ++i) {
            INFO("level " << i - 1 << " had " << levels[i - 1].tets << " tets, level " << i << " has " << levels[i].tets);
            REQUIRE(levels[i].tets > levels[i - 1].tets);
            REQUIRE(levels[i].nodes > levels[i - 1].nodes);
            REQUIRE(levels[i].freeEquations > levels[i - 1].freeEquations);
        }
        REQUIRE(levels.back().tets > 5 * levels.front().tets);
    }

    SECTION("equilibrium is exact at every level: the reaction and the root moment") {
        for (const Level& level : levels) {
            INFO("level with " << level.tets << " tets");
            // The applied resultant is the traction over the FACET area, which
            // is an inscribed polygon's and so slightly less than pi r^2; the
            // reaction balances what was applied, exactly.
            REQUIRE_THAT(level.reactionY, WithinRel(level.appliedForce, kExact));
            // And the inscribed area lies inside P16's derived chord bound:
            // a polygon whose chords deviate from the arc by at most the
            // declared deflection d contains the disc of radius r - d, so
            // the applied force is at least t pi (r - d)^2 and at most t pi
            // r^2. Nothing fitted; the bound follows from the declared
            // deflection alone.
            const double shrunk = 1.0 - level.deflectionMm / (radius * kMmPerM);
            REQUIRE(level.appliedForce >= force * shrunk * shrunk);
            REQUIRE(level.appliedForce <= force * (1.0 + 1e-12));
            // The reaction moment about the root's centre balances the tip
            // force's lever arm: |M_x| = F_applied L.
            REQUIRE_THAT(std::abs(level.reactionMomentX), WithinRel(level.appliedForce * length, kExact));
        }
    }

    SECTION("the tip deflection rises toward beam theory, and the error falls") {
        // A constant-strain tetrahedron is stiffer than the continuum, so a
        // coarse mesh under-deflects and refinement RAISES the deflection
        // toward the target. Both measures -- the tip mean and the
        // work-conjugate 2U/F -- must increase monotonically, and the
        // distance to the Timoshenko value must fall at every step.
        //
        // NO EQUALITY AND NO BAND IS CLAIMED AT ANY LEVEL. Linear
        // tetrahedra lock in bending, and the measured ratios to beam theory
        // -- a few per cent at two elements across the diameter -- are in
        // ANALYTICAL_VALIDATION.md beside the trend they establish.
        for (std::size_t i = 1; i < levels.size(); ++i) {
            INFO("level " << i - 1 << " tip " << levels[i - 1].tipDeflection << ", level " << i << " tip "
                          << levels[i].tipDeflection);
            REQUIRE(levels[i].tipDeflection > levels[i - 1].tipDeflection);
            REQUIRE(levels[i].conjugateDeflection > levels[i - 1].conjugateDeflection);
            REQUIRE(std::abs(levels[i].tipDeflection - beam.timoshenko) <
                    std::abs(levels[i - 1].tipDeflection - beam.timoshenko));
        }
        for (const Level& level : levels) {
            REQUIRE(level.tipDeflection > 0.0);
            REQUIRE(level.tipDeflection < beam.timoshenko);
            // The tip mean and the work-conjugate measure agree with each
            // other to a few per cent: the loaded face moves as a whole.
            REQUIRE_THAT(level.conjugateDeflection, WithinRel(level.tipDeflection, 0.05));
        }
        // And the trend is not a rounding: the finest level deflects by a
        // MARGIN over the coarsest -- measured 1.27x, gated at 1.2x.
        REQUIRE(levels.back().tipDeflection > 1.2 * levels.front().tipDeflection);
    }

    SECTION("the mid-span moment carried by the element stresses has the right sign and is short") {
        // Integrated on the test side from element-constant stresses and
        // centroids, so it is a statement about the recovered stress field
        // and not about the solver. A mathematical singularity sits at the
        // clamped corner; the mid-span slab is nowhere near it. On needle
        // meshes (F3) this instrument is noisy and NOT monotone -- measured
        // 0.44, 0.54, 0.44 N m against 10 -- so only the sign and the
        // shortfall are asserted and the values are recorded.
        const double target = beam.rootMoment / 2.0;
        for (const Level& level : levels) {
            REQUIRE(level.midSpanMoment > 0.0);
            REQUIRE(level.midSpanMoment < target);
        }
    }

    SECTION("F3, measured: every level's wall triangles span the whole length") {
        // The cause of the slow trend, asserted so it cannot silently change
        // under a future surface mesher: the longest boundary edge is the
        // beam's length at every deflection, and a substantial fraction of
        // the tetrahedra carry an edge of at least half the length.
        for (const Level& level : levels) {
            INFO("deflection " << level.deflectionMm << " mm: longest boundary edge " << level.longestBoundaryEdge
                               << " m, needle fraction " << level.needleFraction);
            REQUIRE(level.longestBoundaryEdge > 0.99 * length);
            REQUIRE(level.needleFraction > 0.05);
        }
        WARN("RM-FEA-03 cantilever | F3 | longest boundary edge / L: " << levels[0].longestBoundaryEdge / length
             << " " << levels[1].longestBoundaryEdge / length << " " << levels[2].longestBoundaryEdge / length
             << " | needle fraction: " << levels[0].needleFraction << " " << levels[1].needleFraction << " "
             << levels[2].needleFraction);
    }

    SECTION("every level balances under the study band, and the finding is measured") {
        // The algebraic equilibrium of every level, against the band the
        // study was solved under -- and, recorded rather than asserted, how
        // the measured moment imbalance compares with the production default,
        // which the finest level crosses. See the test's opening comment.
        for (const Level& level : levels) {
            REQUIRE(level.forceEta <= kStudyTolerance.force);
            REQUIRE(level.momentEta <= kStudyTolerance.moment);
            REQUIRE(level.pivotRatio >= 1.0e-12);
        }
        WARN("RM-FEA-03 cantilever | moment imbalance against the production default 1e-12: coarse "
             << levels[0].momentEta << " | medium " << levels[1].momentEta << " | fine " << levels[2].momentEta
             << " | pivot ratios " << levels[0].pivotRatio << " " << levels[1].pivotRatio << " "
             << levels[2].pivotRatio);
        // The committed coarse level passes the production default: that is
        // what the runner and the CLI solve it under.
        REQUIRE(levels.front().momentEta <= structural::EquilibriumTolerance{}.moment);
    }
}

// ---------------------------------------------------------------------------
// RM-FEA-04: the pressure block
// ---------------------------------------------------------------------------

TEST_CASE("StructuralPressureBlock_PreservesTheResultantAndBalancesTheReactions",
          "[refmod][structural][rm04][analytic]") {
    auto built = buildStructuralPressureBlockReferenceModel();
    REQUIRE(built.has_value());
    StructuralReference reference(std::move(built->document));

    const double a = metres(reference.document(), "press_a");
    const double b = metres(reference.document(), "press_b");
    const double c = metres(reference.document(), "press_c");
    const StructuralAnalysisDefinition definition = reference.definition();
    REQUIRE(definition.loads.size() == 1);
    REQUIRE(definition.loads[0].pressure() != nullptr);
    const double pressure = definition.loads[0].pressure()->magnitude.si();
    REQUIRE(pressure == kBlockPressurePa);
    const double force = analytic::pressureResultant(pressure, a * b);

    const StructuralSolveOutcome solved = reference.requireSolve();
    const meshing::Mesh& mesh = reference.model().mesh().mesh();
    const structural::PreparedLoads loads = reference.prepareLoads();

    SECTION("the nodal load conversion preserves the resultant p A, inward") {
        // Summed on the test side over the nodal forces. A positive pressure
        // acts INTO the material, so on the +Z top face the resultant is -pA.
        const Si3 applied = appliedResultant(loads);
        INFO("applied " << applied[0] << ", " << applied[1] << ", " << applied[2] << " N against -pA " << -force);
        WARN("RM-FEA-04 pressure | p A " << force << " N | applied F_z " << applied[2] << " N | relative "
                                         << std::abs(applied[2] + force) / force);
        REQUIRE_THAT(applied[2], WithinRel(-force, kAlgebraic));
        REQUIRE(std::abs(applied[0]) < kAlgebraic * force);
        REQUIRE(std::abs(applied[1]) < kAlgebraic * force);
        // Through the centroid of the face, (a/2, b/2, c): no moment about it.
        const Si3 moment = appliedMoment(mesh, loads, Si3{a / 2.0, b / 2.0, c});
        REQUIRE(norm(moment) < kAlgebraic * force * a);
    }

    SECTION("the support reactions supply +p A and balance") {
        const Si3 total = si3(solved.reactions.totalForce());
        WARN("RM-FEA-04 pressure | reaction F_z " << total[2] << " N | eta_F " << solved.forceBalance.normalized
                                                  << " | eta_M " << solved.momentBalance.normalized);
        REQUIRE_THAT(total[2], WithinRel(force, kExact));
        REQUIRE(std::abs(total[0]) < kExact * force);
        REQUIRE(std::abs(total[1]) < kExact * force);
        REQUIRE(solved.forceBalance.normalized <= 1e-12);
        REQUIRE(solved.momentBalance.normalized <= 1e-12);
        // And about the loaded face's centroid the reactions have no moment
        // either -- a different origin from the balance's, so a reversed cross
        // product would show.
        Result<Moment3D> about = solved.reactions.momentAbout(
            mesh, Point3D{Length::fromSi(a / 2.0), Length::fromSi(b / 2.0), Length::fromSi(c)});
        REQUIRE(about.has_value());
        REQUIRE(norm(si3(*about)) < kExact * force * a);
    }

    SECTION("the volume-averaged axial stress is exactly -p") {
        // A statement of discrete equilibrium, not of mesh quality: see
        // Analytic.hpp. Volume-weighted with the suite's own volumes.
        const VolumeMean mean = meanStressZz(mesh, solved.fields);
        const double expected = analytic::meanAxialStressUnderPressure(pressure);
        WARN("RM-FEA-04 pressure | mean sigma_zz " << mean.mean << " Pa | -p " << expected << " | relative "
                                                   << std::abs(mean.mean - expected) / pressure << " | over "
                                                   << mean.elements << " elements, " << mean.volume << " m^3");
        REQUIRE_THAT(mean.volume, WithinRel(a * b * c, kExact));
        REQUIRE_THAT(mean.mean, WithinRel(expected, kExact));
        REQUIRE(mean.mean < 0.0);
    }
}

// ---------------------------------------------------------------------------
// RM-FEA-05: self-weight
// ---------------------------------------------------------------------------

TEST_CASE("StructuralGravityBlock_WeighsRhoVGAtTheCentreOfVolume", "[refmod][structural][rm05][analytic]") {
    auto built = buildStructuralGravityBlockReferenceModel();
    REQUIRE(built.has_value());
    StructuralReference reference(std::move(built->document));

    const double a = metres(reference.document(), "grav_a");
    const double b = metres(reference.document(), "grav_b");
    const double height = metres(reference.document(), "grav_h");
    const ResolvedSteel steel = reference.steel();
    REQUIRE(steel.density.has_value());
    const StructuralAnalysisDefinition definition = reference.definition();
    REQUIRE(definition.mode == StructuralAnalysisMode::LinearStaticWithGravity);
    REQUIRE(definition.loads.size() == 1);
    REQUIRE(definition.loads[0].gravity() != nullptr);
    const Vector3D g = definition.loads[0].gravity()->acceleration;
    REQUIRE(g.x == 0.0);
    REQUIRE(g.y == 0.0);
    REQUIRE(g.z < 0.0);
    const double volume = a * b * height;
    const double weight = analytic::selfWeight(*steel.density, volume, -g.z);

    const StructuralSolveOutcome solved = reference.requireSolve();
    const meshing::Mesh& mesh = reference.model().mesh().mesh();
    const structural::PreparedLoads loads = reference.prepareLoads();

    SECTION("the equivalent nodal body force is the weight, acting at the centre of volume") {
        const Si3 applied = appliedResultant(loads);
        WARN("RM-FEA-05 gravity | rho V g " << weight << " N | applied F_z " << applied[2] << " N | relative "
                                            << std::abs(applied[2] + weight) / weight);
        REQUIRE_THAT(applied[2], WithinRel(-weight, kAlgebraic * 10.0));
        REQUIRE(std::abs(applied[0]) < kAlgebraic * weight);
        REQUIRE(std::abs(applied[1]) < kAlgebraic * weight);
        // About the origin, the weight W at (a/2, b/2, H/2) has the moment
        // c x W = (-b/2 W, +a/2 W, 0). Two DISTINCT non-zero components, so
        // an axis swap cannot reproduce them.
        const Si3 moment = appliedMoment(mesh, loads, Si3{});
        INFO("applied moment " << moment[0] << ", " << moment[1] << ", " << moment[2] << " N m");
        REQUIRE_THAT(moment[0], WithinRel(-b / 2.0 * weight, kExact));
        REQUIRE_THAT(moment[1], WithinRel(a / 2.0 * weight, kExact));
        REQUIRE(std::abs(moment[2]) < kExact * weight * a);
        REQUIRE(std::abs(moment[0]) != std::abs(moment[1]));
        // Every loaded node is pulled DOWN: a body force weighted by volume
        // has the same sign everywhere.
        for (const structural::NodalLoad& load : loads.nodal()) {
            REQUIRE(load.force.z.si() < 0.0);
        }
        REQUIRE(loads.nodal().size() == mesh.nodeCount());
    }

    SECTION("the reactions carry the weight, upward, and balance") {
        const Si3 total = si3(solved.reactions.totalForce());
        WARN("RM-FEA-05 gravity | reaction F_z " << total[2] << " N | eta_F " << solved.forceBalance.normalized
                                                 << " | eta_M " << solved.momentBalance.normalized);
        REQUIRE_THAT(total[2], WithinRel(weight, kExact));
        REQUIRE(std::abs(total[0]) < kExact * weight);
        REQUIRE(std::abs(total[1]) < kExact * weight);
        REQUIRE(solved.forceBalance.normalized <= 1e-12);
        REQUIRE(solved.momentBalance.normalized <= 1e-12);
        const Si3 reactionMoment = si3(solved.momentBalance.reaction);
        REQUIRE_THAT(reactionMoment[0], WithinRel(b / 2.0 * weight, kExact));
        REQUIRE_THAT(reactionMoment[1], WithinRel(-a / 2.0 * weight, kExact));
    }

    SECTION("the volume-averaged axial stress is exactly -rho g H / 2") {
        const VolumeMean mean = meanStressZz(mesh, solved.fields);
        const double expected = analytic::meanAxialStressUnderSelfWeight(*steel.density, -g.z, height);
        WARN("RM-FEA-05 gravity | mean sigma_zz " << mean.mean << " Pa | -rho g H / 2 " << expected << " | relative "
                                                  << std::abs(mean.mean - expected) / std::abs(expected));
        REQUIRE_THAT(mean.volume, WithinRel(volume, kExact));
        REQUIRE_THAT(mean.mean, WithinRel(expected, kExact));
        REQUIRE(mean.mean < 0.0);
    }
}

// ---------------------------------------------------------------------------
// RM-FEA-06: the rigid transform
// ---------------------------------------------------------------------------

TEST_CASE("StructuralTransformed_KeepsTheInvariantsAndRotatesTheVectors",
          "[refmod][structural][rm06][invariance]") {
    // THE SAME PROBLEM ON TWO FRAMES. What must not move: the strain energy,
    // every element's von Mises and hydrostatic stress, the largest
    // displacement magnitude, the magnitude of the total reaction. What must
    // move by R: every displacement vector and every reaction vector.
    //
    // THE TWO NODE POPULATIONS ARE JUDGED SEPARATELY. A boundary node is a
    // vertex of the kernel's triangulation of a CAD face and follows R x + t
    // to rounding; an interior node is Netgen's own choice and is NOT
    // equivariant under rotation. The gap is MEASURED for each population and
    // the invariants are then compared under the band the interior gap
    // implies, which is recorded rather than tuned.
    const RigidPlacement placement = structuralTransformedPlacement();

    auto baseBuilt = buildStructuralTransformedBaseReferenceModel();
    REQUIRE(baseBuilt.has_value());
    auto placedBuilt = buildStructuralTransformedPlacedReferenceModel();
    REQUIRE(placedBuilt.has_value());
    REQUIRE_FALSE(baseBuilt->placed);
    REQUIRE(placedBuilt->placed);
    StructuralReference base(std::move(baseBuilt->document));
    StructuralReference placed(std::move(placedBuilt->document));

    const StructuralSolveOutcome baseSolved = base.requireSolve();
    const StructuralSolveOutcome placedSolved = placed.requireSolve();
    const meshing::Mesh& baseMesh = base.model().mesh().mesh();
    const meshing::Mesh& placedMesh = placed.model().mesh().mesh();

    // The correspondence: each base node's image under R x + t, matched to
    // the nearest placed node. Built once and reused by every section.
    struct Pair {
        meshing::NodeId base{};
        meshing::NodeId placed{};
        bool boundary = false;
        double gap = 0.0;
    };
    const std::set<meshing::NodeId::ValueType> baseBoundary = meshref::boundaryNodeHandles(baseMesh);
    std::vector<Pair> pairs;
    double worstBoundaryGap = 0.0;
    double worstInteriorGap = 0.0;
    std::size_t interiorCount = 0;
    for (const meshing::Node& node : baseMesh.nodes()) {
        const Si3 image = placedSi(positionSi(node.position), placement);
        Pair pair{.base = node.id, .boundary = baseBoundary.contains(node.id.value()),
                  .gap = std::numeric_limits<double>::infinity()};
        for (const meshing::Node& candidate : placedMesh.nodes()) {
            const double gap = norm(minus(positionSi(candidate.position), image));
            if (gap < pair.gap) {
                pair.gap = gap;
                pair.placed = candidate.id;
            }
        }
        if (pair.boundary) {
            worstBoundaryGap = std::max(worstBoundaryGap, pair.gap);
        } else {
            worstInteriorGap = std::max(worstInteriorGap, pair.gap);
            ++interiorCount;
        }
        pairs.push_back(pair);
    }
    WARN("RM-FEA-06 transformed | " << baseMesh.nodeCount() << " / " << placedMesh.nodeCount() << " nodes | "
                                    << baseMesh.tetrahedra().size() << " / " << placedMesh.tetrahedra().size()
                                    << " tets | worst boundary gap " << worstBoundaryGap << " m | worst interior gap "
                                    << worstInteriorGap << " m over " << interiorCount << " interior nodes | U "
                                    << baseSolved.strainEnergy.si() << " / " << placedSolved.strainEnergy.si()
                                    << " J | max |u| " << baseSolved.largestDisplacement.si() << " / "
                                    << placedSolved.largestDisplacement.si() << " m | max vm "
                                    << baseSolved.largestVonMises.si() << " / " << placedSolved.largestVonMises.si()
                                    << " Pa");

    SECTION("the two documents really are differently placed, and the triad is a rotation") {
        // THE VACUOUS-INSTRUMENT GUARD. Two copies of one document would pass
        // every invariance check below.
        const Si3 first = positionSi(baseMesh.nodes().front().position);
        const Si3 moved = positionSi(placedMesh.nodes().front().position);
        REQUIRE(norm(minus(first, moved)) > 1.0e-3);
        const Si3 ex = rotated(Si3{1.0, 0.0, 0.0}, placement);
        REQUIRE(norm(minus(ex, Si3{1.0, 0.0, 0.0})) > 0.5);
        REQUIRE_THAT(norm(ex), WithinRel(1.0, 1e-12));
        REQUIRE(baseMesh.nodeCount() == placedMesh.nodeCount());
        REQUIRE(baseMesh.tetrahedra().size() == placedMesh.tetrahedra().size());
    }

    SECTION("boundary nodes follow R x + t to rounding; interior nodes are the mesher's own") {
        // BOTH POPULATIONS ARE PRESENT, or the separate judgement below would
        // be judging nothing: this block meshes with one interior node.
        REQUIRE(interiorCount >= 1);
        // The CAD-determined population, at the kernel's accuracy.
        REQUIRE(worstBoundaryGap < 1.0e-9);
        // The mesher's population, at the band P16-REFMOD-001 measured for
        // Netgen's interior point: well inside a millimetre, well outside
        // arithmetic noise. Recorded, not asserted equal.
        REQUIRE(worstInteriorGap < 1.0e-4);
        // And the correspondence is a bijection: no placed node claimed twice.
        std::set<meshing::NodeId::ValueType> claimed;
        for (const Pair& pair : pairs) {
            REQUIRE(claimed.insert(pair.placed.value()).second);
        }
    }

    SECTION("the two meshes are rigid images of each other, CONNECTIVITY INCLUDED") {
        // THE GUARD THAT MAKES THE INVARIANCE CLAIM MEAN SOMETHING. Equal
        // node sets are not enough: a box's tetrahedralisation is not unique,
        // and an 80 x 50 x 30 block on another frame gave the same eight
        // corners two DIFFERENT element sets -- and a 0.4% energy difference
        // that is discretisation, not a convention error. So every base
        // tetrahedron, mapped node by node through the correspondence, must
        // be a tetrahedron of the placed mesh.
        std::map<meshing::NodeId::ValueType, meshing::NodeId::ValueType> image;
        for (const Pair& pair : pairs) {
            image.emplace(pair.base.value(), pair.placed.value());
        }
        std::set<std::array<meshing::NodeId::ValueType, 4>> placedTets;
        for (const meshing::Tetrahedron& tet : placedMesh.tetrahedra()) {
            std::array<meshing::NodeId::ValueType, 4> key{tet.nodes[0].value(), tet.nodes[1].value(),
                                                          tet.nodes[2].value(), tet.nodes[3].value()};
            std::ranges::sort(key);
            placedTets.insert(key);
        }
        std::size_t matched = 0;
        for (const meshing::Tetrahedron& tet : baseMesh.tetrahedra()) {
            std::array<meshing::NodeId::ValueType, 4> key{image.at(tet.nodes[0].value()), image.at(tet.nodes[1].value()),
                                                          image.at(tet.nodes[2].value()), image.at(tet.nodes[3].value())};
            std::ranges::sort(key);
            if (placedTets.contains(key)) {
                ++matched;
            }
        }
        INFO(matched << " of " << baseMesh.tetrahedra().size() << " base tetrahedra have an image in the placed mesh");
        REQUIRE(matched == baseMesh.tetrahedra().size());
        REQUIRE(placedTets.size() == baseMesh.tetrahedra().size());
    }

    SECTION("the scalar invariants do not move") {
        // THE BAND IS SET BY THE INTERIOR NODE, AND IT IS MEASURED. The
        // mesher places the placed model's interior node 2.66e-07 m from the
        // image of the base model's, so the two discrete problems differ by
        // that much geometry; measured, the element invariants then differ by
        // at most 9.2e-07 relative and the rotated displacement vectors by
        // 1.1e-06 (ANALYTICAL_VALIDATION.md). 1e-5 is an order above the
        // measurement and five orders below what a convention error -- a
        // halved engineering shear, a dropped component, a transposed B --
        // would move these by.
        constexpr double kInvariant = 1e-5;
        REQUIRE_THAT(placedSolved.strainEnergy.si(), WithinRel(baseSolved.strainEnergy.si(), kInvariant));
        REQUIRE_THAT(placedSolved.largestDisplacement.si(),
                     WithinRel(baseSolved.largestDisplacement.si(), kInvariant));
        REQUIRE_THAT(placedSolved.largestVonMises.si(), WithinRel(baseSolved.largestVonMises.si(), kInvariant));
        REQUIRE_THAT(norm(si3(placedSolved.reactions.totalForce())),
                     WithinRel(norm(si3(baseSolved.reactions.totalForce())), kExact));
        REQUIRE(placedSolved.freeEquations == baseSolved.freeEquations);

        // Element by element, matched through the rotated centroid: von
        // Mises and the hydrostatic stress are frame-independent scalars.
        double worstVonMises = 0.0;
        double worstHydrostatic = 0.0;
        const double stressScale = baseSolved.largestVonMises.si();
        for (const meshing::Tetrahedron& tet : baseMesh.tetrahedra()) {
            const Si3 image = placedSi(centroidSi(baseMesh, tet), placement);
            const meshing::Tetrahedron* match = nullptr;
            double best = std::numeric_limits<double>::infinity();
            for (const meshing::Tetrahedron& candidate : placedMesh.tetrahedra()) {
                const double gap = norm(minus(centroidSi(placedMesh, candidate), image));
                if (gap < best) {
                    best = gap;
                    match = &candidate;
                }
            }
            REQUIRE(match != nullptr);
            REQUIRE(best < 1.0e-4);
            Result<ElementFields> a = baseSolved.fields.fieldsOf(baseMesh, tet.id);
            Result<ElementFields> b = placedSolved.fields.fieldsOf(placedMesh, match->id);
            REQUIRE(a.has_value());
            REQUIRE(b.has_value());
            worstVonMises = std::max(worstVonMises, std::abs(a->vonMises.si() - b->vonMises.si()) / stressScale);
            worstHydrostatic =
                std::max(worstHydrostatic, std::abs(a->hydrostatic.si() - b->hydrostatic.si()) / stressScale);
        }
        WARN("RM-FEA-06 transformed | element invariants | worst von Mises difference " << worstVonMises
                                                                                        << " | hydrostatic "
                                                                                        << worstHydrostatic
                                                                                        << " (relative to max vm)");
        REQUIRE(worstVonMises < kInvariant);
        REQUIRE(worstHydrostatic < kInvariant);
    }

    SECTION("the vectors rotate by R: displacements and reactions") {
        const double displacementScale = baseSolved.largestDisplacement.si();
        double worstBoundary = 0.0;
        double worstInterior = 0.0;
        for (const Pair& pair : pairs) {
            Result<NodalDisplacement> ub = baseSolved.fields.displacementOf(baseMesh, pair.base);
            Result<NodalDisplacement> up = placedSolved.fields.displacementOf(placedMesh, pair.placed);
            REQUIRE(ub.has_value());
            REQUIRE(up.has_value());
            const double error = norm(minus(si3(up->displacement), rotated(si3(ub->displacement), placement))) /
                                 displacementScale;
            if (pair.boundary) {
                worstBoundary = std::max(worstBoundary, error);
            } else {
                worstInterior = std::max(worstInterior, error);
            }
        }
        WARN("RM-FEA-06 transformed | u_placed against R u_base | worst relative error: boundary " << worstBoundary
                                                                                                   << " | interior "
                                                                                                   << worstInterior);
        // The same band as the scalar invariants, for the same measured
        // reason: the interior node moved, so every displacement did, at the
        // 1e-6 level.
        REQUIRE(worstBoundary < 1e-5);
        REQUIRE(worstInterior < 1e-5);

        // The total reaction rotates with the body: the pressure's resultant
        // is along the face normal, and the normal rotated.
        const Si3 expected = rotated(si3(baseSolved.reactions.totalForce()), placement);
        const Si3 actual = si3(placedSolved.reactions.totalForce());
        REQUIRE(norm(minus(actual, expected)) < kExact * norm(expected));
        // Per constrained node too, through the correspondence.
        double worstReaction = 0.0;
        const double reactionScale = norm(expected);
        for (const Pair& pair : pairs) {
            Result<structural::SupportReaction> rb = baseSolved.reactions.at(baseMesh, pair.base);
            if (!rb.has_value()) {
                continue;
            }
            Result<structural::SupportReaction> rp = placedSolved.reactions.at(placedMesh, pair.placed);
            REQUIRE(rp.has_value());
            worstReaction = std::max(worstReaction,
                                     norm(minus(si3(rp->force), rotated(si3(rb->force), placement))) / reactionScale);
        }
        WARN("RM-FEA-06 transformed | per-node reactions against R r_base | worst relative error " << worstReaction);
        REQUIRE(worstReaction < 1e-5);
    }
}

// ---------------------------------------------------------------------------
// RM-FEA-07: local refinement
// ---------------------------------------------------------------------------

TEST_CASE("StructuralLocalRefinement_RefinesTheRootBandAndTheMirrorRefinesTheTip",
          "[refmod][structural][rm07][refinement]") {
    // THREE MESHES OF ONE CANTILEVER: a local control at the root, the mirror
    // at the tip, and the same body with no local control at all. The
    // refinement is measured in a VOLUME BAND one target size deep at each
    // end, because a planar face's own facets cannot show it, and the mirror
    // is what proves the band moved with the control rather than the whole
    // body getting finer.
    //
    // No stress convergence is claimed at the clamped corner, which is a
    // mathematical singularity; what is claimed is that the solve stays
    // valid, the target region is really refined, the mapping is preserved
    // and the trend is physical.
    auto rootBuilt = buildStructuralLocalRefinementRootReferenceModel();
    REQUIRE(rootBuilt.has_value());
    auto tipBuilt = buildStructuralLocalRefinementTipReferenceModel();
    REQUIRE(tipBuilt.has_value());
    auto plainBuilt = buildStructuralLocalRefinementRootReferenceModel();
    REQUIRE(plainBuilt.has_value());
    const FaceName rootFace = rootBuilt->root();
    const FaceName tipFace = rootBuilt->tip();
    REQUIRE_FALSE(rootBuilt->refinesTip);
    REQUIRE(tipBuilt->refinesTip);
    REQUIRE(rootBuilt->refined() == rootFace);
    REQUIRE(tipBuilt->refined() == tipFace);

    StructuralReference root(std::move(rootBuilt->document));
    StructuralReference tip(std::move(tipBuilt->document));
    StructuralReference plain(std::move(plainBuilt->document));
    // The third mesh: the root document's intent with its local control
    // removed, so the comparison has a baseline nothing refined.
    {
        meshing::MeshControlDefinition intent = plain.meshed().definition()->definition();
        REQUIRE(intent.mesh.sizing.local.size() == 1);
        intent.mesh.sizing.local.clear();
        plain.setMeshDefinition(intent);
    }

    const double length = metres(root.document(), "loc_l");
    const double lengthMm = length * kMmPerM;
    const double targetMm = root.meshed().definition()->definition().mesh.sizing.local.front().targetSize.in(units::mm);
    REQUIRE(targetMm == 5.0);
    // THE BAND REACHES A THIRD OF THE WAY ALONG THE BEAM, deliberately past
    // the slab's own three-target depth: an unrefined box is six tetrahedra
    // whose centroids sit a quarter and three quarters of the way along, so a
    // band this deep holds elements in EVERY mesh, refined or not, and the
    // comparison is between populated bands rather than against an empty one.
    const double reachMm = 0.3 * lengthMm;

    struct Measured {
        std::size_t nodes = 0;
        std::size_t tets = 0;
        std::size_t rootBand = 0;
        std::size_t tipBand = 0;
        double rootMedianMm = 0.0;
        double tipMedianMm = 0.0;
        double energy = 0.0;
        double tipDeflection = 0.0;
        double largestVonMises = 0.0;
        double largestVonMisesZ = 0.0;
        std::size_t rootFacets = 0;
        std::size_t tipFacets = 0;
    };
    const auto measure = [&](StructuralReference& reference, std::string_view label) {
        const meshing::VolumeMesh& volume = reference.require();
        const meshing::Mesh& mesh = volume.mesh();
        const StructuralSolveOutcome solved = reference.requireSolve();
        Measured m;
        m.nodes = mesh.nodeCount();
        m.tets = mesh.tetrahedra().size();
        // The bands: tetrahedra whose centroid lies within `reachMm` of each
        // cap.
        const std::vector<meshing::ElementId> rootBand = meshref::tetsNearPlane(mesh, 2, 0.0, reachMm);
        const std::vector<meshing::ElementId> tipBand = meshref::tetsNearPlane(mesh, 2, lengthMm, reachMm);
        m.rootBand = rootBand.size();
        m.tipBand = tipBand.size();
        // `edgeStatsOf` with an EMPTY list measures the whole mesh, which is
        // not what an empty band means; an empty band is a measurement of
        // nothing and is refused here rather than read as a number.
        REQUIRE_FALSE(rootBand.empty());
        REQUIRE_FALSE(tipBand.empty());
        m.rootMedianMm = meshref::edgeStatsOf(mesh, rootBand).median;
        m.tipMedianMm = meshref::edgeStatsOf(mesh, tipBand).median;
        m.energy = solved.strainEnergy.si();
        const std::vector<meshing::NodeId> tipNodes = reference.nodesOf(tipFace);
        double sum = 0.0;
        for (const meshing::NodeId node : tipNodes) {
            Result<NodalDisplacement> d = solved.fields.displacementOf(mesh, node);
            REQUIRE(d.has_value());
            sum += -d->displacement.y.si();
        }
        m.tipDeflection = sum / static_cast<double>(tipNodes.size());
        m.largestVonMises = solved.largestVonMises.si();
        for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
            Result<ElementFields> e = solved.fields.fieldsOf(mesh, tet.id);
            REQUIRE(e.has_value());
            if (e->vonMises.si() == m.largestVonMises) {
                m.largestVonMisesZ = centroidSi(mesh, tet)[2];
            }
        }
        m.rootFacets = reference.facetsOf(rootFace).size();
        m.tipFacets = reference.facetsOf(tipFace).size();
        REQUIRE(solved.validation.acceptable());
        REQUIRE(solved.forceBalance.normalized <= 1e-12);
        REQUIRE(solved.momentBalance.normalized <= 1e-12);
        WARN("RM-FEA-07 " << label << " | " << m.nodes << " nodes | " << m.tets << " tets | root band " << m.rootBand
                          << " tets, median edge " << m.rootMedianMm << " mm | tip band " << m.tipBand
                          << " tets, median edge " << m.tipMedianMm << " mm | U " << m.energy << " J | tip "
                          << m.tipDeflection << " m | max vm " << m.largestVonMises << " Pa at z "
                          << m.largestVonMisesZ << " m | facets root " << m.rootFacets << " tip " << m.tipFacets);
        return m;
    };
    const Measured atRoot = measure(root, "root-refined");
    const Measured atTip = measure(tip, "tip-refined");
    const Measured none = measure(plain, "unrefined");

    SECTION("the three meshes differ, and the refined one is finer than the baseline") {
        REQUIRE(atRoot.tets > none.tets);
        REQUIRE(atTip.tets > none.tets);
        REQUIRE(atRoot.nodes > none.nodes);
        REQUIRE(atTip.nodes > none.nodes);
    }

    SECTION("the root control refines the root band and leaves the tip band coarse") {
        // Measured: the root band goes from 2 tetrahedra at a 61.6 mm median
        // edge to 315 at 9.0 mm, while the tip band of the same mesh holds 6
        // at 55.9 mm -- Netgen's grading re-tetrahedralises the far end, so
        // its COUNT moves a little and its SCALE does not. The gates are on
        // the scale and on the contrast between the two bands.
        REQUIRE(atRoot.rootBand > 10 * none.rootBand);
        REQUIRE(atRoot.rootMedianMm < 0.25 * none.rootMedianMm);
        REQUIRE(atRoot.tipMedianMm > 0.5 * none.tipMedianMm);
        REQUIRE(atRoot.tipBand * 10 < atRoot.rootBand);
        // Within one mesh the untouched band is graded, not pristine, so the
        // contrast between the two bands is gated at a half: measured 9.0
        // against 55.9 mm here, 11.6 against 40.0 mm in the mirror.
        REQUIRE(atRoot.rootMedianMm < 0.5 * atRoot.tipMedianMm);
    }

    SECTION("the mirror refines the tip band and leaves the root band coarse") {
        // THE DECISIVE CHECK: a control that refined the whole body would
        // pass the section above and fail this one. Measured: tip band 190
        // at 11.6 mm, root band 14 at 40 mm.
        REQUIRE(atTip.tipBand > 10 * none.tipBand);
        REQUIRE(atTip.tipMedianMm < 0.25 * none.tipMedianMm);
        REQUIRE(atTip.rootMedianMm > 0.5 * none.rootMedianMm);
        REQUIRE(atTip.rootBand * 10 < atTip.tipBand);
        REQUIRE(atTip.tipMedianMm < 0.5 * atTip.rootMedianMm);
        // And the two bands swap between the documents, in both directions:
        // measured 11.6 against 55.9 mm at the tip and 9.0 against 40.0 mm at
        // the root, gated at a half.
        REQUIRE(atTip.tipBand > 10 * atRoot.tipBand);
        REQUIRE(atRoot.rootBand > 10 * atTip.rootBand);
        REQUIRE(atTip.tipMedianMm < 0.5 * atRoot.tipMedianMm);
        REQUIRE(atRoot.rootMedianMm < 0.5 * atTip.rootMedianMm);
    }

    SECTION("the mapping is preserved: both caps resolve to the same facets on every mesh") {
        // A planar cap has two boundary triangles whatever the control says,
        // and a restraint or a load on it resolves to exactly those on every
        // one of the three meshes.
        for (const Measured& m : {atRoot, atTip, none}) {
            REQUIRE(m.rootFacets == 2);
            REQUIRE(m.tipFacets == 2);
        }
    }

    SECTION("the trend is physical: refinement softens the model and resolves a higher stress peak") {
        // A constant-strain tetrahedron is stiff, so MORE elements give MORE
        // compliance: both refined meshes deflect more and store more energy
        // than the baseline (measured 3.725e-5 and 3.947e-5 J against
        // 3.721e-5 J; 2.472e-7 and 2.614e-7 m against 2.464e-7 m). And a
        // coarse mesh under-resolves a stress peak, so both refined meshes
        // report a LARGER von Mises maximum than the baseline (427 and 470 kPa
        // against 364 kPa).
        //
        // WHERE the peak sits is recorded and NOT asserted: on these meshes
        // it lands in the graded transition next to the refined band (z = 61
        // mm in the root document, 42 mm in the mirror, 37.5 mm unrefined),
        // not at the clamped corner -- whose value is a mathematical
        // singularity and whose convergence this suite does not claim.
        REQUIRE(atRoot.energy > none.energy);
        REQUIRE(atTip.energy > none.energy);
        REQUIRE(atRoot.tipDeflection > none.tipDeflection);
        REQUIRE(atTip.tipDeflection > none.tipDeflection);
        REQUIRE(atRoot.largestVonMises > none.largestVonMises);
        REQUIRE(atTip.largestVonMises > none.largestVonMises);
    }
}

// ---------------------------------------------------------------------------
// RM-FEA-08: under-constrained
// ---------------------------------------------------------------------------

TEST_CASE("StructuralUnderConstrained_IsRefusedByThePivotGateAndNothingIsPublished",
          "[refmod][structural][rm08][failure]") {
    auto built = buildStructuralUnderConstrainedReferenceModel();
    REQUIRE(built.has_value());
    const FaceName bottom = built->bottom();
    StructuralReference reference(std::move(built->document));
    const meshing::VolumeMesh& volume = reference.require();

    SECTION("the count passes, and the factorisation refuses") {
        // Eight constrained degrees of freedom, all in z: validation's
        // necessary condition is met, which is the whole point of the model.
        const StructuralValidationReport validation = reference.validate();
        INFO((validation.findings.empty() ? std::string{} : validation.findings.front().message));
        REQUIRE(validation.acceptable());
        REQUIRE(validation.constrainedDegreesOfFreedom == 8);
        REQUIRE(validation.reachedStage == StructuralValidationReport::Stage::Complete);

        // THE SOLVE FAILS EXPLICITLY, with the solver's own diagnostic. No
        // outcome, so no displacement of any size was published.
        Result<StructuralSolveOutcome> outcome = reference.solve();
        REQUIRE_FALSE(outcome.has_value());
        INFO(outcome.error().message);
        REQUIRE_THAT(outcome.error().message, ContainsSubstring("singular"));
        WARN("RM-FEA-08 under-constrained | " << volume.nodeCount() << " nodes | validation accepted with "
                                              << validation.constrainedDegreesOfFreedom
                                              << " constrained DOFs | solve refused: " << outcome.error().message);

        // And the classification, through the public stages: it is the
        // SINGULAR verdict and not a residual or a settings problem.
        const structural::GlobalStructuralSystem system = reference.assemble();
        const structural::PreparedRestraints restraints = reference.prepareRestraints(system.numbering());
        REQUIRE(restraints.constraints().size() == 8);
        const std::optional<structural::SolveProblem> problem =
            structural::structuralSolveProblem(system, restraints.constraints());
        REQUIRE(problem.has_value());
        REQUIRE(*problem == structural::SolveProblem::SingularSystem);
        REQUIRE_FALSE(structural::solveStructuralSystem(system, restraints.constraints()).has_value());
    }

    SECTION("fewer than six is refused earlier, by the count, before any factorisation") {
        StructuralAnalysisDefinition definition = reference.definition();
        REQUIRE(definition.restraints.size() == 2);
        definition.restraints.pop_back();
        reference.setDefinition(definition);
        const StructuralValidationReport validation = reference.validate();
        REQUIRE(validation.status() == ValidationStatus::Rejected);
        REQUIRE(carries(validation, ValidationCode::InsufficientRestraint));
        REQUIRE(validation.constrainedDegreesOfFreedom == 4);
        REQUIRE(validation.reachedStage == StructuralValidationReport::Stage::Restraints);
        Result<StructuralSolveOutcome> outcome = reference.solve();
        REQUIRE_FALSE(outcome.has_value());
        REQUIRE(outcome.error().code == ErrorCode::FailedPrecondition);
    }

    SECTION("a fixed base makes the same model solve, so the refusal is about the restraint") {
        // The control case: nothing else about the fixture is broken.
        StructuralAnalysisDefinition definition = reference.definition();
        definition.restraints = {StructuralRestraint::fixedSupport(RestraintId::fromValue(1), bottom)};
        reference.setDefinition(definition);
        REQUIRE(reference.validate().acceptable());
        Result<StructuralSolveOutcome> outcome = reference.solve();
        REQUIRE(outcome.has_value());
        REQUIRE(outcome->pivotRatio >= 1e-12);
        // The sideways push moves the block sideways, and by a SMALL amount.
        double largestX = 0.0;
        for (const NodalDisplacement& d : outcome->fields.displacements()) {
            largestX = std::max(largestX, std::abs(d.displacement.x.si()));
        }
        REQUIRE(largestX > 0.0);
        REQUIRE(largestX < 1e-6);
    }
}

// ---------------------------------------------------------------------------
// RM-FEA-09: the stale mesh
// ---------------------------------------------------------------------------

TEST_CASE("StructuralStaleMesh_IsRefusedBeforeAssemblyAndSolvesAgainAfterRemeshing",
          "[refmod][structural][rm09][failure]") {
    auto built = buildStructuralStaleMeshReferenceModel();
    REQUIRE(built.has_value());
    StructuralReference reference(std::move(built->document));
    const StructuralSolveOutcome first = reference.requireSolve();
    REQUIRE(reference.meshed().currency() == meshing::MeshCurrency::Current);
    const double firstEnergy = first.strainEnergy.si();
    const std::size_t firstNodes = reference.model().mesh().mesh().nodeCount();

    const auto expectStaleRefusal = [&](std::string_view why) {
        // STILL HELD, STILL HANDED OUT: the refusal is about currency and not
        // about a missing mesh. That premise is what makes the test mean
        // something.
        REQUIRE(reference.mesher().mesh(reference.control()) != nullptr);
        const StructuralValidationReport validation = reference.validate();
        INFO(why);
        REQUIRE(validation.status() == ValidationStatus::Rejected);
        REQUIRE(carries(validation, ValidationCode::InputsUnavailable));
        REQUIRE(validation.inputProblem == structural::InputProblem::MeshStale);
        // BEFORE ASSEMBLY: the chain stopped at the input stage, so no
        // restraint was resolved, no load integrated, nothing assembled.
        REQUIRE(validation.reachedStage == StructuralValidationReport::Stage::Inputs);
        REQUIRE(validation.constrainedDegreesOfFreedom == 0);
        REQUIRE(validation.loadedNodes == 0);
        REQUIRE(validation.worstByMetric.empty());
        Result<StructuralSolveOutcome> outcome = reference.solve();
        REQUIRE_FALSE(outcome.has_value());
        REQUIRE(outcome.error().code == ErrorCode::FailedPrecondition);
        REQUIRE_THAT(outcome.error().message, ContainsSubstring("not acceptable"));
        WARN("RM-FEA-09 stale mesh | " << why << " | currency "
                                       << meshing::toString(reference.meshed().currency()) << " | validation "
                                       << structural::toString(validation.status()) << " at "
                                       << structural::toString(validation.reachedStage) << " | input problem "
                                       << structural::toString(*validation.inputProblem));
    };

    SECTION("a geometry edit stales the mesh, the solve is refused, and remeshing restores it") {
        setMm(reference.document(), "stale_c", 36.0);
        reference.meshed().regenerate();
        REQUIRE(reference.meshed().currency() == meshing::MeshCurrency::StaleGeometry);
        expectStaleRefusal("depth 30 -> 36 mm");

        // Remesh from the edited intent, and the model solves again -- to a
        // DIFFERENT answer, which is what makes the first refusal a real one.
        const StructuralSolveOutcome again = reference.requireSolve();
        REQUIRE(reference.meshed().currency() == meshing::MeshCurrency::Current);
        REQUIRE(again.strainEnergy.si() != firstEnergy);
        REQUIRE(again.validation.acceptable());
        WARN("RM-FEA-09 stale mesh | after remesh | " << reference.model().mesh().mesh().nodeCount() << " nodes (was "
                                                      << firstNodes << ") | U " << again.strainEnergy.si() << " J (was "
                                                      << firstEnergy << ")");
    }

    SECTION("a sizing edit stales the mesh for the other reason, and the solve is refused just the same") {
        meshing::MeshControlDefinition intent = reference.meshed().definition()->definition();
        intent.mesh.sizing.globalTargetSize = 12_mm;
        reference.setMeshDefinition(intent);
        REQUIRE(reference.meshed().currency() == meshing::MeshCurrency::StaleIntent);
        expectStaleRefusal("global target 20 -> 12 mm");
        const StructuralSolveOutcome again = reference.requireSolve();
        REQUIRE(again.validation.acceptable());
    }
}

// ---------------------------------------------------------------------------
// RM-FEA-10: the unresolved reference
// ---------------------------------------------------------------------------

TEST_CASE("StructuralUnresolved_IsRefusedWithoutRebindingToTheFaceNextDoor",
          "[refmod][structural][rm10][failure]") {
    auto built = buildStructuralUnresolvedReferenceModel();
    REQUIRE(built.has_value());
    const FaceName missing = built->missing();
    const FaceName top = built->top();
    const FaceName bottom = built->bottom();
    StructuralReference reference(std::move(built->document));
    reference.require();

    SECTION("the committed load is refused at the load stage, after the restraint resolved") {
        const StructuralValidationReport validation = reference.validate();
        REQUIRE(validation.status() == ValidationStatus::Rejected);
        REQUIRE(carries(validation, ValidationCode::LoadsUnresolvable));
        REQUIRE(validation.reachedStage == StructuralValidationReport::Stage::Loads);
        // The restraint stage PASSED: twelve constrained degrees of freedom
        // on the four corner nodes of the fixed base.
        REQUIRE(validation.constrainedDegreesOfFreedom == 12);
        Result<StructuralSolveOutcome> outcome = reference.solve();
        REQUIRE_FALSE(outcome.has_value());
        REQUIRE(outcome.error().code == ErrorCode::FailedPrecondition);
        WARN("RM-FEA-10 unresolved | validation " << structural::toString(validation.status()) << " at "
                                                  << structural::toString(validation.reachedStage) << " | "
                                                  << validation.findings.front().message);

        // The classification, through the public stage: the target is
        // UNRESOLVED, not malformed and not facet-less.
        const structural::StructuralModel model = reference.model();
        const std::optional<structural::LoadProblem> problem =
            structural::structuralLoadProblem(model, reference.material(), reference.definition().loads);
        REQUIRE(problem.has_value());
        REQUIRE(*problem == structural::LoadProblem::TargetUnresolved);
    }

    SECTION("nothing nearby was substituted: the missing face resolves to no facet at all") {
        const structural::StructuralModel model = reference.model();
        Result<meshing::BoundaryFacetSet> facets = meshing::boundaryFacetsOf(model.map(), missing);
        REQUIRE(facets.has_value());
        REQUIRE_FALSE(facets->fullyResolved());
        REQUIRE(facets->facets.empty());
        REQUIRE(facets->requested.size() == 1);
        REQUIRE(facets->requested.front().state == meshing::MappingState::Unresolved);
        // While the faces that DO exist resolve to their two triangles each.
        REQUIRE(reference.facetsOf(top).size() == 2);
        REQUIRE(reference.facetsOf(bottom).size() == 2);
    }

    SECTION("without the unresolvable load the same document solves") {
        StructuralAnalysisDefinition definition = reference.definition();
        REQUIRE(definition.loads.size() == 2);
        definition.loads.pop_back();
        reference.setDefinition(definition);
        REQUIRE(reference.validate().acceptable());
        REQUIRE(reference.solve().has_value());
    }

    SECTION("an unresolvable RESTRAINT is refused at the restraint stage, likewise") {
        StructuralAnalysisDefinition definition = reference.definition();
        definition.loads.pop_back();
        definition.restraints.push_back(StructuralRestraint::fixedSupport(RestraintId::fromValue(2), missing));
        reference.setDefinition(definition);
        const StructuralValidationReport validation = reference.validate();
        REQUIRE(validation.status() == ValidationStatus::Rejected);
        REQUIRE(carries(validation, ValidationCode::RestraintsUnresolvable));
        REQUIRE(validation.reachedStage == StructuralValidationReport::Stage::Restraints);
        REQUIRE_FALSE(reference.solve().has_value());
        const structural::StructuralModel model = reference.model();
        Result<structural::MeshDofMap> numbering = structural::buildMeshDofMap(model.mesh().mesh());
        REQUIRE(numbering.has_value());
        const std::optional<structural::RestraintProblem> problem =
            structural::structuralRestraintProblem(model, *numbering, definition.restraints);
        REQUIRE(problem.has_value());
        REQUIRE(*problem == structural::RestraintProblem::TargetUnresolved);
    }
}

// ---------------------------------------------------------------------------
// Persistence, determinism
// ---------------------------------------------------------------------------

TEST_CASE("StructuralReference_SaveLoadAndResolveReproducesEverySolutionBitForBit",
          "[refmod][structural][persist][cross]") {
    // EVERY DOCUMENT through a file and back: the INTENT must survive exactly
    // -- loads, restraints, mode, solver settings -- and NOTHING of the result
    // may be in the file. Then the reloaded document is regenerated, meshed
    // and solved from scratch, and must give the same numbers to the last
    // bit, which is what persisting intent rather than results promises.
    TempDir directory;
    for (const StructuralReferenceModelInfo& info : kStructuralReferenceModels) {
        INFO(info.id << " " << info.name);
        Result<Document> document = buildStructuralReferenceModel(info.kind);
        REQUIRE(document.has_value());
        StructuralReference before(std::move(*document));
        const StructuralAnalysisDefinition intent = before.definition();
        before.require();
        Result<StructuralSolveOutcome> original = before.solve();
        REQUIRE(original.has_value() == info.expectSolve);

        const std::filesystem::path path = directory.path() / std::format("{}.bcad", info.fileStem);
        REQUIRE(io::saveDocument(before.document(), path).has_value());
        const std::string contents = readFile(path);
        CHECK_THAT(contents, ContainsSubstring("structural-analysis"));
        CHECK_THAT(contents, ContainsSubstring("mesh-control"));
        CHECK_THAT(contents, !ContainsSubstring("\"displacements\""));
        CHECK_THAT(contents, !ContainsSubstring("\"stresses\""));
        CHECK_THAT(contents, !ContainsSubstring("\"strains\""));
        CHECK_THAT(contents, !ContainsSubstring("\"reactions\""));
        CHECK_THAT(contents, !ContainsSubstring("\"nodes\""));
        CHECK_THAT(contents, !ContainsSubstring("\"tetrahedra\""));
        CHECK(contents.size() < 40000);

        Result<Document> loaded = io::loadDocument(path);
        REQUIRE(loaded.has_value());
        StructuralReference after(std::move(*loaded));
        const StructuralAnalysisDefinition restored = after.definition();
        CHECK(restored == intent);
        CHECK(restored.loads == intent.loads);
        CHECK(restored.restraints == intent.restraints);
        CHECK(restored.mode == intent.mode);
        CHECK(restored.solver == intent.solver);
        CHECK(after.mesher().heldMeshCount() == 0);

        after.require();
        Result<StructuralSolveOutcome> again = after.solve();
        REQUIRE(again.has_value() == info.expectSolve);
        if (!info.expectSolve) {
            CHECK(again.error().code == original.error().code);
            continue;
        }
        const std::uint64_t first = fingerprintOf(*original);
        const std::uint64_t second = fingerprintOf(*again);
        WARN(info.id << " save/load | " << contents.size() << " bytes | fingerprint 0x" << std::hex << first
                     << " -> 0x" << second << std::dec);
        CHECK(second == first);
        CHECK(again->strainEnergy.si() == original->strainEnergy.si());
        CHECK(again->largestDisplacement.si() == original->largestDisplacement.si());
        CHECK(again->largestVonMises.si() == original->largestVonMises.si());
    }
}

TEST_CASE("StructuralReference_IsDeterministicOverRepeatedSolves", "[refmod][structural][determinism]") {
    // FIVE FRESH DOCUMENTS of each model, each built, regenerated, meshed and
    // solved from nothing shared -- not one mesh solved five times, which
    // would prove less. The fingerprint hashes every double's own
    // representation, so equality is bit-exact.
    const std::array<StructuralReferenceModelKind, 6> kinds{
        StructuralReferenceModelKind::Patch,         StructuralReferenceModelKind::AxialBar,
        StructuralReferenceModelKind::Cantilever,    StructuralReferenceModelKind::GravityBlock,
        StructuralReferenceModelKind::TransformedPlaced, StructuralReferenceModelKind::LocalRefinementRoot};
    for (const StructuralReferenceModelKind kind : kinds) {
        const StructuralReferenceModelInfo& info = infoOf(kind);
        INFO(info.id << " " << info.name);
        std::optional<std::uint64_t> expected;
        for (int run = 0; run < 5; ++run) {
            Result<Document> document = buildStructuralReferenceModel(kind);
            REQUIRE(document.has_value());
            StructuralReference reference(std::move(*document));
            const StructuralSolveOutcome solved = reference.requireSolve();
            const std::uint64_t print = fingerprintOf(solved);
            if (!expected) {
                expected = print;
                WARN(info.id << " determinism | fingerprint 0x" << std::hex << print << std::dec << " over "
                             << solved.fields.displacements().size() << " nodes and " << solved.fields.elements().size()
                             << " elements");
            }
            INFO("run " << run);
            REQUIRE(print == *expected);
        }
    }

    SECTION("and the fingerprint is not a constant") {
        // A VACUOUS-INSTRUMENT GUARD: a different load must give a different
        // fingerprint, or the equalities above would hold for any encoding.
        auto built = buildStructuralPressureBlockReferenceModel();
        REQUIRE(built.has_value());
        const FaceName top = built->top();
        StructuralReference reference(std::move(built->document));
        const std::uint64_t first = fingerprintOf(reference.requireSolve());
        StructuralAnalysisDefinition definition = reference.definition();
        definition.loads = {StructuralLoad{LoadId::fromValue(1),
                                           structural::PressureLoad{.face = top, .magnitude = Pressure::fromSi(3.0e6)}}};
        reference.setDefinition(definition);
        REQUIRE(fingerprintOf(reference.requireSolve()) != first);
    }

    SECTION("every document's outcome is stable over repeated attempts") {
        for (const StructuralReferenceModelInfo& info : kStructuralReferenceModels) {
            INFO(info.id << " " << info.name);
            std::optional<std::string> diagnostic;
            for (int attempt = 0; attempt < 3; ++attempt) {
                Result<Document> document = buildStructuralReferenceModel(info.kind);
                REQUIRE(document.has_value());
                StructuralReference reference(std::move(*document));
                reference.require();
                Result<StructuralSolveOutcome> outcome = reference.solve();
                REQUIRE(outcome.has_value() == info.expectSolve);
                if (!info.expectSolve) {
                    if (!diagnostic) {
                        diagnostic = outcome.error().message;
                    }
                    REQUIRE(outcome.error().message == *diagnostic);
                }
            }
        }
    }
}
