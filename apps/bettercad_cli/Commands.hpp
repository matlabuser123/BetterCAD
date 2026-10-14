#pragma once

#include "Arguments.hpp"
#include "Cli.hpp"

#include <format>
#include <iosfwd>
#include <string>
#include <string_view>

// Command implementations of bettercad-cli. Each takes the arguments after
// the command name.
namespace bettercad::cli {

inline constexpr std::string_view kNewUsage = "new <file.bcad> [--name <name>] [--force]";
inline constexpr std::string_view kInfoUsage = "info <file.bcad> [--configuration <name>]";
inline constexpr std::string_view kValidateUsage = "validate <file.bcad> [--configuration <name>]";
inline constexpr std::string_view kExportStepUsage = "export-step <file.bcad> <file.step>";
inline constexpr std::string_view kExportStlUsage =
    "export-stl <file.bcad> <file.stl> [--ascii] [--tolerance <length>] [--angle <angle>]";
inline constexpr std::string_view kRegenerateUsage = "regenerate <file.bcad> [--configuration <name>]";
inline constexpr std::string_view kSolveUsage = "solve <file.bcad> [--configuration <name>]";
inline constexpr std::string_view kStatusUsage = "status <file.bcad> [--configuration <name>]";
inline constexpr std::string_view kBatchUsage = "batch <file.bcad> <script> [--dry-run]";
inline constexpr std::string_view kDrawingUsage = "drawing <file.bcad> [--configuration <name>]";
inline constexpr std::string_view kExportPdfUsage =
    "export-pdf <file.bcad> <file.pdf> [--sheet <selector>] [--configuration <name>]";
inline constexpr std::string_view kExportSvgUsage =
    "export-svg <file.bcad> <file.svg> [--sheet <selector>] [--configuration <name>]";
inline constexpr std::string_view kExportDxfUsage =
    "export-dxf <file.bcad> <file.dxf> [--sheet <selector>] [--configuration <name>]";

// P15-CLI-001. Materials: five reports here, and the edits in MaterialEdits.cpp.
// A material selector is `<id>`, `<name>` or `designation:<text>`.
inline constexpr std::string_view kMaterialListUsage = "material-list <file.bcad>";
inline constexpr std::string_view kMaterialShowUsage =
    "material-show <file.bcad> <material> [--provenance]";
inline constexpr std::string_view kMaterialEffectiveUsage = "material-effective <file.bcad>";
inline constexpr std::string_view kMassPropertiesUsage =
    "mass-properties <file.bcad> [<feature>] [--configuration <name>]";
inline constexpr std::string_view kMaterialCompletenessUsage =
    "material-completeness <file.bcad> [<material>] [--consumer <consumer>]";

// P16-CLI-001. Meshing: six reports here, and the edits in MeshingEdits.cpp.
// Each takes --json for a machine-readable form of the same result.
//
// A GENERATED MESH IS NEVER WRITTEN. These load, regenerate, build the mesh
// the question needs and print the answer; the document holds intent
// (P16-PERSIST-001), so a fresh process always generates from what it just
// read rather than trusting anything stored.
inline constexpr std::string_view kMeshSettingsUsage = "mesh-settings <file.bcad> [--json]";
inline constexpr std::string_view kMeshGenerateUsage = "mesh-generate <file.bcad> [--json]";
inline constexpr std::string_view kMeshInfoUsage = "mesh-info <file.bcad> [--json]";
inline constexpr std::string_view kMeshQualityUsage = "mesh-quality <file.bcad> [--json]";
inline constexpr std::string_view kMeshValidateUsage = "mesh-validate <file.bcad> [--json]";
inline constexpr std::string_view kMeshBoundariesUsage =
    "mesh-boundaries <file.bcad> [<face>] [--json]";

// P17-CLI-001. Structural FEA: six reports here, and the five edits in
// StructuralEdits.cpp. Each takes --json for a machine-readable form.
//
// NO RESULT IS EVER WRITTEN. Displacements, stresses and reactions are derived
// state that ADR-048 keeps out of the document, so each result command solves
// from canonical intent in its own process and exits -- exactly as the mesh
// reports generate a mesh rather than loading one. A fresh process therefore
// re-solves, which is correct and is tested rather than worked around.
inline constexpr std::string_view kStructuralSettingsUsage =
    "structural-settings <file.bcad> [--json]";
inline constexpr std::string_view kStructuralValidateUsage =
    "structural-validate <file.bcad> [--json]";
inline constexpr std::string_view kStructuralSolveUsage =
    "structural-solve <file.bcad> [--json]";
inline constexpr std::string_view kStructuralDisplacementUsage =
    "structural-displacement <file.bcad> [--json]";
inline constexpr std::string_view kStructuralStressUsage =
    "structural-stress <file.bcad> [--json]";
inline constexpr std::string_view kStructuralReactionsUsage =
    "structural-reactions <file.bcad> [--json]";

[[nodiscard]] ExitCode runNew(Args args, std::ostream& out, std::ostream& err);
[[nodiscard]] ExitCode runInfo(Args args, std::ostream& out, std::ostream& err);
[[nodiscard]] ExitCode runValidate(Args args, std::ostream& out, std::ostream& err);
[[nodiscard]] ExitCode runExportStep(Args args, std::ostream& out, std::ostream& err);
[[nodiscard]] ExitCode runExportStl(Args args, std::ostream& out, std::ostream& err);

// P13-CLI-001. These three report and never write: a component's transform is
// derived state (ADR-005), so there is nothing for them to persist.
[[nodiscard]] ExitCode runRegenerate(Args args, std::ostream& out, std::ostream& err);
[[nodiscard]] ExitCode runSolve(Args args, std::ostream& out, std::ostream& err);
[[nodiscard]] ExitCode runStatus(Args args, std::ostream& out, std::ostream& err);
/// Applies a script of edits as ONE transaction: all of them, or none.
[[nodiscard]] ExitCode runBatch(Args args, std::ostream& out, std::ostream& err);

/// P14-CLI-001. Reports a drawing -- its sheets, views, dimensions with the
/// values they measure NOW, annotations with their resolution state, and a
/// bill of materials per assembly view. Writes nothing, because everything it
/// prints is derived. Exit status 1 if the drawing does not regenerate.
[[nodiscard]] ExitCode runDrawing(Args args, std::ostream& out, std::ostream& err);

/// P14-EXPORT-001. Writes one sheet out. Each loads, regenerates, assembles
/// the sheet's scene and hands it to a writer; the CLI chooses the writer and
/// builds nothing. Exit status 1 if the drawing does not regenerate or the
/// file cannot be written -- no drawing is written from a sheet that does not
/// resolve.
[[nodiscard]] ExitCode runExportPdf(Args args, std::ostream& out, std::ostream& err);
[[nodiscard]] ExitCode runExportSvg(Args args, std::ostream& out, std::ostream& err);
[[nodiscard]] ExitCode runExportDxf(Args args, std::ostream& out, std::ostream& err);

/// P15-CLI-001. These five report and never write: everything they print is
/// either canonical state the document already holds or state derived from it,
/// and a report that saved its own derivation would be caching an answer the
/// next load has to recompute anyway (ADR-026, ADR-027).
[[nodiscard]] ExitCode runMaterialList(Args args, std::ostream& out, std::ostream& err);
[[nodiscard]] ExitCode runMaterialShow(Args args, std::ostream& out, std::ostream& err);
[[nodiscard]] ExitCode runMaterialEffective(Args args, std::ostream& out, std::ostream& err);
[[nodiscard]] ExitCode runMassProperties(Args args, std::ostream& out, std::ostream& err);
[[nodiscard]] ExitCode runMaterialCompleteness(Args args, std::ostream& out, std::ostream& err);

/// P16-CLI-001. These six report and never write, for the reason P15's do and
/// one more: a generated mesh is derived state, and a report that saved one
/// would make the document claim an authority P16-PERSIST-001 denies it.
[[nodiscard]] ExitCode runMeshSettings(Args args, std::ostream& out, std::ostream& err);
[[nodiscard]] ExitCode runMeshGenerate(Args args, std::ostream& out, std::ostream& err);
[[nodiscard]] ExitCode runMeshInfo(Args args, std::ostream& out, std::ostream& err);
[[nodiscard]] ExitCode runMeshQuality(Args args, std::ostream& out, std::ostream& err);
[[nodiscard]] ExitCode runMeshValidate(Args args, std::ostream& out, std::ostream& err);
[[nodiscard]] ExitCode runMeshBoundaries(Args args, std::ostream& out, std::ostream& err);

/// P17-CLI-001. These six report and never write, for the reason P16's do:
/// a structural result is derived state, and a report that saved one would
/// make the document claim an authority ADR-048 denies it.
///
/// THE FOUR RESULT COMMANDS SOLVE, through `solveStructuralAnalysis` and
/// nothing else, so every P17-VALID refusal reaches the exit status. There is
/// no flag that bypasses validation.
[[nodiscard]] ExitCode runStructuralSettings(Args args, std::ostream& out, std::ostream& err);
[[nodiscard]] ExitCode runStructuralValidate(Args args, std::ostream& out, std::ostream& err);
[[nodiscard]] ExitCode runStructuralSolve(Args args, std::ostream& out, std::ostream& err);
[[nodiscard]] ExitCode runStructuralDisplacement(Args args, std::ostream& out, std::ostream& err);
[[nodiscard]] ExitCode runStructuralStress(Args args, std::ostream& out, std::ostream& err);
[[nodiscard]] ExitCode runStructuralReactions(Args args, std::ostream& out, std::ostream& err);

/// "3 objects", "1 object".
inline std::string plural(std::size_t count, std::string_view singular, std::string_view many) {
    return std::format("{} {}", count, count == 1 ? singular : many);
}

/// Prints "bettercad-cli <command>: <problem>" and the command's usage;
/// returns UsageError.
ExitCode usageError(std::string_view command, std::string_view usage, std::string_view problem, std::ostream& err);
/// Prints "bettercad-cli <command>: <problem>"; returns Failure.
ExitCode failure(std::string_view command, std::string_view problem, std::ostream& err);
/// Prints "bettercad-cli <command>: <code>: <problem>"; returns Failure.
///
/// @p code is a STABLE machine name -- `material_not_found`,
/// `material_ambiguous` -- so a script branches on the code and never on the
/// English. The message stays the thing an engineer reads; the code is the
/// thing a pipeline matches, and only the code is an interface (P15-CLI-001).
ExitCode failure(std::string_view command, std::string_view code, std::string_view problem, std::ostream& err);

} // namespace bettercad::cli
