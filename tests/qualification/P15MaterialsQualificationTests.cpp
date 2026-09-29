#include "cli/CliRunner.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/Completeness.hpp>
#include <bettercad/core/materials/MaterialLibrary.hpp>
#include <bettercad/core/materials/Provenance.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/MassProperties.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/MaterialCommands.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using bettercad::test::cliPath;
using bettercad::test::runCliCommand;
using bettercad::test::TempDir;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using MechKind = materials::MechanicalPropertyKind;
using ThermKind = materials::ThermalPropertyKind;

// P15-QUAL-001 — the final cross-milestone gates.
//
// These are NOT re-runs of the per-milestone suites, which the three-preset
// regression re-runs in full anyway. Each one here crosses milestone boundaries
// and is required by the qualification brief as a FINAL gate rather than as
// inherited evidence: a fact can hold in P15-ASSIGN's tests, hold again in
// P15-PERSIST's, and still fail when identity, deletion, persistence, undo and
// the CLI are put in one sequence.
namespace {

constexpr double kRel = 1e-10;

/// The canonical state of a document's engineering data: every material's ID
/// and full definition, and the assignment. Deliberately NOT the mass, the
/// inertia or any completeness report -- those are derived, and a snapshot that
/// contained them could not tell a restored value from a recomputed one.
struct CanonicalState {
    std::vector<MaterialId> ids;
    std::vector<features::MaterialDefinition> definitions;
    features::MaterialAssignment assignment;

    friend bool operator==(const CanonicalState&, const CanonicalState&) = default;
};

CanonicalState canonicalStateOf(const Document& document) {
    CanonicalState state;
    state.ids = features::materialIds(document);
    for (const MaterialId id : state.ids) {
        state.definitions.push_back(features::findMaterial(document, id)->definition());
    }
    state.assignment = features::materialAssignment(document);
    return state;
}

/// A document with one 100 x 100 x 100 mm box, and nothing else.
struct BoxPart {
    Document document{"Part"};
    ObjectId feature{};

    BoxPart() {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        (void)test::addRectangle(*sketch, 0_mm, 0_mm, 100_mm, 100_mm);
        const ObjectId profile = document.addObject(std::move(sketch)).value();
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 100_mm});
        REQUIRE(extrude.has_value());
        feature = document.addObject(std::move(*extrude)).value();
    }
};

features::MaterialDefinition withDensity(double kgPerM3, std::string designation) {
    features::MaterialDefinition d;
    d.designation = std::move(designation);
    d.notes = "TEST / SYNTHETIC ENGINEERING DATA.";
    d.mechanical.density = materials::MaterialProperty<Density>::known(Density::fromSi(kgPerM3));
    return d;
}

