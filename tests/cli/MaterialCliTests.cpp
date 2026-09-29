#include "CliRunner.hpp"
#include "MaterialVocabulary.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/Completeness.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/Provenance.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/MassProperties.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using bettercad::cli::ExitCode;
using bettercad::test::CliRun;
using bettercad::test::cliPath;
using bettercad::test::runCliCommand;
using bettercad::test::TempDir;
using Catch::Matchers::ContainsSubstring;
using materials::MechanicalPropertyKind;
using materials::ThermalPropertyKind;

// P15-CLI-001: headless engineering-data workflows.
//
// The gate is "CLI semantics == core semantics", so the tests that carry this
// milestone are DIFFERENTIAL: each does the same thing twice, once through the
// core API in memory and once through the CLI and a file on disk, and requires
// the two results to agree. A test that only checked the CLI produced something
// plausible would pass just as happily if the CLI had its own idea of what a
// material is, which is the exact failure this milestone must exclude.
//
// Where a number is an engineering claim rather than a copy -- a mass, a
// centre of mass, an inertia -- it is ALSO checked against closed-form geometry,
// because the core agreeing with the CLI proves they are consistent, not that
// either is right.
namespace {

using features::MaterialDefinition;

/// The temporary file a test drives the CLI against.
std::string documentPath(const TempDir& dir, std::string_view name) {
    return cliPath(dir.path() / name);
}

/// A material with a density and nothing else. Enough for a mass, incomplete
/// for anything structural, which is the interesting shape for completeness.
MaterialDefinition withDensity(Density density) {
    MaterialDefinition definition;
    definition.designation = "Steel S235JR";
    definition.standard = "EN 10025-2";
    definition.mechanical.density = materials::MaterialProperty<Density>::known(density);
    return definition;
}

/// A document holding one extruded box with a corner at the origin.
///
/// A BOX because its mass properties are closed form: for a x b x c at density
/// rho, V = abc, m = rho abc, the centroid is at (a/2, b/2, c/2), and the
/// centroidal moments are m(b^2+c^2)/12, m(a^2+c^2)/12, m(a^2+b^2)/12 with every
/// product of inertia zero. Those expected values are written down from the
/// geometry, never taken from the code being tested.
struct BoxDocument {
    Document document{"Part"};
    ObjectId feature{};
    /// The depth, as a PARAMETER, so that a configuration has something to
    /// override -- which is what the mass guard has to refuse under.
    ParameterId depth{};

    BoxDocument(Length a, Length b, Length c) {
        depth = document.createParameter("depth", c, units::mm).value();
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        (void)test::addRectangle(*sketch, 0_mm, 0_mm, a, b);
        const ObjectId profile = document.addObject(std::move(sketch)).value();
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depthParameter = depth});
        REQUIRE(extrude.has_value());
        feature = document.addObject(std::move(*extrude)).value();
    }
};

void save(const Document& document, const std::string& path) {
    REQUIRE(io::saveDocument(document, std::filesystem::path{path}).has_value());
}

Document load(const std::string& path) {
    auto loaded = io::loadDocument(std::filesystem::path{path});
    REQUIRE(loaded.has_value());
    return std::move(*loaded);
}

/// The text after @p label on the first line that contains it, trimmed.
///
/// Deliberately not a regex over the whole report: a test that asserted the
/// exact layout would fail on a column width and say nothing about the numbers,
/// which are what these tests are about.
std::optional<std::string> fieldOf(std::string_view output, std::string_view label) {
    std::size_t at = output.find(label);
    if (at == std::string_view::npos) {
        return std::nullopt;
    }
    at += label.size();
    const std::size_t end = output.find('\n', at);
    std::string_view value = output.substr(at, end == std::string_view::npos ? end : end - at);
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\r')) {
        value.remove_suffix(1);
    }
    return std::string{value};
}

/// The three numbers of a reported "(x, y, z) mm" field.
std::array<double, 3> pointOf(std::string_view output, std::string_view label) {
    const auto field = fieldOf(output, label);
    REQUIRE(field.has_value());
    std::array<double, 3> point{};
    std::size_t at = field->find('(');
    REQUIRE(at != std::string::npos);
    for (double& value : point) {
        value = std::stod(field->substr(at + 1));
        at = field->find_first_of(",)", at + 1);
        REQUIRE(at != std::string::npos);
    }
    return point;
}

/// The number at the front of a reported field, as the double it round-trips
/// from. The CLI prints shortest-round-trip text, so this recovers exactly the
/// double the core computed -- which is why the comparisons below can be exact.
double numberOf(std::string_view output, std::string_view label) {
    const auto field = fieldOf(output, label);
    REQUIRE(field.has_value());
    return std::stod(*field);
}

} // namespace

// --- the vocabulary is the FILE's vocabulary --------------------------------

