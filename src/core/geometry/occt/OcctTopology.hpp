#pragma once

// Edge enumeration and resolution on kernel shapes, shared by the edge
// queries and the chamfer.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Edges.hpp>

#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_List.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>

#include <vector>

namespace bettercad::geometry::occt {

using AncestorMap = NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher>;

struct KernelEdge {
    TopoDS_Edge edge;
    EdgeInfo info;
    /// The distinct faces bounded by the edge.
    std::vector<TopoDS_Face> faces;
};

/// The non-degenerate edges of @p shape with their geometry and faces.
[[nodiscard]] std::vector<KernelEdge> kernelEdges(const TopoDS_Shape& shape);

/// The edges whose supporting curve matches @p signature.
[[nodiscard]] std::vector<const KernelEdge*> matchingEdges(const std::vector<KernelEdge>& edges,
                                                           const EdgeSignature& signature);

/// Outward unit normal of @p face at the middle of @p edge (one of its
/// boundary edges).
[[nodiscard]] Result<Direction3D> outwardNormalAt(const TopoDS_Face& face, const TopoDS_Edge& edge);

} // namespace bettercad::geometry::occt