Document roundTrip(const Document& document, const TempDir& dir, std::string_view name) {
    const std::filesystem::path path = dir.path() / std::string{name};
    REQUIRE(io::saveDocument(document, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    return std::move(*loaded);
}

double massOf(Document& document) {
    features::Regenerator regenerator;
    REQUIRE(regenerator.regenerateAll(document)->succeeded());
    const std::vector<ObjectId> results = features::resultFeatures(document);
    REQUIRE(results.size() == 1);
    auto properties = features::partMassProperties(document, regenerator, results.front());
    REQUIRE(properties.has_value());
    return properties->mass.si();
}

} // namespace

// === Section 9 / 65 — THE NO-REBINDING FINAL GATE ===========================

TEST_CASE("P15Qual_DeletedMaterialNeverRebindsThroughAnySequence", "[p15qual][identity]") {
    // The brief asks for two materials both NAMED "Steel". That is impossible
    // and the impossibility is the product's: object names are unique across
    // objects and parameters and are re-checked on every rename. The real
    // duplicate-label case is the DESIGNATION, which is free text, and that is
    // what this gate uses. The names differ; the engineering label does not.
    TempDir dir;
    BoxPart part;
    Document& document = part.document;

    const MaterialId a = features::createMaterial(document, "SteelA", withDensity(7800.0, "Steel")).value();
    const MaterialId b = features::createMaterial(document, "SteelB", withDensity(2700.0, "Steel")).value();
    REQUIRE(a != b);
    REQUIRE(features::assignMaterial(document, a).has_value());
    // Two materials, one designation, DIFFERENT densities -- so a rebind would
    // change the mass, not merely the identity.
    REQUIRE(features::findMaterialsByDesignation(document, "Steel").size() == 2);
    const double massWithA = massOf(document);
    CHECK_THAT(massWithA, WithinRel(7800.0 * 1e-3, kRel));

    // --- delete A -----------------------------------------------------------
    REQUIRE(features::removeMaterial(document, a).has_value());
    const auto unresolvedIsA = [&](const Document& d, std::string_view stage) {
        INFO("stage: " << stage);
        const features::MaterialAssignment state = features::materialAssignment(d);
        CHECK(state.state == features::MaterialAssignmentState::Unresolved);
        CHECK(state.material == a);
        CHECK(state.material != b);
        // And nothing resolves it: the effective material is nullptr, not B.
        CHECK(features::effectiveMaterial(d) == nullptr);
    };
    unresolvedIsA(document, "after deleting A");

    // A mass request must now FAIL. It must not fall back to B's density.
    {
        features::Regenerator regenerator;
        REQUIRE(regenerator.regenerateAll(document)->succeeded());
        const auto results = features::resultFeatures(document);
        const auto refused = features::partMassProperties(document, regenerator, results.front());
        CHECK_FALSE(refused.has_value());
    }

    // --- save / load --------------------------------------------------------
    Document loaded = roundTrip(document, dir, "rebind-once.bcad");
    unresolvedIsA(loaded, "after save and load");
    // Twice, because a loader that rebound might do so only on a second pass.
    Document twice = roundTrip(loaded, dir, "rebind-twice.bcad");
    unresolvedIsA(twice, "after a second save and load");

    // --- CLI query ----------------------------------------------------------
    const std::string path = cliPath(dir.path() / "rebind-twice.bcad");
    const auto effective = runCliCommand({"material-effective", path});
    CHECK(effective.exitCode == cli::ExitCode::Failure);
    CHECK_THAT(effective.err, ContainsSubstring("material_unresolved"));
    CHECK_THAT(effective.out, ContainsSubstring(std::string{"material:"} + std::to_string(a.value())));
    // The CLI names the material that is GONE, never the one that remains.
    CHECK_THAT(effective.out, !ContainsSubstring("SteelB"));

    // --- create C with the same designation ---------------------------------
    const MaterialId c =
        features::createMaterial(twice, "SteelC", withDensity(1000.0, "Steel")).value();
    REQUIRE(c != a);
    unresolvedIsA(twice, "after creating a third material with the same designation");
    CHECK(features::findMaterialsByDesignation(twice, "Steel").size() == 2);

    // --- and only the EXACT identity restores it ----------------------------
    // Undoing a delete restores the material under its original ID, so the
    // assignment resolves again -- and it can only ever resolve back to that
    // material, because the assignment holds an ID and not a name.
    BoxPart second;
    Document& fresh = second.document;
    const MaterialId x = features::createMaterial(fresh, "SteelX", withDensity(7800.0, "Steel")).value();
    REQUIRE(features::createMaterial(fresh, "SteelY", withDensity(2700.0, "Steel")).has_value());
    REQUIRE(features::assignMaterial(fresh, x).has_value());
    CommandHistory history;
    REQUIRE(history.execute(fresh, std::make_unique<features::DeleteMaterialCommand>(x)).has_value());
    CHECK(features::materialAssignment(fresh).state == features::MaterialAssignmentState::Unresolved);
    REQUIRE(history.undo(fresh).has_value());
    const features::MaterialAssignment restored = features::materialAssignment(fresh);
    CHECK(restored.state == features::MaterialAssignmentState::Resolved);
    CHECK(restored.material == x);
    CHECK_THAT(massOf(fresh), WithinRel(7800.0 * 1e-3, kRel));
}

// === Section 24 / 25 / 66 — NO FABRICATED ENGINEERING DATA ==================

TEST_CASE("P15Qual_MissingPropertiesAreReportedAndNeverDefaulted", "[p15qual][missing]") {
    BoxPart part;
    Document& document = part.document;

    SECTION("FEA: E known, nu UNKNOWN") {
        features::MaterialDefinition d = withDensity(2700.0, "Half-known alloy");
        d.mechanical.youngsModulus = materials::MaterialProperty<Stress>::known(Stress::fromSi(70.0e9));
        const MaterialId id = features::createMaterial(document, "Alloy", d).value();

        const auto report =
            features::materialCompleteness(document, id, materials::ConsumerKind::FeaLinearStatic);
        REQUIRE(report.has_value());
        CHECK(report->state == materials::CompletenessState::Incomplete);
        REQUIRE(report->missingMechanical.size() == 1);
        CHECK(report->missingMechanical.front() == MechKind::PoissonRatio);
        // NO nu = 0.3. The property is Unknown and the requirement fails.
        CHECK(features::findMaterial(document, id)->definition().mechanical.poissonRatio.isUnknown());
        CHECK_FALSE(features::requireLinearElasticConstants(document, id).has_value());
        // And the derived pair cannot be conjured from half its inputs.
        const materials::MechanicalProperties& p =
            features::findMaterial(document, id)->definition().mechanical;
        CHECK(materials::derivedShearModulus(p).isUnknown());
        CHECK(materials::derivedBulkModulus(p).isUnknown());
    }

    SECTION("ThermalTransient: density and cp known, k UNKNOWN") {
        features::MaterialDefinition d = withDensity(2700.0, "Unmeasured alloy");
        d.thermal.specificHeatCapacity =
            materials::MaterialProperty<SpecificHeatCapacity>::known(SpecificHeatCapacity::fromSi(896.0));
        const MaterialId id = features::createMaterial(document, "Alloy", d).value();

        const auto report =
            features::materialCompleteness(document, id, materials::ConsumerKind::ThermalTransient);
        REQUIRE(report.has_value());
        CHECK(report->state == materials::CompletenessState::Incomplete);
        REQUIRE(report->missingThermal.size() == 1);
        CHECK(report->missingThermal.front() == ThermKind::ThermalConductivity);
        // NO default k.
        CHECK(features::findMaterial(document, id)->definition().thermal.thermalConductivity.isUnknown());
        CHECK_FALSE(features::requireThermalConductivity(document, id).has_value());
        CHECK_FALSE(features::requireTransientConductionProperties(document, id).has_value());
    }

    SECTION("Mass: density UNKNOWN") {
        features::MaterialDefinition d;
        d.designation = "Unweighed alloy";
        d.mechanical.youngsModulus = materials::MaterialProperty<Stress>::known(Stress::fromSi(70.0e9));
        const MaterialId id = features::createMaterial(document, "Alloy", d).value();
        REQUIRE(features::assignMaterial(document, id).has_value());

        features::Regenerator regenerator;
        REQUIRE(regenerator.regenerateAll(document)->succeeded());
        const auto results = features::resultFeatures(document);
        const auto refused = features::partMassProperties(document, regenerator, results.front());
        REQUIRE_FALSE(refused.has_value());
        CHECK_THAT(refused.error().message, ContainsSubstring("density"));
        // NEVER a zero mass, and the geometry is sound -- only the data is short.
        CHECK(regenerator.body(results.front()) != nullptr);
        CHECK_FALSE(features::requireDensity(document, id).has_value());
    }
}

// === Section 7 — SAME DIMENSION, DIFFERENT MEANING ==========================

TEST_CASE("P15Qual_PressureDimensionPropertiesAreNeverInterchangeable", "[p15qual][units]") {
    // E, the yield strength, the UTS, the compressive strength and the shear
    // strength are ALL Quantity<pressure> -- the type system cannot tell them
    // apart, and P15-CLI-001 found that out when two overloads collided. What
    // keeps them distinct is a NAME, in three places: the enumerator, the
    // persisted key and the CLI code. This gate gives each a different value
    // and requires every one to survive on its own slot.
    TempDir dir;
    Document document{"Part"};
    features::MaterialDefinition d = withDensity(7800.0, "Distinct");
    d.mechanical.youngsModulus = materials::MaterialProperty<Stress>::known(Stress::fromSi(201.0e9));
    d.mechanical.yieldStrength = materials::MaterialProperty<Stress>::known(Stress::fromSi(202.0e6));
    d.mechanical.ultimateTensileStrength =
        materials::MaterialProperty<Stress>::known(Stress::fromSi(403.0e6));
    d.mechanical.ultimateCompressiveStrength =
        materials::MaterialProperty<Stress>::known(Stress::fromSi(504.0e6));
    d.mechanical.shearStrength = materials::MaterialProperty<Stress>::known(Stress::fromSi(105.0e6));
    d.mechanical.poissonRatio = materials::MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.28));
    const MaterialId id = features::createMaterial(document, "Steel", d).value();
    REQUIRE(features::assignMaterial(document, id).has_value());

    // Five DIFFERENT numbers, so any swap is visible.
    const std::array<double, 5> distinct{201.0e9, 202.0e6, 403.0e6, 504.0e6, 105.0e6};
    for (std::size_t i = 0; i < distinct.size(); ++i) {
        for (std::size_t j = i + 1; j < distinct.size(); ++j) {
            REQUIRE(distinct[i] != distinct[j]);
        }
    }

    // Through the FILE.
    Document loaded = roundTrip(document, dir, "distinct.bcad");
    const materials::MechanicalProperties& m =
        features::findMaterial(loaded, id)->definition().mechanical;
    CHECK_THAT(m.youngsModulus.value()->si(), WithinRel(201.0e9, kRel));
    CHECK_THAT(m.yieldStrength.value()->si(), WithinRel(202.0e6, kRel));
    CHECK_THAT(m.ultimateTensileStrength.value()->si(), WithinRel(403.0e6, kRel));
    CHECK_THAT(m.ultimateCompressiveStrength.value()->si(), WithinRel(504.0e6, kRel));
    CHECK_THAT(m.shearStrength.value()->si(), WithinRel(105.0e6, kRel));

    // And through the CLI, each under its own code.
    const std::string path = cliPath(dir.path() / "distinct.bcad");
    const auto shown = runCliCommand({"material-show", path, "Steel"});
    REQUIRE(shown.exitCode == cli::ExitCode::Success);
    for (const auto& [code, mpa] : std::vector<std::pair<std::string, double>>{
             {"youngs_modulus", 201000.0}, {"yield_strength", 202.0},
             {"ultimate_tensile_strength", 403.0}, {"ultimate_compressive_strength", 504.0},
             {"shear_strength", 105.0}}) {
        INFO("property " << code);
        CHECK_THAT(shown.out, ContainsSubstring(code + std::string(" ")));
        // The value printed beside that code, in MPa, is that property's.
        const std::size_t at = shown.out.find(code);
        REQUIRE(at != std::string::npos);
        const std::string line = shown.out.substr(at, shown.out.find('\n', at) - at);
        INFO("line: " << line);
        CHECK_THAT(line, ContainsSubstring(std::to_string(static_cast<long long>(mpa))));
    }

    // The requirement sets name them apart too: a yield-strength consumer needs
    // the YIELD strength, and an ultimate tensile strength does not substitute.
    Document withoutYield{"Part"};
    features::MaterialDefinition e = d;
    e.mechanical.yieldStrength = materials::MaterialProperty<Stress>::unknown();
    const MaterialId noYield = features::createMaterial(withoutYield, "NoYield", e).value();
    const auto report = features::materialCompleteness(withoutYield, noYield,
                                                       materials::ConsumerKind::FeaYieldStrength);
    REQUIRE(report.has_value());
    CHECK(report->state == materials::CompletenessState::Incomplete);
    REQUIRE(report->missingMechanical.size() == 1);
    CHECK(report->missingMechanical.front() == MechKind::YieldStrength);
}