TEST_CASE("MaterialCli_PropertyCodes_AreTheKeysTheDocumentFormatAlreadyUses", "[cli][material]") {
    // Through the real writer, not by comparing two tables. If the CLI ever
    // spelled a property differently from the file, a user would meet two names
    // for one number; this fails the moment that happens, in either direction.
    TempDir dir;
    const std::string path = documentPath(dir, "codes.bcad");
    Document document{"Part"};

    MaterialDefinition definition = withDensity(Density::fromSi(7850.0));
    definition.mechanical.youngsModulus = materials::MaterialProperty<Stress>::known(Stress::fromSi(2.1e11));
    definition.mechanical.poissonRatio = materials::MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.3));
    definition.mechanical.yieldStrength = materials::MaterialProperty<Stress>::known(Stress::fromSi(2.35e8));
    definition.mechanical.ultimateTensileStrength =
        materials::MaterialProperty<Stress>::known(Stress::fromSi(3.6e8));
    definition.mechanical.ultimateCompressiveStrength =
        materials::MaterialProperty<Stress>::known(Stress::fromSi(3.7e8));
    definition.mechanical.shearStrength = materials::MaterialProperty<Stress>::known(Stress::fromSi(1.4e8));
    definition.mechanical.elongation =
        materials::MaterialProperty<materials::Elongation>::known(materials::Elongation::of(0.26));
    definition.mechanical.hardness = materials::MaterialProperty<materials::Hardness>::known(
        materials::Hardness::of(120.0, materials::HardnessScale::Brinell));
    definition.thermal.thermalConductivity =
        materials::MaterialProperty<ThermalConductivity>::known(ThermalConductivity::fromSi(50.0));
    definition.thermal.specificHeatCapacity =
        materials::MaterialProperty<SpecificHeatCapacity>::known(SpecificHeatCapacity::fromSi(460.0));
    definition.thermal.thermalExpansion = materials::MaterialProperty<ThermalExpansionCoefficient>::known(
        ThermalExpansionCoefficient::fromSi(1.2e-5));
    definition.thermal.meltingTemperature =
        materials::MaterialProperty<Temperature>::known(Temperature::fromSi(1811.0));
    definition.thermal.electricalResistivity =
        materials::MaterialProperty<materials::ElectricalResistivity>::known(
            materials::ElectricalResistivity::ofOhmMetres(1.43e-7));
    REQUIRE(features::createMaterial(document, "Steel", definition).has_value());
    save(document, path);

    const std::string text = test::readFile(std::filesystem::path{path});
    // Every storable property: the CLI's code is a key in the file.
    for (const std::string_view code :
         {"density", "youngs_modulus", "poisson_ratio", "yield_strength", "ultimate_tensile_strength",
          "ultimate_compressive_strength", "shear_strength", "elongation", "hardness", "thermal_conductivity",
          "specific_heat_capacity", "thermal_expansion", "melting_temperature", "electrical_resistivity"}) {
        INFO("property code " << code);
        REQUIRE_THAT(text, ContainsSubstring(std::string{"\""} + std::string{code} + "\""));
    }

    // And the derived pair is in NEITHER. ADR-027 stores no shear or bulk
    // modulus, so no key exists and a file cannot claim one -- while the CLI
    // still has to name them, because it prints them.
    REQUIRE_THAT(text, !ContainsSubstring("\"shear_modulus\""));
    REQUIRE_THAT(text, !ContainsSubstring("\"bulk_modulus\""));

    const CliRun shown = runCliCommand({"material-show", path, "Steel"});
    REQUIRE(shown.exitCode == ExitCode::Success);
    REQUIRE_THAT(shown.out, ContainsSubstring("shear_modulus"));
    REQUIRE_THAT(shown.out, ContainsSubstring("bulk_modulus"));
}

// --- CLI/core equivalence: every mutation -----------------------------------

TEST_CASE("MaterialCli_Create_ProducesTheSameMaterialAsTheCoreApi", "[cli][material][equivalence]") {
    TempDir dir;
    const std::string path = documentPath(dir, "create.bcad");
    save(Document{"Part"}, path);

    const CliRun run = runCliCommand({"material-create", path, "--name", "Steel", "--designation", "Steel S235JR",
                                      "--standard", "EN 10025-2", "density", "7850"});
    REQUIRE(run.exitCode == ExitCode::Success);

    Document expected{"Part"};
    REQUIRE(features::createMaterial(expected, "Steel", withDensity(Density::fromSi(7850.0))).has_value());

    const Document actual = load(path);
    REQUIRE(features::materialIds(actual) == features::materialIds(expected));
    const MaterialId id = features::materialIds(actual).front();
    REQUIRE(features::findMaterial(actual, id)->definition() ==
            features::findMaterial(expected, id)->definition());
}

TEST_CASE("MaterialCli_Set_ProducesTheSameMaterialAsTheCoreApi", "[cli][material][equivalence]") {
    TempDir dir;
    const std::string path = documentPath(dir, "set.bcad");
    Document seed{"Part"};
    const MaterialId id = features::createMaterial(seed, "Steel", withDensity(Density::fromSi(7850.0))).value();
    save(seed, path);

    REQUIRE(runCliCommand({"material-set", path, "Steel", "youngs_modulus", "210GPa", "poisson_ratio", "0.3"})
                .exitCode == ExitCode::Success);

    Document expected{"Part"};
    REQUIRE(features::createMaterial(expected, "Steel", withDensity(Density::fromSi(7850.0))).has_value());
    materials::MechanicalProperties properties = features::findMaterial(expected, id)->definition().mechanical;
    properties.youngsModulus = materials::MaterialProperty<Stress>::known(Stress::fromSi(2.1e11));
    properties.poissonRatio = materials::MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.3));
    REQUIRE(features::setMaterialMechanical(expected, id, properties).has_value());

    REQUIRE(features::findMaterial(load(path), id)->definition() ==
            features::findMaterial(expected, id)->definition());
}

TEST_CASE("MaterialCli_Unset_MakesThePropertyUnknownAndNotZero", "[cli][material][equivalence]") {
    TempDir dir;
    const std::string path = documentPath(dir, "unset.bcad");
    Document seed{"Part"};
    const MaterialId id = features::createMaterial(seed, "Steel", withDensity(Density::fromSi(7850.0))).value();
    save(seed, path);

    REQUIRE(runCliCommand({"material-unset", path, "Steel", "density"}).exitCode == ExitCode::Success);

    Document expected{"Part"};
    REQUIRE(features::createMaterial(expected, "Steel", withDensity(Density::fromSi(7850.0))).has_value());
    REQUIRE(features::removeMaterialProperty(expected, id, MechanicalPropertyKind::Density).has_value());

    const Document actual = load(path);
    REQUIRE(features::findMaterial(actual, id)->definition() ==
            features::findMaterial(expected, id)->definition());
    // Removal is not zeroing. The property is Unknown, so a consumer reports it
    // missing rather than computing a massless solid.
    REQUIRE(features::findMaterial(actual, id)->definition().mechanical.density.isUnknown());
}

