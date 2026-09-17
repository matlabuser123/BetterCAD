#include "core/geometry/RadiusLaw.hpp"
#include "core/geometry/occt/OcctBlend.hpp"
#include "core/geometry/occt/OcctBody.hpp"
#include "core/geometry/occt/OcctGuard.hpp"
#include "core/geometry/occt/OcctTopology.hpp"

#include <bettercad/core/geometry/VariableFillet.hpp>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Check.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Law_Function.hxx>
#include <NCollection_Array1.hxx>
#include <Precision.hxx>
#include <TopAbs_State.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <string>
#include <vector>

namespace bettercad::geometry {

namespace {

constexpr occt::BlendNames kNames{"variable-radius fillet", "filleted"};

// As for constant fillets: faces whose normals are this close join smoothly,
// and faces this close to opposite fold back on each other.
constexpr double kSmoothAngle = 1e-6;
constexpr double kKnifeAngle = std::numbers::pi - 1e-6;
// The kernel merges stations closer than its precision (1e-7 mm) on the
// edge; ten times that is required.
constexpr double kMinimumStationSpacingMm = 1e-6;
// The kernel extends the fillet of an edge that meets no other rounded edge
// by half its length at each end (ChFi3d_FilBuilder::ExtentOneCorner), and
// its radius law runs over that extension (docs/verification/P12-FEAT-006).
constexpr double kExtension = 0.5;
// The kernel's law against BetterCAD's: equal to 3e-15 mm in every probed
// case, so anything beyond rounding means a different law.
constexpr double kLawToleranceMm = 1e-9;
// Law bounds against the expected extension, relative to the edge length.
constexpr double kBoundsToleranceRelative = 1e-9;
// The fillet's surface and contact lines against the law's geometry: the
// kernel's own precision (Precision::Confusion). The probes measured 4e-14.
constexpr double kGeometryToleranceMm = 1e-7;
constexpr int kLawSamples = 64;
constexpr int kSurfaceSamples = 16; // intervals per surface parameter
constexpr int kBoundarySamples = 32; // intervals per boundary edge

/// A straight edge between two planar faces, as the fillet needs it (model
/// units).
struct EdgeFrame {
    gp_Pnt start;       // the end that comes first along the canonical direction
    gp_Dir direction;   // the signature's canonical direction
    double length = 0.0;
    gp_Dir normal1;     // outward
    gp_Dir normal2;
    double angle = 0.0; // between the outward normals
    bool convex = true;
};

std::unexpected<Error> failed(const std::string& name, std::string_view why) {
    return makeError(ErrorCode::FailedPrecondition, std::format("{}: {} {}", kNames.noun, name, why));
}

std::unexpected<Error> wrongResult(const std::string& what) {
    return makeError(ErrorCode::Internal,
                     std::format("{}: the kernel's result is not the requested fillet: {}", kNames.noun, what));
}

/// The frame of @p edge; FailedPrecondition when its faces are not planes or
/// cannot be rounded.
Result<EdgeFrame> frameOf(const occt::KernelEdge& edge, const EdgeSignature& signature, const std::string& name) {
    for (const TopoDS_Face& face : edge.faces) {
        if (BRepAdaptor_Surface(face).GetType() != GeomAbs_Plane) {
            return failed(name, std::format("is not between two planar faces; a {} rounds edges between planes only",
                                            kNames.noun));
        }
    }
    auto n1 = occt::outwardNormalAt(edge.faces[0], edge.edge);
    auto n2 = occt::outwardNormalAt(edge.faces[1], edge.edge);
    if (!n1 || !n2) {
        return makeError(ErrorCode::Internal,
                         std::format("{}: {}: {}", kNames.noun, name, (!n1 ? n1 : n2).error().message));
    }
    EdgeFrame frame;
    frame.normal1 = occt::toModel(*n1);
    frame.normal2 = occt::toModel(*n2);
    frame.angle = frame.normal1.Angle(frame.normal2);
    if (frame.angle < kSmoothAngle) {
        return failed(name, "joins its two faces smoothly; there is no corner to round");
    }
    if (frame.angle > kKnifeAngle) {
        return failed(name, "lies between faces that fold back onto each other; they cannot be rounded");
    }
    frame.direction = occt::toModel(signature.direction);
    TopoDS_Vertex first;
    TopoDS_Vertex last;
    TopExp::Vertices(edge.edge, first, last);
    const gp_Pnt a = BRep_Tool::Pnt(first);
    const gp_Pnt b = BRep_Tool::Pnt(last);
    frame.length = a.Distance(b);
    frame.start = gp_Vec(a, b).Dot(gp_Vec(frame.direction)) > 0.0 ? a : b;

    // The edge runs through face 1's boundary with the face on its left, seen
    // from outside; so n1 x t points into face 1. The edge is convex when
    // face 1 lies behind face 2's plane.
    for (TopExp_Explorer it(edge.faces[0], TopAbs_EDGE); it.More(); it.Next()) {
        if (!it.Current().IsSame(edge.edge)) {
            continue;
        }
        const TopoDS_Edge& oriented = TopoDS::Edge(it.Current());
        const BRepAdaptor_Curve curve(oriented);
        gp_Vec tangent = curve.DN(0.5 * (curve.FirstParameter() + curve.LastParameter()), 1);
        if (oriented.Orientation() == TopAbs_REVERSED) {
            tangent.Reverse();
        }
        const gp_Vec inward = gp_Vec(frame.normal1).Crossed(tangent);
        frame.convex = inward.Dot(gp_Vec(frame.normal2)) < 0.0;
        return frame;
    }
    return makeError(ErrorCode::Internal,
                     std::format("{}: {}: the edge is not on the boundary of its face", kNames.noun, name));
}

/// Where the fillet of @p edge is expected to be, and how it is built.
struct PlannedEdge {
    const occt::KernelEdge* edge = nullptr;
    EdgeFrame frame;
    detail::RadiusLaw law;
    bool constant = false;
    double radius = 0.0; // model units, when constant
    int contour = 0;
    bool forward = true; // the kernel's spine runs along the canonical direction
};

/// The law's radius at @p point, in model units, from its position along the edge.
double radiusNear(const PlannedEdge& planned, const gp_Pnt& point, double& along) {
    along = gp_Vec(planned.frame.start, point).Dot(gp_Vec(planned.frame.direction));
    return occt::toModel(Length::fromSi(planned.law(along / planned.frame.length)));
}

/// The kernel's radius law along @p planned's edge against BetterCAD's.
Result<void> checkLaw(BRepFilletAPI_MakeFillet& maker, const PlannedEdge& planned, const std::string& name) {
    const TopoDS_Edge& edge = planned.edge->edge;
    const double length = planned.frame.length;
    if (maker.IsConstant(planned.contour, edge)) {
        if (!planned.constant) {
            return wrongResult(std::format("the kernel rounded {} with a constant radius", name));
        }
        const double radius = maker.Radius(planned.contour, edge);
        if (std::abs(radius - planned.radius) > kLawToleranceMm) {
            return wrongResult(std::format("the kernel rounded {} with radius {:.12g} mm, not {:.12g} mm", name,
                                           radius, planned.radius));
        }
        return {};
    }
    if (planned.constant) {
        return wrongResult(std::format("the kernel's radius varies along {}", name));
    }
    double from = 0.0;
    double to = 0.0;
    if (!maker.GetBounds(planned.contour, edge, from, to)) {
        return wrongResult(std::format("the kernel has no radius law for {}", name));
    }
    const double slack = kBoundsToleranceRelative * length;
    if (std::abs(from + kExtension * length) > slack || std::abs(to - (1.0 + kExtension) * length) > slack) {
        return wrongResult(std::format("the kernel's radius law along {} runs over [{:.12g}, {:.12g}] mm, not "
                                       "[{:.12g}, {:.12g}] mm (another blend changed its extension)",
                                       name, from, to, -kExtension * length, (1.0 + kExtension) * length));
    }
    const occ::handle<Law_Function> law = maker.GetLaw(planned.contour, edge);
    if (law.IsNull()) {
        return wrongResult(std::format("the kernel has no radius law for {}", name));
    }
    for (int i = 0; i <= kLawSamples; ++i) {
        const double position = static_cast<double>(i) / kLawSamples;
        const double along = position * length;
        const double kernel = law->Value(planned.forward ? along : length - along);
        const double expected = occt::toModel(Length::fromSi(planned.law(position)));
        if (!(std::abs(kernel - expected) <= kLawToleranceMm)) {
            return wrongResult(std::format("along {} at {:.6g} the kernel's radius is {:.12g} mm, not {:.12g} mm",
                                           name, position, kernel, expected));
        }
    }
    return {};
}

/// The fillet face(s) of @p planned against the law's geometry: every
/// sampled surface point on the arc of radius r(s) tangent to both faces,
/// and every boundary point on either face at r(s) tan(g / 2) from the edge,
/// with some on each face.
Result<void> checkFaces(const NCollection_List<TopoDS_Shape>& faces, const PlannedEdge& planned,
                        const std::string& name) {
    const EdgeFrame& f = planned.frame;
    const gp_Vec n1(f.normal1);
    const gp_Vec n2(f.normal2);
    // The ball's centre sits r / cos(g/2) from the edge, inside the solid for a
    // convex edge and outside for a concave one: at -(n1 + n2) r / (1 + n1.n2)
    // or +(...).
    const gp_Vec toCentre = (n1 + n2) * ((f.convex ? -1.0 : 1.0) / (1.0 + n1.Dot(n2)));
    const double contactFactor = std::tan(f.angle / 2.0);
    const gp_Pln plane1(f.start, f.normal1);
    const gp_Pln plane2(f.start, f.normal2);
    const gp_Vec along(f.direction);
    int onFace1 = 0;
    int onFace2 = 0;
    for (const TopoDS_Shape& shape : faces) {
        const TopoDS_Face& face = TopoDS::Face(shape);
        const BRepAdaptor_Surface surface(face);
        double u0 = 0.0;
        double u1 = 0.0;
        double v0 = 0.0;
        double v1 = 0.0;
        BRepTools::UVBounds(face, u0, u1, v0, v1);
        for (int i = 0; i <= kSurfaceSamples; ++i) {
            for (int j = 0; j <= kSurfaceSamples; ++j) {
                const gp_Pnt2d uv(u0 + (u1 - u0) * i / kSurfaceSamples, v0 + (v1 - v0) * j / kSurfaceSamples);
                const BRepClass_FaceClassifier classifier(face, uv, Precision::Confusion());
                if (classifier.State() == TopAbs_OUT) {
                    continue;
                }
                const gp_Pnt point = surface.Value(uv.X(), uv.Y());
                double s = 0.0;
                const double r = radiusNear(planned, point, s);
                const gp_Pnt centre = f.start.Translated(along * s + toCentre * r);
                const double off = std::abs(point.Distance(centre) - r);
                if (!(off <= kGeometryToleranceMm)) {
                    return wrongResult(std::format("the fillet of {} is {:.3g} mm off the rolling-ball section of "
                                                   "radius {:.9g} mm at {:.6g} along the edge",
                                                   name, off, r, s / f.length));
                }
            }
        }
        for (TopExp_Explorer it(face, TopAbs_EDGE); it.More(); it.Next()) {
            const TopoDS_Edge& boundary = TopoDS::Edge(it.Current());
            if (BRep_Tool::Degenerated(boundary)) {
                continue;
            }
            const BRepAdaptor_Curve curve(boundary);
            const double first = curve.FirstParameter();
            const double last = curve.LastParameter();
            for (int k = 0; k <= kBoundarySamples; ++k) {
                const gp_Pnt point = curve.Value(first + (last - first) * k / kBoundarySamples);
                const bool on1 = plane1.Distance(point) <= kGeometryToleranceMm;
                const bool on2 = plane2.Distance(point) <= kGeometryToleranceMm;
                if (!on1 && !on2) {
                    continue;
                }
                onFace1 += on1 ? 1 : 0;
                onFace2 += on2 ? 1 : 0;
                double s = 0.0;
                const double r = radiusNear(planned, point, s);
                const gp_Pnt foot = f.start.Translated(along * s);
                const double off = std::abs(point.Distance(foot) - r * contactFactor);
                if (!(off <= kGeometryToleranceMm)) {
                    return wrongResult(std::format("the fillet of {} touches its face {:.3g} mm away from where a "
                                                   "radius of {:.9g} mm touches it, at {:.6g} along the edge",
                                                   name, off, r, s / f.length));
                }
            }
        }
    }
    if (onFace1 == 0 || onFace2 == 0) {
        return wrongResult(std::format("the fillet of {} does not touch both of the edge's faces", name));
    }
    return {};
}

} // namespace

Result<Body> variableFilletEdges(const Body& body, const VariableFilletRequest& request) {
    const TopoDS_Shape* shape = occt::BodyAccess::shape(body);
    if (shape == nullptr) {
        return makeError(ErrorCode::FailedPrecondition, std::format("{}: the body is empty", kNames.noun));
    }
    if (auto valid = validate(request); !valid) {
        return makeError(ErrorCode::InvalidArgument, std::format("{}: {}", kNames.noun, valid.error().message));
    }
    const std::size_t solids = body.topology().solids;
    std::vector<EdgeSignature> references;
    for (const VariableFilletEdge& entry : request.edges) {
        references.push_back(entry.edge);
    }

    return occt::guardKernelCall(kNames.noun, [&]() -> Result<Body> {
        const std::vector<occt::KernelEdge> edges = occt::kernelEdges(*shape);
        BRepFilletAPI_MakeFillet maker(*shape);
        std::vector<occt::BlendStrip> strips;
        std::vector<PlannedEdge> planned;
        for (std::size_t i = 0; i < request.edges.size(); ++i) {
            const VariableFilletEdge& entry = request.edges[i];
            const std::string name = occt::referenceName(i, entry.edge);
            auto resolved = occt::resolveBlendEdge(kNames, edges, references, i, maker);
            if (!resolved) {
                return std::unexpected(resolved.error());
            }
            const occt::KernelEdge& edge = **resolved;
            auto frame = frameOf(edge, entry.edge, name);
            if (!frame) {
                return std::unexpected(frame.error());
            }
            for (std::size_t j = 0; j < planned.size(); ++j) {
                TopoDS_Vertex common;
                if (TopExp::CommonVertex(edge.edge, planned[j].edge->edge, common)) {
                    return failed(name, std::format("meets {} at a vertex; a {} rounds edges that meet no other "
                                                    "edge it rounds",
                                                    occt::referenceName(j, references[j]), kNames.noun));
                }
            }
            maker.Add(edge.edge);
            const int contour = maker.Contour(edge.edge);
            if (maker.NbEdges(contour) != 1) {
                return failed(name, std::format("continues smoothly into {} other edge(s); a {} rounds single "
                                                "edges only",
                                                maker.NbEdges(contour) - 1, kNames.noun));
            }
            const gp_Pnt spineStart = BRep_Tool::Pnt(maker.FirstVertex(contour));
            const gp_Pnt spineEnd = BRep_Tool::Pnt(maker.LastVertex(contour));
            bool forward = true;
            if (spineStart.Distance(frame->start) <= Precision::Confusion()) {
                forward = true;
            } else if (spineEnd.Distance(frame->start) <= Precision::Confusion()) {
                forward = false;
            } else {
                return makeError(ErrorCode::Internal,
                                 std::format("{}: {}: cannot tell which way the kernel runs along the edge",
                                             kNames.noun, name));
            }
            const std::vector<RadiusStation>& stations = entry.stations;
            for (std::size_t k = 1; k < stations.size(); ++k) {
                const double spacing = (stations[k].position - stations[k - 1].position) * frame->length;
                if (spacing < kMinimumStationSpacingMm) {
                    return failed(name, std::format("has stations {} and {} only {:.3g} mm apart on it; they must "
                                                    "be at least {:g} mm apart",
                                                    k, k + 1, spacing, kMinimumStationSpacingMm));
                }
            }

            // The stations in the direction the kernel runs.
            const int count = static_cast<int>(stations.size());
            NCollection_Array1<gp_Pnt2d> uandr(1, count);
            for (int k = 0; k < count; ++k) {
                const RadiusStation& station = stations[static_cast<std::size_t>(forward ? k : count - 1 - k)];
                uandr.SetValue(k + 1, gp_Pnt2d(forward ? station.position : 1.0 - station.position,
                                               occt::toModel(station.radius)));
            }
            maker.SetRadius(uandr, contour, 1);

            PlannedEdge plan{.edge = &edge, .frame = *frame, .law = detail::RadiusLaw(stations)};
            plan.constant = std::ranges::all_of(
                stations, [&](const RadiusStation& station) { return station.radius == stations.front().radius; });
            plan.radius = occt::toModel(stations.front().radius);
            plan.contour = contour;
            plan.forward = forward;

            // Room for the largest radius on the edge.
            const double width = occt::toModel(Length::fromSi(plan.law.largest())) * std::tan(frame->angle / 2.0);
            for (const TopoDS_Face& face : edge.faces) {
                strips.push_back({edge.edge, face, width, i, true});
            }
            planned.push_back(std::move(plan));
        }
        if (auto fits = occt::checkRoom(kNames, strips, references); !fits) {
            return std::unexpected(fits.error());
        }
        auto result = occt::buildBlend(kNames, maker, solids, body);
        if (!result) {
            return result;
        }
        const TopoDS_Shape& built = *occt::BodyAccess::shape(*result);
        if (!BRepAlgoAPI_Check(built, /*bTestSE=*/false, /*bTestSI=*/true).IsValid()) {
            return wrongResult("the solid intersects itself");
        }
        occt::ShapeMap resultFaces;
        TopExp::MapShapes(built, TopAbs_FACE, resultFaces);
        for (std::size_t i = 0; i < planned.size(); ++i) {
            const std::string name = occt::referenceName(i, references[i]);
            NCollection_List<TopoDS_Shape> faces;
            for (const TopoDS_Shape& generated : maker.Generated(planned[i].edge->edge)) {
                if (generated.ShapeType() == TopAbs_FACE && resultFaces.Contains(generated)) {
                    faces.Append(generated);
                }
            }
            if (faces.Size() != 1) {
                return wrongResult(std::format("{} fillet faces were generated from {}, not one", faces.Size(),
                                               name));
            }
            if (auto law = checkLaw(maker, planned[i], name); !law) {
                return std::unexpected(law.error());
            }
            if (auto geometry = checkFaces(faces, planned[i], name); !geometry) {
                return std::unexpected(geometry.error());
            }
        }
        return result;
    });
}

} // namespace bettercad::geometry