// === Section 11 — DERIVED MODULI, INDEPENDENTLY ============================

TEST_CASE("P15Qual_DerivedModuliMatchClosedFormAndAreNeverSupplied", "[p15qual][mechanical]") {
    Document document{"Part"};
    // Three (E, nu) pairs, with expected values worked out by hand rather than
    // by the formula under test being re-run in the assertion.
    struct Case {
        double e;
        double nu;
        double g;
        double k;
    };
    const std::array<Case, 3> cases{
        // E = 70 GPa, nu = 0.33: G = 70/(2*1.33) = 26.315789473684212 GPa,
        //                        K = 70/(3*0.34) = 68.62745098039216 GPa
        Case{70.0e9, 0.33, 26.315789473684212e9, 68.62745098039216e9},
        // E = 200 GPa, nu = 0.28: G = 200/2.56 = 78.125 GPa,
        //                         K = 200/(3*0.44) = 151.5151515151515 GPa
        Case{200.0e9, 0.28, 78.125e9, 151.51515151515152e9},
        // nu = 0 is legal and is the edge that shows G = E/2 and K = E/3.
        Case{90.0e9, 0.0, 45.0e9, 30.0e9},
    };
    for (const Case& one : cases) {
        INFO("E = " << one.e << ", nu = " << one.nu);
        features::MaterialDefinition d = withDensity(1000.0, "Elastic");
        d.mechanical.youngsModulus = materials::MaterialProperty<Stress>::known(Stress::fromSi(one.e));
        d.mechanical.poissonRatio = materials::MaterialProperty<PoissonRatio>::known(PoissonRatio::of(one.nu));
        const MaterialId id =
            features::createMaterial(document, document.uniqueName("Elastic"), d).value();

        const auto constants = features::requireLinearElasticConstants(document, id);
        REQUIRE(constants.has_value());
        CHECK_THAT(constants->shearModulus.si(), WithinRel(one.g, kRel));
        CHECK_THAT(constants->bulkModulus.si(), WithinRel(one.k, kRel));

        // DERIVED, and the distinction is real: E and nu come back supplied.
        const materials::MechanicalProperties& p =
            features::findMaterial(document, id)->definition().mechanical;
        CHECK(materials::derivedShearModulus(p).isDerived());
        CHECK(materials::derivedBulkModulus(p).isDerived());
        CHECK_FALSE(materials::derivedShearModulus(p).isKnown());
        CHECK(p.youngsModulus.isKnown());
        CHECK(p.poissonRatio.isKnown());
    }
}