TEST_CASE("MaterialCli_Clone_GivesANewIdentityAndLeavesTheSourceAlone", "[cli][material][equivalence]") {
    TempDir dir;
    const std::string path = documentPath(dir, "clone.bcad");
    Document seed{"Part"};
    const MaterialId source = features::createMaterial(seed, "Steel", withDensity(Density::fromSi(7850.0))).value();
    save(seed, path);

    REQUIRE(runCliCommand({"material-clone", path, "Steel", "--name", "Steel2"}).exitCode == ExitCode::Success);
    REQUIRE(runCliCommand({"material-set", path, "Steel2", "density", "7700"}).exitCode == ExitCode::Success);

    const Document actual = load(path);
    const std::vector<MaterialId> ids = features::materialIds(actual);
    REQUIRE(ids.size() == 2);
    REQUIRE(ids.front() == source);
    REQUIRE(ids.back() != source);
    // Editing the clone cannot reach the source: the definition was copied by
    // value and nothing about the clone refers back.
    REQUIRE(features::findMaterial(actual, source)->definition().mechanical.density.value()->si() == 7850.0);
    REQUIRE(features::findMaterial(actual, ids.back())->definition().mechanical.density.value()->si() == 7700.0);
}

TEST_CASE("MaterialCli_AssignAndUnassign_MatchTheCoreApi", "[cli][material][equivalence]") {
    TempDir dir;
    const std::string path = documentPath(dir, "assign.bcad");
    Document seed{"Part"};
    const MaterialId id = features::createMaterial(seed, "Steel", withDensity(Density::fromSi(7850.0))).value();
    save(seed, path);

    REQUIRE(runCliCommand({"material-assign", path, "Steel"}).exitCode == ExitCode::Success);
    Document expected{"Part"};
    REQUIRE(features::createMaterial(expected, "Steel", withDensity(Density::fromSi(7850.0))).has_value());
    REQUIRE(features::assignMaterial(expected, id).has_value());
    REQUIRE(features::materialAssignment(load(path)) == features::materialAssignment(expected));

    REQUIRE(runCliCommand({"material-unassign", path}).exitCode == ExitCode::Success);
    REQUIRE(features::removeMaterialAssignment(expected).has_value());
    REQUIRE(features::materialAssignment(load(path)) == features::materialAssignment(expected));
}

TEST_CASE("MaterialCli_Delete_LeavesAnAssignmentUnresolvedRatherThanRebinding", "[cli][material][equivalence]") {
    TempDir dir;
    const std::string path = documentPath(dir, "delete.bcad");
    Document seed{"Part"};
    const MaterialId first = features::createMaterial(seed, "Steel", withDensity(Density::fromSi(7850.0))).value();
    // A second material with the SAME designation, so a loader that rebound by
    // designation would have somewhere to rebind to.
    REQUIRE(features::createMaterial(seed, "Steel2", withDensity(Density::fromSi(7700.0))).has_value());
    REQUIRE(features::assignMaterial(seed, first).has_value());
    save(seed, path);

    REQUIRE(runCliCommand({"material-delete", path, "Steel"}).exitCode == ExitCode::Success);

    const features::MaterialAssignment assignment = features::materialAssignment(load(path));
    REQUIRE(assignment.state == features::MaterialAssignmentState::Unresolved);
    REQUIRE(assignment.material == first);
}

// --- CLI/core equivalence: every query --------------------------------------

TEST_CASE("MaterialCli_Show_PrintsTheValuesTheCoreHolds", "[cli][material][equivalence]") {
    TempDir dir;
    const std::string path = documentPath(dir, "show.bcad");
    Document seed{"Part"};
    MaterialDefinition definition = withDensity(Density::fromSi(7850.0));
    definition.mechanical.youngsModulus = materials::MaterialProperty<Stress>::known(Stress::fromSi(2.1e11));
    definition.mechanical.poissonRatio = materials::MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.3));
    const MaterialId id = features::createMaterial(seed, "Steel", definition).value();
    save(seed, path);

    const CliRun run = runCliCommand({"material-show", path, "Steel"});
    REQUIRE(run.exitCode == ExitCode::Success);

    const materials::MechanicalProperties& core = features::findMaterial(seed, id)->definition().mechanical;
    REQUIRE(numberOf(run.out, "density") == core.density.value()->in(units::kg_per_m3));
    REQUIRE(numberOf(run.out, "youngs_modulus") == core.youngsModulus.value()->in(units::MPa));
    REQUIRE(numberOf(run.out, "poisson_ratio") == core.poissonRatio.value()->value());
    // The derived pair comes from the core's derivation, not from a formula
    // written out again here.
    REQUIRE(numberOf(run.out, "shear_modulus") ==
            materials::derivedShearModulus(core).value()->in(units::MPa));
    REQUIRE(numberOf(run.out, "bulk_modulus") == materials::derivedBulkModulus(core).value()->in(units::MPa));
}

TEST_CASE("MaterialCli_Show_PrintsUnknownAndNeverZero", "[cli][material]") {
    TempDir dir;
    const std::string path = documentPath(dir, "unknown.bcad");
    Document seed{"Part"};
    // Nothing but a designation: every property is Unknown.
    MaterialDefinition definition;
    definition.designation = "Mystery";
    REQUIRE(features::createMaterial(seed, "Mystery", definition).has_value());
    save(seed, path);

    const CliRun run = runCliCommand({"material-show", path, "Mystery"});
    REQUIRE(run.exitCode == ExitCode::Success);
    REQUIRE(fieldOf(run.out, "density") == "UNKNOWN");
    REQUIRE(fieldOf(run.out, "youngs_modulus") == "UNKNOWN");
    // The derived pair is Unknown too when its inputs are: a shear modulus of 0
    // would be a claim about a fluid.
    REQUIRE(fieldOf(run.out, "shear_modulus") == "UNKNOWN");
    REQUIRE(fieldOf(run.out, "melting_temperature") == "UNKNOWN");
    // Not one 0 anywhere among the values.
    REQUIRE_THAT(run.out, !ContainsSubstring(" 0 "));
}

