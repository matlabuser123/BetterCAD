#include <bettercad/meshing/VolumeBackend.hpp>

// THE ONLY PLACE IN BETTERCAD THAT MAY INCLUDE A MESH-BACKEND HEADER.
//
// tests/architecture/CheckLayering.cmake rule 5 confines nglib.h and every
// other candidate backend's headers to src/meshing/<backend>/, and fails the
// build otherwise. Nothing above this directory knows Netgen exists.
//
// nglib.h does NOT open a namespace of its own. Netgen's own nglib.cpp does
//
//     namespace nglib { #include "nglib.h" }
//
// so every nglib symbol is really nglib::Ng_*, mangled as such in the DLL.
// A consumer that includes the header at global scope compiles cleanly and
// then fails to link with "undefined reference to __imp__Z10Ng_NewMeshv".
// Wrapping the include the same way is the documented way to use it, not a
// workaround.
namespace nglib {
#include <nglib.h>
}

#include <netgen_version.hpp>

namespace bettercad::meshing {

VolumeBackendInfo volumeBackend() noexcept
{
    // NETGEN_VERSION comes from the backend's own generated header rather
    // than from BetterCAD's build files, so a deps prefix holding a different
    // Netgen than the one that was pinned is visible instead of assumed.
    return VolumeBackendInfo{.available = true, .name = "Netgen", .version = NETGEN_VERSION};
}

bool volumeBackendResponds() noexcept
{
    // Ng_Init/Ng_Exit bracket a single empty mesh. Enough to force the DLL and
    // its runtime closure to load and to prove the entry points are callable;
    // deliberately not enough to mesh anything.
    nglib::Ng_Init();
    nglib::Ng_Mesh* const mesh = nglib::Ng_NewMesh();
    const bool created = mesh != nullptr;
    if (created)
    {
        nglib::Ng_DeleteMesh(mesh);
    }
    nglib::Ng_Exit();
    return created;
}

}  // namespace bettercad::meshing
