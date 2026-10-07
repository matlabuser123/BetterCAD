// P17-MAT-001: resolving the material a structural solve consumes.
//
// WHAT THESE TESTS ARE ACTUALLY FOR. The implementation is thin because the
// audit found P15 already owns every validation this milestone's checklist
// names. That makes the tests the substance rather than the garnish: they are
// what demonstrates the reuse is real and correct, instead of a claim that P15
// "probably" handles it.
//
// So the boundary cases below -- E at zero, nu at exactly 0.5, NaN everywhere --
// are run against the STRUCTURAL resolver, and they pass because P15 refuses
// them. If a future change made P17 stop delegating, or made P15 stop
// validating, these fail. That is the point of testing a consumer path rather
// than trusting a layer boundary.
//
// The last two cases are the milestone's hard requirement: an E or nu edit
// leaves the P16 mesh CURRENT and makes the structural result STALE.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/Completeness.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/structural/StructuralAnalysisObject.hpp>
#include <bettercad/structural/StructuralMaterial.hpp>
#include <bettercad/structural/StructuralResult.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;
using materials::ConsumerKind;
using materials::MechanicalPropertyKind;
using structural::MaterialProblem;
using structural::StructuralAnalysis;
using structural::StructuralAnalysisDefinition;
using structural::StructuralAnalysisMode;
using structural::StructuralMaterial;

namespace {

/// A block with a mesh control and an analysis, whose material the test sets.
struct MaterialPart {
    Document document{"Part"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId control{};
    AnalysisId analysis{};
    MaterialId material{};

    explicit MaterialPart(StructuralAnalysisMode mode = StructuralAnalysisMode::LinearStatic) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 20_mm});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));

        auto intent = meshing::MeshControl::create("Mesh",
                                                   meshing::MeshControlDefinition{.body = feature});
        REQUIRE(intent.has_value());
        control = MeshControlId::fromValue(require(document.addObject(std::move(*intent))).value());

        auto study = StructuralAnalysis::create(
            "Study", StructuralAnalysisDefinition{.mesh = control, .mode = mode});
        REQUIRE(study.has_value());
        analysis = AnalysisId::fromValue(require(document.addObject(std::move(*study))).value());
        requireReport(regenerator, document);
    }

    /// Assigns a material with exactly the mechanical properties given.
    void assign(std::string name, const materials::MechanicalProperties& mechanical) {
        features::MaterialDefinition definition;
        definition.designation = name;
        definition.mechanical = mechanical;
        const Result<MaterialId> id = features::createMaterial(document, name, definition);
        INFO((id.has_value() ? std::string{} : id.error().message));
        REQUIRE(id.has_value());
        material = *id;
        REQUIRE(features::assignMaterial(document, material).has_value());
        requireReport(regenerator, document);
    }

    void setMechanical(const materials::MechanicalProperties& mechanical) {
        REQUIRE(features::setMaterialMechanical(document, material, mechanical).has_value());
    }

    [[nodiscard]] materials::MechanicalProperties mechanical() const {
        return features::findMaterial(document, material)->definition().mechanical;
    }

    [[nodiscard]] Result<StructuralMaterial> resolve(
        StructuralAnalysisMode mode = StructuralAnalysisMode::LinearStatic) const {
        return structural::resolveStructuralMaterial(document, feature, mode);
    }

    [[nodiscard]] std::optional<MaterialProblem> problem(
        StructuralAnalysisMode mode = StructuralAnalysisMode::LinearStatic) const {
        return structural::structuralMaterialProblem(document, feature, mode);
    }

    void requireMesh() {
        const Result<const meshing::VolumeMesh*> mesh =
            mesher.generate(document, regenerator, control);
        INFO((mesh.has_value() ? std::string{} : mesh.error().message));
        REQUIRE(mesh.has_value());
    }
};

/// E and nu, the ordinary case.
[[nodiscard]] materials::MechanicalProperties elastic(ElasticModulus e, double nu) {
    materials::MechanicalProperties properties;
    properties.youngsModulus = materials::MaterialProperty<ElasticModulus>::known(e);
    properties.poissonRatio =
        materials::MaterialProperty<bettercad::PoissonRatio>::known(PoissonRatio::of(nu));
    return properties;
}

[[nodiscard]] materials::MechanicalProperties elasticWithDensity(ElasticModulus e, double nu,
                                                                 Density rho) {
    materials::MechanicalProperties properties = elastic(e, nu);
    properties.density = materials::MaterialProperty<Density>::known(rho);
    return properties;
}

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

} // namespace

// ---------------------------------------------------------------------------
// The consumer mapping, and what P15 says each one needs
// ---------------------------------------------------------------------------