TEST_CASE("MaterialCli_Show_MarksDerivedValuesAsDerived", "[cli][material]") {
    TempDir dir;
    const std::string path = documentPath(dir, "derived.bcad");
    Document seed{"Part"};
    MaterialDefinition definition = withDensity(Density::fromSi(7850.0));
    definition.mechanical.youngsModulus = materials::MaterialProperty<Stress>::known(Stress::fromSi(2.1e11));
    definition.mechanical.poissonRatio = materials::MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.3));
    REQUIRE(features::createMaterial(seed, "Steel", definition).has_value());
    save(seed, path);

    const CliRun run = runCliCommand({"material-show", path, "Steel"});
    REQUIRE(fieldOf(run.out, "shear_modulus").value().ends_with("(derived)"));
    REQUIRE(fieldOf(run.out, "bulk_modulus").value().ends_with("(derived)"));
    // A supplied value is NOT labelled derived, or the label would mean nothing.
    REQUIRE_FALSE(fieldOf(run.out, "youngs_modulus").value().ends_with("(derived)"));
}

TEST_CASE("MaterialCli_Show_ReportsProvenanceAndNeverInventsIt", "[cli][material][provenance]") {
    TempDir dir;
    const std::string path = documentPath(dir, "provenance.bcad");
    Document seed{"Part"};
    const MaterialId id = features::createMaterial(seed, "Steel", withDensity(Density::fromSi(7850.0))).value();

    materials::PropertyProvenance cited;
    cited.kind = materials::SourceKind::Measured;
    cited.source = "Works test report";
    cited.revision = "Rev A";
    cited.date = materials::Date::of(2024, 3, 17);
    REQUIRE(features::setMaterialPropertyProvenance(seed, id, MechanicalPropertyKind::Density, cited).has_value());
    save(seed, path);

    const CliRun run = runCliCommand({"material-show", path, "Steel", "--provenance"});
    REQUIRE(run.exitCode == ExitCode::Success);
    REQUIRE_THAT(run.out, ContainsSubstring("Works test report"));
    REQUIRE_THAT(run.out, ContainsSubstring("Rev A"));
    REQUIRE_THAT(run.out, ContainsSubstring("2024-03-17"));
    REQUIRE_THAT(run.out, ContainsSubstring(std::string{materials::toString(materials::SourceKind::Measured)}));

    // An UNCITED value reports "none". Not a fabricated source, and not a blank
    // that would read as a formatting fault: an uncited value is still a value,
    // and saying so is the whole point of a traceability report.
    REQUIRE_THAT(run.out, ContainsSubstring("none"));

    // Provenance is metadata and changes no number: the same command without
    // it prints the same values.
    const CliRun plain = runCliCommand({"material-show", path, "Steel"});
    REQUIRE(numberOf(plain.out, "density") == numberOf(run.out, "density"));
    REQUIRE_THAT(plain.out, !ContainsSubstring("Works test report"));
}

TEST_CASE("MaterialCli_Set_ClearsTheProvenanceOfAValueItChanges", "[cli][material][provenance]") {
    TempDir dir;
    const std::string path = documentPath(dir, "stale.bcad");
    Document seed{"Part"};
    const MaterialId id = features::createMaterial(seed, "Steel", withDensity(Density::fromSi(7850.0))).value();
    materials::PropertyProvenance cited;
    cited.kind = materials::SourceKind::Measured;
    cited.source = "Works test report";
    REQUIRE(features::setMaterialPropertyProvenance(seed, id, MechanicalPropertyKind::Density, cited).has_value());
    save(seed, path);

    REQUIRE(runCliCommand({"material-set", path, "Steel", "density", "7700"}).exitCode == ExitCode::Success);

    // The citation described 7850. It does not survive onto 7700, because a
    // citation for a number that is no longer there is a lie -- and the CLI
    // does not implement that rule, setMaterialMechanical does.
    const CliRun run = runCliCommand({"material-show", path, "Steel", "--provenance"});
    REQUIRE(numberOf(run.out, "density") == 7700.0);
    REQUIRE_THAT(run.out, !ContainsSubstring("Works test report"));
}

TEST_CASE("MaterialCli_Effective_ReportsTheSameStateAsTheCoreApi", "[cli][material][equivalence]") {
    TempDir dir;
    const std::string path = documentPath(dir, "effective.bcad");
    Document seed{"Part"};
    const MaterialId id = features::createMaterial(seed, "Steel", withDensity(Density::fromSi(7850.0))).value();
    save(seed, path);

    SECTION("unassigned is a resting state, not a fault") {
        const CliRun run = runCliCommand({"material-effective", path});
        REQUIRE(run.exitCode == ExitCode::Success);
        REQUIRE(fieldOf(run.out, "Assignment:") ==
                std::string{features::toString(features::MaterialAssignmentState::Unassigned)});
    }
    SECTION("resolved names the material the core resolves") {
        REQUIRE(features::assignMaterial(seed, id).has_value());
        save(seed, path);
        const CliRun run = runCliCommand({"material-effective", path});
        REQUIRE(run.exitCode == ExitCode::Success);
        REQUIRE(fieldOf(run.out, "Assignment:") ==
                std::string{features::toString(features::MaterialAssignmentState::Resolved)});
        REQUIRE_THAT(run.out, ContainsSubstring("Steel (object:1)"));
    }
    SECTION("a deleted material is a fault and reaches the exit status") {
        REQUIRE(features::assignMaterial(seed, id).has_value());
        REQUIRE(features::removeMaterial(seed, id).has_value());
        save(seed, path);
        const CliRun run = runCliCommand({"material-effective", path});
        REQUIRE(run.exitCode == ExitCode::Failure);
        REQUIRE_THAT(run.err, ContainsSubstring("material_unresolved"));
    }
}

