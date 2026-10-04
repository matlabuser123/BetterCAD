#pragma once

// Mesh inspection: quality, boundary highlighting and current/stale state
// (P16-VIZ-001).
//
// Like MeshView.hpp, this contains NO Qt and NO OCCT, so every question it
// answers is assertable from a headless test. It holds no engineering data of
// its own: a quality view holds a report the core produced, a highlight is a
// list of indices into a MeshView, and the visual state is derived from the
// document's own geometry revision.
//
// THE PROBLEM THIS LAYER EXISTS TO SOLVE is that ElementId and NodeId are
// reused across mesh generations (ADR-031). A report, a mapping or a selection
// that outlives a remesh will happily match a number against a DIFFERENT
// physical element, and nothing about the number says so. Every entry point
// here therefore takes the current mesh and refuses when the two do not belong
// together, rather than answering plausibly.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>
#include <bettercad/renderer/Export.hpp>
#include <bettercad/renderer/MeshView.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace bettercad::renderer {

// ---------------------------------------------------------------------------
// Current, stale, missing, failed
// ---------------------------------------------------------------------------

/// What a mesh is, relative to the model it was built from.
///
/// DERIVED FROM CORE STATE, never from a GUI timestamp: the comparison is
/// between the revision the mesh recorded and the revision the document has
/// now, both of which are P16-GEOM-001's.
enum class MeshVisualState : std::uint8_t {
    /// No mesh has been generated.
    NoMesh,
    /// The mesh was built from the geometry the document has now.
    Current,
    /// The mesh was built from geometry that has since changed. It may still
    /// be inspected; it does not describe the model.
    Stale,
    /// The most recent generation attempt failed. An earlier mesh may still be
    /// held, and if so it is stale by definition -- which is why this is a
    /// state of its own rather than a flag on Current.
    GenerationFailed,
};