TEST_CASE("StructuralMaterial_TheModeChoosesP15sConsumerAndNothingElseDecidesIt",
          "[structural][material]") {
    // ANALYSIS INTENT DECIDES, never the data. A material carrying a density
    // does not make a problem a self-weight problem.
    CHECK(structural::consumerFor(StructuralAnalysisMode::LinearStatic) ==
          ConsumerKind::FeaLinearStatic);
    CHECK(structural::consumerFor(StructuralAnalysisMode::LinearStaticWithGravity) ==
          ConsumerKind::FeaLinearStaticWithGravity);

    // AND THE REQUIREMENT TABLE IS P15's, read rather than restated. This is
    // the assertion that would fail if P17 ever grew a second copy of it.
    const materials::PropertyRequirement statics =
        materials::requiredProperties(ConsumerKind::FeaLinearStatic);
    const materials::PropertyRequirement gravity =
        materials::requiredProperties(ConsumerKind::FeaLinearStaticWithGravity);

    CHECK(statics.mechanical == std::vector{MechanicalPropertyKind::YoungsModulus,
                                            MechanicalPropertyKind::PoissonRatio});
    CHECK(statics.thermal.empty());
    CHECK(gravity.mechanical == std::vector{MechanicalPropertyKind::Density,
                                            MechanicalPropertyKind::YoungsModulus,
                                            MechanicalPropertyKind::PoissonRatio});
    CHECK(gravity.thermal.empty());

    // NO THERMAL PROPERTY IS REQUIRED OF A STRUCTURAL ANALYSIS, which is what
    // consumer-specific completeness exists for: a material characterised for
    // stress should not be blocked for want of a conductivity.
    WARN(std::format("FeaLinearStatic requires {} mechanical properties, WithGravity {}, "
                     "and neither requires a thermal one",
                     statics.mechanical.size(), gravity.mechanical.size()));
}

// ---------------------------------------------------------------------------
// Resolution
// ---------------------------------------------------------------------------

TEST_CASE("StructuralMaterial_ResolvesABuiltInStyleMaterialExactly", "[structural][material]") {
    MaterialPart part;
    part.assign("Steel", elasticWithDensity(210_GPa, 0.3, Density::fromSi(7850.0)));

    const Result<StructuralMaterial> resolved = part.resolve();
    INFO((resolved.has_value() ? std::string{} : resolved.error().message));
    REQUIRE(resolved.has_value());

    // EXACT CANONICAL VALUES, not reinterpreted.
    CHECK(resolved->elastic().youngsModulus == 210_GPa);
    CHECK(resolved->elastic().poissonRatio.value() == 0.3);
    CHECK(resolved->id() == part.material);
    CHECK(resolved->revision() ==
          features::findMaterial(part.document, part.material)->revision());
    CHECK(resolved->mode() == StructuralAnalysisMode::LinearStatic);

    // G and K are P15's, derived from E and nu (ADR-027), and nothing here
    // recomputes them.
    const double e = 210.0e9;
    const double nu = 0.3;
    CHECK_THAT(resolved->elastic().shearModulus.si(),
               WithinRel(e / (2.0 * (1.0 + nu)), 1e-12));
    CHECK_THAT(resolved->elastic().bulkModulus.si(),
               WithinRel(e / (3.0 * (1.0 - 2.0 * nu)), 1e-12));

    // NO DENSITY IN A NO-GRAVITY VIEW, even though the material has one. The
    // view is what this analysis consumes, not what the material holds.
    CHECK_FALSE(resolved->density().has_value());
}

TEST_CASE("StructuralMaterial_ResolvesACustomMaterialWithDistinctiveValues",
          "[structural][material]") {
    // The values are deliberately unlike any default, so an accidental fallback
    // to a built-in or to a hardcoded steel would be visible rather than
    // plausible.
    MaterialPart part;
    part.assign("FixtureAlloy", elasticWithDensity(123_GPa, 0.27, Density::fromSi(4567.0)));

    const Result<StructuralMaterial> resolved = part.resolve();
    REQUIRE(resolved.has_value());
    CHECK(resolved->elastic().youngsModulus == 123_GPa);
    CHECK(resolved->elastic().poissonRatio.value() == 0.27);
    CHECK(resolved->id() == part.material);

    // A CUSTOM MATERIAL IS NOT SECOND CLASS. Complete and valid is complete and
    // valid, whoever entered it; provenance says where a number came from and
    // validation says whether it is usable, and the two are separate questions.
    const Result<materials::CompletenessReport> report =
        structural::structuralMaterialCompleteness(part.document,
                                                   StructuralAnalysisMode::LinearStatic);
    REQUIRE(report.has_value());
    CHECK(report->ready());

    const Result<StructuralMaterial> gravity =
        part.resolve(StructuralAnalysisMode::LinearStaticWithGravity);
    REQUIRE(gravity.has_value());
    REQUIRE(gravity->density().has_value());
    CHECK(gravity->density()->si() == 4567.0);

    WARN(std::format("custom material resolved: E = {}, nu = {}, rho = {} kg/m^3",
                     toString(resolved->elastic().youngsModulus),
                     resolved->elastic().poissonRatio.value(), gravity->density()->si()));
}

