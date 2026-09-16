#include "core/geometry/occt/OcctBody.hpp"
#include "core/geometry/occt/OcctGuard.hpp"

#include <bettercad/core/math/BSpline.hpp>

#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepGProp_Domain.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepGProp_Vinert.hxx>
#include <BRepGProp_VinertGK.hxx>
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
#include <TopoDS_Vertex.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec.hxx>
#include <gp_Vec2d.hxx>
#include <gp_XYZ.hxx>

#include <algorithm>
#include <array>
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
// The integration of swept-curve faces (below) stops when doubling the
// subdivisions changes its results by less than this, relative.
constexpr double kFaceConvergence = 1e-14;
constexpr int kMaxFacePieces = 64;

// Which integration a body's properties need. The kernel's adaptive Gauss
// integration is exact to rounding for faces on planes, cylinders, cones,
// spheres and tori (P3, P11) and for the B-spline faces of lofts (P11,
// docs/verification/P12-DATUM-001/kernel-probe). It is not for faces on
// surfaces of extrusion or revolution swept from ellipses and splines:
// measured, it misses the volume of an extruded periodic spline by 4.5e-2
// and of an elliptic prism by 5.5e-9 while reporting 2e-16, their areas by
// 1e-2 (docs/verification/P12-SKETCH-002/kernel-probe), and the centre of
// an elliptic prism by 6e-4 mm. Its Gauss-Kronrod integration is exact on
// them but takes about 10 s on a full revolution
// (docs/verification/P12-DATUM-001/kernel-probe).
//
// So bodies without swept-curve faces keep the adaptive integration of the
// whole shape, bit for bit. The others are summed face by face as cones from
// one apex, as the kernel does inside its own integration:
//   - swept-curve faces that cover their parameter rectangle are integrated
//     here (volume, first moment and area);
//   - other swept-curve faces take the kernel's Gauss-Kronrod integration and
//     area;
//   - every other face takes the kernel's adaptive integration and area.
bool isSweptCurve(GeomAbs_SurfaceType type) noexcept {
    return type == GeomAbs_SurfaceOfExtrusion || type == GeomAbs_SurfaceOfRevolution;
}

bool hasSweptCurveFace(const TopoDS_Shape& shape) {
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
        if (isSweptCurve(BRepAdaptor_Surface(TopoDS::Face(it.Current()), /*R=*/false).GetType())) {
            return true;
        }
    }
    return false;
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

/// One face's share of a body's properties, about the apex of the sum.
struct FaceShare {
    /// Signed volume of the cone from the apex to the face.
    double volume = 0.0;
    /// The cone's first moment about the apex.
    gp_XYZ moment{0.0, 0.0, 0.0};
    double area = 0.0;
    double volumeError = 0.0;
    double areaError = 0.0;
};

/// A sum of many small terms with the rounding of each addition carried
/// along (Neumaier). Added plainly, a million terms drift by about
/// sqrt(1e6) eps |sum|, 1e-13 relative, hiding the convergence of the rules
/// below.
class CompensatedSum {
public:
    void add(double term) noexcept {
        const double next = sum_ + term;
        correction_ += std::abs(sum_) >= std::abs(term) ? (sum_ - next) + term : (term - next) + sum_;
        sum_ = next;
    }
    [[nodiscard]] double value() const noexcept { return sum_ + correction_; }

private:
    double sum_ = 0.0;
    double correction_ = 0.0;
};

/// The integrals over a face of its area, of its cone's volume and first
/// moment (magnitudes alongside, to judge convergence).
struct RectangleSums {
    CompensatedSum area;
    CompensatedSum volume;
    std::array<CompensatedSum, 3> moment;
    CompensatedSum volumeScale;
    CompensatedSum momentScale;

    [[nodiscard]] gp_XYZ momentValue() const {
        return gp_XYZ(moment[0].value(), moment[1].value(), moment[2].value());
    }
};

/// Properties of a face covering the parameter rectangle @p box: an 8-point
/// Gauss-Legendre rule in each direction on every knot span, each cut into
/// 1, 2, 4, ... pieces until the results settle. The cone from the apex @p o
/// to the element X of the face (normal N = Su x Sv, reversed with the face)
/// has volume (X - o).N / 3 and first moment ((X - o).N) (X - o) / 4 about
/// @p o, the quantities the kernel's cones integrate.
FaceShare rectangleShare(const BRepAdaptor_Surface& surface, const ParameterBox& box, const gp_XYZ& o, double sign) {
    const std::vector<double> us = spans(
        surface.NbUIntervals(GeomAbs_CN), [&](auto& t) { surface.UIntervals(t, GeomAbs_CN); }, box.u1, box.u2);
    const std::vector<double> vs = spans(
        surface.NbVIntervals(GeomAbs_CN), [&](auto& t) { surface.VIntervals(t, GeomAbs_CN); }, box.v1, box.v2);
    const GaussLegendreRule& rule = gaussLegendreRule();
    RectangleSums previous;
    double change = 1.0;
    for (int pieces = 1; pieces <= kMaxFacePieces; pieces *= 2) {
        RectangleSums sums;
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
                                const double w = rule.weights[i] * rule.weights[j] * du * dv;
                                const gp_XYZ normal = dU.Crossed(dV).XYZ();
                                const gp_XYZ r = point.XYZ() - o;
                                const double flux = sign * r.Dot(normal);
                                const double size = normal.Modulus();
                                sums.area.add(w * size);
                                sums.volume.add(w * flux / 3.0);
                                sums.moment[0].add(w * flux / 4.0 * r.X());
                                sums.moment[1].add(w * flux / 4.0 * r.Y());
                                sums.moment[2].add(w * flux / 4.0 * r.Z());
                                sums.volumeScale.add(w * r.Modulus() * size / 3.0);
                                sums.momentScale.add(w * r.SquareModulus() * size / 4.0);
                            }
                        }
                    }
                }
            }
        }
        if (pieces > 1) {
            const gp_XYZ moved = sums.momentValue() - previous.momentValue();
            const double areaChange = std::abs(sums.area.value() - previous.area.value()) / sums.area.value();
            const double volumeChange =
                std::max(std::abs(sums.volume.value() - previous.volume.value()) / sums.volumeScale.value(),
                         std::max({std::abs(moved.X()), std::abs(moved.Y()), std::abs(moved.Z())}) /
                             sums.momentScale.value());
            change = std::max(areaChange, volumeChange);
            if (change <= kFaceConvergence) {
                return FaceShare{sums.volume.value(), sums.momentValue(), sums.area.value(), volumeChange,
                                 areaChange};
            }
        }
        previous = sums;
    }
    return FaceShare{previous.volume.value(), previous.momentValue(), previous.area.value(), change, change};
}

