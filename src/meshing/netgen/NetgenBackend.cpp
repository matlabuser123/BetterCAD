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

#include <algorithm>
#include <cmath>
#include <format>
#include <mutex>
#include <vector>

namespace bettercad::meshing {
namespace {

/// Serialises every call into the backend.
///
/// nglib keeps GLOBAL state: Ng_Init/Ng_Exit bracket it, and the mesher reads
/// module-level meshing parameters. Two threads meshing at once would share
/// that state, so they do not get to. Documented rather than hidden, because
/// the alternative -- hoping no caller ever threads -- is how a mesher becomes
/// intermittently wrong.
std::mutex& backendMutex() {
    static std::mutex mutex;
    return mutex;
}

/// Ng_Init / Ng_Exit around one generation, exception-safe.
///
/// Init-per-generation rather than once per process, deliberately: it gives
/// each generation the same starting state, which is what makes repeated
/// meshing of one body produce one answer. The cycle was proven to survive
/// repetition in INFRA-NETGEN-001 before being relied on here.
class LibraryScope {
public:
    LibraryScope() { nglib::Ng_Init(); }
    ~LibraryScope() { nglib::Ng_Exit(); }

    LibraryScope(const LibraryScope&) = delete;
    LibraryScope& operator=(const LibraryScope&) = delete;
    LibraryScope(LibraryScope&&) = delete;
    LibraryScope& operator=(LibraryScope&&) = delete;
};

/// Owns an Ng_Mesh so that no return path leaks one.
class MeshScope {
public:
    MeshScope() : mesh_(nglib::Ng_NewMesh()) {}
    ~MeshScope() {
        if (mesh_ != nullptr) {
            nglib::Ng_DeleteMesh(mesh_);
        }
    }

    MeshScope(const MeshScope&) = delete;
    MeshScope& operator=(const MeshScope&) = delete;
    MeshScope(MeshScope&&) = delete;
    MeshScope& operator=(MeshScope&&) = delete;

