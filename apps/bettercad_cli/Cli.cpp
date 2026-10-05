#include "Cli.hpp"

#include "Commands.hpp"
#include "Edits.hpp"

#include <bettercad/core/BuildInfo.hpp>

#include <algorithm>
#include <array>
#include <exception>
#include <format>
#include <ostream>

namespace bettercad::cli {

namespace {

struct Command {
    std::string_view name;
    std::string_view usage;
    std::string_view summary;
    ExitCode (*handler)(Args args, std::ostream& out, std::ostream& err);
};

ExitCode runHelp(Args args, std::ostream& out, std::ostream& err);
ExitCode runVersion(Args args, std::ostream& out, std::ostream& err);

constexpr std::array kCommands{
    Command{"new", kNewUsage, "Create an empty document.", &runNew},
    Command{"info", kInfoUsage, "Show the document's metadata, parameters and objects.", &runInfo},
    Command{"validate", kValidateUsage,
            "Check references, cycles, sketches, regeneration and geometry. Exit status 1 if the "
            "document has errors.",
            &runValidate},
    Command{"export-step", kExportStepUsage, "Regenerate and write the result bodies as STEP (AP214, mm).",
            &runExportStep},
    Command{"export-stl", kExportStlUsage,
            "Regenerate, triangulate and write the result bodies as STL (mm; binary unless --ascii). "
            "--tolerance is the largest distance from the true surface (default 0.1 mm), --angle the "
            "largest angle between neighbouring facets (default 20 deg).",
            &runExportStl},
    Command{"regenerate", kRegenerateUsage,
            "Regenerate features and re-solve the assembly, and report what happened. Writes nothing. "
            "Exit status 1 if anything failed to build.",
            &runRegenerate},
    Command{"solve", kSolveUsage,
            "Solve the assembly and report the status, the degrees of freedom and the transforms. Writes "
            "nothing. Exit status 1 unless the assembly solved.",
            &runSolve},
    Command{"status", kStatusUsage,
            "Show the components, mates, suppression, regeneration and solve of an assembly. Writes nothing.",
            &runStatus},
    Command{"export-pdf", kExportPdfUsage,
            "Write one drawing sheet as a vector PDF, at its true physical size.",
            &runExportPdf},
    Command{"export-svg", kExportSvgUsage,
            "Write one drawing sheet as SVG, sized in millimetres.", &runExportSvg},
    Command{"export-dxf", kExportDxfUsage,
            "Write one drawing sheet as DXF (AutoCAD R12), in millimetres.", &runExportDxf},
    Command{"drawing", kDrawingUsage,
            "Show the sheets, views, dimensions, annotations and bill of materials of a drawing, with "
            "what each one resolves to now. Writes nothing. Exit status 1 if the drawing does not "
            "regenerate.",
            &runDrawing},
    Command{"batch", kBatchUsage,
            "Apply a script of edits as one transaction: the file is written only if every edit succeeded. "
            "One edit per line, without the document path; # comments. --dry-run checks without writing.",
            &runBatch},
    Command{"material-list", kMaterialListUsage,
            "List the document's materials, with the one it is assigned. Writes nothing.", &runMaterialList},
    Command{"material-show", kMaterialShowUsage,
            "Show one material's metadata and every engineering property, UNKNOWN where there is no value and "
            "'(derived)' where a value was computed rather than supplied. Writes nothing.",
            &runMaterialShow},
    Command{"material-effective", kMaterialEffectiveUsage,
            "Show what the document is made of and whether its assignment resolves. Exit status 1 if the "
            "assignment names a material that is not there. Writes nothing.",
            &runMaterialEffective},
    Command{"mass-properties", kMassPropertiesUsage,
            "Regenerate and report the volume, mass, centre of mass and inertia of each body, from the assigned "
            "material's density. Exit status 1 if it cannot be computed -- never a zero mass. Writes nothing.",
            &runMassProperties},
    Command{"material-completeness", kMaterialCompletenessUsage,
            "Report what a material has and lacks: for one consumer with --consumer, otherwise across every "
            "property. Exit status 1 unless the state is ready. Writes nothing.",
            &runMaterialCompleteness},
    Command{"version", "version", "Print version, compiler and build information.", &runVersion},
    Command{"help", "help", "Show this help.", &runHelp},
    // P16-CLI-001. Six reports over the P16 meshing core. Each adds --json.
    Command{"mesh-settings", kMeshSettingsUsage,
            "Show the canonical meshing intent: the global target size, the local sizing controls "
            "and their faces, the named boundary sets and the quality thresholds. Writes nothing.",
            &runMeshSettings},
    Command{"mesh-generate", kMeshGenerateUsage,
            "Regenerate the model, mesh it from the document's meshing intent, and report what was "
            "built. The mesh is NOT saved: a document holds intent, and a mesh is derived from it.",
            &runMeshGenerate},
    Command{"mesh-info", kMeshInfoUsage,
            "Mesh the document and summarise it: nodes, elements, element types, volume and the "
            "controls behind it. A summary at any size -- it never lists elements.",
            &runMeshInfo},
    Command{"mesh-quality", kMeshQualityUsage,
            "Mesh the document and report element quality under the control's own thresholds: "
            "invalid, warning and failure counts, and each metric's range and worst element.",
            &runMeshQuality},
    Command{"mesh-validate", kMeshValidateUsage,
            "Mesh the document and check it structurally -- connectivity, degenerate and inverted "
            "elements. Exit status 1 if the mesh is not data-valid. Shape quality is mesh-quality's.",
            &runMeshValidate},
    Command{"mesh-boundaries", kMeshBoundariesUsage,
            "Mesh the document and report the named boundary sets, or one face, with the facets "
            "they resolve to now. Reporting that a face is unresolved is a successful answer.",
            &runMeshBoundaries},
};

void printUsage(std::ostream& os) {
    os << std::format("Usage: {} <command> [arguments]\n\nCommands:\n", kProgramName);
    for (const Command& command : kCommands) {
        os << std::format("  {}\n      {}\n", command.usage, command.summary);
    }
    os << "\nAssembly edits (each loads, edits and saves the document atomically):\n";
    for (const EditCommand& edit : editCommands()) {
        os << std::format("  {}\n      {}\n", edit.usage, edit.summary);
    }
    os << "\nOptions:\n"
          "  -h, --help  Show this help\n"
          "  --version   Print the version\n"
          "\nLengths and angles accept units, e.g. 0.05mm, 0.002in, 15deg, 0.2rad.\n"
          "Exit status: 0 success, 1 failure, 2 invalid command line.\n";
}

ExitCode runHelp(Args args, std::ostream& out, std::ostream& err) {
    if (!args.empty()) {
        return usageError("help", "help", std::format("unexpected argument '{}'", args.front()), err);
    }
    printUsage(out);
    return ExitCode::Success;
}

ExitCode runVersion(Args args, std::ostream& out, std::ostream& err) {
    if (!args.empty()) {
        return usageError("version", "version", std::format("unexpected argument '{}'", args.front()), err);
    }
    out << formatBuildInfo(buildInfo());
    return ExitCode::Success;
}

ExitCode dispatch(Args args, std::ostream& out, std::ostream& err) {
    if (args.empty()) {
        printUsage(err);
        return ExitCode::UsageError;
    }

    const std::string_view first = args.front();
    if (first == "-h" || first == "--help") {
        printUsage(out);
        return ExitCode::Success;
    }
    if (first == "--version") {
        out << std::format("{} {}\n", kProgramName, buildInfo().version);
        return ExitCode::Success;
    }

    if (const EditCommand* edit = findEditCommand(first)) {
        return runEdit(*edit, args.subspan(1), out, err);
    }
    const auto command = std::ranges::find(kCommands, first, &Command::name);
    if (command == kCommands.end()) {
        err << std::format("{0}: unknown command '{1}'\nRun '{0} --help' for a list of commands.\n",
                           kProgramName, first);
        return ExitCode::UsageError;
    }
    return command->handler(args.subspan(1), out, err);
}

} // namespace

ExitCode usageError(std::string_view command, std::string_view usage, std::string_view problem, std::ostream& err) {
    err << std::format("{0} {1}: {2}\nUsage: {0} {3}\n", kProgramName, command, problem, usage);
    return ExitCode::UsageError;
}

ExitCode failure(std::string_view command, std::string_view problem, std::ostream& err) {
    err << std::format("{} {}: {}\n", kProgramName, command, problem);
    return ExitCode::Failure;
}

ExitCode failure(std::string_view command, std::string_view code, std::string_view problem,
                 std::ostream& err) {
    // "<program> <command>: <code>: <problem>". The code sits where a
    // script can find it without parsing the sentence after it.
    err << std::format("{} {}: {}: {}\n", kProgramName, command, code, problem);
    return ExitCode::Failure;
}

ExitCode run(Args args, std::ostream& out, std::ostream& err) noexcept {
    try {
        return dispatch(args, out, err);
    } catch (const std::exception& e) {
        err << kProgramName << ": internal error: " << e.what() << '\n';
    } catch (...) {
        err << kProgramName << ": internal error: unknown exception\n";
    }
    return ExitCode::Failure;
}

} // namespace bettercad::cli
