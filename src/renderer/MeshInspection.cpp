// Mesh inspection: quality, boundary highlighting and current/stale state
// (P16-VIZ-001).
//
// No OCCT and no Qt. Every refusal in this file exists because an ElementId or
// a NodeId is only meaningful against the mesh generation that issued it.

#include <bettercad/renderer/MeshInspection.hpp>

#include <bettercad/meshing/GeometryPreparation.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace bettercad::renderer {
namespace {

/// The refusal every query in this file begins with.
///
/// ONE FUNCTION, so the check cannot be present on three paths and forgotten
/// on the fourth. A caller that held a report, a map or a view across a remesh
/// gets a diagnostic naming both generations instead of a plausible answer
/// about the wrong element.
[[nodiscard]] Result<void> requireSameGeneration(const meshing::MeshStamp& held,
                                                 const meshing::Mesh& mesh,
                                                 std::string_view what) {
    if (!mesh.stamp().isValid()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("mesh inspection: {} was asked about a mesh with no "
                                     "generation stamp",
                                     what));
    }
    if (!mesh.owns(held)) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("mesh inspection: {} belongs to mesh {} generation {}, but "
                                     "the mesh asked about is mesh {} generation {}; element "
                                     "handles are not comparable across generations",
                                     what, held.mesh.value(), held.generation,
                                     mesh.stamp().mesh.value(), mesh.stamp().generation));
    }
    return Result<void>{};
}

/// Facet identities translated into positions in @p view's triangle buffer.
[[nodiscard]] std::vector<std::size_t> trianglesFor(const MeshView& view,
                                                    std::span<const meshing::ElementId> facets) {
    std::vector<std::size_t> triangles;
    for (const meshing::ElementId facet : facets) {
        const std::vector<std::size_t> found = view.trianglesOfElement(facet);
        triangles.insert(triangles.end(), found.begin(), found.end());
    }
    std::ranges::sort(triangles);
    const auto repeated = std::ranges::unique(triangles);
    triangles.erase(repeated.begin(), repeated.end());
    return triangles;
}

[[nodiscard]] Result<MeshHighlight> highlightOfFacetSet(const MeshView& view,
                                                        const meshing::Mesh& mesh,
                                                        const meshing::GeometryMeshMap& map,
                                                        meshing::BoundaryFacetSet facets) {
    if (const Result<void> checked = requireSameGeneration(map.meshStamp(), mesh, "the mapping");
        !checked.has_value()) {
        return std::unexpected(checked.error());
    }
    if (const Result<void> checked = requireSameGeneration(view.stamp(), mesh, "the render cache");
        !checked.has_value()) {
        return std::unexpected(checked.error());
    }

    MeshHighlight highlight;
    highlight.mapping = std::move(facets);
    highlight.triangles = trianglesFor(view, highlight.mapping.facets);
    return highlight;
}

} // namespace

std::string_view toString(MeshVisualState state) noexcept {
    switch (state) {
    case MeshVisualState::NoMesh:
        return "no engineering mesh";
    case MeshVisualState::Current:
        return "current";
    case MeshVisualState::Stale:
        return "stale";
    case MeshVisualState::GenerationFailed:
        return "generation failed";
    }
    return "unknown";
}

MeshStatus statusOf(const Document& document, const MeshHolding& holding) {
    MeshStatus status;
    status.failure = holding.lastFailure;
    status.inspectable = holding.mesh.has_value();

    if (holding.lastFailure.has_value()) {
        // THE AMBIGUOUS CASE, RESOLVED. An older mesh surviving a failed
        // attempt must not read as Current, and must not read as merely Stale
        // either: the user needs to know the attempt to replace it failed.
        // `inspectable` says the old mesh is still there to look at.
        status.state = MeshVisualState::GenerationFailed;
        return status;
    }
    if (!holding.mesh.has_value()) {
        status.state = MeshVisualState::NoMesh;
        return status;
    }

    // CORE STATE, not a GUI timestamp: the revision the mesh recorded against
    // the revision the document has now.
    const meshing::GeometryRevision current =
        meshing::geometryRevision(document, holding.mesh->source());
    status.state = (current == holding.mesh->revision()) ? MeshVisualState::Current
                                                         : MeshVisualState::Stale;
    return status;
}

Result<MeshQualityView> MeshQualityView::evaluate(const meshing::Mesh& mesh,
                                                  const meshing::QualityThresholds& thresholds) {
    return bind(mesh, meshing::evaluateMeshQuality(mesh, thresholds));
}

Result<MeshQualityView> MeshQualityView::bind(const meshing::Mesh& mesh,
                                              meshing::MeshQualityReport report) {
    if (!mesh.stamp().isValid()) {
        return makeError(ErrorCode::FailedPrecondition,
                         "mesh inspection: a quality view cannot be bound to a mesh with no "
                         "generation stamp");
    }
    MeshQualityView view;
    view.stamp_ = mesh.stamp();
    view.report_ = std::move(report);
    return view;
}

