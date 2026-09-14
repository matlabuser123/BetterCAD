#include "Cli.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int runCli(const std::vector<std::string>& arguments) {
    const std::vector<std::string_view> views(arguments.begin(), arguments.end());
    return static_cast<int>(bettercad::cli::run(views, std::cout, std::cerr));
}

} // namespace

#ifdef _WIN32

int wmain(int argc, wchar_t** argv);

// Windows delivers the command line as UTF-16; argv in main() would be in the
// ANSI code page and lose characters. The CLI works with UTF-8 throughout.
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> arguments;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::u8string utf8 = std::filesystem::path(argv[i]).u8string();
            arguments.emplace_back(utf8.begin(), utf8.end());
        }
    } catch (const std::exception&) {
        std::cerr << bettercad::cli::kProgramName << ": the command line is not valid Unicode\n";
        return static_cast<int>(bettercad::cli::ExitCode::UsageError);
    }
    return runCli(arguments);
}

#else

int main(int argc, char** argv) {
    return runCli(std::vector<std::string>(argv + 1, argv + argc));
}

#endif