/// The kernel's share of a face about @p o: Gauss-Kronrod integration over
/// knot spans for swept-curve faces, adaptive Gauss for the others (both
/// integrate the same cones, docs/verification/P12-DATUM-001/kernel-probe),
/// and its adaptive area.
FaceShare kernelShare(const TopoDS_Face& face, const gp_XYZ& o, bool sweptCurve) {
    FaceShare share;
    const bool natural = face.NbChildren() == 0;
    const gp_Pnt apex(o);
    if (sweptCurve) {
        BRepGProp_Face spanned(face, /*IsUseSpan=*/true);
        BRepGProp_VinertGK cone;
        cone.SetLocation(apex);
        if (natural) {
            share.volumeError =
                cone.Perform(spanned, kPropertyRelativeTolerance, /*CGFlag=*/true, /*IFlag=*/false);
        } else {
            BRepGProp_Domain domain(face);
            share.volumeError =
                cone.Perform(spanned, domain, kPropertyRelativeTolerance, /*CGFlag=*/true, /*IFlag=*/false);
        }
        share.volume = cone.Mass();
        share.moment = cone.Mass() * (cone.CentreOfMass().XYZ() - o);
    } else {
        BRepGProp_Face plain(face);
        BRepGProp_Vinert cone;
        cone.SetLocation(apex);
        if (natural) {
            share.volumeError = cone.Perform(plain, kPropertyRelativeTolerance);
        } else {
            BRepGProp_Domain domain(face);
            share.volumeError = cone.Perform(plain, domain, kPropertyRelativeTolerance);
        }
        share.volume = cone.Mass();
        share.moment = cone.Mass() * (cone.CentreOfMass().XYZ() - o);
    }
    GProp_GProps surfaceProps;
    share.areaError = BRepGProp::SurfaceProperties(face, surfaceProps, kPropertyRelativeTolerance);
    share.area = surfaceProps.Mass();
    return share;
}

/// The mean of a shape's vertices (the origin if it has none): the apex of
/// the face-by-face sum, close to the body so that the cones stay small.
gp_XYZ apexOf(const TopoDS_Shape& shape) {
    occt::ShapeMap vertices;
    TopExp::MapShapes(shape, TopAbs_VERTEX, vertices);
    gp_XYZ sum(0.0, 0.0, 0.0);
    for (int i = 1; i <= vertices.Extent(); ++i) {
        sum += BRep_Tool::Pnt(TopoDS::Vertex(vertices(i))).XYZ();
    }
    return vertices.Extent() > 0 ? sum / vertices.Extent() : sum;
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
        if (!hasSweptCurveFace(shape)) {
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

        const gp_XYZ apex = apexOf(shape);
        FaceShare total;
        for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
            const TopoDS_Face& face = TopoDS::Face(it.Current());
            const TopAbs_Orientation orientation = face.Orientation();
            if (orientation != TopAbs_FORWARD && orientation != TopAbs_REVERSED) {
                continue; // as the kernel's integration: no side, no volume
            }
            const BRepAdaptor_Surface surface(face, /*R=*/false);
            const bool sweptCurve = isSweptCurve(surface.GetType());
            std::optional<ParameterBox> box;
            if (sweptCurve) {
                box = coveredRectangle(face);
            }
            const FaceShare share =
                box ? rectangleShare(surface, *box, apex, orientation == TopAbs_REVERSED ? -1.0 : 1.0)
                    : kernelShare(face, apex, sweptCurve);
            if (share.volumeError < 0.0) {
                return makeError(ErrorCode::Internal, "mass properties: the kernel's volume integration failed");
            }
            total.volume += share.volume;
            total.moment += share.moment;
            total.area += share.area;
            total.volumeError = std::max(total.volumeError, share.volumeError);
            total.areaError = std::max(total.areaError, share.areaError);
        }
        if (!(std::abs(total.volume) > 0.0)) {
            return makeError(ErrorCode::Internal, "mass properties: the body encloses no volume");
        }
        properties.volume = occt::volumeFromModel(total.volume);
        properties.surfaceArea = occt::areaFromModel(total.area);
        properties.centerOfMass = occt::pointFromModel(gp_Pnt(apex + total.moment / total.volume));
        properties.volumeRelativeError = total.volumeError;
        properties.areaRelativeError = total.areaError;
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
