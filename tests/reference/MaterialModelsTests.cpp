#include "MaterialVocabulary.hpp"
#include "cli/CliRunner.hpp"
#include "reference/Analytic.hpp"
#include "reference/ReferenceTestSupport.hpp"

#include <MaterialReferenceModels.hpp>

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Resolution.hpp>
#include <bettercad/core/materials/Completeness.hpp>
#include <bettercad/core/materials/Provenance.hpp>
#include <bettercad/core/math/RigidTransform.hpp>
#include <bettercad/features/MassProperties.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/MaterialCommands.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/io/DocumentFile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <cmath>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using bettercad::test::TempDir;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using MechKind = materials::MechanicalPropertyKind;
using ThermKind = materials::ThermalPropertyKind;
namespace analytic = bettercad::test::analytic;

// P15-REFMOD-001: the engineering-data reference models.
//
// EVERY EXPECTED VALUE HERE IS CLOSED FORM. The volumes, masses, centroids and
// inertia tensors come from `analytic::cuboid`, `analytic::cylinder`,
// `analytic::hollowCylinder`, `analytic::rotated` and `analytic::shifted`, none
// of which includes a BetterCAD header. A milestone whose subject is mass
// properties cannot take its expected masses from the code that computes them,
// and the brief makes that an automatic FAIL.
//
// The property values in the models are TEST / SYNTHETIC ENGINEERING DATA and
// each material says so; they are chosen to make this arithmetic exact.
namespace {

// Measured worst relative errors on these models, all from the runner's own
// 17-digit output: volume 6e-16, mass 5e-16, inertia diagonal 3.3e-14. The
// tolerances below are two to four orders above that, and orders BELOW any real
// defect -- a swapped inertia axis on RM-MAT-01 is out by 17%, a bounding
// cylinder on RM-MAT-03 by 80%.
constexpr double kRel = 1e-10;
/// Centroid, in mm. Some coordinates are exactly 0 and the kernel lands within
/// 4e-14 mm of them, so this is absolute.
constexpr double kPositionMm = 1e-9;
/// A product of inertia that closed form says is ZERO. Scaled by the largest
/// diagonal moment, because a product is only meaningful beside them: the worst
/// measured is 3e-16 of it.
constexpr double kZeroProductFraction = 1e-9;

struct Built {
    Document document;
    features::Regenerator regenerator{};
};

/// Builds a material reference model and regenerates it, requiring success.
Built build(reference::MaterialReferenceModelKind kind) {
    auto document = reference::buildMaterialReferenceModel(kind);
    REQUIRE(document.has_value());
    Built built{.document = std::move(*document)};
    auto report = built.regenerator.regenerateAll(built.document);
    REQUIRE(report.has_value());
    INFO("regeneration");
    REQUIRE(report->succeeded());
    return built;
}

/// The ONE result body of a model, and its mass properties.
///
/// resultFeatures() rather than every feature with a body: RM-MAT-03 is a chain
/// whose intermediate is a consumed un-bored cylinder, and this is the defect
/// that model found in the CLI.
features::PartMassProperties massOf(const Built& built) {
    const std::vector<ObjectId> results = features::resultFeatures(built.document);
    REQUIRE(results.size() == 1);
    auto properties = features::partMassProperties(built.document, built.regenerator, results.front());
    INFO("mass properties");
    REQUIRE(properties.has_value());
    return *properties;
}

/// Volume, mass and centroid against closed form, printing the error table the
/// evidence quotes.
void checkBulk(const features::PartMassProperties& actual, const analytic::UniformSolid& expected) {
    const double volume = actual.volume.si();
    const double mass = actual.mass.si();
    INFO("volume   expected " << expected.volume << " m^3, actual " << volume << ", abs "
                              << std::abs(volume - expected.volume) << ", rel "
                              << std::abs(volume - expected.volume) / expected.volume);
    INFO("mass     expected " << expected.mass << " kg, actual " << mass << ", abs "
                              << std::abs(mass - expected.mass) << ", rel "
                              << std::abs(mass - expected.mass) / expected.mass);
    CHECK_THAT(volume, WithinRel(expected.volume, kRel));
    CHECK_THAT(mass, WithinRel(expected.mass, kRel));

    const std::array<double, 3> centre{actual.centreOfMass.x.si(), actual.centreOfMass.y.si(),
                                       actual.centreOfMass.z.si()};
    for (std::size_t i = 0; i < 3; ++i) {
        INFO("centroid[" << i << "] expected " << expected.centroid[i] * 1000.0 << " mm, actual "
                         << centre[i] * 1000.0 << " mm, abs "
                         << std::abs(centre[i] - expected.centroid[i]) * 1000.0 << " mm");
        CHECK_THAT(centre[i] * 1000.0, WithinAbs(expected.centroid[i] * 1000.0, kPositionMm));
    }
}

/// The six components of an inertia tensor against closed form.
///
/// A component that is NEGLIGIBLE BESIDE THE DIAGONAL is compared absolutely,
/// and the rest relatively. Two reasons, and neither is a weakened tolerance:
///
///   * a relative comparison against an expected 0 is undefined;
///   * an expected product is not always exactly 0 even when it should be.
///     rotationAboutX(90) uses cos(pi/2) = 6.1e-17, so a rotated tensor's
///     products come out at 1e-17 rather than at 0. Comparing that noise
///     relatively against the kernel's own noise compares nothing.
///
/// A product of inertia is only meaningful beside the diagonal moments, so the
/// gate is scaled by the largest of them -- and the transformed case below
/// asserts that its Ixz is larger than 0.5 kg m^2, so the genuinely non-zero
/// products are still compared relatively and the case is not vacuous.
void checkInertia(const features::InertiaTensor& actual, const analytic::Inertia& expected,
                  std::string_view what) {
    const std::array<double, 6> got{actual.xx.si(), actual.yy.si(), actual.zz.si(),
                                    actual.xy.si(), actual.xz.si(), actual.yz.si()};
    const std::array<double, 6> want{expected.xx, expected.yy, expected.zz,
                                     expected.xy, expected.xz, expected.yz};
    constexpr std::array<std::string_view, 6> names{"Ixx", "Iyy", "Izz", "Ixy", "Ixz", "Iyz"};
    const double scale = std::max({std::abs(expected.xx), std::abs(expected.yy), std::abs(expected.zz)});
    for (std::size_t i = 0; i < 6; ++i) {
        const double absolute = std::abs(got[i] - want[i]);
        INFO(what << " " << names[i] << " expected " << want[i] << " kg m^2, actual " << got[i] << ", abs "
                  << absolute << ", rel "
                  << (want[i] == 0.0 ? absolute / scale : absolute / std::abs(want[i])));
        if (std::abs(want[i]) <= kZeroProductFraction * scale) {
            CHECK_THAT(got[i], WithinAbs(0.0, kZeroProductFraction * scale));
        } else {
            CHECK_THAT(got[i], WithinRel(want[i], kRel));
        }
    }
}

/// The text after @p label on the first line that holds it, trimmed.
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

/// The number a reported field starts with. The CLI prints shortest-round-trip
/// text, so this recovers the very double the core computed -- which is why the
/// comparisons below can be exact rather than within a tolerance.
double numberOf(std::string_view output, std::string_view label) {
    const auto field = fieldOf(output, label);
    REQUIRE(field.has_value());
    return std::stod(*field);
}

const features::MaterialDefinition& definitionOf(const Document& document, MaterialId id) {
    const features::Material* material = features::findMaterial(document, id);
    REQUIRE(material != nullptr);
    return material->definition();
}

/// Saves, loads and returns the loaded document. The real round trip.
Document roundTrip(const Document& document, const TempDir& dir, std::string_view name) {
    const std::filesystem::path path = dir.path() / std::string{name};
    REQUIRE(io::saveDocument(document, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    return std::move(*loaded);
}

/// Mass properties of the one result body of an already-loaded document,
/// RECOMPUTED. Nothing derived survives a save, so this is the only way to get
/// them and that is the point.
features::PartMassProperties recomputeMass(Document& document) {
    features::Regenerator regenerator;
    auto report = regenerator.regenerateAll(document);
    REQUIRE(report.has_value());
    REQUIRE(report->succeeded());
    const std::vector<ObjectId> results = features::resultFeatures(document);
    REQUIRE(results.size() == 1);
    auto properties = features::partMassProperties(document, regenerator, results.front());
    REQUIRE(properties.has_value());
    return *properties;
}

} // namespace

// === RM-MAT-01 — aluminium rectangular block ================================

TEST_CASE("ReferenceModel_RmMat01_BlockMassPropertiesMatchClosedForm", "[reference][material][rm-mat-01]") {
    const Built built = build(reference::MaterialReferenceModelKind::Block);
    // 200 x 300 x 500 mm at 2700 kg/m^3: V = 0.03 m^3 and m = 81 kg exactly.
    const analytic::UniformSolid expected = analytic::cuboid(0.2, 0.3, 0.5, 2700.0);
    REQUIRE(expected.volume == 0.03);
    REQUIRE(expected.mass == 81.0);

    const features::PartMassProperties actual = massOf(built);
    checkBulk(actual, expected);
    checkInertia(actual.aboutCentreOfMass, expected.centroidal, "centroidal");

    // THREE DISTINCT MOMENTS. A cube would give three equal ones and a product
    // that transposed two inertia axes would pass everything above.
    CHECK(expected.centroidal.xx > expected.centroidal.yy);
    CHECK(expected.centroidal.yy > expected.centroidal.zz);
    CHECK_THAT(actual.aboutCentreOfMass.xx.si(), WithinRel(2.295, kRel));
    CHECK_THAT(actual.aboutCentreOfMass.yy.si(), WithinRel(81.0 * (0.04 + 0.25) / 12.0, kRel));
    CHECK_THAT(actual.aboutCentreOfMass.zz.si(), WithinRel(81.0 * (0.04 + 0.09) / 12.0, kRel));

    // About the ORIGIN, by the parallel-axis theorem from the centroid.
    const analytic::Inertia aboutOrigin =
        analytic::shifted(expected.centroidal, expected.mass, expected.centroid);
    checkInertia(actual.aboutOrigin, aboutOrigin, "about the origin");
    // The products are NOT zero there, and would be missed entirely by a shift
    // that only moved the diagonal.
    CHECK(std::abs(aboutOrigin.xy) > 1.0);
}

TEST_CASE("ReferenceModel_RmMat01_EveryEngineeringPropertyResolvesExactly", "[reference][material][rm-mat-01]") {
    const Built built = build(reference::MaterialReferenceModelKind::Block);
    const Document& document = built.document;

    // The assignment, and that it resolves to the one material.
    const features::MaterialAssignment assignment = features::materialAssignment(document);
    REQUIRE(assignment.state == features::MaterialAssignmentState::Resolved);
    REQUIRE(features::materialCount(document) == 1);
    const MaterialId id = *assignment.material;

    // Mechanical, through the qualified requirement entry points rather than by
    // reading fields: this is the boundary a solver consumes.
    const auto density = features::requireDensity(document, id);
    REQUIRE(density.has_value());
    CHECK_THAT(density->si(), WithinRel(2700.0, kRel));
    const auto elastic = features::requireLinearElasticConstants(document, id);
    REQUIRE(elastic.has_value());
    CHECK_THAT(elastic->youngsModulus.si(), WithinRel(70.0e9, kRel));
    CHECK_THAT(elastic->poissonRatio.value(), WithinRel(0.33, kRel));
    // DERIVED, from closed form: G = E/(2(1+nu)), K = E/(3(1-2nu)).
    CHECK_THAT(elastic->shearModulus.si(), WithinRel(70.0e9 / (2.0 * 1.33), kRel));
    CHECK_THAT(elastic->bulkModulus.si(), WithinRel(70.0e9 / (3.0 * (1.0 - 0.66)), kRel));
    // And derived means DERIVED: the stored properties have no such slot, so
    // the definition cannot be carrying either of them.
    const features::MaterialDefinition& d = definitionOf(document, id);
    CHECK(materials::derivedShearModulus(d.mechanical).isDerived());
    CHECK(materials::derivedBulkModulus(d.mechanical).isDerived());
    CHECK(d.mechanical.youngsModulus.isKnown());

    CHECK_THAT(d.mechanical.yieldStrength.value()->si(), WithinRel(276.0e6, kRel));

    // Thermal.
    const auto conductivity = features::requireThermalConductivity(document, id);
    REQUIRE(conductivity.has_value());
    CHECK_THAT(conductivity->si(), WithinRel(167.0, kRel));
    const auto transient = features::requireTransientConductionProperties(document, id);
    REQUIRE(transient.has_value());
    CHECK_THAT(transient->specificHeatCapacity.si(), WithinRel(896.0, kRel));
    const auto expansion = features::requireThermalExpansion(document, id);
    REQUIRE(expansion.has_value());
    CHECK_THAT(expansion->si(), WithinRel(23.6e-6, kRel));

    // PROVENANCE: three properties cited from three different kinds of source,
    // so a record that moved between properties would be visible.
    const auto densityFrom = features::materialPropertyProvenance(document, id, MechKind::Density);
    REQUIRE(densityFrom.has_value());
    CHECK(densityFrom->kind == materials::SourceKind::Handbook);
    const auto modulusFrom = features::materialPropertyProvenance(document, id, MechKind::YoungsModulus);
    REQUIRE(modulusFrom.has_value());
    CHECK(modulusFrom->kind == materials::SourceKind::ManufacturerData);
    const auto conductivityFrom = features::materialPropertyProvenance(document, id, ThermKind::ThermalConductivity);
    REQUIRE(conductivityFrom.has_value());
    CHECK(conductivityFrom->kind == materials::SourceKind::Measured);
    CHECK(conductivityFrom->date == materials::Date::of(2024, 3, 17));

    // A KNOWN VALUE WITH NO PROVENANCE is not a missing value. The yield
    // strength has a number and no citation, and those are different answers.
    const auto yieldFrom = features::materialPropertyProvenance(document, id, MechKind::YieldStrength);
    REQUIRE(yieldFrom.has_value());
    CHECK(yieldFrom->empty());
    CHECK(d.mechanical.yieldStrength.isKnown());
    // Where the shear modulus is concerned there is no value AND no citation,
    // and it is still not "missing data" -- it is derived.
    CHECK(d.mechanical.density.isKnown());
    CHECK(d.thermal.electricalResistivity.isUnknown());
}

// === RM-MAT-02 — steel cylindrical shaft ====================================

TEST_CASE("ReferenceModel_RmMat02_ShaftMassPropertiesMatchClosedForm", "[reference][material][rm-mat-02]") {
    const Built built = build(reference::MaterialReferenceModelKind::Shaft);
    // r = 50 mm, h = 400 mm, 7800 kg/m^3.
    const analytic::UniformSolid expected = analytic::cylinder(0.05, 0.4, 7800.0);
    const features::PartMassProperties actual = massOf(built);
    checkBulk(actual, expected);
    checkInertia(actual.aboutCentreOfMass, expected.centroidal, "centroidal");

    // THE AXIS IS Z, and that is checked rather than assumed: the model extrudes
    // a circle drawn on the XY plane, so the axis moment must be the zz one and
    // it must be the SMALLEST of the three for a shaft longer than it is wide.
    CHECK_THAT(actual.aboutCentreOfMass.zz.si(), WithinRel(0.5 * expected.mass * 0.05 * 0.05, kRel));
    CHECK(actual.aboutCentreOfMass.zz.si() < actual.aboutCentreOfMass.xx.si());
    CHECK_THAT(actual.aboutCentreOfMass.xx.si(), WithinRel(actual.aboutCentreOfMass.yy.si(), kRel));
}

TEST_CASE("ReferenceModel_RmMat02_TransformedShaftRotatesAndShiftsCorrectly",
          "[reference][material][rm-mat-02]") {
    const Built built = build(reference::MaterialReferenceModelKind::Shaft);
    const features::PartMassProperties local = massOf(built);
    const analytic::UniformSolid expected = analytic::cylinder(0.05, 0.4, 7800.0);

    // Turn it a quarter turn about X and move it. The cylinder's axis leaves Z,
    // which is what makes this a test of the rotation rather than of a
    // translation: a transform that forgot R I R^T would leave the axis moment
    // on zz, where closed form puts it on yy.
    const RigidTransform3D motion =
        RigidTransform3D::translation(Translation3D{100_mm, 200_mm, 300_mm})
            .after(RigidTransform3D::rotation(Axis3D{.origin = Point3D{}, .direction = Direction3D::unitX()},
                                              90_deg));
    const features::PartMassProperties moved = features::transformed(local, motion);

    // Mass, volume and density are invariant under a rigid motion.
    CHECK_THAT(moved.mass.si(), WithinRel(local.mass.si(), 1e-15));
    CHECK_THAT(moved.volume.si(), WithinRel(local.volume.si(), 1e-15));

    const analytic::Rotation r = analytic::rotationAboutX(90.0);
    const std::array<double, 3> translation{0.1, 0.2, 0.3};
    const std::array<double, 3> centre = analytic::placed(expected.centroid, r, translation);
    INFO("centroid expected (" << centre[0] * 1000.0 << ", " << centre[1] * 1000.0 << ", "
                               << centre[2] * 1000.0 << ") mm");
    CHECK_THAT(moved.centreOfMass.x.si() * 1000.0, WithinAbs(centre[0] * 1000.0, kPositionMm));
    CHECK_THAT(moved.centreOfMass.y.si() * 1000.0, WithinAbs(centre[1] * 1000.0, kPositionMm));
    CHECK_THAT(moved.centreOfMass.z.si() * 1000.0, WithinAbs(centre[2] * 1000.0, kPositionMm));

    // I' = R I R^T, computed independently.
    const analytic::Inertia rotatedCentroidal = analytic::rotated(expected.centroidal, r);
    checkInertia(moved.aboutCentreOfMass, rotatedCentroidal, "rotated centroidal");
    // The axis moment really did move from zz to yy.
    CHECK_THAT(rotatedCentroidal.yy, WithinRel(0.5 * expected.mass * 0.05 * 0.05, 1e-15));

    // And about the origin, by the parallel-axis theorem from the MOVED
    // centroid. Ixz is about -0.735 kg m^2 here, so a shift that dropped the
    // products would be caught.
    const analytic::Inertia aboutOrigin = analytic::shifted(rotatedCentroidal, expected.mass, centre);
    checkInertia(moved.aboutOrigin, aboutOrigin, "about the origin after the motion");
    CHECK(std::abs(aboutOrigin.xz) > 0.5);
}

// === RM-MAT-03 — hollow steel tube ==========================================

TEST_CASE("ReferenceModel_RmMat03_TubeAccountsForTheVoid", "[reference][material][rm-mat-03]") {
    const Built built = build(reference::MaterialReferenceModelKind::Tube);
    // Ro = 60, Ri = 40, h = 300 mm, 7800 kg/m^3.
    const analytic::UniformSolid expected = analytic::hollowCylinder(0.06, 0.04, 0.3, 7800.0);
    const features::PartMassProperties actual = massOf(built);
    checkBulk(actual, expected);
    checkInertia(actual.aboutCentreOfMass, expected.centroidal, "centroidal");

    // THE VOID IS ACCOUNTED FOR. A bounding cylinder of the outer radius would
    // be 1.8 times this volume, so the check is not a tolerance question: the
    // measured volume must be nowhere near it.
    const analytic::UniformSolid boundingCylinder = analytic::cylinder(0.06, 0.3, 7800.0);
    CHECK(expected.volume < 0.56 * boundingCylinder.volume);
    CHECK(actual.volume.si() < 0.6 * boundingCylinder.volume);
    CHECK_THAT(actual.volume.si(), WithinRel(expected.volume, kRel));
    // The axis moment of a tube is NOT that of a solid rod of the same mass:
    // m(Ro^2+Ri^2)/2 against mRo^2/2. Both must be distinguishable.
    CHECK_THAT(actual.aboutCentreOfMass.zz.si(),
               WithinRel(0.5 * expected.mass * (0.06 * 0.06 + 0.04 * 0.04), kRel));
    CHECK(actual.aboutCentreOfMass.zz.si() > 0.5 * expected.mass * 0.05 * 0.05);

    // The ONE result body is the bore, not the blank the hole consumed. This is
    // the defect RM-MAT-03 found in the CLI.
    const std::vector<ObjectId> results = features::resultFeatures(built.document);
    REQUIRE(results.size() == 1);
    CHECK(built.document.findObject(results.front())->name() == "Bore");
}

// === RM-MAT-04 — multi-material document set ================================

TEST_CASE("ReferenceModel_RmMat04_EqualVolumesWithDifferentDensitiesGiveDifferentMasses",
          "[reference][material][rm-mat-04]") {
    const Built a = build(reference::MaterialReferenceModelKind::PartA);
    const Built b = build(reference::MaterialReferenceModelKind::PartB);
    const analytic::UniformSolid expectedA = analytic::cuboid(0.1, 0.1, 0.1, 2700.0);
    const analytic::UniformSolid expectedB = analytic::cuboid(0.2, 0.1, 0.05, 7800.0);

    const features::PartMassProperties massA = massOf(a);
    const features::PartMassProperties massB = massOf(b);
    checkBulk(massA, expectedA);
    checkBulk(massB, expectedB);
    checkInertia(massA.aboutCentreOfMass, expectedA.centroidal, "part A centroidal");
    checkInertia(massB.aboutCentreOfMass, expectedB.centroidal, "part B centroidal");

    // THE SAME VOLUME, on purpose: 10^6 mm^3 each. So a mass that followed the
    // geometry rather than the material would give the same answer twice.
    CHECK_THAT(massA.volume.si(), WithinRel(massB.volume.si(), kRel));
    CHECK_THAT(massB.mass.si() / massA.mass.si(), WithinRel(7800.0 / 2700.0, kRel));
    CHECK(a.document.id() != b.document.id());

    // Two documents, two materials in force. One document cannot hold two.
    CHECK(features::materialCount(a.document) == 1);
    CHECK(features::materialCount(b.document) == 1);
}

TEST_CASE("ReferenceModel_RmMat04_AssemblyKeepsOneDocumentLevelMaterialForTwoOccurrences",
          "[reference][material][rm-mat-04]") {
    const Built built = build(reference::MaterialReferenceModelKind::Assembly);

    // TWO OCCURRENCES of ONE part definition. That is what an assembly is here:
    // an occurrence carries a transform, not a material.
    const std::vector<ComponentId> components = assembly::components(built.document);
    REQUIRE(components.size() == 2);
    const auto* first = built.document.findObjectAs<assembly::Component>(ObjectId{components.front()});
    const auto* second = built.document.findObjectAs<assembly::Component>(ObjectId{components.back()});
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    // Same part, different instances, different placements.
    CHECK(first->definition().part == second->definition().part);
    CHECK(components.front() != components.back());
    CHECK_FALSE(first->definition().placement == second->definition().placement);

    // ONE material, for the document. There is no per-occurrence assignment to
    // check because ComponentDefinition has no material field -- P15-ASSIGN-001
    // has a compile-fail case proving the field is absent, which is a stronger
    // statement than any runtime check here could make.
    const features::MaterialAssignment assignment = features::materialAssignment(built.document);
    REQUIRE(assignment.state == features::MaterialAssignmentState::Resolved);
    CHECK(features::materialCount(built.document) == 1);

    // The part's own mass is answerable, and matches closed form for the 80 mm
    // cube at 7800 kg/m^3. An AGGREGATE over the two occurrences is not: the
    // product provides no aggregation, and computing one here would validate
    // this test's arithmetic rather than the product. Recorded as N/A.
    const analytic::UniformSolid expected = analytic::cuboid(0.08, 0.08, 0.08, 7800.0);
    checkBulk(massOf(built), expected);
}

// === RM-MAT-05 — custom engineering material ================================

TEST_CASE("ReferenceModel_RmMat05_CustomMaterialIsIndependentOfItsLibrarySource",
          "[reference][material][rm-mat-05]") {
    auto model = reference::buildMaterialCustomReferenceModel();
    REQUIRE(model.has_value());
    Document& document = model->document;

    // Three materials: the import, its clone, and a twin sharing a designation.
    REQUIRE(features::materialCount(document) == 3);
    CHECK(model->imported != model->custom);
    CHECK(model->custom != model->sameDesignation);

    // The import records WHERE ITS VALUES CAME FROM, and the clone carries that
    // over -- cloning does not change the values, so it does not change where
    // they came from. Neither is a view of the library: it is a compiled-in
    // constant and nothing consults it on the document's behalf.
    const features::MaterialDefinition& imported = definitionOf(document, model->imported);
    const features::MaterialDefinition& custom = definitionOf(document, model->custom);
    REQUIRE(imported.origin.has_value());
    REQUIRE(custom.origin.has_value());
    CHECK(imported.origin->entry == "al-6061-t6");
    CHECK(custom.origin->entry == "al-6061-t6");
    CHECK(imported.designation == custom.designation);

    // The part is made of the CLONE.
    CHECK(features::materialAssignment(document).material == model->custom);

    // EDIT THE CLONE. The source keeps its density; the clone takes the new one.
    const double before = definitionOf(document, model->imported).mechanical.density.value()->si();
    materials::MechanicalProperties edited = custom.mechanical;
    edited.density = materials::MaterialProperty<Density>::known(Density::fromSi(8000.0));
    REQUIRE(features::setMaterialMechanical(document, model->custom, edited).has_value());
    CHECK_THAT(definitionOf(document, model->imported).mechanical.density.value()->si(), WithinRel(before, 1e-15));
    CHECK_THAT(definitionOf(document, model->custom).mechanical.density.value()->si(), WithinRel(8000.0, 1e-15));

    // And the library entry itself is untouched, because it has no values to
    // touch: the separation is that identity comes from the library and numbers
    // come from the document.
    auto entry = materials::findLibraryMaterial(materials::builtInLibraryName(), "al-6061-t6");
    REQUIRE(entry.has_value());
    CHECK(entry->designation() == imported.designation);
}

TEST_CASE("ReferenceModel_RmMat05_MassFollowsADensityEditAndNotTheOtherWay",
          "[reference][material][rm-mat-05]") {
    Built built = build(reference::MaterialReferenceModelKind::Custom);
    const MaterialId custom = *features::materialAssignment(built.document).material;

    // 120 x 80 x 60 mm at 2700 kg/m^3.
    const analytic::UniformSolid first = analytic::cuboid(0.12, 0.08, 0.06, 2700.0);
    const features::PartMassProperties m1 = massOf(built);
    checkBulk(m1, first);

    // rho1 -> rho2, geometry untouched.
    materials::MechanicalProperties edited = definitionOf(built.document, custom).mechanical;
    edited.density = materials::MaterialProperty<Density>::known(Density::fromSi(8000.0));
    REQUIRE(features::setMaterialMechanical(built.document, custom, edited).has_value());

    const analytic::UniformSolid second = analytic::cuboid(0.12, 0.08, 0.06, 8000.0);
    const features::PartMassProperties m2 = massOf(built);
    checkBulk(m2, second);

    // NO STALE MASS. Volume and centroid unchanged; mass and inertia scale
    // exactly with the density.
    CHECK_THAT(m2.volume.si(), WithinRel(m1.volume.si(), 1e-15));
    CHECK_THAT(m2.centreOfMass.x.si(), WithinAbs(m1.centreOfMass.x.si(), 1e-15));
    CHECK_THAT(m2.mass.si() / m1.mass.si(), WithinRel(8000.0 / 2700.0, 1e-13));
    CHECK_THAT(m2.aboutCentreOfMass.xx.si() / m1.aboutCentreOfMass.xx.si(),
               WithinRel(8000.0 / 2700.0, 1e-13));
}

TEST_CASE("ReferenceModel_RmMat05_DerivedConstantsRecomputeAndMassDoesNot",
          "[reference][material][rm-mat-05]") {
    Built built = build(reference::MaterialReferenceModelKind::Custom);
    const MaterialId custom = *features::materialAssignment(built.document).material;
    const double massBefore = massOf(built).mass.si();

    const auto before = features::requireLinearElasticConstants(built.document, custom);
    REQUIRE(before.has_value());
    CHECK_THAT(before->shearModulus.si(), WithinRel(70.0e9 / (2.0 * 1.33), kRel));

    // E and nu -> 200 GPa and 0.28.
    materials::MechanicalProperties edited = definitionOf(built.document, custom).mechanical;
    edited.youngsModulus = materials::MaterialProperty<Stress>::known(Stress::fromSi(200.0e9));
    edited.poissonRatio = materials::MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.28));
    REQUIRE(features::setMaterialMechanical(built.document, custom, edited).has_value());

