#include "core/geometry/occt/OcctBody.hpp"
#include "core/geometry/occt/OcctGuard.hpp"

#include <bettercad/core/math/BSpline.hpp>

#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Geom2d_Curve.hxx>
#include <GeomAbs_Shape.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <NCollection_Array1.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec.hxx>
#include <gp_Vec2d.hxx>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace bettercad::geometry {

namespace {

// Target relative error of the adaptive volume/area integration.
constexpr double kPropertyRelativeTolerance = 1e-10;
// The area integration of swept-curve faces (below) stops when doubling the
// subdivisions changes the area by less than this, relative.
constexpr double kAreaConvergence = 1e-14;
constexpr int kMaxAreaPieces = 64;

// Which integration a body's properties need. The kernel's adaptive Gauss
// integration is exact to rounding for faces on planes, cylinders, cones,
// spheres and tori (P3, P11). It is not for faces swept from ellipses and
// B-splines, nor reliably for B-spline faces: measured, it misses the
// volume of an extruded spline by 4.5e-2 and of an elliptic prism by
// 5.5e-9 while reporting 2e-16, and their areas by 1e-2
// (docs/verification/P12-SKETCH-002/kernel-probe). Bodies with any such
// face take the kernel's Gauss-Kronrod volume integration over knot spans
// (exact to rounding in every probed case), and the areas of swept-curve
// faces are integrated here.
bool isElementary(GeomAbs_SurfaceType type) noexcept {
    switch (type) {
    case GeomAbs_Plane:
    case GeomAbs_Cylinder:
    case GeomAbs_Cone:
    case GeomAbs_Sphere:
    case GeomAbs_Torus:
        return true;
    default:
        return false;
    }
}

bool isSweptCurve(GeomAbs_SurfaceType type) noexcept {
    return type == GeomAbs_SurfaceOfExtrusion || type == GeomAbs_SurfaceOfRevolution;
}

bool allFacesElementary(const TopoDS_Shape& shape) {
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
        if (!isElementary(BRepAdaptor_Surface(TopoDS::Face(it.Current()), /*R=*/false).GetType())) {
            return false;
        }
    }
    return true;
}

struct ParameterBox {
    double u1 = 0.0;
    double u2 = 0.0;
    double v1 = 0.0;
    double v2 = 0.0;
};

