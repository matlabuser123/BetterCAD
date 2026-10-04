#pragma once

// The read-only visualisation adapter for a P16 engineering mesh
// (P16-VIZ-001).
//
//     canonical P16 mesh state          meshing::Mesh, owned elsewhere
//             |
//             v
//     read-only visualisation adapter   THIS FILE
//             |
//             v
//     viewer/render representation      src/renderer/occt/
//             |
//             v
//     selection / inspection UI         apps/bettercad/
//
// WHAT THIS IS NOT. It is not a mesh. It holds no node, no element, no
// connectivity and no quality -- it holds vertex positions and index arrays
// for a GPU, plus the two lookup tables that turn a render index back into a
// NodeId or an ElementId. Every engineering question is answered by asking the
// canonical mesh again, which is why this header exposes no way to ask one.
//
// It contains NO Qt and NO OCCT, which is what makes it testable without a
// display: the counts, the identity translation and the revision guard are all
// assertable headlessly, and that is where the real content of this milestone
// is verified.
//
// THE RENDER CACHE RULE. A MeshView is derived, disposable and keyed to the
// MeshStamp of the mesh it was built from. It is never updated in place and
// there is no API to do so: a changed mesh means a new MeshView, and the old
// one answers `describes(mesh) == false` rather than silently reinterpreting
// its indices against different geometry. That is ADR-031's refusal applied to
// rendering.

#include <bettercad/core/Error.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/renderer/Export.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace bettercad::renderer {

/// Which canonical mesh object a view was built from.
///
/// NAMED RATHER THAN INFERRED, because an engineering surface mesh and a
/// volume mesh's boundary can be geometrically identical and the user still has
/// to know which one is on screen (brief requirement: "Do not silently swap
/// between them"). The enumerator is carried on the view and reported by the
/// UI; it is not a rendering hint.
enum class MeshSource : std::uint8_t {
    /// The Triangle3 elements of an `EngineeringSurfaceMesh`.
    EngineeringSurface,
    /// The Triangle3 elements of a `VolumeMesh`, which ARE its boundary: a
    /// volume mesh carries "nodes, Tet4 elements and the boundary Triangle3
    /// elements, in one". No interior tetrahedron face is ever included, and
    /// no face-incidence counting happens here, because the canonical boundary
    /// is already a set of elements.
    VolumeBoundary,
    /// The four faces of each of a chosen few tetrahedra: interior inspection,
    /// built on demand for a selection and never for a whole mesh.
    SelectedTetrahedra,
};