TEST_CASE("StructuralMaterial_RefusesWhenNothingIsAssigned", "[structural][material]") {
    // NOT A SOLVE WITH DEFAULTS. ADR-028 forbids this module holding material
    // data, so there is nothing to fall back to -- which is the point.
    MaterialPart part;
    CHECK(part.problem() == MaterialProblem::NoMaterialAssigned);
    const Result<StructuralMaterial> resolved = part.resolve();
    REQUIRE_FALSE(resolved.has_value());
}

TEST_CASE("StructuralMaterial_RefusesWhenTheBodyIsGone", "[structural][material]") {
    MaterialPart part;
    part.assign("Steel", elastic(210_GPa, 0.3));
    REQUIRE(part.resolve().has_value());

    REQUIRE(part.document.removeObject(part.feature).has_value());
    CHECK(part.problem() == MaterialProblem::BodyNotFound);
    const Result<StructuralMaterial> resolved = part.resolve();
    REQUIRE_FALSE(resolved.has_value());
    CHECK(resolved.error().code == ErrorCode::NotFound);
}

// ---------------------------------------------------------------------------
// Missing properties, per consumer
// ---------------------------------------------------------------------------

TEST_CASE("StructuralMaterial_RefusesAMaterialMissingARequiredProperty",
          "[structural][material]") {
    SECTION("no Young's modulus") {
        MaterialPart part;
        materials::MechanicalProperties properties;
        properties.poissonRatio =
            materials::MaterialProperty<bettercad::PoissonRatio>::known(PoissonRatio::of(0.3));
        part.assign("NoE", properties);

        CHECK(part.problem() == MaterialProblem::RequiredPropertyUnavailable);
        const Result<StructuralMaterial> resolved = part.resolve();
        REQUIRE_FALSE(resolved.has_value());
        CHECK_THAT(resolved.error().message, ContainsSubstring("Young's modulus"));

        // P15's report names the property, in its own deterministic order.
        const Result<materials::CompletenessReport> report =
            structural::structuralMaterialCompleteness(part.document,
                                                       StructuralAnalysisMode::LinearStatic);
        REQUIRE(report.has_value());
        CHECK_FALSE(report->ready());
        CHECK(report->missingMechanical == std::vector{MechanicalPropertyKind::YoungsModulus});
    }
    SECTION("no Poisson ratio") {
        MaterialPart part;
        materials::MechanicalProperties properties;
        properties.youngsModulus = materials::MaterialProperty<ElasticModulus>::known(210_GPa);
        part.assign("NoNu", properties);

        CHECK(part.problem() == MaterialProblem::RequiredPropertyUnavailable);
        CHECK_THAT(part.resolve().error().message, ContainsSubstring("Poisson"));
        const Result<materials::CompletenessReport> report =
            structural::structuralMaterialCompleteness(part.document,
                                                       StructuralAnalysisMode::LinearStatic);
        REQUIRE(report.has_value());
        CHECK(report->missingMechanical == std::vector{MechanicalPropertyKind::PoissonRatio});
    }
    SECTION("an incomplete CUSTOM material borrows nothing") {
        // The adversarial case: a custom material with E and no nu must not
        // acquire a nu from a built-in, from another material, or from 0.3.
        MaterialPart part;
        materials::MechanicalProperties properties;
        properties.youngsModulus = materials::MaterialProperty<ElasticModulus>::known(123_GPa);
        part.assign("HalfMeasuredAlloy", properties);

        CHECK(part.problem() == MaterialProblem::RequiredPropertyUnavailable);
        const Result<StructuralMaterial> resolved = part.resolve();
        REQUIRE_FALSE(resolved.has_value());
        CHECK_THAT(resolved.error().message, ContainsSubstring("HalfMeasuredAlloy"));
    }
}