[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::string_view toString(MeshVisualState state) noexcept;

/// What the application holds about one feature's engineering mesh.
///
/// GUI-SIDE BOOKKEEPING ABOUT GENERATION ATTEMPTS, and not canonical model
/// state: P16-PERSIST-001 owns persistence and comes later, so nothing here is
/// saved. What makes it legitimate is that it holds the core's own objects --
/// a `VolumeMesh` and an `Error` the core produced -- and derives nothing.
struct MeshHolding {
    /// The last mesh that generated successfully, if any.
    std::optional<meshing::VolumeMesh> mesh{};
    /// Why the most recent attempt failed, if it did.
    std::optional<Error> lastFailure{};
};

/// The single visible mesh state, and whether anything can still be looked at.
///
/// ONE SOURCE, deliberately: a UI that derived "stale" in one widget and
/// "current" in another would eventually disagree with itself.
struct MeshStatus {
    MeshVisualState state = MeshVisualState::NoMesh;
    /// A mesh is held and can be inspected -- which is true for `Stale` and
    /// can be true for `GenerationFailed`, where an older mesh survives the
    /// failed attempt.
    bool inspectable = false;
    /// The core's own diagnostic for a failed attempt.
    std::optional<Error> failure{};
};

/// The state of @p holding against @p document.
///
/// A held mesh plus a failed latest attempt is `GenerationFailed`, NOT
/// `Current` and not merely `Stale`: the user has to be able to tell "this is
/// old" from "this is old AND the attempt to replace it failed".
[[nodiscard]] BETTERCAD_RENDERER_EXPORT MeshStatus statusOf(const Document& document,
                                                            const MeshHolding& holding);

// ---------------------------------------------------------------------------
// Quality
// ---------------------------------------------------------------------------

/// A quality report bound to the mesh generation it was computed on.
///
/// WHY THIS WRAPPER EXISTS. `meshing::MeshQualityReport` carries counts,
/// per-element metrics, per-metric summaries and the threshold policy -- and
/// nothing that says which mesh it describes. `GeometryMeshMap`, by contrast,
/// carries a `MeshStamp`. So a report held across a remesh can name an
/// `ElementId` that exists in the new mesh and means a different element, and
/// navigating to it would select the wrong tetrahedron while looking entirely
/// successful.
///
/// This class is the pairing, made at the only moment it is known for certain:
/// when the report is computed. Recorded in the audit as an observation that a
/// `MeshStamp` inside the report would be the general fix, and that changing
/// P16-QUALITY-001's qualified data model is not this milestone's to do.
///
/// IT HOLDS NO QUALITY POLICY. Thresholds and classifications come out of the
/// report; there is not a single comparison against a number in this layer.
class BETTERCAD_RENDERER_EXPORT MeshQualityView {
public:
    /// Evaluates @p mesh under @p thresholds and binds the result to it.
    [[nodiscard]] static Result<MeshQualityView> evaluate(const meshing::Mesh& mesh,
                                                          const meshing::QualityThresholds& thresholds);

    /// Binds an already-computed @p report to @p mesh.
    ///
    /// The caller is asserting that the report was computed on this mesh;
    /// there is no way to check that, which is precisely the gap. Prefer
    /// `evaluate`.
    [[nodiscard]] static Result<MeshQualityView> bind(const meshing::Mesh& mesh,
                                                      meshing::MeshQualityReport report);

    [[nodiscard]] const meshing::MeshStamp& stamp() const noexcept { return stamp_; }
    [[nodiscard]] bool describes(const meshing::Mesh& mesh) const noexcept {
        return mesh.owns(stamp_);
    }
    [[nodiscard]] const meshing::MeshQualityReport& report() const noexcept { return report_; }

    /// The element holding the worst value for @p metric, in @p mesh.
    ///
    /// Refuses with `FailedPrecondition` when this report does not describe
    /// @p mesh, and with `NotFound` for a metric the report has no summary for
    /// or whose worst element is not classifiable. Per metric, because
    /// P16-QUALITY-001 reports worst per metric and there is no overall score.
    [[nodiscard]] Result<meshing::ElementId> worstElementFor(const meshing::Mesh& mesh,
                                                             meshing::QualityMetric metric) const;

    /// How @p element is classified. The classification is the report's.
    [[nodiscard]] Result<meshing::QualityClass> classOf(const meshing::Mesh& mesh,
                                                        meshing::ElementId element) const;

private:
    MeshQualityView() = default;

    meshing::MeshStamp stamp_{};
    meshing::MeshQualityReport report_{};
};

// ---------------------------------------------------------------------------
// Boundary highlighting, both directions
// ---------------------------------------------------------------------------

/// A resolved highlight: what the mapping said, which facets it named, and
/// where they are in a render buffer.
struct MeshHighlight {
    /// THE MAPPING'S ANSWER, CARRIED WHOLE, including the per-reference
    /// `MappingState`. Restating it as one status would lose exactly what
    /// P16-MAP-001 was careful to keep: "Unresolved is a status, not an empty
    /// list", so a deleted face stays distinguishable from a face that is
    /// simply not meshed. A GUI that flattened both to "nothing highlighted"
    /// would be the error this field prevents.
    meshing::BoundaryFacetSet mapping{};
    /// Where the resolved facets are in the view's triangle buffer, ascending
    /// and without repeats.
    std::vector<std::size_t> triangles{};

    /// The canonical facets, as element identities.
    [[nodiscard]] std::span<const meshing::ElementId> facets() const noexcept {
        return mapping.facets;
    }
    [[nodiscard]] bool fullyResolved() const noexcept { return mapping.fullyResolved(); }
    [[nodiscard]] bool empty() const noexcept { return triangles.empty(); }
};

/// The render triangles currently belonging to @p reference.
///
/// CAD -> MESH. Every step is canonical: `boundaryFacetsOf` resolves the
/// reference against the map, and the view translates facet identities into
/// buffer positions. There is no geometric search, no nearest-triangle match
/// and no tolerance anywhere in this path -- if there were, the highlight
/// would be the GUI's opinion rather than the mapping's.
///
/// Refuses when @p map or @p view does not belong to @p mesh.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<MeshHighlight>
highlightFor(const MeshView& view, const meshing::Mesh& mesh, const meshing::GeometryMeshMap& map,
             const FaceName& reference);

/// The render triangles currently belonging to @p set.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<MeshHighlight>
highlightFor(const MeshView& view, const meshing::Mesh& mesh, const meshing::GeometryMeshMap& map,
             const meshing::NamedBoundarySet& set);

/// The render triangles belonging to the CAD face at @p faceIndex of @p map.
///
/// THE OVERLOAD A CLICK USES, and the reason it has to exist: a picked face is
/// identified by WHERE IT IS in `geometry::listFaces(body)` order, which is
/// the order `GeometryMeshMap::faces()` is in -- and not every face has a
/// name. P16-MAP-001 is explicit that `cutHole` names a hole's flat faces and
/// not its cylindrical wall, so **a bored hole's wall has no FaceName at all**:
/// it "is still mapped and is still answerable in reverse; what it cannot be
/// is the target of a forward query, because there is nothing to ask with".
///
/// A GUI restricted to the `FaceName` overload therefore could not highlight
/// the mesh of a drilled hole's wall -- which is one of this milestone's
/// mandatory reference cases. This overload asks the map by position instead,
/// so it is still the mapping that answers and there is still no geometric
/// search.
///
/// An index is a correlation handle for ONE map and is never an identity: it
/// is not persisted and not comparable between maps. A named boundary set
/// stores `FaceName`s, which is what survives a remesh.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<MeshHighlight>
highlightFor(const MeshView& view, const meshing::Mesh& mesh, const meshing::GeometryMeshMap& map,
             std::size_t faceIndex);

/// The CAD face a boundary facet came from.
///
/// MESH -> CAD, the reverse linkage. Refuses when @p map does not belong to
/// @p mesh, and reports the mapping's state rather than inventing a face.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<meshing::FacetSource>
sourceOfFacet(const meshing::Mesh& mesh, const meshing::GeometryMeshMap& map,
              meshing::ElementId facet);

} // namespace bettercad::renderer