    // G and K RECOMPUTE from closed form. There is no slot they could have been
    // stored in, so a stale value is not merely unlikely -- it is impossible.
    const auto after = features::requireLinearElasticConstants(built.document, custom);
    REQUIRE(after.has_value());
    CHECK_THAT(after->shearModulus.si(), WithinRel(200.0e9 / (2.0 * 1.28), kRel));
    CHECK_THAT(after->bulkModulus.si(), WithinRel(200.0e9 / (3.0 * (1.0 - 0.56)), kRel));

    // And the MASS did not move: a stiffness edit is not a mass edit.
    CHECK_THAT(massOf(built).mass.si(), WithinRel(massBefore, 1e-15));
}

TEST_CASE("ReferenceModel_RmMat05_ADuplicateDesignationNeverRebinds", "[reference][material][rm-mat-05]") {
    auto model = reference::buildMaterialCustomReferenceModel();
    REQUIRE(model.has_value());
    Document& document = model->document;

    // The twin shares the imported material's DESIGNATION and is a different
    // material. Two objects cannot share a NAME, so this is what a duplicate
    // engineering label actually is.
    CHECK(definitionOf(document, model->sameDesignation).designation ==
          definitionOf(document, model->imported).designation);
    const std::vector<MaterialId> byDesignation =
        features::findMaterialsByDesignation(document, definitionOf(document, model->imported).designation);
    // The import, the clone and the twin all answer to it: three, and the core
    // returns all three rather than choosing.
    CHECK(byDesignation.size() == 3);

    // Delete the assigned material. The assignment goes UNRESOLVED keeping its
    // intent, and the two remaining same-designation materials do NOT inherit
    // it -- through a save and a load, twice.
    const MaterialId assigned = model->custom;
    REQUIRE(features::removeMaterial(document, assigned).has_value());
    features::MaterialAssignment state = features::materialAssignment(document);
    CHECK(state.state == features::MaterialAssignmentState::Unresolved);
    CHECK(state.material == assigned);

    TempDir dir;
    Document once = roundTrip(document, dir, "twin-once.bcad");
    state = features::materialAssignment(once);
    CHECK(state.state == features::MaterialAssignmentState::Unresolved);
    CHECK(state.material == assigned);
    Document twice = roundTrip(once, dir, "twin-twice.bcad");
    state = features::materialAssignment(twice);
    CHECK(state.state == features::MaterialAssignmentState::Unresolved);
    CHECK(state.material == assigned);
    CHECK(features::materialCount(twice) == 2);
}