// === Section 13 — THERMAL RELATIONSHIPS, INDEPENDENTLY =====================

TEST_CASE("P15Qual_StoredThermalPropertiesSatisfyTheirDefiningRelations", "[p15qual][thermal]") {
    // P15 stores k, cp and alpha; it implements no solver. What is qualified
    // here is that the STORED values are the ones those laws consume, to the
    // unit, so a later solver reading them gets the physics right.
    Document document{"Part"};
    features::MaterialDefinition d = withDensity(2700.0, "Thermal");
    d.thermal.thermalExpansion =
        materials::MaterialProperty<ThermalExpansionCoefficient>::known(
            ThermalExpansionCoefficient::fromSi(23.0e-6));
    d.thermal.specificHeatCapacity =
        materials::MaterialProperty<SpecificHeatCapacity>::known(SpecificHeatCapacity::fromSi(900.0));
    d.thermal.thermalConductivity =
        materials::MaterialProperty<ThermalConductivity>::known(ThermalConductivity::fromSi(237.0));
    d.thermal.meltingTemperature = materials::MaterialProperty<Temperature>::known(Temperature::fromSi(933.0));
    const MaterialId id = features::createMaterial(document, "Aluminium", d).value();

    // dL = alpha L0 dT. A 2 m bar, 23e-6 /K, 100 K: 4.6 mm. By hand.
    const auto alpha = features::requireThermalExpansion(document, id);
    REQUIRE(alpha.has_value());
    const double elongationMm = alpha->si() * 2.0 * 100.0 * 1000.0;
    CHECK_THAT(elongationMm, WithinRel(4.6, kRel));

    // Q = m cp dT. 5 kg, 900 J/(kg K), 40 K: 180 000 J. By hand.
    const auto transient = features::requireTransientConductionProperties(document, id);
    REQUIRE(transient.has_value());
    CHECK_THAT(transient->specificHeatCapacity.si() * 5.0 * 40.0, WithinRel(180000.0, kRel));
    // The density a transient solve uses is the ONE canonical density, the same
    // number the mass uses -- there is no second thermal density to diverge.
    CHECK_THAT(transient->density.si(),
               WithinRel(features::requireDensity(document, id)->si(), 1e-15));

    // q = -k grad(T): the conductivity is stored in W/(m K) and is what the
    // Fourier consumer asks for.
    CHECK_THAT(features::requireThermalConductivity(document, id)->si(), WithinRel(237.0, kRel));

    // An ABSOLUTE temperature, not an interval: 933 K, and the unit system has
    // no offset, so there is no degC path that could shift it.
    CHECK_THAT(features::findMaterial(document, id)->definition().thermal.meltingTemperature.value()->si(),
               WithinRel(933.0, kRel));
}

