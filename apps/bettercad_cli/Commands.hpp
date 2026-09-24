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

/// "3 objects", "1 object".
inline std::string plural(std::size_t count, std::string_view singular, std::string_view many) {
    return std::format("{} {}", count, count == 1 ? singular : many);
}

/// Prints "bettercad-cli <command>: <problem>" and the command's usage;
/// returns UsageError.
ExitCode usageError(std::string_view command, std::string_view usage, std::string_view problem, std::ostream& err);
/// Prints "bettercad-cli <command>: <problem>"; returns Failure.
ExitCode failure(std::string_view command, std::string_view problem, std::ostream& err);

} // namespace bettercad::cli
