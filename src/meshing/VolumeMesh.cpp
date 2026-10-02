#include <bettercad/meshing/VolumeMesh.hpp>

#include <bettercad/meshing/MeshValidation.hpp>
#include <bettercad/meshing/VolumeBackend.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <set>
#include <utility>

namespace bettercad::meshing {
namespace {

/// The one region a volume mesh uses today.
///
/// ADR-032 gives a mesh "one region per solid", and `volumeMeshFor` refuses a
/// body with more than one solid precisely so that this single region is never
/// a lie about a multi-solid body.
constexpr RegionId kVolume = RegionId::fromValue(1);

/// A node position as an exact key.
///
/// The same rule, and the same justification, as P16-SURF-001's node
/// unification: the backend returns the boundary points it was given, bit for
/// bit, because it copies them rather than recomputing them. So matching a
/// boundary face against a surface triangle by exact coordinate is a
/// topological identity and not an approximation, and there is no tolerance
/// here to tune. If a backend ever moved a boundary node, the conformity check
/// would report it as unmatched -- which is the honest outcome, not a reason to
/// add an epsilon.
struct PositionKey {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    friend auto operator<=>(const PositionKey&, const PositionKey&) = default;
    friend bool operator==(const PositionKey&, const PositionKey&) = default;
};

[[nodiscard]] PositionKey keyOf(const Point3D& point) noexcept {
    return PositionKey{point.x.si(), point.y.si(), point.z.si()};
}

/// Three positions as an unordered key: a face identity independent of winding
/// and of which node happens to be listed first.
using FaceKey = std::array<PositionKey, 3>;

[[nodiscard]] FaceKey faceKeyOf(const Point3D& a, const Point3D& b, const Point3D& c) {
    FaceKey key{keyOf(a), keyOf(b), keyOf(c)};
    std::sort(key.begin(), key.end());
    return key;
}

/// The four faces of a tetrahedron, each wound to face OUT of it.
///
/// With a positive signed volume on (n0,n1,n2,n3), these windings give outward
/// normals. They are written out rather than generated, because an index
/// permutation produced by a loop is exactly the kind of thing that is wrong in
/// one of four cases and still passes a test that only counts faces.
constexpr std::array<std::array<std::size_t, 3>, 4> kTetFaces{{
    {0, 2, 1},
    {0, 1, 3},
    {1, 2, 3},
    {0, 3, 2},
}};

[[nodiscard]] std::unexpected<Error> failure(VolumeMeshFailure reason, std::string detail = {}) {
    std::string message = std::format("volume mesh: {}", toString(reason));
    if (!detail.empty()) {
        message += std::format(" ({})", detail);
    }
    const ErrorCode code = reason == VolumeMeshFailure::BackendUnavailable
                               ? ErrorCode::FailedPrecondition
                           : reason == VolumeMeshFailure::SurfaceNotUsable ||
                                   reason == VolumeMeshFailure::MultipleSolids
                               ? ErrorCode::InvalidArgument
                               : ErrorCode::Internal;
    return makeError(code, std::move(message));
}

} // namespace

std::string_view toString(VolumeBackendFailure failure) noexcept {
    switch (failure) {
    case VolumeBackendFailure::NotAvailable:
        return "no volume-meshing backend in this build";
    case VolumeBackendFailure::EmptyBoundary:
        return "the boundary has no points or no triangles";
    case VolumeBackendFailure::MalformedRequest:
        return "the boundary request is malformed";
    case VolumeBackendFailure::InvalidElementSize:
        return "the requested element size is not positive and finite";
    case VolumeBackendFailure::SurfaceRejected:
        return "the backend rejected the surface";
    case VolumeBackendFailure::GenerationFailed:
        return "the backend failed to generate a volume mesh";
    case VolumeBackendFailure::NoTetrahedra:
        return "the backend reported success and produced no tetrahedra";
    case VolumeBackendFailure::InconsistentOutput:
        return "the backend returned an inconsistent mesh";
    }
    return "unknown volume backend failure";
}

std::string_view toString(VolumeMeshFailure failure) noexcept {
    switch (failure) {
    case VolumeMeshFailure::BackendUnavailable:
        return "this build has no volume-meshing backend";
    case VolumeMeshFailure::SurfaceNotUsable:
        return "the boundary surface is empty or does not validate";
    case VolumeMeshFailure::MultipleSolids:
        return "the body holds more than one solid";
    case VolumeMeshFailure::BackendFailed:
        return "the backend produced no usable tetrahedralisation";
    case VolumeMeshFailure::InvalidMesh:
        return "the generated mesh is not data-valid";
    case VolumeMeshFailure::BoundaryNotConforming:
        return "the mesh boundary is not the surface it was built from";
    case VolumeMeshFailure::VolumeNotRecovered:
        return "the tetrahedra do not fill the volume the boundary encloses";
    }
    return "unknown volume mesh failure";
}

Volume tetrahedralVolume(const Mesh& mesh) {
    double total = 0.0;
    for (const Tetrahedron& tet : mesh.tetrahedra()) {
        const Node* n0 = mesh.findNode(tet.nodes[0]);
        const Node* n1 = mesh.findNode(tet.nodes[1]);
        const Node* n2 = mesh.findNode(tet.nodes[2]);
        const Node* n3 = mesh.findNode(tet.nodes[3]);
        if (n0 == nullptr || n1 == nullptr || n2 == nullptr || n3 == nullptr) {
            continue;
        }
        total += signedVolume(n0->position, n1->position, n2->position, n3->position).si();
    }
    return Volume::fromSi(total);
}

std::vector<std::array<NodeId, 3>> tetrahedralBoundary(const Mesh& mesh) {
    // Keyed on the SORTED node handles, so the two tetrahedra sharing a face
    // agree on its identity whatever order each lists it in. The stored value
    // keeps the outward winding of the first tetrahedron that claimed it.
    std::map<std::array<NodeId, 3>, std::pair<std::array<NodeId, 3>, std::size_t>> faces;
    for (const Tetrahedron& tet : mesh.tetrahedra()) {
        for (const std::array<std::size_t, 3>& corners : kTetFaces) {
            const std::array<NodeId, 3> oriented{tet.nodes[corners[0]], tet.nodes[corners[1]],
                                                 tet.nodes[corners[2]]};
            std::array<NodeId, 3> key = oriented;
            std::sort(key.begin(), key.end());
            auto [entry, inserted] = faces.try_emplace(key, oriented, 1);
            if (!inserted) {
                ++entry->second.second;
            }
        }
    }

    // std::map, so the enumeration order is the sorted key order and is the
    // same in every build configuration.
    std::vector<std::array<NodeId, 3>> boundary;
    for (const auto& [key, value] : faces) {
        if (value.second == 1) {
            boundary.push_back(value.first);
        }
    }
    return boundary;
}

Result<VolumeMesh> generateVolumeMesh(const MeshableGeometry& geometry,
                                      const EngineeringSurfaceMesh& surface,
                                      const VolumeMeshControls& controls) {
    if (!volumeBackend().available) {
        return failure(VolumeMeshFailure::BackendUnavailable);
    }

    // ADR-032's one-region-per-solid is the eventual design; this milestone
    // implements one region, so a body with more than one solid is refused
    // rather than described by a region that would be a lie. Checked here, not
    // only in volumeMeshFor, so the direct entry point cannot bypass it.
    if (geometry.solidCount != 1) {
        return failure(VolumeMeshFailure::MultipleSolids,
                       std::format("{} solid(s)", geometry.solidCount));
    }

    // The input must be a USABLE engineering surface. generateSurfaceMesh
    // cannot return one that is not, so this catches a surface assembled by
    // hand -- which is exactly how a viewer tessellation would arrive.
    if (surface.mesh.triangles().empty() || surface.mesh.nodes().empty()) {
        return failure(VolumeMeshFailure::SurfaceNotUsable, "the surface is empty");
    }
    // A boundary is triangles. If the caller handed over a mesh that already
    // holds volume elements -- a volume mesh passed as a surface -- the
    // triangles would be used and the tetrahedra silently ignored, and the
    // result would look like a successful remesh of something else. Refused
    // instead.
    if (!surface.mesh.tetrahedra().empty()) {
        return failure(VolumeMeshFailure::SurfaceNotUsable,
                       std::format("the surface already holds {} volume element(s)",
                                   surface.mesh.tetrahedra().size()));
    }

    // RE-VALIDATED, not trusted. SurfaceValidation is a plain struct and a
    // caller can zero every count, so the report that decides this is the one
    // computed HERE from the triangles themselves. Trusting the field would
    // make the gate a label.
    const SurfaceValidation checked = validateSurface(surface.mesh);
    if (!checked.valid()) {
        return failure(VolumeMeshFailure::SurfaceNotUsable,
                       std::format("not a watertight manifold boundary: {} boundary edge(s), {} "
                                   "non-manifold edge(s), {} orientation conflict(s), {} "
                                   "degenerate, {} duplicate, {} unused node(s)",
                                   checked.boundaryEdgeCount, checked.nonManifoldEdgeCount,
                                   checked.orientationConflictCount,
                                   checked.degenerateTriangleCount, checked.duplicateTriangleCount,
                                   checked.unusedNodeCount));
    }

    // Likewise the enclosed volume: recomputed from the oriented triangles, so
    // the comparison at the end of this function is against something this
    // function established rather than something it was told.
    const Volume enclosed = enclosedVolume(surface.mesh);
    if (!(enclosed.si() > 0.0)) {
        return failure(VolumeMeshFailure::SurfaceNotUsable,
                       std::format("the surface encloses {} m^3, so it is inward or degenerate",
                                   enclosed.si()));
    }

    // Translate the Mesh's handles into the seam's dense indices. The surface
    // mesh's node handles may be sparse, so this is a lookup and never an
    // arithmetic conversion.
    std::vector<Point3D> points;
    points.reserve(surface.mesh.nodes().size());
    std::map<NodeId, std::uint32_t> indexOf;
    for (const Node& node : surface.mesh.nodes()) {
        indexOf.emplace(node.id, static_cast<std::uint32_t>(points.size()));
        points.push_back(node.position);
    }

    std::vector<std::array<std::uint32_t, 3>> triangles;
    triangles.reserve(surface.mesh.triangles().size());
    for (const Triangle& triangle : surface.mesh.triangles()) {
        std::array<std::uint32_t, 3> indices{};
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto found = indexOf.find(triangle.nodes[corner]);
            if (found == indexOf.end()) {
                return failure(VolumeMeshFailure::SurfaceNotUsable,
                                         "a surface triangle names an unknown node");
            }
            indices[corner] = found->second;
        }
        triangles.push_back(indices);
    }