TEST_CASE("MaterialCli_MassProperties_MatchTheCoreApiAndClosedFormGeometry", "[cli][material][equivalence]") {
    TempDir dir;
    const std::string path = documentPath(dir, "mass.bcad");

    // 100 x 50 x 20 mm at 7850 kg/m^3.
    BoxDocument box{100_mm, 50_mm, 20_mm};
    const MaterialId id =
        features::createMaterial(box.document, "Steel", withDensity(Density::fromSi(7850.0))).value();
    REQUIRE(features::assignMaterial(box.document, id).has_value());
    save(box.document, path);

    const CliRun run = runCliCommand({"mass-properties", path});
    REQUIRE(run.exitCode == ExitCode::Success);

    // Against the core, exactly: the CLI prints shortest-round-trip text, so
    // the doubles recovered here are the doubles the core produced.
    features::Regenerator regenerator;
    REQUIRE(regenerator.regenerateAll(box.document).has_value());
    const auto core = features::partMassProperties(box.document, regenerator, box.feature);
    REQUIRE(core.has_value());
    REQUIRE(numberOf(run.out, "volume") == core->volume.in(units::mm3));
    REQUIRE(numberOf(run.out, "mass") == core->mass.in(units::kg));

    // And against the geometry, independently: V = abc, m = rho V.
    const double volume = 100.0 * 50.0 * 20.0;                 // mm^3
    const double mass = 7850.0 * (volume * 1.0e-9);            // kg
    REQUIRE_THAT(numberOf(run.out, "volume"), Catch::Matchers::WithinRel(volume, 1e-9));
    REQUIRE_THAT(numberOf(run.out, "mass"), Catch::Matchers::WithinRel(mass, 1e-9));
    // The centroid of a box with a corner at the origin is at half of each
    // side. Compared within a tolerance rather than as text, because the
    // kernel's integration leaves a few ulp of residue on an exact half, and a
    // string comparison would be testing the arithmetic of the integrator
    // rather than the position of the centroid.
    const std::array<double, 3> centroid = pointOf(run.out, "centre of mass");
    REQUIRE_THAT(centroid[0], Catch::Matchers::WithinRel(50.0, 1e-12));
    REQUIRE_THAT(centroid[1], Catch::Matchers::WithinRel(25.0, 1e-12));
    REQUIRE_THAT(centroid[2], Catch::Matchers::WithinRel(10.0, 1e-12));
    // Ixx = m(b^2 + c^2)/12 about the centroid, in kg mm^2.
    const double ixx = mass * (50.0 * 50.0 + 20.0 * 20.0) / 12.0;
    REQUIRE_THAT(numberOf(run.out, "xx"), Catch::Matchers::WithinRel(ixx, 1e-9));
}

TEST_CASE("MaterialCli_MassProperties_ReportTheResultBodiesAndNotConsumedIntermediates",
          "[cli][material][mass]") {
    // FOUND BY RM-MAT-03 (P15-REFMOD-001). A bored part is a CHAIN: the extrude
    // makes a solid and the hole consumes it. Both features have a body, so
    // listing everything with a body reported the UN-BORED solid first, as
    // though the part had two bodies -- and the first of the two numbers was
    // the bounding cylinder's mass.
    //
    // features::resultFeatures() is the product's own answer to which bodies
    // are results, and it is what validate and export-step already use.
    TempDir dir;
    const std::string path = documentPath(dir, "bored.bcad");

    // 100 x 100 x 40 mm with a 40 mm diameter hole through it.
    BoxDocument box{100_mm, 100_mm, 40_mm};
    const geometry::FaceSignature startPlane =
        geometry::planeSignature(Point3D{0_mm, 0_mm, 0_mm}, Direction3D::unitZ().reversed());
    auto hole = features::HoleFeature::create(
        "Bore", {.target = FeatureId::fromValue(box.feature.value()), .face = startPlane,
                 .center = Point2D{50_mm, 50_mm}, .diameter = 40_mm});
    REQUIRE(hole.has_value());
    const ObjectId bore = box.document.addObject(std::move(*hole)).value();
    const MaterialId id =
        features::createMaterial(box.document, "Steel", withDensity(Density::fromSi(7800.0))).value();
    REQUIRE(features::assignMaterial(box.document, id).has_value());
    save(box.document, path);

    // The product says there is ONE result body, and which.
    const std::vector<ObjectId> results = features::resultFeatures(box.document);
    REQUIRE(results.size() == 1);
    REQUIRE(results.front() == bore);

    const CliRun run = runCliCommand({"mass-properties", path});
    REQUIRE(run.exitCode == ExitCode::Success);

    // ONE body reported, and it is the bored one. The un-bored solid is a
    // consumed intermediate and is not a body of this part.
    REQUIRE_THAT(run.out, ContainsSubstring("Bore (object:"));
    REQUIRE_THAT(run.out, !ContainsSubstring("Solid (object:"));

    // And the number is the BORED volume: 100 x 100 x 40 - pi x 20^2 x 40,
    // closed form, which is 12.6% less than the un-bored block.
    const double bored = 100.0 * 100.0 * 40.0 - 3.14159265358979311599796346854 * 20.0 * 20.0 * 40.0;
    REQUIRE_THAT(numberOf(run.out, "volume"), Catch::Matchers::WithinRel(bored, 1e-9));
    // Named explicitly, an intermediate still answers -- asking what the solid
    // weighed before the hole is a legitimate question, and that form is
    // unchanged.
    const CliRun named = runCliCommand({"mass-properties", path, "Solid"});
    REQUIRE(named.exitCode == ExitCode::Success);
    REQUIRE_THAT(numberOf(named.out, "volume"), Catch::Matchers::WithinRel(100.0 * 100.0 * 40.0, 1e-9));
}

