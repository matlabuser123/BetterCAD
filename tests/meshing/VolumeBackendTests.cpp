// INFRA-NETGEN-001: the volume-meshing backend links, loads and runs.
//
// These are infrastructure tests, not meshing tests. They prove that the
// third-party backend BetterCAD was configured against is actually present and
// callable in this process. They mesh nothing: volume meshing is P16-VOL-001's
// and is not implemented.
//
// WHY THE EXPECTATION IS COMPILED IN RATHER THAN DISCOVERED AT RUN TIME:
//
// A test that asks the library whether it is there, and then asserts whatever
// the answer was, cannot fail. BETTERCAD_TESTS_EXPECT_NETGEN is defined by the
// build exactly when CMake found and linked Netgen, so the build's intent and
// the binary's behaviour are compared against each other. If the backend were
// configured in but silently failed to load, these fail.
#include <bettercad/meshing/VolumeBackend.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using namespace bettercad::meshing;

TEST_CASE("VolumeBackend_IdentityIsConsistentWithAvailability", "[meshing][backend]")
{
    const VolumeBackendInfo info = volumeBackend();

    if (info.available)
    {
        CHECK_FALSE(info.name.empty());
        CHECK(info.name != "none");
        CHECK_FALSE(info.version.empty());
    }
    else
    {
        CHECK(info.name == "none");
        CHECK(info.version.empty());
        // Without a backend the probe must say so rather than pretending.
        CHECK_FALSE(volumeBackendResponds());
    }
}

#ifdef BETTERCAD_TESTS_EXPECT_NETGEN

TEST_CASE("VolumeBackend_IsNetgenWhenTheBuildLinkedIt", "[meshing][backend]")
{
    const VolumeBackendInfo info = volumeBackend();
    REQUIRE(info.available);
    CHECK(info.name == "Netgen");
}

TEST_CASE("VolumeBackend_ReportsThePinnedNetgenVersion", "[meshing][backend]")
{
    // The pinned version in deps/CMakeLists.txt is v6.2.2604.
    //
    // This is a regression test for a real defect, not a tautology. Netgen
    // derives its version with "git describe"; built from a source archive it
    // finds no git metadata, falls through to a hardcoded default and reports
    // itself as "6.2.0" while in fact being 6.2.2604. BetterCAD's patch 0004
    // passes the pinned version in instead. Without that patch this fails.
    CHECK(volumeBackend().version == std::string_view{"6.2.2604"});
}

TEST_CASE("VolumeBackend_InitialisesAndShutsDownCleanly", "[meshing][backend]")
{
    // Proves the DLL and its whole runtime closure -- ngcore and zlib as well
    // as nglib -- actually load. A link-time check alone would not: the
    // executable links against import libraries and can still fail at startup.
    CHECK(volumeBackendResponds());
}

TEST_CASE("VolumeBackend_SurvivesRepeatedInitialisation", "[meshing][backend]")
{
    // Ng_Init/Ng_Exit are global library state. Running the cycle repeatedly
    // in one process must stay stable: ctest --repeat re-runs tests in fresh
    // processes, but a single process reaching the backend more than once is
    // exactly what P16-VOL-001 will do.
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        INFO("attempt " << attempt);
        CHECK(volumeBackendResponds());
    }
}

#endif  // BETTERCAD_TESTS_EXPECT_NETGEN