// === RM-MAT-06 — missing-property failure model =============================

TEST_CASE("ReferenceModel_RmMat06_ConsumerCompletenessIsPerConsumerAndNeverDefaulted",
          "[reference][material][rm-mat-06]") {
    const Built built = build(reference::MaterialReferenceModelKind::Incomplete);
    const Document& document = built.document;
    const MaterialId partial = *features::materialAssignment(document).material;

    // READY for a mass: the density is known and a mass needs nothing else.
    const auto mass = features::materialCompleteness(document, partial, materials::ConsumerKind::MassProperties);
    REQUIRE(mass.has_value());
    CHECK(mass->state == materials::CompletenessState::Ready);

    // INCOMPLETE for a linear-static stiffness, missing the POISSON RATIO --
    // and no nu = 0.3 appears anywhere.
    const auto fea = features::materialCompleteness(document, partial, materials::ConsumerKind::FeaLinearStatic);
    REQUIRE(fea.has_value());
    CHECK(fea->state == materials::CompletenessState::Incomplete);
    REQUIRE(fea->missingMechanical.size() == 1);
    CHECK(fea->missingMechanical.front() == MechKind::PoissonRatio);
    CHECK(definitionOf(document, partial).mechanical.poissonRatio.isUnknown());
    CHECK_FALSE(features::requireLinearElasticConstants(document, partial).has_value());

    // INCOMPLETE for transient conduction, missing the CONDUCTIVITY; the
    // density and the specific heat it also needs are present.
    const auto thermal =
        features::materialCompleteness(document, partial, materials::ConsumerKind::ThermalTransient);
    REQUIRE(thermal.has_value());
    CHECK(thermal->state == materials::CompletenessState::Incomplete);
    REQUIRE(thermal->missingThermal.size() == 1);
    CHECK(thermal->missingThermal.front() == ThermKind::ThermalConductivity);
    CHECK(thermal->presentMechanical == std::vector<MechKind>{MechKind::Density});
    CHECK_FALSE(features::requireThermalConductivity(document, partial).has_value());

    // Three different answers from one material, which is the whole point:
    // incomplete for FEA is not incomplete for a mass.
    CHECK(mass->state != fea->state);
}