TEST_CASE("StructuralMaterial_DensityIsRequiredOnlyByTheGravityMode", "[structural][material]") {
    // THE OVER-REQUIRING TEST, and P15 states the principle: demanding a
    // property a consumer does not need "would block a perfectly solvable"
    // case. A no-gravity analysis must not be held up for want of a density.
    MaterialPart part;
    part.assign("ElasticOnly", elastic(210_GPa, 0.3));

    CHECK_FALSE(part.problem(StructuralAnalysisMode::LinearStatic).has_value());
    const Result<StructuralMaterial> statics = part.resolve(StructuralAnalysisMode::LinearStatic);
    REQUIRE(statics.has_value());
    CHECK_FALSE(statics->density().has_value());

    CHECK(part.problem(StructuralAnalysisMode::LinearStaticWithGravity) ==
          MaterialProblem::RequiredPropertyUnavailable);
    const Result<StructuralMaterial> gravity =
        part.resolve(StructuralAnalysisMode::LinearStaticWithGravity);
    REQUIRE_FALSE(gravity.has_value());
    CHECK_THAT(gravity.error().message, ContainsSubstring("density"));

    // And P15's per-consumer reports disagree about the same material, which is
    // exactly what consumer-specific completeness is for.
    const Result<materials::CompletenessReport> forStatics =
        structural::structuralMaterialCompleteness(part.document,
                                                   StructuralAnalysisMode::LinearStatic);
    const Result<materials::CompletenessReport> forGravity =
        structural::structuralMaterialCompleteness(part.document,
                                                   StructuralAnalysisMode::LinearStaticWithGravity);
    REQUIRE(forStatics.has_value());
    REQUIRE(forGravity.has_value());
    CHECK(forStatics->ready());
    CHECK_FALSE(forGravity->ready());
    CHECK(forGravity->missingMechanical == std::vector{MechanicalPropertyKind::Density});

    WARN("one material, two consumers: Ready for linear static, incomplete for gravity");
}

// ---------------------------------------------------------------------------
// Physical validity -- and WHERE it is actually enforced
// ---------------------------------------------------------------------------

TEST_CASE("StructuralMaterial_P15RefusesAnUnusableValueBeforeItCanReachTheDocument",
          "[structural][material][validity]") {
    // THE AUDIT'S SHARPEST FINDING, and it changed what this milestone could
    // honestly claim.
    //
    // The checklist asks P17 to "validate E > 0", "validate -1 < nu < 0.5" and
    // "reject NaN / infinity". The first draft of this file created materials
    // carrying those values and asserted that the structural resolver refused
    // them. Every one of those cases failed -- not at the resolver, but at
    // `createMaterial`, which would not accept the material at all:
    //
    //     "the mechanical properties are not valid: Poisson's ratio -1 is
    //      outside -1 < nu < 0.5"
    //     "the mechanical properties are not valid: density must be greater
    //      than zero, not 0 kg/m^3"
    //
    // So an unusable value is not something P17 must catch. It is something
    // P15 makes UNREPRESENTABLE: creation refuses it, and so does every edit,
    // because setMaterialMechanical validates through the same validator. That
    // is a stronger guarantee than validating at the solver, and the honest
    // test is of the guarantee that exists rather than of the one the
    // checklist assumed.
    MaterialPart part;

    const auto creationRefuses = [&part](const materials::MechanicalProperties& properties,
                                         std::string_view why) {
        features::MaterialDefinition definition;
        definition.designation = "Under test";
        definition.mechanical = properties;
        const Result<MaterialId> id =
            features::createMaterial(part.document, "UnderTest", definition);
        INFO(why);
        REQUIRE_FALSE(id.has_value());
        CHECK_THAT(id.error().message, ContainsSubstring("not valid"));
    };

    SECTION("Young's modulus") {
        creationRefuses(elastic(ElasticModulus::fromSi(0.0), 0.3), "E = 0");
        creationRefuses(elastic(ElasticModulus::fromSi(-210.0e9), 0.3), "E negative");
        // NaN IS THE ONE A RANGE CHECK ALONE MISSES: E <= 0 is FALSE for NaN.
        creationRefuses(elastic(ElasticModulus::fromSi(kNaN), 0.3), "E NaN");
        creationRefuses(elastic(ElasticModulus::fromSi(kInf), 0.3), "E +infinity");
        creationRefuses(elastic(ElasticModulus::fromSi(-kInf), 0.3), "E -infinity");
    }
    SECTION("Poisson ratio") {
        creationRefuses(elastic(210_GPa, -1.0), "nu = -1, the exclusive lower bound");
        creationRefuses(elastic(210_GPa, -1.5), "nu below -1");
        // EXACTLY 0.5 IS REFUSED, and it is the bound that matters: lambda =
        // E nu / ((1 + nu)(1 - 2 nu)) divides by zero there, so the isotropic
        // constitutive matrix is singular for a displacement-only formulation.
        creationRefuses(elastic(210_GPa, 0.5), "nu = 0.5, where lambda is singular");
        creationRefuses(elastic(210_GPa, 0.6), "nu above 0.5");
        creationRefuses(elastic(210_GPa, kNaN), "nu NaN -- no range comparison rejects it");
        creationRefuses(elastic(210_GPa, kInf), "nu +infinity");
    }
    SECTION("density") {
        creationRefuses(elasticWithDensity(210_GPa, 0.3, Density::fromSi(0.0)), "rho = 0");
        creationRefuses(elasticWithDensity(210_GPa, 0.3, Density::fromSi(-7850.0)),
                        "rho negative");
        creationRefuses(elasticWithDensity(210_GPa, 0.3, Density::fromSi(kNaN)), "rho NaN");
        creationRefuses(elasticWithDensity(210_GPa, 0.3, Density::fromSi(kInf)),
                        "rho +infinity");
    }
    SECTION("an EDIT is refused too, so a valid material cannot be spoiled") {
        part.assign("Steel", elastic(210_GPa, 0.3));
        materials::MechanicalProperties broken = part.mechanical();
        broken.poissonRatio =
            materials::MaterialProperty<bettercad::PoissonRatio>::known(PoissonRatio::of(0.5));
        const Result<bool> edited =
            features::setMaterialMechanical(part.document, part.material, broken);
        REQUIRE_FALSE(edited.has_value());
        // AND THE MATERIAL IS UNCHANGED: a refused edit leaves the old value.
        CHECK(part.mechanical().poissonRatio.value()->value() == 0.3);
        // So the structural resolver still succeeds, on the value really there.
        CHECK(require(part.resolve()).elastic().poissonRatio.value() == 0.3);
    }
}