    [[nodiscard]] nglib::Ng_Mesh* get() const noexcept { return mesh_; }

private:
    nglib::Ng_Mesh* mesh_ = nullptr;
};

[[nodiscard]] std::unexpected<Error> failure(VolumeBackendFailure reason, std::string detail = {}) {
    std::string message = std::format("volume backend: {}", toString(reason));
    if (!detail.empty()) {
        message += std::format(" ({})", detail);
    }
    // InvalidArgument for a request BetterCAD got wrong, Internal for the
    // backend letting us down. The distinction matters to a caller deciding
    // whether to fix its input or report a dependency defect.
    const ErrorCode code = reason == VolumeBackendFailure::EmptyBoundary ||
                                   reason == VolumeBackendFailure::MalformedRequest ||
                                   reason == VolumeBackendFailure::InvalidElementSize
                               ? ErrorCode::InvalidArgument
                               : ErrorCode::Internal;
    return makeError(code, std::move(message));
}

} // namespace

VolumeBackendInfo volumeBackend() noexcept {
    // NETGEN_VERSION comes from the backend's own generated header rather
    // than from BetterCAD's build files, so a deps prefix holding a different
    // Netgen than the one that was pinned is visible instead of assumed.
    return VolumeBackendInfo{.available = true, .name = "Netgen", .version = NETGEN_VERSION};
}

bool volumeBackendResponds() noexcept {
    // Ng_Init/Ng_Exit bracket a single empty mesh. Enough to force the DLL and
    // its runtime closure to load and to prove the entry points are callable;
    // deliberately not enough to mesh anything.
    const std::lock_guard<std::mutex> lock(backendMutex());
    const LibraryScope library;
    const MeshScope mesh;
    return mesh.get() != nullptr;
}

Result<VolumeBackendMesh> generateTetrahedra(const VolumeBackendRequest& request) {
    if (request.points.empty() || request.triangles.empty()) {
        return failure(VolumeBackendFailure::EmptyBoundary);
    }

    // Every index is checked HERE, before the backend sees it. nglib takes raw
    // int arrays and would read out of bounds rather than complain.
    const auto pointCount = static_cast<std::uint32_t>(request.points.size());
    for (const std::array<std::uint32_t, 3>& triangle : request.triangles) {
        for (std::size_t corner = 0; corner < 3; ++corner) {
            if (triangle[corner] >= pointCount) {
                return failure(VolumeBackendFailure::MalformedRequest,
                                         std::format("triangle index {} of {} points",
                                                     triangle[corner], pointCount));
            }
        }
        if (triangle[0] == triangle[1] || triangle[1] == triangle[2] || triangle[0] == triangle[2]) {
            return failure(VolumeBackendFailure::MalformedRequest, "a triangle repeats a node");
        }
    }
    for (const Point3D& point : request.points) {
        if (!std::isfinite(point.x.si()) || !std::isfinite(point.y.si()) ||
            !std::isfinite(point.z.si())) {
            return failure(VolumeBackendFailure::MalformedRequest, "a coordinate is not finite");
        }
    }

    double maxElementSize = 0.0;
    if (request.maxElementSize.has_value()) {
        maxElementSize = request.maxElementSize->si();
        if (!(maxElementSize > 0.0) || !std::isfinite(maxElementSize)) {
            return failure(VolumeBackendFailure::InvalidElementSize);
        }
    }

    const std::lock_guard<std::mutex> lock(backendMutex());
    const LibraryScope library;
    MeshScope mesh;
    if (mesh.get() == nullptr) {
        return failure(VolumeBackendFailure::GenerationFailed, "Ng_NewMesh returned null");
    }

    // Netgen numbers points from 1. Everything above this line is 0-based and
    // everything below adds or subtracts exactly one, in one place each way, so
    // the off-by-one cannot be half-applied.
    for (const Point3D& point : request.points) {
        std::array<double, 3> coordinates{point.x.si(), point.y.si(), point.z.si()};
        nglib::Ng_AddPoint(mesh.get(), coordinates.data());
    }
    for (const std::array<std::uint32_t, 3>& triangle : request.triangles) {
        std::array<int, 3> indices{static_cast<int>(triangle[0]) + 1,
                                   static_cast<int>(triangle[1]) + 1,
                                   static_cast<int>(triangle[2]) + 1};
        nglib::Ng_AddSurfaceElement(mesh.get(), nglib::NG_TRIG, indices.data());
    }

    nglib::Ng_Meshing_Parameters parameters;
    if (maxElementSize > 0.0) {
        parameters.maxh = maxElementSize;
    }
    // Tet4 is P16's only volume element (ADR-031), so second order is pinned
    // OFF rather than left to a default: a Tet10 would arrive as ten nodes
    // through an interface that promises four, and the type check below would
    // reject the whole mesh for a reason that looks like a backend defect.
    parameters.second_order = 0;
    parameters.quad_dominated = 0;
    // The boundary is already closed, manifold and coherently oriented by
    // P16-SURF-001, so nothing here may flip it.
    parameters.invert_tets = 0;
    parameters.invert_trigs = 0;

    const nglib::Ng_Result status = nglib::Ng_GenerateVolumeMesh(mesh.get(), &parameters);
    if (status == nglib::NG_SURFACE_INPUT_ERROR || status == nglib::NG_SURFACE_FAILURE) {
        return failure(VolumeBackendFailure::SurfaceRejected,
                                 std::format("Ng_GenerateVolumeMesh returned {}",
                                             static_cast<int>(status)));
    }
    if (status != nglib::NG_OK) {
        return failure(VolumeBackendFailure::GenerationFailed,
                                 std::format("Ng_GenerateVolumeMesh returned {}",
                                             static_cast<int>(status)));
    }

    // NG_OK IS NOT EVIDENCE OF SUCCESS. nglib returns it with zero elements
    // when it gives up on a surface, after printing its own complaint. This is
    // the single most important line in the file.
    const int nodeCount = nglib::Ng_GetNP(mesh.get());
    const int elementCount = nglib::Ng_GetNE(mesh.get());
    if (elementCount <= 0 || nodeCount <= 0) {
        return failure(VolumeBackendFailure::NoTetrahedra,
                                 std::format("Ng_GenerateVolumeMesh reported success with {} "
                                             "node(s) and {} element(s)",
                                             nodeCount, elementCount));
    }

    VolumeBackendMesh result;
    result.points.reserve(static_cast<std::size_t>(nodeCount));
    for (int node = 1; node <= nodeCount; ++node) {
        std::array<double, 3> coordinates{};
        nglib::Ng_GetPoint(mesh.get(), node, coordinates.data());
        if (!std::isfinite(coordinates[0]) || !std::isfinite(coordinates[1]) ||
            !std::isfinite(coordinates[2])) {
            return failure(VolumeBackendFailure::InconsistentOutput,
                                     std::format("node {} has a non-finite coordinate", node));
        }
        result.points.push_back(Point3D{Length::fromSi(coordinates[0]),
                                        Length::fromSi(coordinates[1]),
                                        Length::fromSi(coordinates[2])});
    }

    result.tetrahedra.reserve(static_cast<std::size_t>(elementCount));
    for (int element = 1; element <= elementCount; ++element) {
        // Sized for the largest element nglib can report, not for Tet4, so a
        // Tet10 cannot overrun this buffer on its way to being rejected.
        std::array<int, 10> indices{};
        const nglib::Ng_Volume_Element_Type type =
            nglib::Ng_GetVolumeElement(mesh.get(), element, indices.data());
        if (type != nglib::NG_TET) {
            return failure(VolumeBackendFailure::InconsistentOutput,
                                     std::format("element {} is volume type {}, not a Tet4",
                                                 element, static_cast<int>(type)));
        }
        std::array<std::uint32_t, 4> nodes{};
        for (std::size_t corner = 0; corner < 4; ++corner) {
            const int index = indices[corner];
            if (index < 1 || index > nodeCount) {
                return failure(VolumeBackendFailure::InconsistentOutput,
                            std::format("element {} names node {} of {}", element, index, nodeCount));
            }
            nodes[corner] = static_cast<std::uint32_t>(index - 1);
        }

        // ORIENTATION IS TRANSLATED HERE, which is what an adapter is for:
        // ADR-033 says "the Mesh a backend produces is BetterCAD's Mesh,
        // translated at the boundary", and a node-ordering convention is part
        // of the representation being translated.
        //
        // Netgen orders a tetrahedron's nodes so that
        // det(p1 - p0, p2 - p0, p3 - p0) is NEGATIVE. BetterCAD's convention is
        // the opposite: a positive signed volume is the valid orientation
        // (ADR-032), and validation refuses a negative one. Without this swap
        // every element Netgen produces arrives inverted -- measured, not
        // guessed: a 20x30x40 box came back as 12 tetrahedra, 12 of them
        // negative.
        //
        // ONE SWAP, because a swap is an ODD permutation and therefore actually
        // flips the sign. Reversing all four nodes would NOT: (0,1,2,3) ->
        // (3,2,1,0) is (0 3)(1 2), two transpositions, an EVEN permutation that
        // leaves the signed volume exactly as it was. That mistake looks like a
        // fix and changes nothing.
        //
        // This is NOT abs() and NOT a per-element repair. The permutation is
        // fixed and unconditional, so a genuinely inverted element -- one
        // Netgen got wrong rather than one it ordered by its own convention --
        // still arrives negative and is still refused by validate(). The
        // uniformity this relies on is pinned by
        // VolumeBackend_ReturnsEveryElementInBetterCadsOrientation.
        std::swap(nodes[0], nodes[1]);
        result.tetrahedra.push_back(nodes);
    }

    return result;
}

} // namespace bettercad::meshing