    const Result<VolumeBackendMesh> generated = generateTetrahedra(VolumeBackendRequest{
        .points = points, .triangles = triangles, .maxElementSize = controls.maxElementSize});
    if (!generated.has_value()) {
        // The backend's own diagnostic is QUOTED, not replaced: ADR-033 permits
        // a backend's error text inside a BetterCAD diagnostic and forbids its
        // types, and losing the detail would make a dependency failure
        // indistinguishable from a modelling one.
        return failure(VolumeMeshFailure::BackendFailed, generated.error().message);
    }

    // Build the canonical mesh: every returned node, then the tetrahedra, then
    // the boundary triangles of the tetrahedralisation itself.
    MeshBuilder builder;
    std::vector<NodeId> handles;
    handles.reserve(generated->points.size());
    for (const Point3D& point : generated->points) {
        const Result<NodeId> added = builder.addNode(point);
        if (!added.has_value()) {
            return failure(VolumeMeshFailure::InvalidMesh, added.error().message);
        }
        handles.push_back(*added);
    }

    for (const std::array<std::uint32_t, 4>& tet : generated->tetrahedra) {
        std::array<NodeId, 4> nodes{};
        for (std::size_t corner = 0; corner < 4; ++corner) {
            if (tet[corner] >= handles.size()) {
                return failure(VolumeMeshFailure::InvalidMesh,
                                         "a tetrahedron names a node the backend did not return");
            }
            nodes[corner] = handles[tet[corner]];
        }
        const Result<ElementId> added = builder.addTetrahedron(nodes, kVolume);
        if (!added.has_value()) {
            return failure(VolumeMeshFailure::InvalidMesh, added.error().message);
        }
    }