TEST_CASE("MaterialCli_MassProperties_RefuseRatherThanReportZero", "[cli][material]") {
    TempDir dir;
    const std::string path = documentPath(dir, "nomass.bcad");
    BoxDocument box{10_mm, 10_mm, 10_mm};

    SECTION("with no material assigned") {
        save(box.document, path);
        const CliRun run = runCliCommand({"mass-properties", path});
        REQUIRE(run.exitCode == ExitCode::Failure);
        REQUIRE_THAT(run.err, ContainsSubstring("mass_properties_unavailable"));
        // Never a zero mass for a question that could not be answered.
        REQUIRE_THAT(run.out, !ContainsSubstring("mass              0"));
    }
    SECTION("with a material that has no density") {
        MaterialDefinition definition;
        definition.designation = "Mystery";
        const MaterialId id = features::createMaterial(box.document, "Mystery", definition).value();
        REQUIRE(features::assignMaterial(box.document, id).has_value());
        save(box.document, path);
        const CliRun run = runCliCommand({"mass-properties", path});
        REQUIRE(run.exitCode == ExitCode::Failure);
        REQUIRE_THAT(run.err, ContainsSubstring("mass_properties_unavailable"));
    }
}

TEST_CASE("MaterialCli_Completeness_MatchesTheCoreApiPerConsumer", "[cli][material][equivalence]") {
    TempDir dir;
    const std::string path = documentPath(dir, "complete.bcad");
    Document seed{"Part"};
    const MaterialId id = features::createMaterial(seed, "Steel", withDensity(Density::fromSi(7850.0))).value();
    REQUIRE(features::assignMaterial(seed, id).has_value());
    save(seed, path);

    SECTION("ready for a mass, and the exit status says so") {
        const auto core = features::materialCompleteness(seed, id, materials::ConsumerKind::MassProperties);
        REQUIRE(core.has_value());
        REQUIRE(core->ready());
        const CliRun run = runCliCommand({"material-completeness", path, "--consumer", "mass_properties"});
        REQUIRE(run.exitCode == ExitCode::Success);
        REQUIRE_THAT(run.out, ContainsSubstring("ready"));
    }
    SECTION("incomplete for a stiffness, naming the missing INPUTS") {
        const auto core = features::materialCompleteness(seed, id, materials::ConsumerKind::FeaLinearStatic);
        REQUIRE(core.has_value());
        REQUIRE_FALSE(core->ready());
        const CliRun run = runCliCommand({"material-completeness", path, "--consumer", "fea_linear_static"});
        REQUIRE(run.exitCode == ExitCode::Failure);
        REQUIRE_THAT(run.out, ContainsSubstring("incomplete"));
        // Every property the core called missing is named, with its code.
        for (const MechanicalPropertyKind kind : core->missingMechanical) {
            INFO("missing " << static_cast<int>(kind));
            REQUIRE_THAT(run.out, ContainsSubstring(std::string{cli::propertyCode(kind)}));
        }
        // It names the missing INPUT, never the derived constant: told the
        // shear modulus is unavailable a user has nothing to do about it.
        REQUIRE_THAT(run.out, !ContainsSubstring("missing_property:shear_modulus"));
    }
}

// --- selectors --------------------------------------------------------------

TEST_CASE("MaterialCli_Selector_ResolvesByIdAndByNameAndByDesignation", "[cli][material][selector]") {
    TempDir dir;
    const std::string path = documentPath(dir, "select.bcad");
    Document seed{"Part"};
    const MaterialId id = features::createMaterial(seed, "Steel", withDensity(Density::fromSi(7850.0))).value();
    save(seed, path);

    for (const std::string& selector : {std::to_string(id.value()), std::string{"Steel"},
                                        std::string{"designation:Steel S235JR"}}) {
        INFO("selector " << selector);
        const CliRun run = runCliCommand({"material-show", path, selector});
        REQUIRE(run.exitCode == ExitCode::Success);
        REQUIRE_THAT(run.out, ContainsSubstring("Steel (object:1)"));
    }
}

TEST_CASE("MaterialCli_Selector_RefusesAnAmbiguousDesignationInsteadOfPickingOne",
          "[cli][material][selector]") {
    TempDir dir;
    const std::string path = documentPath(dir, "ambiguous.bcad");
    Document seed{"Part"};
    // Two materials, one designation, DIFFERENT densities -- so picking either
    // one silently would give a different mass.
    REQUIRE(features::createMaterial(seed, "SteelA", withDensity(Density::fromSi(7850.0))).has_value());
    REQUIRE(features::createMaterial(seed, "SteelB", withDensity(Density::fromSi(7700.0))).has_value());
    save(seed, path);

    const CliRun run = runCliCommand({"material-show", path, "designation:Steel S235JR"});
    REQUIRE(run.exitCode == ExitCode::Failure);
    REQUIRE_THAT(run.err, ContainsSubstring("material_ambiguous"));
    // Both are named, with the IDs that disambiguate them, because the point of
    // refusing is that the user can now choose.
    REQUIRE_THAT(run.err, ContainsSubstring("SteelA (object:1)"));
    REQUIRE_THAT(run.err, ContainsSubstring("SteelB (object:2)"));
    // Nothing was reported: an ambiguous query answers nothing at all.
    REQUIRE(run.out.empty());

    // And an EDIT refuses on the same terms, with the same code.
    const CliRun assigned = runCliCommand({"material-assign", path, "designation:Steel S235JR"});
    REQUIRE(assigned.exitCode == ExitCode::Failure);
    REQUIRE_THAT(assigned.err, ContainsSubstring("material_ambiguous"));
    REQUIRE_FALSE(features::materialAssignment(load(path)).material.has_value());
}

