#pragma once

// One feature's mesh inspection session (P16-VIZ-001).
//
// No Qt and no OCCT: this is the model a panel displays, and it is testable
// without a window.
//
// WHAT IT IS FOR. A mesh, its geometry mapping and its quality report are
// three separate objects that are only meaningful TOGETHER, against one mesh
// generation. Checking that at every call site works until one call site
// forgets. A MeshScene can only be constructed from a set that agrees, so
// after that nothing inside it has to re-check -- and a caller cannot assemble
// a scene out of a mesh from one remesh and a report from another, because
// `adopt` refuses it.
//
// It holds the core's own objects and derives nothing: every number it reports
// comes from `meshing`, and the inspection structs below are views onto them
// rather than recalculations. There is no threshold, no tolerance and no
// geometric search in this file.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>
#include <bettercad/renderer/Export.hpp>
#include <bettercad/renderer/MeshInspection.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace bettercad::renderer {

/// What inspecting one node reports.
///
/// The position is a `Point3D`, so its unit is part of its type and the UI
/// formats it with the project's existing unit machinery. A raw unlabelled
/// number out of a render buffer would be a different number in a different
/// unit, and the user could not tell.
struct NodeInspection {
    meshing::NodeId node{};
    /// Canonical, from `Mesh::findNode`, in the mesh's body frame.
    Point3D position{};
    /// Whether any boundary facet of the mesh uses this node.
    bool onBoundary = false;
    /// The CAD face behind one of its boundary facets, when the mapping has
    /// one. Absent for an interior node, and absent for a boundary node whose
    /// facets the map attributes to nothing.
    std::optional<meshing::FacetSource> source{};
};

/// What inspecting one element reports.
///
/// EVERY FIELD COMES FROM CORE. The signed volume is `meshing::signedVolume`'s,
/// the metrics are the quality report's, and the classification is the
/// report's classification -- the GUI computes none of them and holds no
/// thresholds, so it cannot disagree with the engine about whether an element
/// is acceptable.
struct ElementInspection {
    meshing::ElementId element{};
    meshing::ElementType type = meshing::ElementType::Tetrahedron4;
    std::vector<meshing::NodeId> nodes{};
    /// SIGNED, for a Tet4. Positive is the valid orientation and the sign is
    /// never discarded: it is the only evidence of an inverted element.
    std::optional<Volume> signedVolume{};
    meshing::QualityClass classification = meshing::QualityClass::Valid;
    /// The report's own measurements, when it has them for this element.
    std::optional<meshing::TetQuality> tetQuality{};
    std::optional<meshing::TriangleQuality> triangleQuality{};
    /// For a boundary facet, the CAD face it came from.
    std::optional<meshing::FacetSource> source{};
};

/// A mesh, its mapping and its quality report, known to describe one another.
class BETTERCAD_RENDERER_EXPORT MeshScene {
public:
    /// Takes a set that must agree.
    ///
    /// Refuses unless the mesh, the map and the quality report all carry the
    /// SAME `MeshStamp`. That refusal is the whole value of this class: it
    /// happens once, at the only moment the three are brought together, rather
    /// than being re-checked by every query and missed by one.
    [[nodiscard]] static Result<MeshScene> adopt(ObjectId feature, meshing::VolumeMesh mesh,
                                                 meshing::GeometryMeshMap map,
                                                 MeshQualityView quality);

    /// The feature this mesh was built from.
    [[nodiscard]] ObjectId feature() const noexcept { return feature_; }
    [[nodiscard]] const meshing::VolumeMesh& volume() const noexcept { return volume_; }
    [[nodiscard]] const meshing::Mesh& mesh() const noexcept { return volume_.mesh(); }
    [[nodiscard]] const meshing::GeometryMeshMap& map() const noexcept { return map_; }
    [[nodiscard]] const MeshQualityView& quality() const noexcept { return quality_; }
    [[nodiscard]] const meshing::MeshStamp& stamp() const noexcept { return mesh().stamp(); }

    /// This mesh's state against @p document: current, stale, or superseded by
    /// a failed attempt.
    [[nodiscard]] MeshStatus status(const Document& document,
                                   const std::optional<Error>& lastFailure = std::nullopt) const;

    [[nodiscard]] Result<NodeInspection> inspectNode(meshing::NodeId node) const;
    [[nodiscard]] Result<ElementInspection> inspectElement(meshing::ElementId element) const;

    /// The worst element for @p metric, from the report.
    [[nodiscard]] Result<meshing::ElementId> worstElementFor(meshing::QualityMetric metric) const;

    /// The CAD faces of this mesh's body, in the map's own order, with a label
    /// for each: its first name, or a description of why it has none.
    ///
    /// Provided so a panel can list faces WITHOUT inventing identities. An
    /// unnamed face is listed and is selectable by its index, which is the
    /// only way a drilled hole's wall can be reached at all.
    struct FaceEntry {
        std::size_t index = 0;
        std::string label{};
        bool named = false;
        std::size_t facetCount = 0;
    };
    [[nodiscard]] std::vector<FaceEntry> faces() const;

private:
    MeshScene(ObjectId feature, meshing::VolumeMesh volume, meshing::GeometryMeshMap map,
              MeshQualityView quality);

    ObjectId feature_{};
    // No default member initialisers: a VolumeMesh and a GeometryMeshMap are
    // constructible ONLY by their building path (ADR-030), because possessing
    // one is the evidence that a geometry and a mesh were checked to be the
    // same pair. They are initialised in the constructor or not at all, which
    // is why MeshScene has no default constructor either.
    meshing::VolumeMesh volume_;
    meshing::GeometryMeshMap map_;
    MeshQualityView quality_;
};

} // namespace bettercad::renderer