// === Section 30 / 31 / 67 — THE EXACT UNDO / REDO CHAIN ====================

TEST_CASE("P15Qual_TheFullCommandChainUndoesAndRedoesExactly", "[p15qual][commands]") {
    BoxPart part;
    Document& document = part.document;
    CommandHistory history;

    const CanonicalState initial = canonicalStateOf(document);
    REQUIRE(initial.ids.empty());

    // Create A -> Edit A -> Assign P to A -> Edit A's density -> Remove the
    // assignment. Five commands, through the production history.
    auto create = std::make_unique<features::CreateMaterialCommand>("Steel", withDensity(7800.0, "Steel"));
    features::CreateMaterialCommand* createRaw = create.get();
    REQUIRE(history.execute(document, std::move(create)).has_value());
    const MaterialId a = createRaw->materialId();

    features::MaterialDefinition edited = features::findMaterial(document, a)->definition();
    edited.standard = "EN 10025-2";
    edited.mechanical.youngsModulus = materials::MaterialProperty<Stress>::known(Stress::fromSi(200.0e9));
    REQUIRE(history.execute(document, std::make_unique<features::EditMaterialCommand>(a, edited)).has_value());

    REQUIRE(history.execute(document, std::make_unique<features::AssignMaterialCommand>(a)).has_value());
    const double massBefore = massOf(document);
    CHECK_THAT(massBefore, WithinRel(7.8, kRel));

    features::MaterialDefinition denser = features::findMaterial(document, a)->definition();
    denser.mechanical.density = materials::MaterialProperty<Density>::known(Density::fromSi(8000.0));
    REQUIRE(history.execute(document, std::make_unique<features::EditMaterialCommand>(a, denser)).has_value());
    CHECK_THAT(massOf(document), WithinRel(8.0, kRel));

    REQUIRE(history.execute(document, std::make_unique<features::RemoveMaterialAssignmentCommand>())
                .has_value());

    const CanonicalState finalState = canonicalStateOf(document);
    REQUIRE(history.undoCount() == 5);

    // --- undo all -----------------------------------------------------------
    while (history.canUndo()) {
        REQUIRE(history.undo(document).has_value());
    }
    CHECK(canonicalStateOf(document) == initial);
    CHECK(features::materialCount(document) == 0);

    // --- redo all -----------------------------------------------------------
    while (history.canRedo()) {
        REQUIRE(history.redo(document).has_value());
    }
    CHECK(canonicalStateOf(document) == finalState);
    // THE SAME MaterialId, not a new one: redo restores identity.
    REQUIRE(features::materialIds(document).size() == 1);
    CHECK(features::materialIds(document).front() == a);

    // --- and the mass is RECOMPUTED, not restored ---------------------------
    // Nothing in the history holds a mass, so after re-assigning, the mass has
    // to come from the density that is there now.
    REQUIRE(features::assignMaterial(document, a).has_value());
    CHECK_THAT(massOf(document), WithinRel(8.0, kRel));
    // Undo the density edit and the mass follows the density back.
    while (history.canUndo() && history.undoCount() > 3) {
        REQUIRE(history.undo(document).has_value());
    }
    REQUIRE(features::assignMaterial(document, a).has_value());
    CHECK_THAT(massOf(document), WithinRel(7.8, kRel));
}