    Mesh volume = builder.build();

    // Conformity, computed from the tetrahedra's own connectivity and compared
    // with the input surface by exact position. The backend's account of its
    // surface is not consulted.
    const std::vector<std::array<NodeId, 3>> boundary = tetrahedralBoundary(volume);

    std::set<FaceKey> surfaceFaces;
    for (const Triangle& triangle : surface.mesh.triangles()) {
        const Node* a = surface.mesh.findNode(triangle.nodes[0]);
        const Node* b = surface.mesh.findNode(triangle.nodes[1]);
        const Node* c = surface.mesh.findNode(triangle.nodes[2]);
        if (a == nullptr || b == nullptr || c == nullptr) {
            return failure(VolumeMeshFailure::SurfaceNotUsable,
                                     "a surface triangle names an unknown node");
        }
        surfaceFaces.insert(faceKeyOf(a->position, b->position, c->position));
    }

    std::set<FaceKey> boundaryFaces;
    VolumeConformity conformity;
    conformity.volumeBoundaryFaceCount = boundary.size();
    conformity.surfaceTriangleCount = surface.mesh.triangles().size();
    for (const std::array<NodeId, 3>& face : boundary) {
        const Node* a = volume.findNode(face[0]);
        const Node* b = volume.findNode(face[1]);
        const Node* c = volume.findNode(face[2]);
        if (a == nullptr || b == nullptr || c == nullptr) {
            return failure(VolumeMeshFailure::InvalidMesh,
                                     "a boundary face names an unknown node");
        }
        const FaceKey key = faceKeyOf(a->position, b->position, c->position);
        boundaryFaces.insert(key);
        if (!surfaceFaces.contains(key)) {
            ++conformity.unmatchedBoundaryFaceCount;
        }
    }
    for (const FaceKey& key : surfaceFaces) {
        if (!boundaryFaces.contains(key)) {
            ++conformity.unmatchedSurfaceTriangleCount;
        }
    }