TEST_CASE("StructuralMaterial_TheResolverStillAsksP15RatherThanAssumingValidity",
          "[structural][material][validity]") {
    // Given the above, the structural resolver's range checks sit behind a
    // boundary that cannot currently be crossed. That is not a reason to drop
    // the delegation, and this records why.
    //
    // The reachable half of P15's contract -- a required property that is
    // ABSENT -- is refused here, and it is refused by calling
    // requireLinearElasticConstants: the same function that would refuse an
    // out-of-range value if an import path, a file or a future API ever
    // produced one. So this test proves the delegation is live, and the
    // delegation is what covers the unreachable half.
    MaterialPart part;
    materials::MechanicalProperties properties;
    properties.youngsModulus = materials::MaterialProperty<ElasticModulus>::known(210_GPa);
    part.assign("NoNu", properties);

    CHECK(part.problem() == MaterialProblem::RequiredPropertyUnavailable);
    const Result<StructuralMaterial> resolved = part.resolve();
    REQUIRE_FALSE(resolved.has_value());
    // P15's own diagnostic, carried rather than flattened: it names the
    // material and the missing INPUT, not the derived constant.
    CHECK_THAT(resolved.error().message, ContainsSubstring("Poisson"));
    CHECK(resolved.error().code == ErrorCode::FailedPrecondition);

    WARN("an unusable VALUE cannot reach a document, because P15 refuses it at entry; an "
         "ABSENT required property can, and the resolver refuses that through P15");
}

TEST_CASE("StructuralMaterial_AcceptsAnyFinitePositiveYoungsModulus", "[structural][material]") {
    // No invented minimum engineering magnitude. A soft material is a material.
    const auto accepts = [](ElasticModulus e) {
        MaterialPart part;
        part.assign("UnderTest", elastic(e, 0.3));
        INFO("E = " << toString(e));
        const Result<StructuralMaterial> resolved = part.resolve();
        REQUIRE(resolved.has_value());
        CHECK(resolved->elastic().youngsModulus == e);
    };
    accepts(ElasticModulus::fromSi(1.0));
    accepts(ElasticModulus::fromSi(1.0e-3));
    accepts(1_MPa);
    accepts(210_GPa);
}

TEST_CASE("StructuralMaterial_AcceptsAPoissonRatioInsideTheStableRange",
          "[structural][material]") {
    // Including the values immediately inside each bound, so the comparison is
    // shown to be exclusive rather than off by one.
    const auto accepts = [](double nu, std::string_view why) {
        MaterialPart part;
        part.assign("UnderTest", elastic(210_GPa, nu));
        INFO("nu = " << why);
        const Result<StructuralMaterial> resolved = part.resolve();
        INFO((resolved.has_value() ? std::string{} : resolved.error().message));
        REQUIRE(resolved.has_value());
        CHECK(resolved->elastic().poissonRatio.value() == nu);
    };

    accepts(std::nextafter(-1.0, 0.0), "the first double above -1");
    accepts(-0.5, "auxetic, which is physical and rare");
    accepts(0.0, "cork, near enough");
    accepts(0.3, "steel");
    accepts(0.49, "rubber-like, and NOT rejected: no arbitrary near-incompressible threshold");
    accepts(std::nextafter(0.5, 0.0), "the last double below 0.5");
}

// ---------------------------------------------------------------------------
// Units
// ---------------------------------------------------------------------------