// === Section 33 / 67 — NO DERIVED STATE IN THE FILE ========================

TEST_CASE("P15Qual_ThePersistedFileHoldsNoDerivedEngineeringAuthority", "[p15qual][persistence]") {
    TempDir dir;
    BoxPart part;
    Document& document = part.document;
    features::MaterialDefinition d = withDensity(7800.0, "Steel");
    d.mechanical.youngsModulus = materials::MaterialProperty<Stress>::known(Stress::fromSi(200.0e9));
    d.mechanical.poissonRatio = materials::MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.28));
    const MaterialId id = features::createMaterial(document, "Steel", d).value();
    REQUIRE(features::assignMaterial(document, id).has_value());

    // Compute EVERYTHING derived first, so a cache would have something to hold.
    const double mass = massOf(document);
    CHECK_THAT(mass, WithinRel(7.8, kRel));
    REQUIRE(features::requireLinearElasticConstants(document, id).has_value());
    REQUIRE(features::materialReport(document, id).has_value());
    REQUIRE(features::effectiveMaterialCompleteness(document, materials::ConsumerKind::MassProperties)
                .has_value());

    const std::filesystem::path path = dir.path() / "derived.bcad";
    REQUIRE(io::saveDocument(document, path).has_value());
    const std::string text = test::readFile(path);

    // No key, and no computed NUMBER, for anything derived.
    for (const std::string_view forbidden :
         {"shear_modulus", "bulk_modulus", "centre_of_mass", "centreOfMass", "centroid", "inertia",
          "\"mass\"", "massProperties", "completeness", "effective_material", "effectiveMaterial",
          "moment_of_inertia"}) {
        INFO("forbidden token: " << forbidden);
        CHECK_THAT(text, !ContainsSubstring(std::string{forbidden}));
    }
    // The derived numbers themselves, to a few digits, are absent too.
    CHECK_THAT(text, !ContainsSubstring("78125000000"));  // G
    CHECK_THAT(text, !ContainsSubstring("7.8"));          // the mass in kg

    // After a load, every one of them RECOMPUTES to the same answer.
    Document loaded = roundTrip(document, dir, "derived-2.bcad");
    CHECK_THAT(massOf(loaded), WithinRel(mass, 1e-15));
    const auto constants = features::requireLinearElasticConstants(loaded, id);
    REQUIRE(constants.has_value());
    CHECK_THAT(constants->shearModulus.si(), WithinRel(200.0e9 / (2.0 * 1.28), kRel));
    CHECK(materials::derivedShearModulus(features::findMaterial(loaded, id)->definition().mechanical)
              .isDerived());
}

