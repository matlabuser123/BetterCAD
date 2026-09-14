#pragma once

#include "Arguments.hpp"
#include "Cli.hpp"

#include <iosfwd>
#include <string_view>

// Command implementations of bettercad-cli. Each takes the arguments after
// the command name.
namespace bettercad::cli {

inline constexpr std::string_view kNewUsage = "new <file.bcad> [--name <name>] [--force]";
inline constexpr std::string_view kInfoUsage = "info <file.bcad>";
inline constexpr std::string_view kValidateUsage = "validate <file.bcad>";
inline constexpr std::string_view kExportStepUsage = "export-step <file.bcad> <file.step>";
inline constexpr std::string_view kExportStlUsage =
    "export-stl <file.bcad> <file.stl> [--ascii] [--tolerance <length>] [--angle <angle>]";

[[nodiscard]] ExitCode runNew(Args args, std::ostream& out, std::ostream& err);
[[nodiscard]] ExitCode runInfo(Args args, std::ostream& out, std::ostream& err);
[[nodiscard]] ExitCode runValidate(Args args, std::ostream& out, std::ostream& err);
[[nodiscard]] ExitCode runExportStep(Args args, std::ostream& out, std::ostream& err);
[[nodiscard]] ExitCode runExportStl(Args args, std::ostream& out, std::ostream& err);

/// Prints "bettercad-cli <command>: <problem>" and the command's usage;
/// returns UsageError.
ExitCode usageError(std::string_view command, std::string_view usage, std::string_view problem, std::ostream& err);
/// Prints "bettercad-cli <command>: <problem>"; returns Failure.
ExitCode failure(std::string_view command, std::string_view problem, std::ostream& err);

} // namespace bettercad::cli
