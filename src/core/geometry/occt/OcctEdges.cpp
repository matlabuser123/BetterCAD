#include "core/geometry/EdgeMatching.hpp"
#include "core/geometry/occt/OcctBody.hpp"
#include "core/geometry/occt/OcctGuard.hpp"
#include "core/geometry/occt/OcctTopology.hpp"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <Geom2d_Curve.hxx>
#include <GeomAbs_CurveType.hxx>
#include <Precision.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <gp_Circ.hxx>
#include <gp_Lin.hxx>
#include <gp_Pnt2d.hxx>

#include <algorithm>

namespace bettercad::geometry {

namespace occt {

namespace {

Direction3D direction(const gp_Dir& d) {
    // gp_Dir is unit length by construction.
    return *Direction3D::fromComponents(d.X(), d.Y(), d.Z());
}

EdgeInfo describeEdge(const TopoDS_Edge& edge, std::size_t faces) {
    const BRepAdaptor_Curve curve(edge);
    const double first = curve.FirstParameter();
    const double last = curve.LastParameter();
    EdgeInfo info;
    info.start = pointFromModel(curve.Value(first));
    info.end = pointFromModel(curve.Value(last));
    info.midpoint = pointFromModel(curve.Value(0.5 * (first + last)));
    info.length = lengthFromModel(GCPnts_AbscissaPoint::Length(curve));
    info.faces = faces;
    switch (curve.GetType()) {
    case GeomAbs_Line: {
        const gp_Lin line = curve.Line();
        info.curve = EdgeCurve::Line;
        info.signature = lineSignature(pointFromModel(line.Location()), direction(line.Direction()));
        break;
    }
    case GeomAbs_Circle: {
        const gp_Circ circle = curve.Circle();
        info.curve = EdgeCurve::Circle;
        auto signature = circleSignature(pointFromModel(circle.Location()), direction(circle.Axis().Direction()),
                                         lengthFromModel(circle.Radius()));
        if (signature) {
            info.signature = *signature;
        }
        break;
    }
    default:
        info.curve = EdgeCurve::Other;
        break;
    }
    return info;
}

} // namespace

std::vector<KernelEdge> kernelEdges(const TopoDS_Shape& shape) {
    AncestorMap ancestors;
    TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, ancestors);
    std::vector<KernelEdge> edges;
    for (int i = 1; i <= ancestors.Extent(); ++i) {
        const TopoDS_Edge& edge = TopoDS::Edge(ancestors.FindKey(i));
        if (BRep_Tool::Degenerated(edge)) {
            continue;
        }
        std::vector<TopoDS_Face> faces;
        for (const TopoDS_Shape& face : ancestors.FindFromIndex(i)) {
            const bool known = std::ranges::any_of(faces, [&](const TopoDS_Face& f) { return f.IsSame(face); });
            if (!known) {
                faces.push_back(TopoDS::Face(face));
            }
        }
        KernelEdge entry{edge, describeEdge(edge, faces.size()), std::move(faces)};
        edges.push_back(std::move(entry));
    }
    return edges;
}

std::vector<const KernelEdge*> matchingEdges(const std::vector<KernelEdge>& edges, const EdgeSignature& signature) {
    std::vector<const KernelEdge*> matches;
    for (const KernelEdge& edge : edges) {
        if (edge.info.signature && detail::sameCurve(*edge.info.signature, signature)) {
            matches.push_back(&edge);
        }
    }
    return matches;
}

Result<Direction3D> outwardNormalAt(const TopoDS_Face& face, const TopoDS_Edge& edge) {
    double first = 0.0;
    double last = 0.0;
    const occ::handle<Geom2d_Curve> pcurve = BRep_Tool::CurveOnSurface(edge, face, first, last);
    if (pcurve.IsNull()) {
        return makeError(ErrorCode::Internal, "the edge has no curve on its face");
    }
    const gp_Pnt2d uv = pcurve->Value(0.5 * (first + last));
    const BRepAdaptor_Surface surface(face);
    BRepLProp_SLProps properties(surface, uv.X(), uv.Y(), 1, Precision::Confusion());
    if (!properties.IsNormalDefined()) {
        return makeError(ErrorCode::Internal, "the face normal is undefined at the edge");
    }
    gp_Dir normal = properties.Normal();
    if (face.Orientation() == TopAbs_REVERSED) {
        normal.Reverse();
    }
    return direction(normal);
}

} // namespace occt

Result<std::vector<EdgeInfo>> listEdges(const Body& body) {
    const TopoDS_Shape* shape = occt::BodyAccess::shape(body);
    if (shape == nullptr) {
        return makeError(ErrorCode::FailedPrecondition, "an empty body has no edges");
    }
    return occt::guardKernelCall("listEdges", [&]() -> Result<std::vector<EdgeInfo>> {
        std::vector<EdgeInfo> result;
        for (occt::KernelEdge& edge : occt::kernelEdges(*shape)) {
            result.push_back(std::move(edge.info));
        }
        return result;
    });
}

Result<std::vector<EdgeInfo>> findEdges(const Body& body, const EdgeSignature& signature) {
    if (auto valid = validate(signature); !valid) {
        return std::unexpected(valid.error());
    }
    const TopoDS_Shape* shape = occt::BodyAccess::shape(body);
    if (shape == nullptr) {
        return makeError(ErrorCode::FailedPrecondition, "an empty body has no edges");
    }
    return occt::guardKernelCall("findEdges", [&]() -> Result<std::vector<EdgeInfo>> {
        const auto edges = occt::kernelEdges(*shape);
        std::vector<EdgeInfo> result;
        for (const occt::KernelEdge* edge : occt::matchingEdges(edges, signature)) {
            result.push_back(edge->info);
        }
        return result;
    });
}

} // namespace bettercad::geometry