TEST_CASE("ReferenceModel_RmMat06_MassStillSucceedsForAnIncompleteMaterial",
          "[reference][material][rm-mat-06]") {
    const Built built = build(reference::MaterialReferenceModelKind::Incomplete);
    // 100^3 mm at 2700 kg/m^3. An incomplete material is not a broken document.
    checkBulk(massOf(built), analytic::cuboid(0.1, 0.1, 0.1, 2700.0));
}

TEST_CASE("ReferenceModel_RmMat06_NoDensityMakesTheMassAnExplicitFailure",
          "[reference][material][rm-mat-06]") {
    auto model = reference::buildMaterialIncompleteReferenceModel();
    REQUIRE(model.has_value());
    Built built{.document = std::move(model->document)};
    REQUIRE(built.regenerator.regenerateAll(built.document)->succeeded());

    // Assign the material that has NO DENSITY.
    REQUIRE(features::assignMaterial(built.document, model->withoutDensity).has_value());
    const std::vector<ObjectId> results = features::resultFeatures(built.document);
    REQUIRE(results.size() == 1);
    const auto properties = features::partMassProperties(built.document, built.regenerator, results.front());

    // Refused, saying which property is missing. NEVER a zero mass.
    REQUIRE_FALSE(properties.has_value());
    CHECK_THAT(properties.error().message, ContainsSubstring("density"));
    CHECK_FALSE(features::requireDensity(built.document, model->withoutDensity).has_value());
    // The geometry is sound; only the material is short of data.
    CHECK(built.regenerator.body(results.front()) != nullptr);
}

