#pragma once

#include <bettercad/meshing/Export.hpp>

#include <string_view>

// The volume-meshing backend's presence and liveness (INFRA-NETGEN-001).
//
// WHAT THIS IS, AND WHAT IT IS DELIBERATELY NOT:
//
// This is a *toolchain probe*. It answers two questions and no others:
// whether BetterCAD was built against a volume-meshing backend, and whether
// that backend actually links, loads and runs in this process. It converts no
// geometry, meshes nothing, and holds no state.
//
// Volume meshing itself -- feeding P16-SURF-001's validated engineering
// surface to the backend and validating the tetrahedra that come back -- is
// P16-VOL-001's and does not exist yet. Nothing here should grow into it: when
// that milestone is implemented, the adapter is a separate interface and this
// probe stays what it is.
//
// WHY IT IS PERMANENT RATHER THAN A ONE-OFF SCRIPT:
//
// The backend is a third-party native library resolved at configure time,
// loaded from a DLL at run time, and built by a different project with a
// different build system. Every one of those steps can break without a
// BetterCAD source change -- a stale deps prefix, a half-installed
// dependency, a DLL that links but cannot load because its own dependency is
// missing. A probe compiled and run by the ordinary test suite turns all of
// that from a confusing downstream failure into one named test.
namespace bettercad::meshing {

/// Which volume-meshing backend this build was configured with.
struct VolumeBackendInfo
{
    /// True when a backend was found at configure time and linked in.
    bool available = false;
    /// Backend name, or "none" when `available` is false.
    std::string_view name = "none";
    /// Backend version as the backend itself reports it, or "" when absent.
    /// Read from the backend, never hardcoded: a dependency that misreports
    /// its own version is a defect this is meant to expose, not hide.
    std::string_view version = "";
};

/// Reports the configured backend. Does not load or call it.
[[nodiscard]] BETTERCAD_MESHING_EXPORT VolumeBackendInfo volumeBackend() noexcept;

/// Initialises the backend, then shuts it down again.
///
/// Returns false when no backend is configured. When one is, this proves the
/// library links, its DLL and the whole of its runtime closure load, and its
/// entry points can be called -- which a compile-and-link check alone does
/// not. It meshes nothing.
[[nodiscard]] BETTERCAD_MESHING_EXPORT bool volumeBackendResponds() noexcept;

}  // namespace bettercad::meshing
