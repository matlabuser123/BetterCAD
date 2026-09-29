#include "AssemblyParts.hpp"
#include "BuildSupport.hpp"
#include "MaterialReferenceModels.hpp"

#include <bettercad/core/materials/MaterialLibrary.hpp>
#include <bettercad/core/materials/Provenance.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>

#include <string>
#include <utility>

namespace bettercad::reference {

using namespace bettercad::literals;
using detail::blockPart;
using detail::discPart;
using detail::featureId;
using detail::pointMm;
using detail::place;
using materials::MaterialProperty;
using materials::SourceKind;

namespace {

/// The note every material in this suite carries, so that no reader can take a
/// number here for a sourced datasheet figure.
constexpr std::string_view kSynthetic =
    "TEST / SYNTHETIC ENGINEERING DATA. Round, physically plausible values chosen so that "
    "closed-form validation is exact. Not sourced datasheet figures.";

/// A known property, with no reference temperature.
template <typename T>
MaterialProperty<T> known(T value) {
    return MaterialProperty<T>::known(value);
}

/// One provenance record. Deliberately varied across properties in RM-MAT-01
/// and RM-MAT-05 so that a citation moving between properties would show.
materials::PropertyProvenance cite(SourceKind kind, std::string source, std::string reference, int year,
                                   int month, int day) {
    materials::PropertyProvenance record;
    record.kind = kind;
    record.source = std::move(source);
    record.reference = std::move(reference);
    record.date = materials::Date::of(year, month, day);
    return record;
}

/// A synthetic aluminium: rho 2700, E 70 GPa, nu 0.33.
features::MaterialDefinition syntheticAluminium() {
    features::MaterialDefinition d;
    d.designation = "Aluminium 6061-T6";
    d.standard = "ASTM B221";
    d.family = "Aluminium Alloy";
    d.notes = std::string{kSynthetic};
    d.mechanical.density = known(Density::fromSi(2700.0));
    d.mechanical.youngsModulus = known(Stress::fromSi(70.0e9));
    d.mechanical.poissonRatio = known(PoissonRatio::of(0.33));
    d.mechanical.yieldStrength = known(Stress::fromSi(276.0e6));
    d.mechanical.ultimateTensileStrength = known(Stress::fromSi(310.0e6));
    d.mechanical.elongation = known(materials::Elongation::of(0.12));
    d.mechanical.hardness = known(materials::Hardness::of(95.0, materials::HardnessScale::Brinell));
    d.thermal.thermalConductivity = known(ThermalConductivity::fromSi(167.0));
    d.thermal.specificHeatCapacity = known(SpecificHeatCapacity::fromSi(896.0));
    d.thermal.thermalExpansion = known(ThermalExpansionCoefficient::fromSi(23.6e-6));
    d.thermal.meltingTemperature = known(Temperature::fromSi(855.0));
    return d;
}

/// A synthetic steel: rho 7800, E 200 GPa, nu 0.28.
features::MaterialDefinition syntheticSteel() {
    features::MaterialDefinition d;
    d.designation = "Steel S235JR";
    d.standard = "EN 10025-2";
    d.family = "Carbon Steel";
    d.notes = std::string{kSynthetic};
    d.mechanical.density = known(Density::fromSi(7800.0));
    d.mechanical.youngsModulus = known(Stress::fromSi(200.0e9));
    d.mechanical.poissonRatio = known(PoissonRatio::of(0.28));
    d.mechanical.yieldStrength = known(Stress::fromSi(250.0e6));
    d.mechanical.ultimateTensileStrength = known(Stress::fromSi(400.0e6));
    d.thermal.thermalConductivity = known(ThermalConductivity::fromSi(50.0));
    d.thermal.specificHeatCapacity = known(SpecificHeatCapacity::fromSi(460.0));
    d.thermal.thermalExpansion = known(ThermalExpansionCoefficient::fromSi(12.0e-6));
    return d;
}

/// Three DIFFERENT provenance kinds on three properties of one material, so
/// that a citation moving between properties across a save and a load would be
/// visible rather than plausible.
void citeAluminium(detail::ModelBuilder& b, MaterialId material) {
    using MechKind = materials::MechanicalPropertyKind;
    using ThermKind = materials::ThermalPropertyKind;
    b.need(features::setMaterialPropertyProvenance(
               b.document(), material, MechKind::Density,
               cite(SourceKind::Handbook, "Synthetic reference table", "Table 1", 2024, 1, 15)),
           "density provenance");
    b.need(features::setMaterialPropertyProvenance(
               b.document(), material, MechKind::YoungsModulus,
               cite(SourceKind::ManufacturerData, "Synthetic mill certificate", "Section 3", 2024, 2, 20)),
           "modulus provenance");
    b.need(features::setMaterialPropertyProvenance(
               b.document(), material, ThermKind::ThermalConductivity,
               cite(SourceKind::Measured, "Synthetic laboratory report", "Run 7", 2024, 3, 17)),
           "conductivity provenance");
    // The YIELD STRENGTH is deliberately left UNCITED while its value is
    // known. "Known value, no provenance" and "no value" are different
    // answers, and RM-MAT-01 is where that distinction is proved.
}

/// Creates a material and assigns it, the ordinary way.
MaterialId assigned(detail::ModelBuilder& b, const std::string& name,
                    const features::MaterialDefinition& definition) {
    const MaterialId id = b.need(features::createMaterial(b.document(), name, definition), name);
    b.need(features::assignMaterial(b.document(), id), "assignment");
    return id;
}

} // namespace

// --- RM-MAT-01 ---------------------------------------------------------------

Result<MaterialBlockModel> buildMaterialBlockReferenceModel() {
    MaterialBlockModel m{.document = Document(detail::fixedDocumentId("5eed15a1-0000-4000-8000-000000000001"),
                                              "MaterialBlock")};
    detail::ModelBuilder b(m.document);

    // 200 x 300 x 500 mm, one corner at the origin. Three DIFFERENT edge
    // lengths, so Ixx, Iyy and Izz are three different numbers: a cube would
    // let a product with two inertia axes transposed pass every check here.
    const detail::BlockPart part = blockPart(b, "Block", "block", 200.0, 300.0, 500.0);
    m.a = part.width;
    m.b = part.depth;
    m.c = part.height;
    m.sketch = part.sketch;
    m.solid = part.solid;

    m.material = assigned(b, "Aluminium", syntheticAluminium());
    citeAluminium(b, m.material);

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

// --- RM-MAT-02 ---------------------------------------------------------------

Result<MaterialShaftModel> buildMaterialShaftReferenceModel() {
    MaterialShaftModel m{.document = Document(detail::fixedDocumentId("5eed15a1-0000-4000-8000-000000000002"),
                                              "MaterialShaft")};
    detail::ModelBuilder b(m.document);

    // A circle on the XY plane extruded in +Z, so the cylinder's axis IS Z --
    // established by the construction, not assumed. The test that compares the
    // axis moment against the transverse one asserts that orientation.
    const detail::DiscPart part = discPart(b, "Shaft", "shaft", 50.0, 400.0);
    m.radius = part.radius;
    m.length = part.height;
    m.sketch = part.sketch;
    m.solid = part.solid;

    m.material = assigned(b, "Steel", syntheticSteel());

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

// --- RM-MAT-03 ---------------------------------------------------------------

Result<MaterialTubeModel> buildMaterialTubeReferenceModel() {
    MaterialTubeModel m{.document = Document(detail::fixedDocumentId("5eed15a1-0000-4000-8000-000000000003"),
                                             "MaterialTube")};
    detail::ModelBuilder b(m.document);

    // Outer Ø120 x 300 long, bored Ø80 right through from the start plane, so
    // the bore stays through whatever the length becomes. The void is 44% of
    // the outer cylinder: a product that measured a bounding cylinder would be
    // out by 80%, which no tolerance could absorb.
    const detail::DiscPart part = discPart(b, "Tube", "tube", 60.0, 300.0);
    m.outerRadius = part.radius;
    m.length = part.height;
    m.sketch = part.sketch;
    m.solid = part.solid;

    m.boreDiameter = b.length("tube_bore_d", 80.0);
    const geometry::FaceSignature startPlane =
        geometry::planeSignature(pointMm(0.0, 0.0, 0.0), Direction3D::unitZ().reversed());
    m.bore = b.feature<features::HoleFeature>("Bore", {.target = featureId(part.solid),
                                                       .face = startPlane,
                                                       .diameter = 80_mm,
                                                       .diameterParameter = m.boreDiameter});

    m.material = assigned(b, "Steel", syntheticSteel());

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

// --- RM-MAT-04 ---------------------------------------------------------------

Result<MaterialPartAModel> buildMaterialPartAReferenceModel() {
    MaterialPartAModel m{.document = Document(detail::fixedDocumentId("5eed15a1-0000-4000-8000-000000000004"),
                                              "MaterialPartA")};
    detail::ModelBuilder b(m.document);

    // 100^3 mm = 10^6 mm^3, the SAME volume as part B at a different density.
    const detail::BlockPart part = blockPart(b, "PartA", "part_a", 100.0, 100.0, 100.0);
    m.side = part.height;
    m.sketch = part.sketch;
    m.solid = part.solid;

    m.material = assigned(b, "Aluminium", syntheticAluminium());

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

Result<MaterialPartBModel> buildMaterialPartBReferenceModel() {
    MaterialPartBModel m{.document = Document(detail::fixedDocumentId("5eed15a1-0000-4000-8000-000000000005"),
                                              "MaterialPartB")};
    detail::ModelBuilder b(m.document);

    // 200 x 100 x 50 mm = 10^6 mm^3 as well. Same volume, 2.888... times the
    // mass, because the density differs and nothing else does.
    const detail::BlockPart part = blockPart(b, "PartB", "part_b", 200.0, 100.0, 50.0);
    m.length = part.width;
    m.width = part.depth;
    m.height = part.height;
    m.sketch = part.sketch;
    m.solid = part.solid;

    m.material = assigned(b, "Steel", syntheticSteel());

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

Result<MaterialAssemblyModel> buildMaterialAssemblyReferenceModel() {
    MaterialAssemblyModel m{.document = Document(detail::fixedDocumentId("5eed15a1-0000-4000-8000-000000000006"),
                                                 "MaterialAssembly")};
    detail::ModelBuilder b(m.document);

    // TWO OCCURRENCES OF ONE PART DEFINITION, separated along X. An occurrence
    // carries a transform and not a material -- ComponentDefinition has no
    // material field -- and a document holds exactly one optional MaterialId,
    // so this is what a "multi-part assembly" can be in the qualified
    // architecture. The evidence records the three absences it cannot show.
    const detail::BlockPart part = blockPart(b, "Cube", "cube", 80.0, 80.0, 80.0);
    m.side = part.height;
    m.sketch = part.sketch;
    m.solid = part.solid;

    m.first = place(b, "CubeOne", part.solid);
    m.second = place(b, "CubeTwo", part.solid, detail::at(200.0, 0.0, 0.0));

    m.material = assigned(b, "Steel", syntheticSteel());

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

// --- RM-MAT-05 ---------------------------------------------------------------

Result<MaterialCustomModel> buildMaterialCustomReferenceModel() {
    MaterialCustomModel m{.document = Document(detail::fixedDocumentId("5eed15a1-0000-4000-8000-000000000007"),
                                               "MaterialCustom")};
    detail::ModelBuilder b(m.document);

    const detail::BlockPart part = blockPart(b, "Body", "custom", 120.0, 80.0, 60.0);
    m.length = part.width;
    m.width = part.depth;
    m.height = part.height;
    m.sketch = part.sketch;
    m.solid = part.solid;

    // A GENUINE library import: the entry is a compiled-in constant, so it
    // carries a real designation and standard and no property values at all.
    // The import copies its metadata and records its key as the origin; the
    // values below are this document's own, which is the whole of ADR-025.
    auto entry = materials::findLibraryMaterial(materials::builtInLibraryName(), "al-6061-t6");
    if (!entry) {
        return std::unexpected(entry.error());
    }
    m.imported = b.need(features::importLibraryMaterial(m.document, "Imported", *entry), "import");
    features::MaterialDefinition values = syntheticAluminium();
    // Keep the metadata and the ORIGIN the import gave it; only supply numbers.
    if (const features::Material* material = features::findMaterial(m.document, m.imported)) {
        features::MaterialDefinition kept = material->definition();
        kept.mechanical = values.mechanical;
        kept.thermal = values.thermal;
        kept.notes = std::string{kSynthetic};
        b.need(features::setMaterialDefinition(m.document, m.imported, kept), "imported values");
    }
    citeAluminium(b, m.imported);

    // The clone: a NEW identity, every property copied, the origin carried
    // over because the values still came from where they came from. This is
    // what the part is made of, so every downstream consumer has to resolve a
    // document-local custom material with no special branch for it.
    m.custom = b.need(features::cloneMaterial(m.document, m.imported, "Custom"), "clone");
    b.need(features::assignMaterial(m.document, m.custom), "assignment");

    // A THIRD material sharing the imported one's DESIGNATION. Two objects
    // cannot share a NAME, so this is what a duplicate engineering label
    // actually looks like -- and it is what must never be substituted for
    // another material by a lookup.
    features::MaterialDefinition twin = syntheticSteel();
    twin.designation = values.designation;
    twin.notes = std::string{kSynthetic};
    m.sameDesignation = b.need(features::createMaterial(m.document, "Twin", twin), "twin");

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

// --- RM-MAT-06 ---------------------------------------------------------------

Result<MaterialIncompleteModel> buildMaterialIncompleteReferenceModel() {
    MaterialIncompleteModel m{.document = Document(detail::fixedDocumentId("5eed15a1-0000-4000-8000-000000000008"),
                                                   "MaterialIncomplete")};
    detail::ModelBuilder b(m.document);

    const detail::BlockPart part = blockPart(b, "Body", "partial", 100.0, 100.0, 100.0);
    m.side = part.height;
    m.sketch = part.sketch;
    m.solid = part.solid;

    // DELIBERATELY INCOMPLETE, and this is a valid document. Density, E and
    // specific heat are known; the POISSON RATIO and the CONDUCTIVITY are not.
    // So it is ready for a mass, incomplete for a linear-static stiffness (no
    // nu) and incomplete for transient conduction (no k) -- three different
    // answers from one material, which is the point.
    features::MaterialDefinition partial;
    partial.designation = "Partially characterised alloy";
    partial.family = "Unspecified";
    partial.notes = std::string{kSynthetic} +
                    " Incomplete ON PURPOSE: no Poisson ratio and no thermal conductivity.";
    partial.mechanical.density = known(Density::fromSi(2700.0));
    partial.mechanical.youngsModulus = known(Stress::fromSi(70.0e9));
    partial.thermal.specificHeatCapacity = known(SpecificHeatCapacity::fromSi(896.0));
    m.partial = assigned(b, "Partial", partial);

    // A subcase, not the assigned material: NO DENSITY. A mass request against
    // this must fail saying so, never return zero.
    features::MaterialDefinition withoutDensity;
    withoutDensity.designation = "Unweighed alloy";
    withoutDensity.notes = std::string{kSynthetic} + " No density ON PURPOSE.";
    withoutDensity.mechanical.youngsModulus = known(Stress::fromSi(70.0e9));
    withoutDensity.mechanical.poissonRatio = known(PoissonRatio::of(0.33));
    m.withoutDensity = b.need(features::createMaterial(m.document, "Unweighed", withoutDensity), "unweighed");

    // The INVALID subcase: an ultimate tensile strength BELOW the yield
    // strength. Every property a yield-strength consumer requires is present,
    // so this is not Incomplete -- it is Invalid, and it is the one
    // inconsistency the product detects. A supplied shear modulus inconsistent
    // with E and nu cannot be built, because ADR-027 gives it no slot.
    features::MaterialDefinition inconsistent;
    inconsistent.designation = "Inconsistent alloy";
    inconsistent.notes = std::string{kSynthetic} +
                         " Inconsistent ON PURPOSE: the ultimate tensile strength is below the yield strength.";
    inconsistent.mechanical.density = known(Density::fromSi(2700.0));
    inconsistent.mechanical.youngsModulus = known(Stress::fromSi(70.0e9));
    inconsistent.mechanical.poissonRatio = known(PoissonRatio::of(0.33));
    inconsistent.mechanical.yieldStrength = known(Stress::fromSi(250.0e6));
    inconsistent.mechanical.ultimateTensileStrength = known(Stress::fromSi(200.0e6));
    m.inconsistent = b.need(features::createMaterial(m.document, "Inconsistent", inconsistent), "inconsistent");

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
