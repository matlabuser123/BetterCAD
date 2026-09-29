#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Document.hpp>

#include <array>
#include <string_view>

// BetterCAD's engineering-data reference models (P15-REFMOD-001): realistic
// parts carrying realistic material data, built only through the public
// document, parameter, sketch, feature and material APIs.
//
// A fourth suite beside the parts (P11/P12), the assemblies (P13) and the
// drawings (P14), following the same shape as those three: a struct of IDs per
// model, a builder per model, one catalog, and one loop in the runner.
//
// EVERY PROPERTY VALUE HERE IS TEST / SYNTHETIC ENGINEERING DATA. The numbers
// are round and physically plausible, chosen so that closed-form arithmetic is
// exact and legible, and each material says so in its own `notes`. They are NOT
// sourced datasheet figures and must not be read as any. The library entries
// these models import carry real designations and standards and NO values,
// which is the separation P15-MAT-001 built: identity and provenance come from
// the library, numbers come from whoever measured them.
//
// What the suite is for, per model:
//
//   RM-MAT-01  a NON-SYMMETRIC cuboid, so the three principal moments differ
//              and a swapped inertia axis cannot hide
//   RM-MAT-02  a cylinder, whose axis moment differs from its transverse one,
//              and which is also placed away from the origin to exercise
//              I' = R I R^T and the parallel-axis shift
//   RM-MAT-03  a hollow tube, where the void must reduce volume, mass AND
//              inertia -- a bounding cylinder would be 1.8x the volume
//   RM-MAT-04  two parts of the SAME volume and different densities, so mass
//              must follow the density and not the geometry, plus an assembly
//              proving the assignment is document-level
//   RM-MAT-05  a library entry imported, given values, cloned and edited, so
//              that a document-local custom material can be proved independent
//   RM-MAT-06  a deliberately incomplete material: ready for a mass, not ready
//              for a stiffness, and never defaulted
namespace bettercad::reference {

/// RM-MAT-01. A 200 x 300 x 500 mm block with one corner at the origin, of a
/// synthetic aluminium carrying mechanical, thermal AND provenance data.
///
/// Non-symmetric deliberately: a cube would give three equal moments, and a
/// product that swapped two inertia axes would pass every check.
struct MaterialBlockModel {
    Document document;
    ParameterId a{}, b{}, c{};
    ObjectId sketch{}, solid{};
    MaterialId material{};
};

[[nodiscard]] Result<MaterialBlockModel> buildMaterialBlockReferenceModel();

/// RM-MAT-02. A solid shaft, radius 50 mm and length 400 mm, axis along Z
/// from z = 0, of a synthetic steel.
struct MaterialShaftModel {
    Document document;
    ParameterId radius{}, length{};
    ObjectId sketch{}, solid{};
    MaterialId material{};
};

[[nodiscard]] Result<MaterialShaftModel> buildMaterialShaftReferenceModel();

/// RM-MAT-03. A tube, outer radius 60 mm, bore radius 40 mm, length 300 mm,
/// of the same synthetic steel as the shaft -- so the only difference in the
/// answer is the void.
struct MaterialTubeModel {
    Document document;
    ParameterId outerRadius{}, boreDiameter{}, length{};
    ObjectId sketch{}, solid{}, bore{};
    MaterialId material{};
};

[[nodiscard]] Result<MaterialTubeModel> buildMaterialTubeReferenceModel();

/// RM-MAT-04, part one of three. A 100 x 100 x 100 mm box of synthetic
/// aluminium.
///
/// Its volume is deliberately EQUAL to part B's while its density differs, so
/// a mass that followed the geometry rather than the material would give the
/// same answer for both and be caught.
struct MaterialPartAModel {
    Document document;
    ParameterId side{};
    ObjectId sketch{}, solid{};
    MaterialId material{};
};

[[nodiscard]] Result<MaterialPartAModel> buildMaterialPartAReferenceModel();

/// RM-MAT-04, part two of three. A 200 x 100 x 50 mm box of synthetic steel:
/// the same 10^6 mm^3 as part A, at 2.888... times the density.
struct MaterialPartBModel {
    Document document;
    ParameterId length{}, width{}, height{};
    ObjectId sketch{}, solid{};
    MaterialId material{};
};

[[nodiscard]] Result<MaterialPartBModel> buildMaterialPartBReferenceModel();

/// RM-MAT-04, part three of three. An assembly placing TWO occurrences of one
/// part definition, with one material.
///
/// Two occurrences because that is what the architecture supports: an
/// occurrence carries a transform, not a material. ComponentDefinition has no
/// material field, a Document holds exactly one optional MaterialId, and there
/// is no assembly mass aggregation anywhere in the product -- so this model
/// proves what IS there (a document-level assignment surviving an assembly, and
/// two occurrences of one definition) and the evidence records the three
/// absences rather than inventing behaviour for them.
struct MaterialAssemblyModel {
    Document document;
    ParameterId side{};
    ObjectId sketch{}, solid{};
    ComponentId first{}, second{};
    MaterialId material{};
};

[[nodiscard]] Result<MaterialAssemblyModel> buildMaterialAssemblyReferenceModel();

/// RM-MAT-05. A 120 x 80 x 60 mm box with a library material imported, given
/// synthetic values, and CLONED into a document-local custom material which is
/// what the part is assigned.
///
/// The document also holds a third material sharing the imported one's
/// DESIGNATION, because two objects cannot share a NAME -- names are unique and
/// re-checked on every rename -- while a designation is free text and
/// duplicates are legitimate. That is the duplicate-label fixture, and the one
/// that must never rebind.
struct MaterialCustomModel {
    Document document;
    ParameterId length{}, width{}, height{};
    ObjectId sketch{}, solid{};
    /// Imported from bettercad/al-6061-t6 rev 1, then given values.
    MaterialId imported{};
    /// A clone of `imported` with a new identity; what the part is assigned.
    MaterialId custom{};
    /// A different material with the SAME designation as `imported`.
    MaterialId sameDesignation{};
};

[[nodiscard]] Result<MaterialCustomModel> buildMaterialCustomReferenceModel();

/// RM-MAT-06. A 100 x 100 x 100 mm box whose material is deliberately
/// incomplete, plus two subcase materials the document also holds.
///
///   partial       density and E and cp known; POISSON RATIO and CONDUCTIVITY
///                 unknown. Ready for a mass, incomplete for a stiffness and
///                 for transient conduction, and never defaulted.
///   withoutDensity  E known, DENSITY unknown -- a mass request must fail
///                 explicitly rather than return zero.
///   inconsistent  E, nu, a yield strength and an ULTIMATE TENSILE STRENGTH
///                 BELOW IT. Every property a yield-strength consumer needs is
///                 present, so it is not Incomplete; it is INVALID. This is the
///                 only inconsistency the product detects -- a supplied shear
///                 modulus inconsistent with E and nu cannot exist, because
///                 ADR-027 gives it no slot.
///
/// `partial` is the assigned one: an incomplete material is not a broken
/// document.
struct MaterialIncompleteModel {
    Document document;
    ParameterId side{};
    ObjectId sketch{}, solid{};
    MaterialId partial{};
    MaterialId withoutDensity{};
    MaterialId inconsistent{};
};

[[nodiscard]] Result<MaterialIncompleteModel> buildMaterialIncompleteReferenceModel();

/// The models of the material suite.
enum class MaterialReferenceModelKind {
    Block,      ///< RM-MAT-01
    Shaft,      ///< RM-MAT-02
    Tube,       ///< RM-MAT-03
    PartA,      ///< RM-MAT-04
    PartB,      ///< RM-MAT-04
    Assembly,   ///< RM-MAT-04
    Custom,     ///< RM-MAT-05
    Incomplete, ///< RM-MAT-06
};

struct MaterialReferenceModelInfo {
    MaterialReferenceModelKind kind;
    /// The reference ID, e.g. "RM-MAT-01".
    std::string_view id;
    /// Document name, e.g. "MaterialBlock".
    std::string_view name;
    /// File name stem, e.g. "material_block" for material_block.bcad.
    std::string_view fileStem;
    /// What this model exists to prove, one line, for the runner's output.
    std::string_view purpose;
    /// A main dimension, and a value to try it at (mm): what the
    /// model-change gate changes to make the model regenerate.
    std::string_view mainParameter;
    double mainParameterMm;
};

inline constexpr std::array kMaterialReferenceModels{
    MaterialReferenceModelInfo{MaterialReferenceModelKind::Block, "RM-MAT-01", "MaterialBlock", "material_block",
                               "non-symmetric cuboid: three distinct principal moments", "block_h", 600.0},
    MaterialReferenceModelInfo{MaterialReferenceModelKind::Shaft, "RM-MAT-02", "MaterialShaft", "material_shaft",
                               "cylinder: axis moment differs from transverse; also placed away from the origin",
                               "shaft_h", 500.0},
    MaterialReferenceModelInfo{MaterialReferenceModelKind::Tube, "RM-MAT-03", "MaterialTube", "material_tube",
                               "hollow tube: the void must reduce volume, mass and inertia", "tube_h", 350.0},
    MaterialReferenceModelInfo{MaterialReferenceModelKind::PartA, "RM-MAT-04", "MaterialPartA", "material_part_a",
                               "one of two parts of EQUAL volume and different density", "part_a_h", 120.0},
    MaterialReferenceModelInfo{MaterialReferenceModelKind::PartB, "RM-MAT-04", "MaterialPartB", "material_part_b",
                               "the other: same volume, 2.888x the density", "part_b_w", 240.0},
    MaterialReferenceModelInfo{MaterialReferenceModelKind::Assembly, "RM-MAT-04", "MaterialAssembly",
                               "material_assembly", "two occurrences of one part, one document-level material",
                               "cube_h", 90.0},
    MaterialReferenceModelInfo{MaterialReferenceModelKind::Custom, "RM-MAT-05", "MaterialCustom", "material_custom",
                               "library import, clone, and a duplicate designation that must not rebind",
                               "custom_w", 150.0},
    MaterialReferenceModelInfo{MaterialReferenceModelKind::Incomplete, "RM-MAT-06", "MaterialIncomplete",
                               "material_incomplete", "incomplete for a stiffness, ready for a mass, never defaulted",
                               "partial_h", 150.0},
};

/// The model's document, from its builder.
[[nodiscard]] Result<Document> buildMaterialReferenceModel(MaterialReferenceModelKind kind);

} // namespace bettercad::reference