/// The parameter rectangle a face covers entirely, if it does: its boundary
/// runs along the sides of the p-curves' bounding box, so the area the
/// boundary encloses in the parameter plane (Green's theorem, exact for the
/// straight p-curves of such a boundary) is the box's.
std::optional<ParameterBox> coveredRectangle(const TopoDS_Face& face) {
    const GaussLegendreRule& rule = gaussLegendreRule();
    ParameterBox box{std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
                     std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    double enclosed = 0.0;
    for (TopExp_Explorer it(face, TopAbs_EDGE); it.More(); it.Next()) {
        const TopoDS_Edge& edge = TopoDS::Edge(it.Current());
        double first = 0.0;
        double last = 0.0;
        const occ::handle<Geom2d_Curve> pcurve = BRep_Tool::CurveOnSurface(edge, face, first, last);
        if (pcurve.IsNull()) {
            return std::nullopt;
        }
        const double sign = edge.Orientation() == TopAbs_REVERSED ? -1.0 : 1.0;
        for (const double t : {first, last}) {
            const gp_Pnt2d p = pcurve->Value(t);
            box.u1 = std::min(box.u1, p.X());
            box.u2 = std::max(box.u2, p.X());
            box.v1 = std::min(box.v1, p.Y());
            box.v2 = std::max(box.v2, p.Y());
        }
        for (std::size_t k = 0; k < GaussLegendreRule::kPoints; ++k) {
            gp_Pnt2d p;
            gp_Vec2d d;
            pcurve->D1(first + rule.nodes[k] * (last - first), p, d);
            enclosed += sign * rule.weights[k] * (last - first) * p.X() * d.Y();
        }
    }
    const double area = (box.u2 - box.u1) * (box.v2 - box.v1);
    if (!(area > 0.0) || std::abs(std::abs(enclosed) - area) > 1e-9 * area) {
        return std::nullopt;
    }
    return box;
}

/// The parameter values in [low, high] where the surface's derivatives may
/// jump (its knots), with low and high.
std::vector<double> spans(int count, const std::function<void(NCollection_Array1<double>&)>& fill, double low,
                          double high) {
    NCollection_Array1<double> breaks(1, count + 1);
    fill(breaks);
    std::vector<double> result{low};
    for (int i = 1; i <= count + 1; ++i) {
        if (breaks(i) > low && breaks(i) < high) {
            result.push_back(breaks(i));
        }
    }
    result.push_back(high);
    return result;
}

/// Area of a face covering the parameter rectangle @p box, and the relative
/// change of the last refinement: an 8-point Gauss-Legendre rule in each
/// direction on every knot span, each cut into 1, 2, 4, ... pieces until the
/// area settles.
std::pair<double, double> rectangleArea(const BRepAdaptor_Surface& surface, const ParameterBox& box) {
    const std::vector<double> us = spans(
        surface.NbUIntervals(GeomAbs_CN), [&](auto& t) { surface.UIntervals(t, GeomAbs_CN); }, box.u1, box.u2);
    const std::vector<double> vs = spans(
        surface.NbVIntervals(GeomAbs_CN), [&](auto& t) { surface.VIntervals(t, GeomAbs_CN); }, box.v1, box.v2);
    const GaussLegendreRule& rule = gaussLegendreRule();
    double previous = 0.0;
    double change = 1.0;
    for (int pieces = 1; pieces <= kMaxAreaPieces; pieces *= 2) {
        double area = 0.0;
        for (std::size_t a = 0; a + 1 < us.size(); ++a) {
            const double du = (us[a + 1] - us[a]) / pieces;
            for (std::size_t b = 0; b + 1 < vs.size(); ++b) {
                const double dv = (vs[b + 1] - vs[b]) / pieces;
                for (int p = 0; p < pieces; ++p) {
                    for (int q = 0; q < pieces; ++q) {
                        for (std::size_t i = 0; i < GaussLegendreRule::kPoints; ++i) {
                            for (std::size_t j = 0; j < GaussLegendreRule::kPoints; ++j) {
                                gp_Pnt point;
                                gp_Vec dU;
                                gp_Vec dV;
                                surface.D1(us[a] + (p + rule.nodes[i]) * du, vs[b] + (q + rule.nodes[j]) * dv, point,
                                           dU, dV);
                                area += rule.weights[i] * rule.weights[j] * du * dv * dU.Crossed(dV).Magnitude();
                            }
                        }
                    }
                }
            }
        }
        if (pieces > 1) {
            change = std::abs(area - previous) / std::abs(area);
            if (change <= kAreaConvergence) {
                return {area, change};
            }
        }
        previous = area;
    }
    return {previous, change};
}

std::size_t countSubShapes(const TopoDS_Shape& shape, TopAbs_ShapeEnum type) {
    occt::ShapeMap map;
    TopExp::MapShapes(shape, type, map);
    return static_cast<std::size_t>(map.Extent());
}

std::unexpected<Error> emptyBodyError(std::string_view what) {
    return makeError(ErrorCode::FailedPrecondition, std::format("{} of an empty body is undefined", what));
}

} // namespace

Body::Body() noexcept = default;

Body::Body(std::shared_ptr<const occt::BodyData> data) noexcept : data_(std::move(data)) {}

bool Body::isEmpty() const noexcept {
    return data_ == nullptr || data_->shape.IsNull();
}

bool Body::isValid() const {
    if (isEmpty()) {
        return false;
    }
    try {
        const BRepCheck_Analyzer analyzer(data_->shape);
        return analyzer.IsValid();
    } catch (const Standard_Failure&) {
        return false;
    }
}