[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::string_view toString(MeshSource source) noexcept;

/// A derived, disposable, stamp-keyed render cache for one canonical mesh.
///
/// Vertices are the mesh nodes the displayed triangles actually reference, in
/// ascending `NodeId`. Triangles are in ascending `ElementId`. Edges are
/// deduplicated on the unordered node pair and sorted. Nothing here depends on
/// an unordered container, so the same mesh gives byte-identical buffers in
/// Debug, Release and Debug-shared.
class BETTERCAD_RENDERER_EXPORT MeshView {
public:
    /// The engineering surface: every Triangle3 of @p mesh.
    [[nodiscard]] static Result<MeshView> surfaceOf(const meshing::Mesh& mesh);

    /// The volume mesh's boundary: every Triangle3 of @p mesh.
    ///
    /// Identical machinery to `surfaceOf`, and deliberately a separate entry
    /// point: what differs is the `MeshSource` the view reports, which is the
    /// thing the user needs and the thing a shared function would have lost.
    [[nodiscard]] static Result<MeshView> volumeBoundaryOf(const meshing::Mesh& mesh);

    /// The faces of @p tetrahedra, for interior inspection.
    ///
    /// Refuses an empty selection, and refuses an `ElementId` that is not a
    /// tetrahedron of @p mesh -- rather than skipping it, because a caller that
    /// asked to inspect four elements and silently got three has been misled.
    [[nodiscard]] static Result<MeshView> tetrahedraOf(const meshing::Mesh& mesh,
                                                       std::span<const meshing::ElementId> tetrahedra);

    [[nodiscard]] MeshSource source() const noexcept { return source_; }

    /// The mesh generation these buffers and lookups belong to.
    [[nodiscard]] const meshing::MeshStamp& stamp() const noexcept { return stamp_; }

    /// Whether this view may be used against @p mesh.
    ///
    /// THE REVISION GUARD. A caller holding a view across a possible remesh
    /// asks this first and gets a false it must handle. Anything that pairs a
    /// view with a mesh -- rendering, picking, highlighting -- goes through it,
    /// so a stale cache cannot be drawn as current.
    [[nodiscard]] bool describes(const meshing::Mesh& mesh) const noexcept {
        return mesh.owns(stamp_);
    }

    [[nodiscard]] std::size_t vertexCount() const noexcept { return nodeOfVertex_.size(); }
    [[nodiscard]] std::size_t triangleCount() const noexcept { return elementOfTriangle_.size(); }
    [[nodiscard]] std::size_t edgeCount() const noexcept { return edgeIndices_.size() / 2U; }

    /// Vertex positions, three doubles per vertex, in SI metres, in the mesh's
    /// own body frame.
    ///
    /// FOR DRAWING ONLY. Node inspection reads the canonical mesh instead --
    /// `Mesh::findNode(id)->position` gives a `Point3D` whose units are part of
    /// its type, and formatting that is the UI's job. Reporting a coordinate
    /// out of this array would be reporting a number with no unit on it.
    [[nodiscard]] std::span<const double> positions() const noexcept { return positions_; }

    /// Three vertex indices per triangle.
    [[nodiscard]] std::span<const std::uint32_t> triangleIndices() const noexcept {
        return triangleIndices_;
    }

    /// Two vertex indices per edge, deduplicated and sorted.
    [[nodiscard]] std::span<const std::uint32_t> edgeIndices() const noexcept {
        return edgeIndices_;
    }

    // --- the translation that keeps render indices out of engineering -------
    //
    // A render index is a position in an array that exists for a GPU. It is
    // not identity, it is not persisted, and it is invalidated wholesale by a
    // remesh. These four functions are the only bridge, and they are explicit
    // so that no caller can reach a NodeId by arithmetic on a vertex number.

    [[nodiscard]] Result<meshing::NodeId> nodeOfVertex(std::size_t vertex) const;
    [[nodiscard]] Result<meshing::ElementId> elementOfTriangle(std::size_t triangle) const;
    /// Where @p node is in the vertex buffer, if this view draws it at all. An
    /// interior node of a volume mesh is not drawn by a boundary view.
    [[nodiscard]] Result<std::size_t> vertexOfNode(meshing::NodeId node) const;
    /// Which render triangles belong to @p element, ascending. Empty when the
    /// element is not drawn by this view -- one triangle for a Triangle3, four
    /// for a Tet4 in an interior view.
    [[nodiscard]] std::vector<std::size_t> trianglesOfElement(meshing::ElementId element) const;

private:
    MeshView() = default;

    /// One render triangle before the vertex buffer exists: the element it
    /// belongs to and its three nodes, in winding order.
    ///
    /// Private, and the assembly below is private with it. The alternative --
    /// a free helper in the .cpp -- cannot reach these members, which is the
    /// same wall INFRA-VIEWER-001 hit with its viewer Impl.
    struct Facet {
        meshing::ElementId element{};
        std::array<meshing::NodeId, 3> nodes{};
    };

    [[nodiscard]] static Result<MeshView> build(const meshing::Mesh& mesh, MeshSource source,
                                                std::vector<Facet> facets);
    [[nodiscard]] static std::vector<Facet> facetsOfTriangles(const meshing::Mesh& mesh);

    MeshSource source_ = MeshSource::EngineeringSurface;
    meshing::MeshStamp stamp_{};
    std::vector<double> positions_{};
    std::vector<std::uint32_t> triangleIndices_{};
    std::vector<std::uint32_t> edgeIndices_{};
    /// Parallel to the vertex buffer.
    std::vector<meshing::NodeId> nodeOfVertex_{};
    /// Parallel to the triangle buffer. A Tet4 appears four times.
    std::vector<meshing::ElementId> elementOfTriangle_{};
};

} // namespace bettercad::renderer
