#pragma once

#include <iosfwd>
#include <span>
#include <string_view>

namespace bettercad::cli {

inline constexpr std::string_view kProgramName = "bettercad-cli";

/// Process exit codes of bettercad-cli.
enum class ExitCode : int {
    Success = 0,
    Failure = 1,    ///< The command ran but reported a problem.
    UsageError = 2, ///< The command line was invalid.
};

/// Runs bettercad-cli with @p args (program name excluded). Normal output goes
/// to @p out, diagnostics to @p err. Never throws.
[[nodiscard]] ExitCode run(std::span<const std::string_view> args, std::ostream& out,
                           std::ostream& err) noexcept;

} // namespace bettercad::cli