TopologySummary Body::topology() const {
    TopologySummary summary;
    if (isEmpty()) {
        return summary;
    }
    const TopoDS_Shape& shape = data_->shape;
    summary.solids = countSubShapes(shape, TopAbs_SOLID);
    summary.shells = countSubShapes(shape, TopAbs_SHELL);
    summary.faces = countSubShapes(shape, TopAbs_FACE);
    summary.edges = countSubShapes(shape, TopAbs_EDGE);
    summary.vertices = countSubShapes(shape, TopAbs_VERTEX);
    return summary;
}

Result<MassProperties> Body::massProperties() const {
    if (isEmpty()) {
        return emptyBodyError("mass properties");
    }
    return occt::guardKernelCall("mass properties", [&]() -> Result<MassProperties> {
        const TopoDS_Shape& shape = data_->shape;
        MassProperties properties;
        if (allFacesElementary(shape)) {
            GProp_GProps volumeProps;
            const double volumeError =
                BRepGProp::VolumeProperties(shape, volumeProps, kPropertyRelativeTolerance, /*OnlyClosed=*/true);
            GProp_GProps surfaceProps;
            const double areaError = BRepGProp::SurfaceProperties(shape, surfaceProps, kPropertyRelativeTolerance);
            properties.volume = occt::volumeFromModel(volumeProps.Mass());
            properties.surfaceArea = occt::areaFromModel(surfaceProps.Mass());
            properties.centerOfMass = occt::pointFromModel(volumeProps.CentreOfMass());
            properties.volumeRelativeError = volumeError;
            properties.areaRelativeError = areaError;
            return properties;
        }

        GProp_GProps volumeProps;
        const double volumeError =
            BRepGProp::VolumePropertiesGK(shape, volumeProps, kPropertyRelativeTolerance, /*OnlyClosed=*/true,
                                          /*IsUseSpan=*/true, /*CGFlag=*/true, /*IFlag=*/false);
        if (volumeError < 0.0) {
            return makeError(ErrorCode::Internal, "mass properties: the kernel's volume integration failed");
        }
        double area = 0.0;
        double areaError = 0.0;
        for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
            const TopoDS_Face& face = TopoDS::Face(it.Current());
            const BRepAdaptor_Surface surface(face, /*R=*/false);
            if (isSweptCurve(surface.GetType())) {
                if (const auto box = coveredRectangle(face)) {
                    const auto [faceArea, change] = rectangleArea(surface, *box);
                    area += faceArea;
                    areaError = std::max(areaError, change);
                    continue;
                }
            }
            GProp_GProps faceProps;
            areaError = std::max(areaError, BRepGProp::SurfaceProperties(face, faceProps, kPropertyRelativeTolerance));
            area += faceProps.Mass();
        }
        properties.volume = occt::volumeFromModel(volumeProps.Mass());
        properties.surfaceArea = occt::areaFromModel(area);
        properties.centerOfMass = occt::pointFromModel(volumeProps.CentreOfMass());
        properties.volumeRelativeError = volumeError;
        properties.areaRelativeError = areaError;
        return properties;
    });
}

Result<BoundingBox3D> Body::boundingBox() const {
    if (isEmpty()) {
        return emptyBodyError("the bounding box");
    }
    return occt::guardKernelCall("bounding box", [&]() -> Result<BoundingBox3D> {
        Bnd_Box box;
        // Exact geometric bounds: no triangulation, no tolerance padding.
        BRepBndLib::AddOptimal(data_->shape, box, /*useTriangulation=*/false,
                               /*useShapeTolerance=*/false);
        if (box.IsVoid()) {
            return makeError(ErrorCode::Internal, "bounding box: the kernel returned a void box");
        }
        box.SetGap(0.0);
        double xmin = 0.0;
        double ymin = 0.0;
        double zmin = 0.0;
        double xmax = 0.0;
        double ymax = 0.0;
        double zmax = 0.0;
        box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        return BoundingBox3D{occt::pointFromModel(gp_Pnt(xmin, ymin, zmin)),
                           occt::pointFromModel(gp_Pnt(xmax, ymax, zmax))};
    });
}

} // namespace bettercad::geometry
