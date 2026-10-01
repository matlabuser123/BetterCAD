#include <bettercad/meshing/VolumeBackend.hpp>

// Compiled instead of src/meshing/netgen/NetgenBackend.cpp when BetterCAD is
// configured without a volume-meshing backend (BETTERCAD_VOLUME_MESHING=OFF,
// or AUTO with none found).
//
// The API stays total: callers get a truthful "no backend" rather than a link
// error or a preprocessor fork at every call site. The reason the whole
// translation unit is swapped, rather than guarding the Netgen one with
// #ifdef, is that a backend header must not appear in a file that is compiled
// when the backend is absent -- the architecture check confines those headers
// to src/meshing/<backend>/ regardless of configuration.
namespace bettercad::meshing {

VolumeBackendInfo volumeBackend() noexcept
{
    return VolumeBackendInfo{};
}

bool volumeBackendResponds() noexcept
{
    return false;
}

}  // namespace bettercad::meshing
