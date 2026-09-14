#include "Cli.hpp"

#include "Commands.hpp"

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
    Command{"version", "version", "Print version, compiler and build information.", &runVersion},
    Command{"help", "help", "Show this help.", &runHelp},
};

void printUsage(std::ostream& os) {
    os << std::format("Usage: {} <command> [arguments]\n\nCommands:\n", kProgramName);
    for (const Command& command : kCommands) {
        os << std::format("  {}\n      {}\n", command.usage, command.summary);
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
