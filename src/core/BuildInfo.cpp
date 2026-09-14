#include <bettercad/core/BuildInfo.hpp>
#include <bettercad/core/Version.hpp>

#include "core/GitRevision.hpp"

#include <format>

#ifndef BETTERCAD_BUILD_CONFIG
#error "BETTERCAD_BUILD_CONFIG must be defined by the build system"
#endif

#define BETTERCAD_STRINGIFY_IMPL(x) #x
#define BETTERCAD_STRINGIFY(x) BETTERCAD_STRINGIFY_IMPL(x)

namespace bettercad {

namespace {

// Compiler identity comes from the compiler's own predefined macros, so it
// always describes the compiler that actually built this translation unit.
#if defined(__clang__)
constexpr std::string_view kCompilerId = "Clang";
constexpr std::string_view kCompilerVersion = BETTERCAD_STRINGIFY(__clang_major__) "." BETTERCAD_STRINGIFY(
    __clang_minor__) "." BETTERCAD_STRINGIFY(__clang_patchlevel__);
#elif defined(__GNUC__)
constexpr std::string_view kCompilerId = "GCC";
constexpr std::string_view kCompilerVersion = BETTERCAD_STRINGIFY(__GNUC__) "." BETTERCAD_STRINGIFY(
    __GNUC_MINOR__) "." BETTERCAD_STRINGIFY(__GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
constexpr std::string_view kCompilerId = "MSVC";
constexpr std::string_view kCompilerVersion = BETTERCAD_STRINGIFY(_MSC_FULL_VER);
#else
constexpr std::string_view kCompilerId = "unknown";
constexpr std::string_view kCompilerVersion = "unknown";
#endif

#if defined(_WIN32)
constexpr std::string_view kPlatform = "Windows";
#elif defined(__APPLE__)
constexpr std::string_view kPlatform = "macOS";
#elif defined(__linux__)
constexpr std::string_view kPlatform = "Linux";
#else
constexpr std::string_view kPlatform = "unknown";
#endif

#if defined(__x86_64__) || defined(_M_X64)
constexpr std::string_view kArchitecture = "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
constexpr std::string_view kArchitecture = "arm64";
#else
constexpr std::string_view kArchitecture = "unknown";
#endif

#ifdef BETTERCAD_CORE_STATIC_DEFINE
constexpr bool kSharedLibraries = false;
#else
constexpr bool kSharedLibraries = true;
#endif

constexpr std::string_view buildConfig() noexcept {
    constexpr std::string_view config = BETTERCAD_BUILD_CONFIG;
    return config.empty() ? std::string_view{"unspecified"} : config;
}

} // namespace

const BuildInfo& buildInfo() noexcept {
    static const BuildInfo info{
        .version = BETTERCAD_VERSION_STRING,
        .versionMajor = BETTERCAD_VERSION_MAJOR,
        .versionMinor = BETTERCAD_VERSION_MINOR,
        .versionPatch = BETTERCAD_VERSION_PATCH,
        .gitRevision = detail::gitRevision(),
        .gitDirty = detail::gitDirty(),
        .buildType = buildConfig(),
        .compilerId = kCompilerId,
        .compilerVersion = kCompilerVersion,
        .cplusplus = static_cast<long>(__cplusplus),
        .platform = kPlatform,
        .architecture = kArchitecture,
        .sharedLibraries = kSharedLibraries,
    };
    return info;
}

std::string_view cppStandardName(long cplusplus) noexcept {
    if (cplusplus > 202302L) {
        return "C++26 (draft)";
    }
    if (cplusplus == 202302L) {
        return "C++23";
    }
    if (cplusplus >= 202002L) {
        return "C++20";
    }
    if (cplusplus >= 201703L) {
        return "C++17";
    }
    return "pre-C++17";
}

std::string formatBuildInfo(const BuildInfo& info) {
    const std::string revision =
        info.gitDirty ? std::format("{} (modified)", info.gitRevision) : std::string{info.gitRevision};
    return std::format("BetterCAD {}\n"
                       "  revision   : {}\n"
                       "  build type : {}\n"
                       "  compiler   : {} {}\n"
                       "  language   : {} (__cplusplus={})\n"
                       "  platform   : {} {}\n"
                       "  libraries  : {}\n",
                       info.version, revision, info.buildType, info.compilerId,
                       info.compilerVersion, cppStandardName(info.cplusplus), info.cplusplus,
                       info.platform, info.architecture, info.sharedLibraries ? "shared" : "static");
}

} // namespace bettercad