// === Section 28 — CLONE AND SOURCE ARE INDEPENDENT IN EVERY RESPECT ========

TEST_CASE("P15Qual_CloneAndSourceShareNothingAfterAnEdit", "[p15qual][custom]") {
    TempDir dir;
    Document document{"Part"};
    auto entry = materials::findLibraryMaterial(materials::builtInLibraryName(), "al-6061-t6");
    REQUIRE(entry.has_value());
    const MaterialId source = features::importLibraryMaterial(document, "Source", *entry).value();

    features::MaterialDefinition full = features::findMaterial(document, source)->definition();
    full.mechanical.density = materials::MaterialProperty<Density>::known(Density::fromSi(2700.0));
    full.mechanical.youngsModulus = materials::MaterialProperty<Stress>::known(Stress::fromSi(70.0e9));
    full.thermal.thermalConductivity =
        materials::MaterialProperty<ThermalConductivity>::known(ThermalConductivity::fromSi(167.0));
    REQUIRE(features::setMaterialDefinition(document, source, full).has_value());
    materials::PropertyProvenance cite;
    cite.kind = materials::SourceKind::Measured;
    cite.source = "Source report";
    REQUIRE(features::setMaterialPropertyProvenance(document, source, MechKind::Density, cite).has_value());

    const MaterialId clone = features::cloneMaterial(document, source, "Clone").value();
    REQUIRE(clone != source);
    const features::MaterialDefinition before = features::findMaterial(document, source)->definition();
    CHECK(features::findMaterial(document, clone)->definition() == before);

    // Edit the clone HEAVILY: metadata, mechanical, thermal, provenance, and a
    // property removed outright.
    features::MaterialDefinition changed = before;
    changed.designation = "Heavily edited";
    changed.standard = "None";
    changed.notes = "changed";
    changed.mechanical.density = materials::MaterialProperty<Density>::known(Density::fromSi(9999.0));
    changed.mechanical.youngsModulus = materials::MaterialProperty<Stress>::known(Stress::fromSi(1.0e9));
    changed.thermal.thermalConductivity =
        materials::MaterialProperty<ThermalConductivity>::known(ThermalConductivity::fromSi(1.0));
    materials::PropertyProvenance other;
    other.kind = materials::SourceKind::UserEntered;
    other.source = "Clone report";
    changed.provenance.mechanical[MechKind::Density] = other;
    REQUIRE(features::setMaterialDefinition(document, clone, changed).has_value());
    REQUIRE(features::removeMaterialProperty(document, clone, ThermKind::ThermalConductivity).has_value());

    // THE SOURCE IS UNTOUCHED, in every respect.
    CHECK(features::findMaterial(document, source)->definition() == before);

    // And the library entry, which is a compiled-in constant, still reads the
    // same -- nothing in the document can reach it.
    auto again = materials::findLibraryMaterial(materials::builtInLibraryName(), "al-6061-t6");
    REQUIRE(again.has_value());
    CHECK(again->designation() == entry->designation());
    CHECK(again->revision() == entry->revision());

    // Through a file, both keep their own state.
    Document loaded = roundTrip(document, dir, "clone.bcad");
    CHECK(features::findMaterial(loaded, source)->definition() == before);
    CHECK_THAT(features::findMaterial(loaded, clone)->definition().mechanical.density.value()->si(),
               WithinRel(9999.0, 1e-15));
    CHECK(features::findMaterial(loaded, clone)->definition().thermal.thermalConductivity.isUnknown());
    CHECK(features::findMaterial(loaded, source)->definition().thermal.thermalConductivity.isKnown());
}