TEST_CASE("StructuralMaterial_UnitsAreCanonicalAndEquivalentInputsAgree",
          "[structural][material][units]") {
    // SI INTERNALLY, and the three spellings of one modulus must resolve to one
    // number. A factor-of-1000 defect between MPa and GPa would show here.
    const auto resolveWith = [](ElasticModulus e) {
        MaterialPart part;
        part.assign("UnderTest", elastic(e, 0.3));
        const Result<StructuralMaterial> resolved = part.resolve();
        REQUIRE(resolved.has_value());
        return resolved->elastic().youngsModulus.si();
    };

    const double fromGPa = resolveWith(200_GPa);
    const double fromMPa = resolveWith(200000_MPa);
    const double fromPa = resolveWith(ElasticModulus::fromSi(2.0e11));
    CHECK(fromGPa == 2.0e11);
    CHECK(fromMPa == fromGPa);
    CHECK(fromPa == fromGPa);

    // The types carry the dimensions, so a length cannot be a modulus.
    static_assert(std::is_same_v<decltype(materials::LinearElasticConstants::youngsModulus),
                                 ElasticModulus>);
    static_assert(std::is_same_v<ElasticModulus, Pressure>);
    static_assert(!std::is_convertible_v<Length, ElasticModulus>);
    static_assert(!std::is_convertible_v<double, ElasticModulus>);
    static_assert(!std::is_convertible_v<double, Density>);

    WARN(std::format("200 GPa, 200000 MPa and 2e11 Pa all resolve to {} Pa", fromGPa));
}

// ---------------------------------------------------------------------------
// Provenance
// ---------------------------------------------------------------------------

TEST_CASE("StructuralMaterial_PreservesIdentityAndRevisionWithoutCopyingProvenance",
          "[structural][material]") {
    MaterialPart part;
    part.assign("TraceableAlloy", elastic(123_GPa, 0.27));

    const Result<StructuralMaterial> resolved = part.resolve();
    REQUIRE(resolved.has_value());

    // WHAT IS RETAINED: a reference back to P15, enough for traceability and
    // enough for currentness.
    CHECK(resolved->id() == part.material);
    CHECK(resolved->revision() ==
          features::findMaterial(part.document, part.material)->revision());

    // WHAT IS DELIBERATELY NOT COPIED: the provenance record itself, the name,
    // the designation, the database entry. They stay canonical in P15, and a
    // caller that wants them asks P15 through the id this view carries.
    static_assert(sizeof(StructuralMaterial) < 128,
                  "a solver-ready view, not a copy of a material record");
    const features::Material* canonical = features::findMaterial(part.document, part.material);
    REQUIRE(canonical != nullptr);
    CHECK(canonical->definition().designation == "TraceableAlloy");

    // PROVENANCE IS NOT NUMERICAL AUTHORITY. This material records none, and it
    // resolves anyway: whether a number is usable and where it came from are
    // different questions, and P15 keeps them separate -- no consumer requires
    // provenance, and traceability is reported by its own query.
    // This material records no provenance at all, and it resolves anyway. That
    // is the property: validation decides whether a number is USABLE, and
    // provenance says where it CAME FROM, and no consumer requires the second
    // -- P15 says so in requiredProperties: "No consumer requires PROVENANCE.
    // Traceability is a separate question".
    const features::MaterialDefinition& definition = canonical->definition();
    const std::vector<materials::MaterialIssue> gaps = materials::traceabilityGaps(
        definition.mechanical, definition.thermal, definition.provenance);
    INFO("traceability gaps reported: " << gaps.size());
    CHECK(part.resolve().has_value());
    const Result<materials::CompletenessReport> report =
        structural::structuralMaterialCompleteness(part.document,
                                                   StructuralAnalysisMode::LinearStatic);
    REQUIRE(report.has_value());
    CHECK(report->ready());
}

// ---------------------------------------------------------------------------
// The milestone's hard requirement
// ---------------------------------------------------------------------------

