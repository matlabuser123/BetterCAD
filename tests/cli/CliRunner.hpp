#pragma once

#include "Cli.hpp"

#include <filesystem>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::test {

/// What one in-process run of bettercad-cli produced.
struct CliRun {
    cli::ExitCode exitCode;
    std::string out;
    std::string err;
};

/// Runs the CLI in process with @p args (without the program name).
inline CliRun runCliCommand(const std::vector<std::string>& args) {
    const std::vector<std::string_view> argv(args.begin(), args.end());
    std::ostringstream out;
    std::ostringstream err;
    const cli::ExitCode code = cli::run(argv, out, err);
    return {code, out.str(), err.str()};
}

inline CliRun runCliCommand(std::initializer_list<std::string_view> args) {
    return runCliCommand(std::vector<std::string>(args.begin(), args.end()));
}

/// UTF-8 text of a path, as the command line passes it.
inline std::string cliPath(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

} // namespace bettercad::test