TEST_CASE("MaterialCli_Selector_SeparatesNotFoundFromNotAMaterial", "[cli][material][selector]") {
    TempDir dir;
    const std::string path = documentPath(dir, "kinds.bcad");
    BoxDocument box{10_mm, 10_mm, 10_mm};
    REQUIRE(features::createMaterial(box.document, "Steel", withDensity(Density::fromSi(7850.0))).has_value());
    save(box.document, path);

    const CliRun missing = runCliCommand({"material-show", path, "Ghost"});
    REQUIRE(missing.exitCode == ExitCode::Failure);
    REQUIRE_THAT(missing.err, ContainsSubstring("material_not_found"));

    // A sketch is not a material, and saying "no such material" would send the
    // user looking for something that is right there.
    const CliRun wrongKind = runCliCommand({"material-show", path, "Profile"});
    REQUIRE(wrongKind.exitCode == ExitCode::Failure);
    REQUIRE_THAT(wrongKind.err, ContainsSubstring("not_a_material"));
    REQUIRE_THAT(wrongKind.err, ContainsSubstring("sketch"));
}

TEST_CASE("MaterialCli_Assignment_SurvivesARenameBecauseItIsHeldById", "[cli][material][selector]") {
    TempDir dir;
    const std::string path = documentPath(dir, "rename.bcad");
    Document seed{"Part"};
    const MaterialId id = features::createMaterial(seed, "Steel", withDensity(Density::fromSi(7850.0))).value();
    save(seed, path);

    // Assigned BY NAME on the command line, stored by ID.
    REQUIRE(runCliCommand({"material-assign", path, "Steel"}).exitCode == ExitCode::Success);
    Document renamed = load(path);
    REQUIRE(renamed.rename(ObjectId{id}, "Carbon").has_value());
    save(renamed, path);

    const CliRun run = runCliCommand({"material-effective", path});
    REQUIRE(run.exitCode == ExitCode::Success);
    REQUIRE_THAT(run.out, ContainsSubstring("Carbon (object:1)"));
    REQUIRE(features::materialAssignment(load(path)).material == id);
}

// --- exit codes and the stream contract -------------------------------------

TEST_CASE("MaterialCli_ExitCodes_FollowTheDocumentedContract", "[cli][material][exit]") {
    TempDir dir;
    const std::string path = documentPath(dir, "exits.bcad");
    Document seed{"Part"};
    const MaterialId id = features::createMaterial(seed, "Steel", withDensity(Density::fromSi(7850.0))).value();
    REQUIRE(features::assignMaterial(seed, id).has_value());
    save(seed, path);

    SECTION("0: the query answered and the answer is good") {
        REQUIRE(runCliCommand({"material-list", path}).exitCode == ExitCode::Success);
        REQUIRE(runCliCommand({"material-completeness", path, "--consumer", "mass_properties"}).exitCode ==
                ExitCode::Success);
    }
    SECTION("1: the query answered and the answer is not ready") {
        const CliRun run = runCliCommand({"material-completeness", path, "--consumer", "thermal_steady"});
        REQUIRE(run.exitCode == ExitCode::Failure);
        // It ANSWERED, so the report is on stdout. That is what separates this
        // from a query that could not run, which prints nothing there.
        REQUIRE_FALSE(run.out.empty());
    }
    SECTION("1: the query could not answer, and stdout stays empty") {
        const CliRun run = runCliCommand({"material-completeness", path, "Ghost"});
        REQUIRE(run.exitCode == ExitCode::Failure);
        REQUIRE(run.out.empty());
        REQUIRE_THAT(run.err, ContainsSubstring("material_not_found"));
    }
    SECTION("2: the command line could not be read") {
        REQUIRE(runCliCommand({"material-completeness", path, "--consumer", "cfd"}).exitCode ==
                ExitCode::UsageError);
        REQUIRE(runCliCommand({"material-show", path}).exitCode == ExitCode::UsageError);
        REQUIRE(runCliCommand({"material-set", path, "Steel", "density"}).exitCode == ExitCode::UsageError);
        REQUIRE(runCliCommand({"material-set", path, "Steel", "no_such_property", "1"}).exitCode ==
                ExitCode::UsageError);
    }
    SECTION("a missing file fails and does not pretend to have answered") {
        const CliRun run = runCliCommand({"material-list", cliPath(dir.path() / "absent.bcad")});
        REQUIRE(run.exitCode == ExitCode::Failure);
        REQUIRE_THAT(run.err, ContainsSubstring("document_not_loaded"));
        REQUIRE(run.out.empty());
    }
}

TEST_CASE("MaterialCli_DerivedProperties_CannotBeSet", "[cli][material]") {
    TempDir dir;
    const std::string path = documentPath(dir, "noset.bcad");
    Document seed{"Part"};
    REQUIRE(features::createMaterial(seed, "Steel", withDensity(Density::fromSi(7850.0))).has_value());
    save(seed, path);

    for (const std::string_view code : {"shear_modulus", "bulk_modulus"}) {
        INFO("property " << code);
        const CliRun run = runCliCommand({"material-set", path, "Steel", code, "80GPa"});
        REQUIRE(run.exitCode == ExitCode::Failure);
        // Refused BY NAME, saying what to do instead -- not reported as an
        // unknown property, which would be a different and misleading answer.
        REQUIRE_THAT(run.err, ContainsSubstring("ADR-027"));
        REQUIRE_THAT(run.err, ContainsSubstring("youngs_modulus"));
    }
}

TEST_CASE("MaterialCli_FailedEdit_WritesNothing", "[cli][material][atomicity]") {
    TempDir dir;
    const std::string path = documentPath(dir, "atomic.bcad");
    Document seed{"Part"};
    REQUIRE(features::createMaterial(seed, "Steel", withDensity(Density::fromSi(7850.0))).has_value());
    save(seed, path);
    const std::string before = test::readFile(std::filesystem::path{path});

    // The designation would apply, and then the density is refused: a density
    // of 0 is a claim about a massless solid and validation rejects it.
    const CliRun run = runCliCommand({"material-set", path, "Steel", "--designation", "Changed", "density", "0"});
    REQUIRE(run.exitCode != ExitCode::Success);
    // Not "mostly unchanged": byte for byte what it was. The metadata edit had
    // already been applied to the in-memory copy, and that copy was discarded.
    REQUIRE(test::readFile(std::filesystem::path{path}) == before);
}