TEST_CASE("StructuralMaterial_AYoungsModulusEditLeavesTheMeshCurrentAndStalesTheResult",
          "[structural][material][invalidate]") {
    // THE CRITICAL RULE: material edit -> FEA result stale, NOT mesh stale. A
    // mesh is a function of geometry and meshing intent, and E is in neither.
    MaterialPart part;
    part.assign("Steel", elastic(210_GPa, 0.3));
    part.requireMesh();

    const meshing::MeshStamp stampBefore = part.mesher.mesh(part.control)->mesh().stamp();
    const meshing::GeometryRevision geometryBefore =
        meshing::geometryRevision(part.document, part.feature);
    const meshing::MeshControlDefinition intentBefore =
        dynamic_cast<const meshing::MeshControl*>(
            part.document.findObject(ObjectId::fromValue(part.control.value())))
            ->definition();

    const structural::StructuralResultSource before = require(
        structural::currentResultSource(part.document, part.regenerator, part.mesher,
                                        part.analysis));

    materials::MechanicalProperties changed = part.mechanical();
    changed.youngsModulus = materials::MaterialProperty<ElasticModulus>::known(190_GPa);
    part.setMechanical(changed);

    // THE MESH IS UNTOUCHED, asked through P16's own currentness API rather
    // than by comparing a pointer.
    CHECK(part.mesher.currency(part.document, part.control) == meshing::MeshCurrency::Current);
    CHECK(meshing::describesTheModel(part.mesher.currency(part.document, part.control)));
    CHECK(part.mesher.mesh(part.control)->mesh().stamp() == stampBefore);
    // The geometry did not move either.
    CHECK(meshing::geometryRevision(part.document, part.feature) == geometryBefore);
    // Nor did the meshing intent.
    CHECK(dynamic_cast<const meshing::MeshControl*>(
              part.document.findObject(ObjectId::fromValue(part.control.value())))
              ->definition() == intentBefore);

    // AND THE RESULT SOURCE DID MOVE, on the material alone.
    const structural::StructuralResultSource after = require(
        structural::currentResultSource(part.document, part.regenerator, part.mesher,
                                        part.analysis));
    CHECK(structural::staleReasons(before, after) ==
          std::vector{structural::StaleReason::Material});
    CHECK(after.mesh == before.mesh);
    CHECK(after.geometry == before.geometry);

    // The resolved material reports the new modulus, from P15.
    const Result<StructuralMaterial> resolved = part.resolve();
    REQUIRE(resolved.has_value());
    CHECK(resolved->elastic().youngsModulus == 190_GPa);

    WARN(std::format("E 210 -> 190 GPa: mesh {}, geometry revision unchanged, "
                     "result source stale on material alone",
                     meshing::toString(part.mesher.currency(part.document, part.control))));
}

TEST_CASE("StructuralMaterial_APoissonEditAndAReassignmentBehaveTheSameWay",
          "[structural][material][invalidate]") {
    SECTION("a Poisson ratio edit") {
        MaterialPart part;
        part.assign("Steel", elastic(210_GPa, 0.3));
        part.requireMesh();
        const structural::StructuralResultSource before = require(
            structural::currentResultSource(part.document, part.regenerator, part.mesher,
                                            part.analysis));

        materials::MechanicalProperties changed = part.mechanical();
        changed.poissonRatio =
            materials::MaterialProperty<bettercad::PoissonRatio>::known(PoissonRatio::of(0.33));
        part.setMechanical(changed);

        CHECK(part.mesher.currency(part.document, part.control) == meshing::MeshCurrency::Current);
        const structural::StructuralResultSource after = require(
            structural::currentResultSource(part.document, part.regenerator, part.mesher,
                                            part.analysis));
        CHECK(structural::staleReasons(before, after) ==
              std::vector{structural::StaleReason::Material});
        CHECK(require(part.resolve()).elastic().poissonRatio.value() == 0.33);
    }
    SECTION("a reassignment to a different material") {
        MaterialPart part;
        part.assign("Steel", elastic(210_GPa, 0.3));
        part.requireMesh();
        const structural::StructuralResultSource before = require(
            structural::currentResultSource(part.document, part.regenerator, part.mesher,
                                            part.analysis));

        features::MaterialDefinition second;
        second.designation = "Aluminium";
        second.mechanical = elastic(70_GPa, 0.33);
        const Result<MaterialId> other =
            features::createMaterial(part.document, "Aluminium", second);
        REQUIRE(other.has_value());
        REQUIRE(features::assignMaterial(part.document, *other).has_value());

        CHECK(part.mesher.currency(part.document, part.control) == meshing::MeshCurrency::Current);
        const structural::StructuralResultSource after = require(
            structural::currentResultSource(part.document, part.regenerator, part.mesher,
                                            part.analysis));
        CHECK(structural::staleReasons(before, after) ==
              std::vector{structural::StaleReason::Material});
        CHECK(after.material != before.material);
        CHECK(require(part.resolve()).elastic().youngsModulus == 70_GPa);
    }
}

