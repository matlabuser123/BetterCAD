#include <bettercad/meshing/GeometryMeshMap.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <set>
#include <tuple>
#include <utility>

namespace bettercad::meshing {
namespace {

/// The four faces of a tetrahedron, each wound to face OUT of it.
///
/// THE SAME TABLE AS `tetrahedralBoundary`, and it has to be: a `TetrahedronFace`
/// ordinal means nothing unless both agree on which three corners face number
/// two is. Copied rather than shared because VolumeMesh.cpp's copy is file-local
/// and the duplication is four lines, but the two must not drift -- a test
/// compares every ordinal against the boundary the mesh itself reports.
constexpr std::array<std::array<std::size_t, 3>, 4> kTetFaces{{
    {0, 2, 1},
    {0, 1, 3},
    {1, 2, 3},
    {0, 3, 2},
}};

[[nodiscard]] std::unexpected<Error> refuse(MappingIssueKind kind, ErrorCode code,
                                            std::string detail) {
    return makeError(code, std::format("geometry/mesh mapping: {} ({})", toString(kind),
                                       std::move(detail)));
}

/// Every query starts here. A map built for a different geometry revision or a
/// different mesh generation describes neither, and answering from it would be
/// worse than refusing: the answer would look current.
[[nodiscard]] Result<void> checkCurrent(const GeometryMeshMap& map) {
    if (!map.meshStamp().isValid() || !map.revision().isValid()) {
        return refuse(MappingIssueKind::MappingStale, ErrorCode::FailedPrecondition,
                      "the map carries no geometry revision or no mesh stamp");
    }
    return {};
}

/// The mesh a query is asked about must be the one the map was built for.
[[nodiscard]] Result<void> checkSameMesh(const GeometryMeshMap& map, const Mesh& mesh) {
    if (auto current = checkCurrent(map); !current.has_value()) {
        return current;
    }
    if (!mesh.owns(map.meshStamp())) {
        return refuse(MappingIssueKind::MappingStale, ErrorCode::FailedPrecondition,
                      "the mesh is a different generation from the one the map was built for");
    }
    return {};
}

void sortUnique(std::vector<ElementId>& ids) {
    std::ranges::sort(ids);
    ids.erase(std::ranges::unique(ids).begin(), ids.end());
}

} // namespace

std::string_view toString(MappingState state) noexcept {
    switch (state) {
    case MappingState::Resolved:
        return "resolved";
    case MappingState::Unresolved:
        return "unresolved";
    }
    return "unknown_mapping_state";
}

std::string_view toString(MappingIssueKind kind) noexcept {
    switch (kind) {
    case MappingIssueKind::GeometryReferenceInvalid:
        return "geometry_reference_invalid";
    case MappingIssueKind::GeometryReferenceUnresolved:
        return "geometry_reference_unresolved";
    case MappingIssueKind::MappingStale:
        return "mapping_stale";
    case MappingIssueKind::MeshFacetInvalid:
        return "mesh_facet_invalid";
    case MappingIssueKind::NoBoundaryCorrespondence:
        return "no_boundary_correspondence";
    case MappingIssueKind::FaceWithoutFacets:
        return "face_without_facets";
    case MappingIssueKind::FacetAttributedTwice:
        return "facet_attributed_twice";
    }
    return "unknown_mapping_issue";
}

Result<GeometryMeshMap> buildGeometryMeshMap(const MeshableGeometry& geometry,
                                             const VolumeMesh& mesh) {
    // ONE GEOMETRY, ONE MESH. A map across a mismatched pair is not a weaker
    // answer; it is a wrong one that looks right.
    if (geometry.source != mesh.source()) {
        return refuse(MappingIssueKind::MappingStale, ErrorCode::FailedPrecondition,
                      std::format("the geometry is {} and the mesh was built from {}",
                                  geometry.source, mesh.source()));
    }
    if (!(geometry.revision == mesh.revision())) {
        return refuse(MappingIssueKind::MappingStale, ErrorCode::FailedPrecondition,
                      std::format("the mesh was built from geometry revision {} and the current "
                                  "revision is {}",
                                  mesh.revision().value, geometry.revision.value));
    }

    // The CAD faces, from the kernel, in the order the attribution indexes.
    const Result<std::vector<geometry::FaceInfo>> faces = geometry::listFaces(geometry.body);
    if (!faces.has_value()) {
        return std::unexpected(faces.error());
    }

    GeometryMeshMap map;
    map.source_ = geometry.source;
    map.revision_ = geometry.revision;
    map.meshStamp_ = mesh.mesh().stamp();
    map.faceOfFacet_ = mesh.boundarySourceFaces();

    map.faces_.reserve(faces->size());
    for (std::size_t index = 0; index < faces->size(); ++index) {
        const geometry::FaceInfo& info = (*faces)[index];
        MappedFace face;
        face.index = index;
        face.surface = info.surface;
        face.names = info.names;
        face.signature = info.signature;
        face.cylinder = info.cylinder;
        map.faces_.push_back(std::move(face));
    }

    // INVERT THE ATTRIBUTION. An index the geometry does not have would mean
    // the mesh was built from a different body than the one being mapped, which
    // the revision check above should already have caught -- so it is a refusal
    // and not a dropped facet.
    for (const auto& [facet, index] : map.faceOfFacet_) {
        if (index >= map.faces_.size()) {
            return refuse(MappingIssueKind::MappingStale, ErrorCode::FailedPrecondition,
                          std::format("{} is attributed to CAD face {}, and the body has {}",
                                      facet, index, map.faces_.size()));
        }
        map.faces_[index].facets.push_back(facet);
    }
    for (MappedFace& face : map.faces_) {
        sortUnique(face.facets);
    }

    GeometryMeshMappingReport report;
    report.cadFaceCount = map.faces_.size();
    report.boundaryFacetCount = mesh.mesh().triangles().size();
    for (const MappedFace& face : map.faces_) {
        if (face.names.empty()) {
            ++report.unnamedFaceCount;
        } else {
            ++report.namedFaceCount;
        }
        if (face.facets.empty()) {
            ++report.facesWithoutFacets;
            report.issues.push_back(MappingIssue{
                MappingIssueKind::FaceWithoutFacets, std::nullopt, std::nullopt, face.index,
                std::format("CAD face {} ({}) produced no boundary facet", face.index,
                            geometry::toString(face.surface))});
        }
    }

    // COUNTED FROM THE PER-FACE LISTS, so the inversion above is checked rather
    // than trusted: a facet appearing in two lists would mean the attribution
    // was not a function.
    std::map<ElementId, std::size_t> timesAttributed;
    for (const MappedFace& face : map.faces_) {
        for (const ElementId facet : face.facets) {
            ++timesAttributed[facet];
        }
    }
    for (const auto& [facet, count] : timesAttributed) {
        if (count > 1) {
            ++report.facetsWithSeveralFaces;
            report.issues.push_back(MappingIssue{
                MappingIssueKind::FacetAttributedTwice, std::nullopt, facet, std::nullopt,
                std::format("{} is attributed to {} CAD faces", facet, count)});
        }
    }
    report.mappedFacetCount = timesAttributed.size();
    for (const Triangle& triangle : mesh.mesh().triangles()) {
        if (!timesAttributed.contains(triangle.id)) {
            ++report.unmappedFacetCount;
            report.issues.push_back(MappingIssue{
                MappingIssueKind::NoBoundaryCorrespondence, std::nullopt, triangle.id, std::nullopt,
                std::format("boundary {} is attributed to no CAD face", triangle.id)});
        }
    }
    // Most severe first is not meaningful here -- these are observations, not
    // verdicts -- so the order is by kind, then element, then face: total, and
    // therefore the same in every build configuration.
    std::ranges::stable_sort(report.issues, [](const MappingIssue& a, const MappingIssue& b) {
        return std::tuple{static_cast<std::uint8_t>(a.kind), a.element.value_or(ElementId{}).value(),
                          a.face.value_or(0)} <
               std::tuple{static_cast<std::uint8_t>(b.kind), b.element.value_or(ElementId{}).value(),
                          b.face.value_or(0)};
    });
    map.report_ = std::move(report);
    return map;
}

Result<GeometryMeshMap> geometryMeshMapFor(const Document& document,
                                           const features::Regenerator& regenerator,
                                           ObjectId feature, const VolumeMesh& mesh) {
    const Result<MeshableGeometry> prepared = requireMeshableGeometry(document, regenerator, feature);
    if (!prepared.has_value()) {
        // Returned unchanged: a stale or failed model is refused here with the
        // same diagnostic P16-GEOM-001 gives. This layer has no second opinion
        // about what is current.
        return std::unexpected(prepared.error());
    }
    return buildGeometryMeshMap(*prepared, mesh);
}

Result<BoundaryFacetSet> boundaryFacetsOf(const GeometryMeshMap& map, const FaceName& reference) {
    const std::array<FaceName, 1> one{reference};
    return boundaryFacetsOf(map, std::span<const FaceName>{one});
}

Result<BoundaryFacetSet> boundaryFacetsOf(const GeometryMeshMap& map,
                                          std::span<const FaceName> references) {
    if (auto current = checkCurrent(map); !current.has_value()) {
        return std::unexpected(current.error());
    }
    if (references.empty()) {
        return makeError(ErrorCode::InvalidArgument,
                         "geometry/mesh mapping: no reference was asked about");
    }

    BoundaryFacetSet set;
    set.requested.reserve(references.size());
    for (const FaceName& reference : references) {
        // CORE'S OWN RULE, not a second opinion about what a selector may say.
        if (const Result<void> checked = validate(reference.face); !checked.has_value()) {
            return refuse(MappingIssueKind::GeometryReferenceInvalid, ErrorCode::InvalidArgument,
                          checked.error().message);
        }

        FaceResolution resolution;
        resolution.reference = reference;
        // MATCHED BY VALUE against the names the kernel's history carried. No
        // geometry is consulted, so a face cannot be chosen for lying near the
        // one that was asked for, and two coincident faces stay distinct.
        for (const MappedFace& face : map.faces()) {
            if (std::ranges::find(face.names, reference) != face.names.end()) {
                resolution.faces.push_back(face.index);
                resolution.facets.insert(resolution.facets.end(), face.facets.begin(),
                                         face.facets.end());
            }
        }
        sortUnique(resolution.facets);
        resolution.state =
            resolution.faces.empty() ? MappingState::Unresolved : MappingState::Resolved;
        set.facets.insert(set.facets.end(), resolution.facets.begin(), resolution.facets.end());
        set.requested.push_back(std::move(resolution));
    }
    sortUnique(set.facets);
    return set;
}

Result<FacetSource> sourceFaceOf(const GeometryMeshMap& map, ElementId facet) {
    if (auto current = checkCurrent(map); !current.has_value()) {
        return std::unexpected(current.error());
    }
    for (const MappedFace& face : map.faces()) {
        if (std::ranges::binary_search(face.facets, facet)) {
            return FacetSource{facet, face.index, face.names};
        }
    }
    return refuse(MappingIssueKind::NoBoundaryCorrespondence, ErrorCode::FailedPrecondition,
                  std::format("{} is attributed to no CAD face of this body", facet));
}

Result<FacetSource> sourceFaceOf(const GeometryMeshMap& map, const Mesh& mesh,
                                 const TetrahedronFace& face) {
    if (auto same = checkSameMesh(map, mesh); !same.has_value()) {
        return std::unexpected(same.error());
    }
    if (face.ordinal >= kTetFaces.size()) {
        return refuse(MappingIssueKind::MeshFacetInvalid, ErrorCode::InvalidArgument,
                      std::format("a tetrahedron has 4 faces, numbered 0 to 3, not {}",
                                  face.ordinal));
    }
    const Tetrahedron* tet = mesh.findTetrahedron(face.tetrahedron);
    if (tet == nullptr) {
        return refuse(MappingIssueKind::MeshFacetInvalid, ErrorCode::InvalidArgument,
                      std::format("{} is not a tetrahedron of this mesh", face.tetrahedron));
    }

    // Which three nodes, and is that triple a boundary triangle of this mesh?
    // Decided by the mesh's own connectivity: the boundary triangles ARE the
    // faces exactly one tetrahedron uses, and an interior face is simply not
    // among them.
    const std::array<std::size_t, 3>& corners = kTetFaces[face.ordinal];
    std::array<NodeId, 3> wanted{tet->nodes[corners[0]], tet->nodes[corners[1]],
                                 tet->nodes[corners[2]]};
    std::ranges::sort(wanted);
    for (const Triangle& triangle : mesh.triangles()) {
        std::array<NodeId, 3> candidate = triangle.nodes;
        std::ranges::sort(candidate);
        if (candidate == wanted) {
            return sourceFaceOf(map, triangle.id);
        }
    }
    return refuse(MappingIssueKind::NoBoundaryCorrespondence, ErrorCode::FailedPrecondition,
                  std::format("face {} of {} is interior: two tetrahedra share it, so it lies on "
                              "no CAD boundary face",
                              face.ordinal, face.tetrahedron));
}

Result<std::vector<NodeId>> boundaryNodesOf(const GeometryMeshMap& map, const Mesh& mesh,
                                            std::span<const ElementId> facets) {
    if (auto same = checkSameMesh(map, mesh); !same.has_value()) {
        return std::unexpected(same.error());
    }
    std::vector<NodeId> nodes;
    for (const ElementId facet : facets) {
        const Triangle* triangle = mesh.findTriangle(facet);
        if (triangle == nullptr) {
            return refuse(MappingIssueKind::MeshFacetInvalid, ErrorCode::InvalidArgument,
                          std::format("{} is not a boundary triangle of this mesh", facet));
        }
        nodes.insert(nodes.end(), triangle->nodes.begin(), triangle->nodes.end());
    }
    std::ranges::sort(nodes);
    nodes.erase(std::ranges::unique(nodes).begin(), nodes.end());
    return nodes;
}

Result<std::vector<ElementId>> owningTetrahedraOf(const GeometryMeshMap& map, const Mesh& mesh,
                                                  std::span<const ElementId> facets) {
    if (auto same = checkSameMesh(map, mesh); !same.has_value()) {
        return std::unexpected(same.error());
    }

    // Keyed on the sorted node triple, which is how a tetrahedron's face and a
    // boundary triangle are the same face at all.
    std::map<std::array<NodeId, 3>, std::vector<ElementId>> owners;
    for (const Tetrahedron& tet : mesh.tetrahedra()) {
        for (const std::array<std::size_t, 3>& corners : kTetFaces) {
            std::array<NodeId, 3> key{tet.nodes[corners[0]], tet.nodes[corners[1]],
                                      tet.nodes[corners[2]]};
            std::ranges::sort(key);
            owners[key].push_back(tet.id);
        }
    }

    std::vector<ElementId> elements;
    for (const ElementId facet : facets) {
        const Triangle* triangle = mesh.findTriangle(facet);
        if (triangle == nullptr) {
            return refuse(MappingIssueKind::MeshFacetInvalid, ErrorCode::InvalidArgument,
                          std::format("{} is not a boundary triangle of this mesh", facet));
        }
        std::array<NodeId, 3> key = triangle->nodes;
        std::ranges::sort(key);
        const auto found = owners.find(key);
        // EXACTLY ONE, CHECKED. An external boundary facet is a face of one
        // tetrahedron; none means the facet is not a face of the volume at all,
        // and two would mean it is interior. Either is reported rather than
        // resolved to whichever came first.
        if (found == owners.end() || found->second.empty()) {
            return refuse(MappingIssueKind::NoBoundaryCorrespondence, ErrorCode::FailedPrecondition,
                          std::format("{} is a face of no tetrahedron of this mesh", facet));
        }
        if (found->second.size() != 1) {
            return refuse(MappingIssueKind::NoBoundaryCorrespondence, ErrorCode::FailedPrecondition,
                          std::format("{} is a face of {} tetrahedra, so it is interior", facet,
                                      found->second.size()));
        }
        elements.push_back(found->second.front());
    }
    sortUnique(elements);
    return elements;
}

Result<MappedRegion> regionOf(const GeometryMeshMap& map, const Mesh& mesh) {
    if (auto same = checkSameMesh(map, mesh); !same.has_value()) {
        return std::unexpected(same.error());
    }
    MappedRegion region;
    region.source = map.source();
    for (const Tetrahedron& tet : mesh.tetrahedra()) {
        if (!region.region.isValid()) {
            region.region = tet.region;
        } else if (tet.region != region.region) {
            // P16 meshes one solid, so one region. More than one means the mesh
            // did not come from the pipeline this map describes.
            return refuse(MappingIssueKind::MappingStale, ErrorCode::FailedPrecondition,
                          std::format("the mesh holds more than one volume region ({} and {})",
                                      region.region, tet.region));
        }
        region.elements.push_back(tet.id);
    }
    if (region.elements.empty()) {
        return refuse(MappingIssueKind::NoBoundaryCorrespondence, ErrorCode::FailedPrecondition,
                      "the mesh holds no volume element, so it describes no region");
    }
    sortUnique(region.elements);
    return region;
}

Result<ObjectId> regionSourceOf(const GeometryMeshMap& map, const Mesh& mesh, ElementId element) {
    if (auto same = checkSameMesh(map, mesh); !same.has_value()) {
        return std::unexpected(same.error());
    }
    if (mesh.findTetrahedron(element) == nullptr) {
        return refuse(MappingIssueKind::MeshFacetInvalid, ErrorCode::InvalidArgument,
                      std::format("{} is not a volume element of this mesh, so it belongs to no "
                                  "volume region",
                                  element));
    }
    return map.source();
}

Result<void> validate(const NamedBoundarySet& set) {
    if (!set.id.isValid()) {
        return makeError(ErrorCode::InvalidArgument,
                         "boundary set: the invalid handle cannot name a set");
    }
    if (set.name.empty()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("boundary set {}: a set needs a name", set.id));
    }
    if (set.faces.empty()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("boundary set {} ({}): a set needs at least one face",
                                     set.id, set.name));
    }
    for (std::size_t i = 0; i < set.faces.size(); ++i) {
        if (const Result<void> checked = validate(set.faces[i].face); !checked.has_value()) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("boundary set {} ({}): face {} is malformed: {}", set.id,
                                         set.name, i + 1, checked.error().message));
        }
        for (std::size_t j = i + 1; j < set.faces.size(); ++j) {
            // A set listing one face twice is a mistake, not a union: it would
            // make the set's own size depend on how often it was written.
            // Two DIFFERENT sets naming the same face is a different matter and
            // is allowed -- see NamedBoundarySet.
            if (set.faces[i] == set.faces[j]) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("boundary set {} ({}): faces {} and {} are the same "
                                             "reference",
                                             set.id, set.name, i + 1, j + 1));
            }
        }
    }
    return {};
}

Result<ResolvedBoundarySet> resolveBoundarySet(const NamedBoundarySet& set,
                                               const GeometryMeshMap& map) {
    if (const Result<void> checked = validate(set); !checked.has_value()) {
        return std::unexpected(checked.error());
    }
    Result<BoundaryFacetSet> mapping = boundaryFacetsOf(map, std::span<const FaceName>{set.faces});
    if (!mapping.has_value()) {
        return std::unexpected(mapping.error());
    }
    // THE SET SURVIVES ITS GEOMETRY. A reference that no longer resolves comes
    // back as Unresolved inside a set that still has its id and its name; it is
    // never dropped, and the set is never rebound to a different face.
    return ResolvedBoundarySet{set.id, set.name, std::move(*mapping)};
}

} // namespace bettercad::meshing
