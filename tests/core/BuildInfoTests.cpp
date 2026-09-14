#include <bettercad/core/BuildInfo.hpp>
#include <bettercad/core/Version.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <format>
#include <string>

using bettercad::buildInfo;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::Matches;

TEST_CASE("Build info reports the project version configured by CMake", "[core][build-info]") {
    const auto& info = buildInfo();

    CHECK(info.version == BETTERCAD_EXPECTED_VERSION);
    CHECK(info.version == BETTERCAD_VERSION_STRING);
    CHECK(std::format("{}.{}.{}", info.versionMajor, info.versionMinor, info.versionPatch) ==
          info.version);
}

TEST_CASE("Build info reports the active build configuration", "[core][build-info]") {
    CHECK(buildInfo().buildType == BETTERCAD_EXPECTED_BUILD_CONFIG);
}

TEST_CASE("Build info identifies the compiler that built the core library", "[core][build-info]") {
    const auto& info = buildInfo();

#if defined(__clang__)
    CHECK(info.compilerId == "Clang");
#elif defined(__GNUC__)
    CHECK(info.compilerId == "GCC");
    CHECK(info.compilerVersion ==
          std::format("{}.{}.{}", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__));
#elif defined(_MSC_VER)
    CHECK(info.compilerId == "MSVC");
#endif
    CHECK_THAT(std::string{info.compilerVersion}, Matches(R"([0-9]+(\.[0-9]+)*)"));
}

TEST_CASE("Core library is compiled as C++23 or newer", "[core][build-info]") {
    CHECK(buildInfo().cplusplus >= 202302L);
    CHECK(buildInfo().cplusplus == static_cast<long>(__cplusplus));
}

TEST_CASE("Build info reports the library linkage", "[core][build-info]") {
#ifdef BETTERCAD_EXPECT_SHARED_LIBS
    CHECK(buildInfo().sharedLibraries);
#else
    CHECK_FALSE(buildInfo().sharedLibraries);
#endif
}

TEST_CASE("C++ standard names map __cplusplus values", "[core][build-info]") {
    CHECK(bettercad::cppStandardName(201703L) == "C++17");
    CHECK(bettercad::cppStandardName(202002L) == "C++20");
    CHECK(bettercad::cppStandardName(202302L) == "C++23");
    CHECK(bettercad::cppStandardName(202400L) == "C++26 (draft)");
    CHECK(bettercad::cppStandardName(201402L) == "pre-C++17");
}

TEST_CASE("Formatted build info contains every field", "[core][build-info]") {
    const auto& info = buildInfo();
    const std::string report = bettercad::formatBuildInfo(info);

    CHECK_THAT(report, ContainsSubstring(std::format("BetterCAD {}", info.version)));
    CHECK_THAT(report, ContainsSubstring(std::string{info.gitRevision}));
    CHECK_THAT(report, ContainsSubstring(std::format("build type : {}", info.buildType)));
    CHECK_THAT(report, ContainsSubstring(
                           std::format("compiler   : {} {}", info.compilerId, info.compilerVersion)));
    CHECK_THAT(report, ContainsSubstring(std::format("__cplusplus={}", info.cplusplus)));
    CHECK_THAT(report,
               ContainsSubstring(std::format("platform   : {} {}", info.platform, info.architecture)));
}

TEST_CASE("Formatted build info marks modified working trees", "[core][build-info]") {
    auto info = buildInfo();
    info.gitRevision = "0123456789ab";

    info.gitDirty = false;
    CHECK_THAT(bettercad::formatBuildInfo(info), ContainsSubstring("revision   : 0123456789ab\n"));

    info.gitDirty = true;
    CHECK_THAT(bettercad::formatBuildInfo(info),
               ContainsSubstring("revision   : 0123456789ab (modified)\n"));
}