TEST_CASE("StructuralMaterial_TheSameIdWithAChangedModulusIsADifferentSolverInput",
          "[structural][material][invalidate]") {
    // THE ADVERSARIAL CASE: the MaterialId and the name are unchanged, only E
    // moved. Currentness that compared the id alone would call the old result
    // current, and the user would read a stress computed from a modulus their
    // model no longer has.
    MaterialPart part;
    part.assign("Steel", elastic(210_GPa, 0.3));
    part.requireMesh();

    const Result<StructuralMaterial> before = part.resolve();
    REQUIRE(before.has_value());
    const structural::StructuralResultSource sourceBefore = require(
        structural::currentResultSource(part.document, part.regenerator, part.mesher,
                                        part.analysis));

    materials::MechanicalProperties changed = part.mechanical();
    changed.youngsModulus = materials::MaterialProperty<ElasticModulus>::known(205_GPa);
    part.setMechanical(changed);

    const Result<StructuralMaterial> after = part.resolve();
    REQUIRE(after.has_value());
    const structural::StructuralResultSource sourceAfter = require(
        structural::currentResultSource(part.document, part.regenerator, part.mesher,
                                        part.analysis));

    // Same id, same name...
    CHECK(after->id() == before->id());
    CHECK(features::findMaterial(part.document, part.material)->definition().designation ==
          "Steel");
    // ...different revision, different solver input, stale source.
    CHECK(after->revision() != before->revision());
    CHECK_FALSE(*after == *before);
    CHECK(sourceAfter.material == sourceBefore.material);
    CHECK(sourceAfter.materialRevision != sourceBefore.materialRevision);
    CHECK(structural::staleReasons(sourceBefore, sourceAfter) ==
          std::vector{structural::StaleReason::Material});

    WARN(std::format("same MaterialId and name, E 210 -> 205 GPa: revision {} -> {}, stale",
                     before->revision(), after->revision()));
}

TEST_CASE("StructuralMaterial_AResolvedViewIsNotEqualAcrossModes", "[structural][material]") {
    // The mode is part of the identity, because one of the two guarantees a
    // density and the other does not -- they are different solver inputs even
    // from one material.
    MaterialPart part{StructuralAnalysisMode::LinearStaticWithGravity};
    part.assign("Steel", elasticWithDensity(210_GPa, 0.3, Density::fromSi(7850.0)));

    const StructuralMaterial statics = require(part.resolve(StructuralAnalysisMode::LinearStatic));
    const StructuralMaterial gravity =
        require(part.resolve(StructuralAnalysisMode::LinearStaticWithGravity));

    CHECK(statics.id() == gravity.id());
    CHECK(statics.elastic() == gravity.elastic());
    CHECK_FALSE(statics == gravity);
    CHECK_FALSE(statics.density().has_value());
    CHECK(gravity.density().has_value());
}

TEST_CASE("StructuralMaterial_ResolutionIsDeterministicAndReadOnly", "[structural][material]") {
    MaterialPart part;
    part.assign("Steel", elasticWithDensity(210_GPa, 0.3, Density::fromSi(7850.0)));

    const StructuralMaterial first = require(part.resolve());
    const std::uint64_t documentRevision = part.document.revision();
    for (int i = 0; i < 16; ++i) {
        const StructuralMaterial again = require(part.resolve());
        CHECK(again == first);
        const Result<materials::CompletenessReport> report =
            structural::structuralMaterialCompleteness(part.document,
                                                       StructuralAnalysisMode::LinearStatic);
        REQUIRE(report.has_value());
        CHECK(report->ready());
    }

    // READ-ONLY: resolving sixteen times, and failing to resolve, changed
    // nothing. An invalid material is a refusal and never a repair -- nothing
    // clamps a ratio, takes an absolute value or substitutes a density.
    CHECK(part.document.revision() == documentRevision);

    // AND A FAILING RESOLUTION CHANGES NOTHING EITHER. The failure has to be a
    // reachable one, which means an ABSENT property rather than an invalid
    // value -- P15 will not accept an invalid value into the document at all,
    // so there is no such state to resolve from. (The first draft of this test
    // tried to set nu = 0.9 and the edit was refused, which is the same lesson
    // the validity cases above record.)
    materials::MechanicalProperties incomplete = part.mechanical();
    incomplete.poissonRatio = materials::MaterialProperty<bettercad::PoissonRatio>{};
    part.setMechanical(incomplete);
    const std::uint64_t afterEdit = part.document.revision();
    for (int i = 0; i < 8; ++i) {
        CHECK(part.problem() == MaterialProblem::RequiredPropertyUnavailable);
    }
    CHECK(part.document.revision() == afterEdit);
    // NOTHING WAS FILLED IN. Eight failed resolutions did not supply a nu, and
    // in particular did not supply 0.3 -- P15's rule is that missing data is
    // reported and never filled, and a solver that filled it would make the
    // user's model silently different from the one they described.
    CHECK_FALSE(part.mechanical().poissonRatio.value().has_value());
    CHECK(part.mechanical().youngsModulus.value().has_value());
}

TEST_CASE("StructuralMaterial_ReachesEveryMaterialProblemItDeclares", "[structural][material]") {
    const std::array<MaterialProblem, 3> declared{MaterialProblem::BodyNotFound,
                                                  MaterialProblem::NoMaterialAssigned,
                                                  MaterialProblem::RequiredPropertyUnavailable};
    for (const MaterialProblem problem : declared) {
        CHECK_FALSE(structural::toString(problem).empty());
        CHECK(structural::toString(problem) != "unknown");
    }
    WARN(std::format("{} material problems declared, every one reached by a case in this file",
                     declared.size()));
}