TEST_CASE("ReferenceModel_RmMat06_InconsistentStrengthsReportInvalidRatherThanIncomplete",
          "[reference][material][rm-mat-06]") {
    auto model = reference::buildMaterialIncompleteReferenceModel();
    REQUIRE(model.has_value());
    const Document& document = model->document;

    // Every property a yield-strength consumer requires is PRESENT, so this is
    // not Incomplete. The ultimate tensile strength is below the yield
    // strength, which cannot happen in a real tensile test, so it is INVALID.
    const auto report =
        features::materialCompleteness(document, model->inconsistent, materials::ConsumerKind::FeaYieldStrength);
    REQUIRE(report.has_value());
    CHECK(report->missingMechanical.empty());
    CHECK(report->missingThermal.empty());
    CHECK(report->state == materials::CompletenessState::Invalid);
    const bool inconsistent =
        std::ranges::any_of(report->issues, [](const materials::MaterialIssue& issue) {
            return issue.kind == materials::IssueKind::InconsistentValues;
        });
    CHECK(inconsistent);
    // NO SILENT CORRECTION: the values are still exactly what was entered.
    const features::MaterialDefinition& d = definitionOf(document, model->inconsistent);
    CHECK_THAT(d.mechanical.yieldStrength.value()->si(), WithinRel(250.0e6, kRel));
    CHECK_THAT(d.mechanical.ultimateTensileStrength.value()->si(), WithinRel(200.0e6, kRel));
}

TEST_CASE("ReferenceModel_MaterialModels_CoverReadyIncompleteAndInvalid", "[reference][material]") {
    // The suite collectively reaches all three states, which is what makes the
    // consumer matrix evidence rather than an assertion about one material.
    const Built block = build(reference::MaterialReferenceModelKind::Block);
    const MaterialId aluminium = *features::materialAssignment(block.document).material;
    for (const materials::ConsumerKind consumer :
         {materials::ConsumerKind::MassProperties, materials::ConsumerKind::FeaLinearStatic,
          materials::ConsumerKind::FeaLinearStaticWithGravity, materials::ConsumerKind::FeaYieldStrength,
          materials::ConsumerKind::ThermalSteady, materials::ConsumerKind::ThermalTransient,
          materials::ConsumerKind::ThermoMechanical}) {
        INFO("consumer " << static_cast<int>(consumer));
        const auto report = features::materialCompleteness(block.document, aluminium, consumer);
        REQUIRE(report.has_value());
        // RM-MAT-01 is fully characterised, so every consumer is Ready.
        CHECK(report->state == materials::CompletenessState::Ready);
    }

    auto incomplete = reference::buildMaterialIncompleteReferenceModel();
    REQUIRE(incomplete.has_value());
    CHECK(features::materialCompleteness(incomplete->document, incomplete->partial,
                                         materials::ConsumerKind::FeaLinearStatic)
              ->state == materials::CompletenessState::Incomplete);
    CHECK(features::materialCompleteness(incomplete->document, incomplete->inconsistent,
                                         materials::ConsumerKind::FeaYieldStrength)
              ->state == materials::CompletenessState::Invalid);
}

// === configuration / assignment ==============================================

TEST_CASE("ReferenceModel_MaterialModels_AssignmentSurvivesConfigurationSwitching",
          "[reference][material][configuration]") {
    // ADR-026 makes an assignment configuration-INDEPENDENT: a configuration
    // overrides free PARAMETER values, and a MaterialId is not one. So the
    // brief's first branch applies -- two configurations, switched A to B to A,
    // and the material identity must not move.
    Built built = build(reference::MaterialReferenceModelKind::Block);
    Document& document = built.document;
    const MaterialId assigned = *features::materialAssignment(document).material;

    const ConfigurationId tall = document.createConfiguration("Tall").value();
    const ConfigurationId plain = document.createConfiguration("Plain").value();

    const auto assignmentNow = [&] { return features::materialAssignment(document); };
    for (const std::optional<ConfigurationId> which : {std::optional<ConfigurationId>{},
                                                       std::optional<ConfigurationId>{tall},
                                                       std::optional<ConfigurationId>{plain},
                                                       std::optional<ConfigurationId>{tall},
                                                       std::optional<ConfigurationId>{}}) {
        REQUIRE(document.setActiveConfiguration(which).has_value());
        const features::MaterialAssignment state = assignmentNow();
        CHECK(state.state == features::MaterialAssignmentState::Resolved);
        CHECK(state.material == assigned);
    }
    // Unchanged identity, not merely an unchanged name.
    CHECK(features::materialCount(document) == 1);
    CHECK(definitionOf(document, assigned).designation == "Aluminium 6061-T6");
}

TEST_CASE("ReferenceModel_MaterialModels_MassRefusesUnderAConfigurationThatOverridesAParameter",
          "[reference][material][configuration]") {
    // The CARRIED regeneration defect: a configuration override does not rebuild
    // the geometry it changes, so the volume in hand would be the base
    // configuration's. P15-MASS-001 refuses rather than reporting a wrong mass
    // as a right one, and the reference suite pins that refusal.
    Built built = build(reference::MaterialReferenceModelKind::Block);
    Document& document = built.document;
    const auto item = document.findByName("block_h");
    REQUIRE(item.has_value());
    const auto parameter = document.asParameter(*item);
    REQUIRE(parameter.has_value());

    const ConfigurationId taller = document.createConfiguration("Taller").value();
    REQUIRE(document.setConfigurationOverride(taller, *parameter, 600_mm).has_value());
    REQUIRE(document.setActiveConfiguration(taller).has_value());

    const std::vector<ObjectId> results = features::resultFeatures(document);
    REQUIRE(results.size() == 1);
    const auto refused = features::partMassProperties(document, built.regenerator, results.front());
    CHECK_FALSE(refused.has_value());
    // At the base it answers again, so the refusal is about the override and
    // not about the document.
    REQUIRE(document.setActiveConfiguration(std::nullopt).has_value());
    CHECK(features::partMassProperties(document, built.regenerator, results.front()).has_value());
}

// === model-change recomputation ==============================================

TEST_CASE("ReferenceModel_MaterialModels_GeometryChangeRecomputesEveryMassProperty",
          "[reference][material][recompute]") {
    // A GEOMETRY parameter changes and every derived quantity must follow, each
    // against closed form for the NEW dimension -- not against the old answer.
    Built built = build(reference::MaterialReferenceModelKind::Shaft);
    Document& document = built.document;
    checkBulk(massOf(built), analytic::cylinder(0.05, 0.4, 7800.0));

    const auto item = document.findByName("shaft_h");
    REQUIRE(item.has_value());
    const auto parameter = document.asParameter(*item);
    REQUIRE(parameter.has_value());
    REQUIRE(document.setParameterValue(*parameter, 500_mm).has_value());
    REQUIRE(built.regenerator.regenerateAll(document)->succeeded());

    // h = 400 -> 500 mm. V, m, the centroid and BOTH transverse moments move;
    // the axis moment moves only because the mass did.
    const analytic::UniformSolid longer = analytic::cylinder(0.05, 0.5, 7800.0);
    const features::PartMassProperties after = massOf(built);
    checkBulk(after, longer);
    checkInertia(after.aboutCentreOfMass, longer.centroidal, "centroidal after the change");
    CHECK_THAT(after.centreOfMass.z.si() * 1000.0, WithinAbs(250.0, kPositionMm));
    CHECK_THAT(after.volume.si() / analytic::cylinder(0.05, 0.4, 7800.0).volume, WithinRel(1.25, 1e-12));
}