TEST_CASE("MaterialCli_Designation_IsMatchedExactlyAndNeverFolded", "[cli][material][selector]") {
    TempDir dir;
    const std::string path = documentPath(dir, "exact.bcad");
    Document seed{"Part"};
    REQUIRE(features::createMaterial(seed, "Steel", withDensity(Density::fromSi(7850.0))).has_value());
    save(seed, path);

    // Case-folding or trimming would quietly merge materials a user meant to
    // keep apart, so findMaterialsByDesignation() does neither -- and the CLI
    // must not add it back on the way in.
    for (const std::string_view selector : {"designation:steel s235jr", "designation:STEEL S235JR",
                                            "designation: Steel S235JR", "designation:Steel S235JR "}) {
        INFO("selector " << selector);
        const CliRun run = runCliCommand({"material-show", path, selector});
        REQUIRE(run.exitCode == ExitCode::Failure);
        REQUIRE_THAT(run.err, ContainsSubstring("material_not_found"));
    }
    REQUIRE(runCliCommand({"material-show", path, "designation:Steel S235JR"}).exitCode == ExitCode::Success);
}

TEST_CASE("MaterialCli_Designation_CarriesNonAsciiTextThroughTheCommandLine", "[cli][material][selector]") {
    TempDir dir;
    const std::string path = documentPath(dir, "unicode.bcad");
    save(Document{"Part"}, path);

    // The CLI is UTF-8 throughout, so a designation is engineering text and not
    // an ASCII identifier. "Acier doux, 20 um" -- an accent and a micro sign.
    const std::string designation = "Acier doux \u00E9tir\u00E9 \u00B5m";
    REQUIRE(runCliCommand({"material-create", path, "--name", "Acier", "--designation", designation, "density",
                           "7850"})
                .exitCode == ExitCode::Success);

    const CliRun run = runCliCommand({"material-show", path, "designation:" + designation});
    REQUIRE(run.exitCode == ExitCode::Success);
    REQUIRE_THAT(run.out, ContainsSubstring(designation));
    // And it survived the file, byte for byte.
    REQUIRE(features::findMaterial(load(path), MaterialId::fromValue(1))->definition().designation == designation);
}

TEST_CASE("MaterialCli_MassProperties_RefuseUnderAConfigurationThatOverridesAParameter",
          "[cli][material][mass]") {
    TempDir dir;
    const std::string path = documentPath(dir, "override.bcad");
    BoxDocument box{100_mm, 50_mm, 20_mm};
    const MaterialId id =
        features::createMaterial(box.document, "Steel", withDensity(Density::fromSi(7850.0))).value();
    REQUIRE(features::assignMaterial(box.document, id).has_value());
    const ConfigurationId tall = box.document.createConfiguration("Tall").value();
    REQUIRE(box.document.setConfigurationOverride(tall, box.depth, 100_mm).has_value());
    save(box.document, path);

    // P15-MASS-001 refuses here, because a configuration override does not
    // currently rebuild the geometry it changes, so the volume in hand would be
    // the base configuration's. The CARRIED DEFECT must still be refused when
    // the question arrives through the CLI -- a guard that only holds in-process
    // is not a guard.
    const CliRun overridden = runCliCommand({"mass-properties", path, "--configuration", "Tall"});
    REQUIRE(overridden.exitCode == ExitCode::Failure);
    REQUIRE_THAT(overridden.err, ContainsSubstring("mass_properties_unavailable"));
    // At the base, the same document answers.
    REQUIRE(runCliCommand({"mass-properties", path}).exitCode == ExitCode::Success);
}

TEST_CASE("MaterialCli_MassProperties_RefuseAfterTheAssignedMaterialIsDeleted", "[cli][material][mass]") {
    TempDir dir;
    const std::string path = documentPath(dir, "deleted.bcad");
    BoxDocument box{10_mm, 10_mm, 10_mm};
    const MaterialId id =
        features::createMaterial(box.document, "Steel", withDensity(Density::fromSi(7850.0))).value();
    REQUIRE(features::assignMaterial(box.document, id).has_value());
    save(box.document, path);
    REQUIRE(runCliCommand({"mass-properties", path}).exitCode == ExitCode::Success);

    // Deleting the material leaves the assignment Unresolved with its intent
    // intact. The mass then has no density to use, and the honest answer is a
    // refusal -- not the last mass it happened to compute, and not zero.
    REQUIRE(runCliCommand({"material-delete", path, "Steel"}).exitCode == ExitCode::Success);
    const CliRun run = runCliCommand({"mass-properties", path});
    REQUIRE(run.exitCode == ExitCode::Failure);
    REQUIRE_THAT(run.err, ContainsSubstring("mass_properties_unavailable"));
    REQUIRE_THAT(run.out, !ContainsSubstring(" kg"));
}

TEST_CASE("MaterialCli_Report_IsDeterministic", "[cli][material][determinism]") {
    TempDir dir;
    const std::string path = documentPath(dir, "deterministic.bcad");
    Document seed{"Part"};
    // Built in reverse name order, so that any order in the report that came
    // from insertion rather than from ID would show.
    REQUIRE(features::createMaterial(seed, "Zinc", withDensity(Density::fromSi(7140.0))).has_value());
    REQUIRE(features::createMaterial(seed, "Aluminium", withDensity(Density::fromSi(2700.0))).has_value());
    save(seed, path);

    const CliRun first = runCliCommand({"material-list", path});
    const CliRun second = runCliCommand({"material-list", path});
    REQUIRE(first.out == second.out);
    // Ascending ID, which is the creation order and not the alphabet.
    REQUIRE(first.out.find("Zinc") < first.out.find("Aluminium"));
}