Result<meshing::ElementId> MeshQualityView::worstElementFor(const meshing::Mesh& mesh,
                                                            meshing::QualityMetric metric) const {
    if (const Result<void> checked =
            requireSameGeneration(stamp_, mesh, "the quality report");
        !checked.has_value()) {
        return std::unexpected(checked.error());
    }

    const auto summary = report_.summaries.find(metric);
    if (summary == report_.summaries.end()) {
        return makeError(ErrorCode::NotFound,
                         std::format("mesh inspection: the quality report has no summary for {}",
                                     meshing::toString(metric)));
    }
    if (!summary->second.worst.isValid()) {
        // A ContextOnly metric has no worst, by design. Reported rather than
        // answered with element 0.
        return makeError(ErrorCode::NotFound,
                         std::format("mesh inspection: {} has no worst element -- it is reported "
                                     "for context and is never classified",
                                     meshing::toString(metric)));
    }
    // AND IT MUST STILL BE AN ELEMENT OF THIS MESH. The generation check above
    // makes that true, and this makes it checked.
    if (!mesh.elementType(summary->second.worst).has_value()) {
        return makeError(ErrorCode::Internal,
                         std::format("mesh inspection: the quality report names {} as the worst "
                                     "for {}, and this mesh has no such element",
                                     summary->second.worst, meshing::toString(metric)));
    }
    return summary->second.worst;
}

Result<meshing::QualityClass> MeshQualityView::classOf(const meshing::Mesh& mesh,
                                                       meshing::ElementId element) const {
    if (const Result<void> checked =
            requireSameGeneration(stamp_, mesh, "the quality report");
        !checked.has_value()) {
        return std::unexpected(checked.error());
    }
    if (!mesh.elementType(element).has_value()) {
        return makeError(ErrorCode::NotFound,
                         std::format("mesh inspection: {} is not an element of this mesh",
                                     element));
    }

    // THE CLASSIFICATION IS THE REPORT'S, and there is no threshold comparison
    // in this file. An element with no finding is Valid -- which is what the
    // report's own validElements count means -- and an element with several
    // findings takes the most severe, since Valid < Warning < Failure <
    // Invalid is the enumeration's own order.
    meshing::QualityClass worst = meshing::QualityClass::Valid;
    for (const meshing::QualityFinding& finding : report_.findings) {
        if (finding.element == element &&
            static_cast<std::uint8_t>(finding.classification) >
                static_cast<std::uint8_t>(worst)) {
            worst = finding.classification;
        }
    }
    return worst;
}

Result<MeshHighlight> highlightFor(const MeshView& view, const meshing::Mesh& mesh,
                                   const meshing::GeometryMeshMap& map,
                                   const FaceName& reference) {
    Result<meshing::BoundaryFacetSet> facets = meshing::boundaryFacetsOf(map, reference);
    if (!facets.has_value()) {
        return std::unexpected(facets.error());
    }
    return highlightOfFacetSet(view, mesh, map, std::move(*facets));
}

Result<MeshHighlight> highlightFor(const MeshView& view, const meshing::Mesh& mesh,
                                   const meshing::GeometryMeshMap& map,
                                   const meshing::NamedBoundarySet& set) {
    Result<meshing::ResolvedBoundarySet> resolved = meshing::resolveBoundarySet(set, map);
    if (!resolved.has_value()) {
        return std::unexpected(resolved.error());
    }
    return highlightOfFacetSet(view, mesh, map, std::move(resolved->mapping));
}

Result<MeshHighlight> highlightFor(const MeshView& view, const meshing::Mesh& mesh,
                                   const meshing::GeometryMeshMap& map, std::size_t faceIndex) {
    if (faceIndex >= map.faces().size()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("mesh inspection: face {} of {} mapped faces", faceIndex,
                                     map.faces().size()));
    }
    // Built to look exactly like the resolved form of the FaceName path, so a
    // caller handles one shape of answer whichever way it asked. The face is
    // Resolved by construction: it is a face of this map.
    const meshing::MappedFace& face = map.faces()[faceIndex];
    meshing::BoundaryFacetSet set;
    set.requested.push_back(meshing::FaceResolution{
        .reference = face.names.empty() ? FaceName{} : face.names.front(),
        .state = meshing::MappingState::Resolved,
        .faces = {faceIndex},
        .facets = face.facets});
    set.facets = face.facets;
    return highlightOfFacetSet(view, mesh, map, std::move(set));
}

Result<meshing::FacetSource> sourceOfFacet(const meshing::Mesh& mesh,
                                           const meshing::GeometryMeshMap& map,
                                           meshing::ElementId facet) {
    if (const Result<void> checked = requireSameGeneration(map.meshStamp(), mesh, "the mapping");
        !checked.has_value()) {
        return std::unexpected(checked.error());
    }
    // The canonical reverse query. No geometric search: if the map attributes
    // this facet to no CAD face, that is the answer.
    return meshing::sourceFaceOf(map, facet);
}

} // namespace bettercad::renderer