// === rename invariance =======================================================

TEST_CASE("ReferenceModel_MaterialModels_RenamingAMaterialChangesNothingButItsName",
          "[reference][material][rename]") {
    Built built = build(reference::MaterialReferenceModelKind::Block);
    Document& document = built.document;
    const MaterialId id = *features::materialAssignment(document).material;
    const features::MaterialDefinition before = definitionOf(document, id);
    const features::PartMassProperties massBefore = massOf(built);
    const auto elasticBefore = features::requireLinearElasticConstants(document, id);
    const auto conductivityBefore = features::requireThermalConductivity(document, id);
    REQUIRE(elasticBefore.has_value());
    REQUIRE(conductivityBefore.has_value());

    REQUIRE(document.rename(ObjectId{id}, "Alloy").has_value());

    // The ID, the assignment and every value are exactly as they were.
    CHECK(features::materialAssignment(document).material == id);
    CHECK(definitionOf(document, id) == before);
    const features::PartMassProperties massAfter = massOf(built);
    CHECK_THAT(massAfter.mass.si(), WithinRel(massBefore.mass.si(), 1e-15));
    CHECK_THAT(massAfter.aboutCentreOfMass.xx.si(), WithinRel(massBefore.aboutCentreOfMass.xx.si(), 1e-15));
    CHECK_THAT(features::requireLinearElasticConstants(document, id)->shearModulus.si(),
               WithinRel(elasticBefore->shearModulus.si(), 1e-15));
    CHECK_THAT(features::requireThermalConductivity(document, id)->si(),
               WithinRel(conductivityBefore->si(), 1e-15));
    // Only the displayed name moved.
    CHECK(document.findObject(ObjectId{id})->name() == "Alloy");
}

// === persistence, and derived state recomputed after load ====================

TEST_CASE("ReferenceModel_MaterialModels_EveryModelRoundTripsAndRecomputesItsDerivedState",
          "[reference][material][io]") {
    TempDir dir;
    struct Case {
        reference::MaterialReferenceModelKind kind;
        std::string_view file;
        analytic::UniformSolid expected;
    };
    const std::array cases{
        Case{reference::MaterialReferenceModelKind::Block, "block.bcad", analytic::cuboid(0.2, 0.3, 0.5, 2700.0)},
        Case{reference::MaterialReferenceModelKind::Shaft, "shaft.bcad", analytic::cylinder(0.05, 0.4, 7800.0)},
        Case{reference::MaterialReferenceModelKind::Tube, "tube.bcad",
             analytic::hollowCylinder(0.06, 0.04, 0.3, 7800.0)},
        Case{reference::MaterialReferenceModelKind::PartA, "part_a.bcad", analytic::cuboid(0.1, 0.1, 0.1, 2700.0)},
        Case{reference::MaterialReferenceModelKind::PartB, "part_b.bcad",
             analytic::cuboid(0.2, 0.1, 0.05, 7800.0)},
        Case{reference::MaterialReferenceModelKind::Custom, "custom.bcad",
             analytic::cuboid(0.12, 0.08, 0.06, 2700.0)},
        Case{reference::MaterialReferenceModelKind::Incomplete, "incomplete.bcad",
             analytic::cuboid(0.1, 0.1, 0.1, 2700.0)},
    };

    for (const Case& one : cases) {
        INFO("model " << one.file);
        auto original = reference::buildMaterialReferenceModel(one.kind);
        REQUIRE(original.has_value());
        const features::MaterialAssignment before = features::materialAssignment(*original);
        const std::size_t materials = features::materialCount(*original);

        Document loaded = roundTrip(*original, dir, one.file);

        // CANONICAL INTENT survives: the identity, the assignment, the metadata,
        // the values, the known/unknown states and the provenance.
        CHECK(features::materialCount(loaded) == materials);
        const features::MaterialAssignment after = features::materialAssignment(loaded);
        CHECK(after.state == before.state);
        CHECK(after.material == before.material);
        for (const MaterialId id : features::materialIds(loaded)) {
            CHECK(definitionOf(loaded, id) == definitionOf(*original, id));
        }

        // DERIVED STATE IS RECOMPUTED, not read back: the mass is regenerated
        // from the loaded intent and compared against CLOSED FORM, which is the
        // only way to prove the file carried no stale authority.
        checkBulk(recomputeMass(loaded), one.expected);
    }
}

TEST_CASE("ReferenceModel_MaterialModels_RichModelsAreStableThroughTwoRoundTrips",
          "[reference][material][io]") {
    TempDir dir;
    for (const auto kind : {reference::MaterialReferenceModelKind::Assembly,
                            reference::MaterialReferenceModelKind::Custom,
                            reference::MaterialReferenceModelKind::Incomplete}) {
        auto original = reference::buildMaterialReferenceModel(kind);
        REQUIRE(original.has_value());
        const std::filesystem::path first = dir.path() / "twice-first.bcad";
        const std::filesystem::path second = dir.path() / "twice-second.bcad";

        REQUIRE(io::saveDocument(*original, first).has_value());
        auto once = io::loadDocument(first);
        REQUIRE(once.has_value());
        REQUIRE(io::saveDocument(*once, second).has_value());
        auto twice = io::loadDocument(second);
        REQUIRE(twice.has_value());

        // BYTE IDENTICAL: save, load, save gives the same file, so the loader
        // adds no normalisation of its own (P15-PERSIST-001).
        CHECK(test::readFile(first) == test::readFile(second));
        // And the canonical state is stable across both hops.
        CHECK(features::materialIds(*twice) == features::materialIds(*original));
        for (const MaterialId id : features::materialIds(*twice)) {
            CHECK(definitionOf(*twice, id) == definitionOf(*original, id));
        }
        CHECK(features::materialAssignment(*twice) == features::materialAssignment(*original));
    }
}

// === determinism =============================================================

TEST_CASE("ReferenceModel_MaterialModels_AreDeterministic", "[reference][material][determinism]") {
    TempDir dir;
    for (const reference::MaterialReferenceModelInfo& info : reference::kMaterialReferenceModels) {
        INFO("model " << info.id << " " << info.name);
        auto first = reference::buildMaterialReferenceModel(info.kind);
        auto second = reference::buildMaterialReferenceModel(info.kind);
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());

        // The same IDs, the same values, the same assignment: built twice from
        // clean, nothing depends on allocation order or on a previous run.
        CHECK(first->id() == second->id());
        CHECK(features::materialIds(*first) == features::materialIds(*second));
        for (const MaterialId id : features::materialIds(*first)) {
            CHECK(definitionOf(*first, id) == definitionOf(*second, id));
        }
        CHECK(features::materialAssignment(*first) == features::materialAssignment(*second));

        // And the same bytes. Two saves of two independently built documents,
        // which is a stronger statement than two saves of one.
        const std::filesystem::path a = dir.path() / (std::string{info.fileStem} + "-a.bcad");
        const std::filesystem::path b = dir.path() / (std::string{info.fileStem} + "-b.bcad");
        REQUIRE(io::saveDocument(*first, a).has_value());
        REQUIRE(io::saveDocument(*second, b).has_value());
        CHECK(test::readFile(a) == test::readFile(b));

        // The geometry is deterministic too, to the last bit of every property.
        Built one{.document = std::move(*first)};
        Built two{.document = std::move(*second)};
        REQUIRE(one.regenerator.regenerateAll(one.document)->succeeded());
        REQUIRE(two.regenerator.regenerateAll(two.document)->succeeded());
        const auto printOne = reference::fingerprint(one.document, one.regenerator);
        const auto printTwo = reference::fingerprint(two.document, two.regenerator);
        REQUIRE(printOne.has_value());
        REQUIRE(printTwo.has_value());
        CHECK(*printOne == *printTwo);
    }
}