// === the audit finding that needed a test ==================================

TEST_CASE("P15Qual_ARejectedDefinitionIsReportedAndNeverASilentNoChange", "[p15qual][errors]") {
    // The P15-QUAL code audit found `setDefinition(...).value_or(false)` inside
    // setMaterialDefinition's modifyObject lambda. It is unreachable on the
    // failure path -- the free function validates the SAME definition with the
    // SAME function before touching the document -- but "unreachable" is a
    // claim, and this is the test that makes it one the suite checks.
    Document document{"Part"};
    const MaterialId id = features::createMaterial(document, "Steel", withDensity(7800.0, "Steel")).value();
    const features::MaterialDefinition before = features::findMaterial(document, id)->definition();

    features::MaterialDefinition invalid = before;
    // A Poisson ratio of 0.5 divides by zero in the bulk modulus, so it is
    // refused by the property layer.
    invalid.mechanical.poissonRatio = materials::MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.5));
    const auto rejected = features::setMaterialDefinition(document, id, invalid);

    // AN ERROR, not `false`. A silent "nothing changed" would look like success
    // to every caller.
    REQUIRE_FALSE(rejected.has_value());
    CHECK_FALSE(rejected.error().message.empty());
    // And the material is exactly as it was.
    CHECK(features::findMaterial(document, id)->definition() == before);

    // A negative density is refused the same way.
    features::MaterialDefinition negative = before;
    negative.mechanical.density = materials::MaterialProperty<Density>::known(Density::fromSi(-1.0));
    CHECK_FALSE(features::setMaterialDefinition(document, id, negative).has_value());
    CHECK(features::findMaterial(document, id)->definition() == before);

    // A no-op edit reports false WITHOUT an error, which is the other half of
    // the contract and is what makes the distinction meaningful.
    const auto unchanged = features::setMaterialDefinition(document, id, before);
    REQUIRE(unchanged.has_value());
    CHECK_FALSE(*unchanged);
}