    // The boundary triangles go into the mesh with the winding the tetrahedra
    // imply, so a consumer gets one mesh carrying both the volume and its
    // surface rather than having to recompute the boundary.
    for (const std::array<NodeId, 3>& face : boundary) {
        const Result<ElementId> added = builder.addTriangle(face, kVolume);
        if (!added.has_value()) {
            return failure(VolumeMeshFailure::InvalidMesh, added.error().message);
        }
    }
    volume = builder.build();

    // VALIDATION COMES BEFORE ANY GEOMETRIC CLAIM. A mesh with an inverted or
    // degenerate element has no meaningful volume, so reporting that its volume
    // disagrees would describe a symptom and hide the cause.
    const MeshValidationReport report = validate(volume);
    if (!report.dataValid()) {
        std::string detail = std::format("{} issue(s), first: {}", report.issues.size(),
                                         toString(report.issues.front().kind));
        return failure(VolumeMeshFailure::InvalidMesh, std::move(detail));
    }

    if (!conformity.conforms()) {
        return failure(VolumeMeshFailure::BoundaryNotConforming,
                    std::format("{} boundary face(s) of {} unmatched, {} surface triangle(s) of "
                                "{} unmatched",
                                conformity.unmatchedBoundaryFaceCount,
                                conformity.volumeBoundaryFaceCount,
                                conformity.unmatchedSurfaceTriangleCount,
                                conformity.surfaceTriangleCount));
    }

    const Volume tetVolume = tetrahedralVolume(volume);

    // THE TETRAHEDRA TILE EXACTLY THE POLYHEDRON THE BOUNDARY BOUNDS, so this
    // is not an approximation and its tolerance is not a modelling choice: both
    // sides are sums of the same kind of determinant over the same vertices, and
    // they differ only by floating-point accumulation. The comparison is made
    // RELATIVE to the enclosed volume so it is scale-independent.
    //
    // It is deliberately NOT compared against the CAD volume, which a faceted
    // boundary understates for any curved face. That comparison is a
    // convergence property and is asserted in the tests, where the deflection
    // can be varied; using it as an acceptance gate here would need a tolerance
    // nobody could justify.
    constexpr double kVolumeAccumulationTolerance = 1e-9;
    const double relative = std::abs(tetVolume.si() - enclosed.si()) / enclosed.si();
    if (!std::isfinite(relative) || relative > kVolumeAccumulationTolerance) {
        return failure(VolumeMeshFailure::VolumeNotRecovered,
                                 std::format("tetrahedra sum to {} m^3, the boundary encloses {} "
                                             "m^3, relative difference {}",
                                             tetVolume.si(), enclosed.si(), relative));
    }

    VolumeMesh result;
    result.mesh_ = std::move(volume);
    result.source_ = geometry.source;
    result.conformity_ = conformity;
    result.tetrahedralVolume_ = tetVolume;
    result.boundaryVolume_ = enclosed;
    result.cadVolume_ = geometry.volume;
    result.revision_ = geometry.revision;
    result.controls_ = controls;
    return result;
}

Result<VolumeMesh> volumeMeshFor(const Document& document,
                                 const features::Regenerator& regenerator, ObjectId feature,
                                 const VolumeMeshControls& controls) {
    const Result<MeshableGeometry> prepared = requireMeshableGeometry(document, regenerator, feature);
    if (!prepared.has_value()) {
        return std::unexpected(prepared.error());
    }
    const Result<EngineeringSurfaceMesh> surface =
        generateSurfaceMesh(*prepared, controls.surface);
    if (!surface.has_value()) {
        return std::unexpected(surface.error());
    }
    return generateVolumeMesh(*prepared, *surface, controls);
}

bool isStale(const Document& document, const VolumeMesh& mesh) {
    const GeometryRevision current = geometryRevision(document, mesh.source());
    // An invalid current revision means the feature is gone or carries no
    // geometry, which makes any mesh of it stale rather than current.
    if (!current.isValid()) {
        return true;
    }
    return !(current == mesh.revision());
}

} // namespace bettercad::meshing