TEST_CASE("ReferenceModel_MaterialModels_CommandsDriveTheSameStateAndUndoRestoresIt",
          "[reference][material][commands]") {
    // The PRODUCTION command path, because that is what a GUI uses. And the
    // point of the undo check is that the mass is RECOMPUTED rather than
    // restored: no mass is in the history, because none is canonical.
    Built built = build(reference::MaterialReferenceModelKind::Custom);
    Document& document = built.document;
    const MaterialId custom = *features::materialAssignment(document).material;
    const double before = massOf(built).mass.si();

    features::MaterialDefinition edited = definitionOf(document, custom);
    edited.mechanical.density = materials::MaterialProperty<Density>::known(Density::fromSi(8000.0));
    features::EditMaterialCommand edit{custom, edited};
    REQUIRE(edit.execute(document).has_value());
    const double after = massOf(built).mass.si();
    CHECK_THAT(after / before, WithinRel(8000.0 / 2700.0, 1e-13));

    REQUIRE(edit.undo(document).has_value());
    CHECK_THAT(massOf(built).mass.si(), WithinRel(before, 1e-15));
    // The ID never moved: editing content cannot allocate a new material.
    CHECK(features::materialAssignment(document).material == custom);

    REQUIRE(edit.redo(document).has_value());
    CHECK_THAT(massOf(built).mass.si(), WithinRel(after, 1e-15));
    CHECK(features::materialAssignment(document).material == custom);

    // Assign and unassign through their commands too.
    features::RemoveMaterialAssignmentCommand remove;
    REQUIRE(remove.execute(document).has_value());
    CHECK(features::materialAssignment(document).state == features::MaterialAssignmentState::Unassigned);
    REQUIRE(remove.undo(document).has_value());
    CHECK(features::materialAssignment(document).material == custom);
}

// === CLI / core equivalence, on the reference models =========================

TEST_CASE("ReferenceModel_MaterialModels_CliReportsWhatTheCoreAndClosedFormBothSay",
          "[reference][material][cli]") {
    // STRUCTURED comparison, not a string match: each number is parsed out of
    // the CLI's output and compared against the core's own double AND against
    // closed form. A CLI that printed a plausible number in the wrong unit
    // would pass a substring check and fail this.
    using bettercad::test::CliRun;
    using bettercad::test::cliPath;
    using bettercad::test::runCliCommand;

    TempDir dir;
    struct Case {
        reference::MaterialReferenceModelKind kind;
        std::string_view file;
        std::string_view material;
        analytic::UniformSolid expected;
    };
    const std::array cases{
        Case{reference::MaterialReferenceModelKind::Block, "cli_block.bcad", "Aluminium",
             analytic::cuboid(0.2, 0.3, 0.5, 2700.0)},
        Case{reference::MaterialReferenceModelKind::Shaft, "cli_shaft.bcad", "Steel",
             analytic::cylinder(0.05, 0.4, 7800.0)},
        Case{reference::MaterialReferenceModelKind::Tube, "cli_tube.bcad", "Steel",
             analytic::hollowCylinder(0.06, 0.04, 0.3, 7800.0)},
        Case{reference::MaterialReferenceModelKind::PartA, "cli_part_a.bcad", "Aluminium",
             analytic::cuboid(0.1, 0.1, 0.1, 2700.0)},
        Case{reference::MaterialReferenceModelKind::PartB, "cli_part_b.bcad", "Steel",
             analytic::cuboid(0.2, 0.1, 0.05, 7800.0)},
    };

    for (const Case& one : cases) {
        INFO("model " << one.file);
        Built built = build(one.kind);
        const std::string path = cliPath(dir.path() / std::string{one.file});
        REQUIRE(io::saveDocument(built.document, std::filesystem::path{path}).has_value());
        const MaterialId id = *features::materialAssignment(built.document).material;
        const features::PartMassProperties core = massOf(built);
        const features::MaterialDefinition& d = definitionOf(built.document, id);

        // MASS PROPERTIES. Exactly the core's doubles, and within tolerance of
        // closed form.
        const CliRun mass = runCliCommand({"mass-properties", path});
        REQUIRE(mass.exitCode == cli::ExitCode::Success);
        CHECK(numberOf(mass.out, "volume") == core.volume.in(units::mm3));
        CHECK(numberOf(mass.out, "mass") == core.mass.in(units::kg));
        CHECK_THAT(numberOf(mass.out, "volume") * 1e-9, WithinRel(one.expected.volume, kRel));
        CHECK_THAT(numberOf(mass.out, "mass"), WithinRel(one.expected.mass, kRel));
        // The inertia the CLI prints is in kg mm^2; closed form is in kg m^2.
        CHECK_THAT(numberOf(mass.out, "xx") * 1e-6, WithinRel(one.expected.centroidal.xx, kRel));

        // ENGINEERING PROPERTIES. The density, E and nu the CLI prints are the
        // core's, and the derived pair is the core's derivation.
        const CliRun show = runCliCommand({"material-show", path, one.material});
        REQUIRE(show.exitCode == cli::ExitCode::Success);
        CHECK(numberOf(show.out, "density") == d.mechanical.density.value()->in(units::kg_per_m3));
        CHECK(numberOf(show.out, "youngs_modulus") == d.mechanical.youngsModulus.value()->in(units::MPa));
        CHECK(numberOf(show.out, "poisson_ratio") == d.mechanical.poissonRatio.value()->value());
        CHECK(numberOf(show.out, "shear_modulus") ==
              materials::derivedShearModulus(d.mechanical).value()->in(units::MPa));
        if (d.thermal.thermalConductivity.hasValue()) {
            CHECK(numberOf(show.out, "thermal_conductivity") ==
                  d.thermal.thermalConductivity.value()->in(units::W_per_m_K));
            CHECK(numberOf(show.out, "specific_heat_capacity") ==
                  d.thermal.specificHeatCapacity.value()->in(units::J_per_kg_K));
        }
        // The SYNTHETIC label reaches the user, so nobody reads these as
        // sourced datasheet figures.
        CHECK_THAT(show.out, ContainsSubstring("TEST / SYNTHETIC ENGINEERING DATA"));

        // THE EFFECTIVE MATERIAL is the one the core resolves, by ID.
        const CliRun effective = runCliCommand({"material-effective", path});
        REQUIRE(effective.exitCode == cli::ExitCode::Success);
        CHECK_THAT(effective.out, ContainsSubstring(std::string{one.material} + " (object:" +
                                                    std::to_string(id.value()) + ")"));

        // COMPLETENESS, per consumer, agreeing with the core's own report.
        for (const auto consumer : {materials::ConsumerKind::MassProperties,
                                    materials::ConsumerKind::FeaLinearStatic,
                                    materials::ConsumerKind::ThermalSteady}) {
            const auto report = features::materialCompleteness(built.document, id, consumer);
            REQUIRE(report.has_value());
            const std::string code{cli::consumerCode(consumer)};
            const CliRun completeness = runCliCommand({"material-completeness", path, "--consumer", code});
            CHECK((completeness.exitCode == cli::ExitCode::Success) == report->ready());
            CHECK_THAT(completeness.out, ContainsSubstring(std::string{cli::stateCode(report->state)}));
        }
    }
}

TEST_CASE("ReferenceModel_RmMat06_CliReportsTheMissingPropertyAndNeverADefault",
          "[reference][material][cli]") {
    using bettercad::test::CliRun;
    using bettercad::test::cliPath;
    using bettercad::test::runCliCommand;

    TempDir dir;
    auto model = reference::buildMaterialIncompleteReferenceModel();
    REQUIRE(model.has_value());
    const std::string path = cliPath(dir.path() / "cli_incomplete.bcad");
    REQUIRE(io::saveDocument(model->document, std::filesystem::path{path}).has_value());

    // Incomplete for a stiffness, naming the POISSON RATIO, exit 1.
    const CliRun fea = runCliCommand({"material-completeness", path, "--consumer", "fea_linear_static"});
    CHECK(fea.exitCode == cli::ExitCode::Failure);
    CHECK_THAT(fea.out, ContainsSubstring("incomplete"));
    CHECK_THAT(fea.out, ContainsSubstring("missing_property:poisson_ratio"));
    // Incomplete for transient conduction, naming the CONDUCTIVITY.
    const CliRun thermal = runCliCommand({"material-completeness", path, "--consumer", "thermal_transient"});
    CHECK(thermal.exitCode == cli::ExitCode::Failure);
    CHECK_THAT(thermal.out, ContainsSubstring("missing_property:thermal_conductivity"));
    // READY for a mass, exit 0, from the very same material.
    CHECK(runCliCommand({"material-completeness", path, "--consumer", "mass_properties"}).exitCode ==
          cli::ExitCode::Success);

    // The unknown properties print UNKNOWN. No nu = 0.3 and no default k.
    const CliRun show = runCliCommand({"material-show", path, "Partial"});
    REQUIRE(show.exitCode == cli::ExitCode::Success);
    CHECK(fieldOf(show.out, "poisson_ratio") == "UNKNOWN");
    CHECK(fieldOf(show.out, "thermal_conductivity") == "UNKNOWN");
    CHECK_THAT(show.out, !ContainsSubstring("0.3"));
    // And the derived pair is UNKNOWN too, because one of its inputs is.
    CHECK(fieldOf(show.out, "shear_modulus") == "UNKNOWN");

    // The mass still answers, because the density is known.
    const CliRun mass = runCliCommand({"mass-properties", path});
    CHECK(mass.exitCode == cli::ExitCode::Success);
    CHECK_THAT(numberOf(mass.out, "mass"), WithinRel(2.7, kRel));
}

