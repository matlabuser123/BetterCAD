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

    // THE GLOBAL BOUND GOES THROUGH Ng_RestrictMeshSizeGlobal, NOT maxh.
    //
    // This is the single most surprising thing about sizing via nglib, and it
    // was established by measurement and then by reading the source rather
    // than from the parameter's name. Setting Ng_Meshing_Parameters::maxh
    // alone changes NOTHING on this pathway: a 40 mm block meshed with maxh of
    // 20, 10 and 5 mm gave the identical 9-node, 12-tetrahedron mesh three
    // times.
    //
    // The volume mesher asks Mesh::GetH(p), which is
    //
    //     min(hglob, localh(p))                 (meshclass.cpp)
    //
    // where hglob is set ONLY by SetGlobalH -- that is,
    // Ng_RestrictMeshSizeGlobal -- and localh is a tree built by CalcLocalH
    // from the SURFACE ELEMENT SIZES. BetterCAD supplies the surface, so for a
    // coarsely triangulated body localh is as big as the body and maxh never
    // gets a say: maxh is consulted only for a domain maximum
    // (meshfunc.cpp) and inside one local decision (meshing3.cpp).
    //
    // So both are set. hglob is the one that works; maxh is set to the same
    // value for consistency and because it does participate in those two
    // places.
    if (maxElementSize > 0.0) {
        nglib::Ng_RestrictMeshSizeGlobal(mesh.get(), maxElementSize);
    }

    // LOCAL SIZING, before Ng_GenerateVolumeMesh.
    //
    // The order matters and is established from Netgen's source, not guessed.
    // Ng_RestrictMeshSizePoint -> Mesh::RestrictLocalH, which CREATES the
    // mesh-size tree if none exists. Ng_GenerateVolumeMesh then calls
    // Mesh::CalcLocalH, whose first act is
    //
    //     if (!lochfunc[layer-1]) { ... SetLocalH(...); }
    //
    // -- it creates the tree only when ABSENT, so restrictions set here
    // survive. CalcLocalH then loops over the surface elements calling
    // LocalH::SetH, which looks like it would overwrite them and does not:
    // SetH begins
    //
    //     if (box->HOpt() <= 1.2 * h) return;
    //
    // so it only ever REFINES and cannot coarsen a restriction already in
    // place. Restricting AFTER generation would do nothing at all.
    for (const SizeRestriction& restriction : request.localSizes) {
        std::array<double, 3> at{restriction.at.x.si(), restriction.at.y.si(),
                                 restriction.at.z.si()};
        nglib::Ng_RestrictMeshSizePoint(mesh.get(), at.data(), restriction.maxSize.si());
    }

    // Regions, through nglib's own box mechanism. Ng_RestrictMeshSizeBox walks
    // a grid of step h across the box calling RestrictLocalH at each node, so
    // the restricted region genuinely has volume -- which a restriction
    // confined to a face's surface does not.
    for (const BoxSizeRestriction& region : request.localRegions) {
        std::array<double, 3> lo{region.min.x.si(), region.min.y.si(), region.min.z.si()};
        std::array<double, 3> hi{region.max.x.si(), region.max.y.si(), region.max.z.si()};
        nglib::Ng_RestrictMeshSizeBox(mesh.get(), lo.data(), hi.data(), region.maxSize.si());
    }

    nglib::Ng_Meshing_Parameters parameters;
    if (maxElementSize > 0.0) {
        parameters.maxh = maxElementSize;
    }

    // EVERY PARAMETER nglib ACTUALLY TRANSFERS IS PINNED HERE.
    //
    // Ng_Meshing_Parameters::Transfer_Parameters() is the only route from this
    // struct into the mesher, and it copies exactly fourteen fields. Each one
    // is set explicitly below so that a BetterCAD document's meaning cannot
    // change because a future Netgen altered a default. The values are
    // Netgen 6.2.2604's own defaults where BetterCAD has no reason to differ --
    // which makes them BETTERCAD's values now, not Netgen's.
    //
    // Seven fields the header declares are NOT transferred and are therefore
    // inert on this pathway -- fineness, closeedgeenable, closeedgefact,
    // minedgelenenable, minedgelen, optsurfmeshenable, optvolmeshenable.
    // Setting them would be theatre; see
    // docs/verification/P16-SIZE-001/AUDIT.md.
    //
    // uselocalh GATES THE LOCAL SIZE FUNCTION (libsrc/meshing/meshing3.cpp
    // reads mp.uselocalh), so local sizing silently does nothing without it.
    // It is the one pinned value this milestone actively depends on.
    parameters.uselocalh = 1;
    // minh = 0 means NO minimum, and that is a deliberate choice rather than a
    // copied default. Mesh::RestrictLocalH begins
    //     if (hloc < hmin) hloc = hmin;
    // so a non-zero minimum would SILENTLY RAISE a local target the caller
    // asked for. BetterCAD does not clamp sizing requests, so the mechanism
    // that would is switched off.
    parameters.minh = 0.0;
    // Growth away from a refinement. Not exposed canonically (its engineering
    // meaning is not statable yet), so it is fixed here instead of inherited.
    //
    // 0.8 AND NOT NETGEN'S OWN 0.3, and the reason is a trap worth recording.
    // Mesh::RestrictLocalH creates the mesh-size tree when none exists yet:
    //
    //     if (!lochfunc[layer-1]) { ... SetLocalH (boxmin, boxmax, 0.8, layer); }
    //
    // -- a HARDCODED 0.8, which bypasses mparam.grading entirely. Ng_Generate-
    // VolumeMesh's own CalcLocalH would have used mparam.grading, but it only
    // builds the tree when one is absent, and a local restriction has already
    // built it by then.
    //
    // So with a local control the grading is 0.8 and without one it is
    // whatever mparam says. Measured, that made merely HAVING a control change
    // the whole mesh: a cylinder's interior near the refined face came out
    // COARSER than with no control at all, because 0.8 lets size grow away
    // from a constraint far faster than 0.3. Sizing intent must not depend on
    // whether another control happens to exist, so BetterCAD pins the value
    // the restriction path forces, and both paths now agree.
    parameters.grading = 0.8;
    // Surface-side controls. BetterCAD supplies an already-triangulated
    // boundary, so these cannot affect this pathway; pinned for version
    // stability rather than for effect.
    parameters.elementsperedge = 2.0;
    parameters.elementspercurve = 2.0;
    parameters.optsteps_2d = 3;
    // Volume optimisation steps: this one does affect the result.
    parameters.optsteps_3d = 3;
    // Input checking, left on: it is the backend's own refusal of a bad
    // boundary, and turning it off would hide a defect P16-SURF should have
    // caught.
    parameters.check_overlap = 1;
    parameters.check_overlapping_boundary = 1;
    parameters.meshsize_filename = nullptr;
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
