#pragma once

#include <bettercad/core/Export.hpp>

#include <string>
#include <string_view>

namespace bettercad {

/// Describes how this BetterCAD binary was built. Used for `--version`
/// reporting, bug reports and diagnostics.
struct BuildInfo {
    std::string_view version; ///< Semantic version, e.g. "0.1.0".
    int versionMajor;
    int versionMinor;
    int versionPatch;
    std::string_view gitRevision; ///< Short commit hash, or "unknown".
    bool gitDirty;                ///< Tracked files had uncommitted changes at build time.
    std::string_view buildType;   ///< Build configuration, e.g. "Debug" or "Release".
    std::string_view compilerId;  ///< "GCC", "Clang", "MSVC" or "unknown".
    std::string_view compilerVersion;
    long cplusplus;                ///< Value of __cplusplus used to compile the core library.
    std::string_view platform;     ///< "Windows", "Linux", "macOS" or "unknown".
    std::string_view architecture; ///< "x86_64", "arm64" or "unknown".
    bool sharedLibraries;          ///< Core library was built as a shared library.
};

/// Build information of the core library.
[[nodiscard]] BETTERCAD_CORE_EXPORT const BuildInfo& buildInfo() noexcept;

/// Name of the C++ standard for a __cplusplus value, e.g. "C++23".
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string_view cppStandardName(long cplusplus) noexcept;

/// Multi-line, human-readable report of @p info.
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string formatBuildInfo(const BuildInfo& info);

} // namespace bettercad