// === the four gaps the adversarial review found ==============================

TEST_CASE("ReferenceModel_MaterialModels_AFailedRegenerationDoesNotLeaveTheLastGoodMass",
          "[reference][material][adversarial]") {
    // ATTACK: can a failed regeneration return the last successful mass as
    // though it were current? The answer has to be no -- a stale mass is worse
    // than no mass, because nothing marks it stale.
    Built built = build(reference::MaterialReferenceModelKind::Tube);
    Document& document = built.document;
    const std::vector<ObjectId> results = features::resultFeatures(document);
    REQUIRE(results.size() == 1);
    const double good = massOf(built).mass.si();
    CHECK_THAT(good, WithinRel(analytic::hollowCylinder(0.06, 0.04, 0.3, 7800.0).mass, kRel));

    // Bore it out wider than the tube is: a 200 mm hole through a 120 mm
    // cylinder leaves nothing to weigh.
    const auto item = document.findByName("tube_bore_d");
    REQUIRE(item.has_value());
    const auto parameter = document.asParameter(*item);
    REQUIRE(parameter.has_value());
    REQUIRE(document.setParameterValue(*parameter, 200_mm).has_value());
    const auto report = built.regenerator.regenerateAll(document);
    REQUIRE(report.has_value());

    // Either the regeneration refused, or it produced nothing usable. What must
    // NOT happen is a mass equal to the one from before the change.
    const auto after = features::partMassProperties(document, built.regenerator, results.front());
    // REFUSED, and asserted as such rather than branched on. A test that
    // accepted either outcome would pass whichever the product did, including
    // the one it must not do.
    INFO("after an impossible bore: " << (after ? std::to_string(after->mass.si()) + " kg"
                                               : "refused: " + after.error().message));
    REQUIRE_FALSE(after.has_value());
    CHECK(after.error().code != ErrorCode::Internal);
    CHECK_FALSE(after.error().message.empty());
}

TEST_CASE("ReferenceModel_MaterialModels_ProvenanceStaysOnItsOwnPropertyThroughAFile",
          "[reference][material][adversarial]") {
    // ATTACK: can a citation move between properties across a save and a load?
    // RM-MAT-01 cites three properties from three DIFFERENT kinds of source, so
    // any swap changes an answer rather than shuffling equal things.
    TempDir dir;
    auto original = reference::buildMaterialBlockReferenceModel();
    REQUIRE(original.has_value());
    Document loaded = roundTrip(original->document, dir, "provenance.bcad");
    const MaterialId id = *features::materialAssignment(loaded).material;

    const auto density = features::materialPropertyProvenance(loaded, id, MechKind::Density);
    const auto modulus = features::materialPropertyProvenance(loaded, id, MechKind::YoungsModulus);
    const auto conductivity = features::materialPropertyProvenance(loaded, id, ThermKind::ThermalConductivity);
    REQUIRE(density.has_value());
    REQUIRE(modulus.has_value());
    REQUIRE(conductivity.has_value());
    // Each on its own property, by kind, source, reference AND date.
    CHECK(density->kind == materials::SourceKind::Handbook);
    CHECK(density->reference == "Table 1");
    CHECK(density->date == materials::Date::of(2024, 1, 15));
    CHECK(modulus->kind == materials::SourceKind::ManufacturerData);
    CHECK(modulus->reference == "Section 3");
    CHECK(modulus->date == materials::Date::of(2024, 2, 20));
    CHECK(conductivity->kind == materials::SourceKind::Measured);
    CHECK(conductivity->reference == "Run 7");
    CHECK(conductivity->date == materials::Date::of(2024, 3, 17));

    // And the property that had a value and NO citation still has neither a
    // citation nor a fabricated one.
    const auto yield = features::materialPropertyProvenance(loaded, id, MechKind::YieldStrength);
    REQUIRE(yield.has_value());
    CHECK(yield->empty());
    CHECK(definitionOf(loaded, id).mechanical.yieldStrength.isKnown());
}

TEST_CASE("ReferenceModel_MaterialModels_DerivedConstantsAreStillDerivedAfterALoad",
          "[reference][material][adversarial]") {
    // ATTACK: can a derived G or K come back from a file as a SUPPLIED value?
    // It cannot, and the reason is structural rather than careful: ADR-027
    // gives neither a slot, so there is nothing for a loader to fill.
    TempDir dir;
    auto original = reference::buildMaterialBlockReferenceModel();
    REQUIRE(original.has_value());
    const std::filesystem::path path = dir.path() / "derived.bcad";
    REQUIRE(io::saveDocument(original->document, path).has_value());

    // The FILE names no such property.
    const std::string text = test::readFile(path);
    CHECK_THAT(text, !ContainsSubstring("shear_modulus"));
    CHECK_THAT(text, !ContainsSubstring("bulk_modulus"));

    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    const MaterialId id = *features::materialAssignment(*loaded).material;
    const materials::MechanicalProperties& properties = definitionOf(*loaded, id).mechanical;

    // Still DERIVED, and still equal to closed form from the loaded E and nu.
    CHECK(materials::derivedShearModulus(properties).isDerived());
    CHECK(materials::derivedBulkModulus(properties).isDerived());
    CHECK_FALSE(materials::derivedShearModulus(properties).isKnown());
    CHECK_THAT(materials::derivedShearModulus(properties).value()->si(),
               WithinRel(70.0e9 / (2.0 * 1.33), kRel));
    // While the SUPPLIED ones come back supplied, so "derived" is a real
    // distinction and not a label everything wears.
    CHECK(properties.youngsModulus.isKnown());
    CHECK(properties.poissonRatio.isKnown());
}

TEST_CASE("ReferenceModel_RmMat05_CliRefusesTheThreeWayDuplicateDesignation",
          "[reference][material][adversarial][cli]") {
    // ATTACK: can the CLI answer a duplicate engineering label by returning the
    // first match? RM-MAT-05 holds THREE materials designated
    // "Aluminium 6061-T6" with two different densities between them, so a first
    // match would be a silently different mass.
    using bettercad::test::CliRun;
    using bettercad::test::cliPath;
    using bettercad::test::runCliCommand;

    TempDir dir;
    auto model = reference::buildMaterialCustomReferenceModel();
    REQUIRE(model.has_value());
    const std::string path = cliPath(dir.path() / "cli_duplicate.bcad");
    REQUIRE(io::saveDocument(model->document, std::filesystem::path{path}).has_value());

    const CliRun run = runCliCommand({"material-show", path, "designation:Aluminium 6061-T6"});
    CHECK(run.exitCode == cli::ExitCode::Failure);
    CHECK_THAT(run.err, ContainsSubstring("material_ambiguous"));
    CHECK_THAT(run.err, ContainsSubstring("3 materials are designated"));
    // All three named, with the IDs that separate them, and NOTHING reported.
    CHECK_THAT(run.err, ContainsSubstring("Imported (object:6)"));
    CHECK_THAT(run.err, ContainsSubstring("Custom (object:7)"));
    CHECK_THAT(run.err, ContainsSubstring("Twin (object:8)"));
    CHECK(run.out.empty());

    // Named by ID, each answers for itself -- the clone and the twin hold
    // different densities, which is what made the ambiguity dangerous.
    const CliRun byId = runCliCommand({"material-show", path, std::to_string(model->sameDesignation.value())});
    CHECK(byId.exitCode == cli::ExitCode::Success);
    CHECK_THAT(numberOf(byId.out, "density"), WithinRel(7800.0, kRel));
    const CliRun clone = runCliCommand({"material-show", path, std::to_string(model->custom.value())});
    CHECK(clone.exitCode == cli::ExitCode::Success);
    CHECK_THAT(numberOf(clone.out, "density"), WithinRel(2700.0, kRel));
}
