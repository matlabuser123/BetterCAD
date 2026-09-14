#include "core/geometry/occt/OcctBody.hpp"
#include "core/geometry/occt/OcctGuard.hpp"
#include "core/geometry/occt/OcctTopology.hpp"

#include <bettercad/core/geometry/Chamfer.hpp>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_State.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>

namespace bettercad::geometry {

namespace {

// Faces are told apart by the reference side when the cosines of their
// normals with it differ by more than this.
constexpr double kSideTolerance = 1e-6;

// Room every chamfer must leave on the faces it cuts, in model units (mm).
// OCCT 8.0.1 cannot be trusted with chamfers that do not fit: in this
// toolchain it can run for seconds and then crash the process instead of
// failing (docs/verification/P11-FEAT-002). Near the limit it built chamfers
// leaving 1e-4 mm and gave up below that, so 1e-3 mm is required.
constexpr double kMinimumRemainderMm = 1e-3;

// Sample counts for finding how far a face reaches from an edge.
constexpr int kBoundarySamples = 16; // intervals per boundary edge
constexpr int kInteriorSamples = 8;  // intervals per surface parameter

std::string referenceName(std::size_t index, const EdgeSignature& signature) {
    return std::format("edge reference {} ({})", index + 1, describe(signature));
}

/// Of the edge's two faces, the one whose outward normal is closer to @p side.
Result<TopoDS_Face> referenceFace(const occt::KernelEdge& edge, const Direction3D& side, std::size_t index,
                                  const EdgeSignature& signature) {
    auto first = occt::outwardNormalAt(edge.faces[0], edge.edge);
    auto second = occt::outwardNormalAt(edge.faces[1], edge.edge);
    if (!first || !second) {
        return makeError(ErrorCode::Internal, std::format("chamfer: {}: {}", referenceName(index, signature),
                                                          (!first ? first : second).error().message));
    }
    const double a = first->dot(side);
    const double b = second->dot(side);
    if (std::abs(a - b) <= kSideTolerance) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("chamfer: the reference side does not tell the two faces of {} apart",
                                     referenceName(index, signature)));
    }
    return a > b ? edge.faces[0] : edge.faces[1];
}

/// How one reference is chamfered: its edge and, for the modes with a
/// reference face, that face.
struct Plan {
    const occt::KernelEdge* edge = nullptr;
    std::optional<TopoDS_Face> referenceFace;
};

/// The part of a face a chamfer removes: points within `width` of `edge`.
struct Strip {
    TopoDS_Edge edge;
    TopoDS_Face face;
    double width = 0.0; // model units
    std::size_t reference = 0;
    bool referencedEdge = true; // false for an edge the kernel added along a smooth chain
};

/// Shortest distance between two shapes in model units, or nothing if the
/// kernel cannot measure it.
std::optional<double> distance(const TopoDS_Shape& a, const TopoDS_Shape& b) {
    BRepExtrema_DistShapeShape extrema(a, b);
    if (!extrema.IsDone() || extrema.NbSolution() == 0) {
        return std::nullopt;
    }
    return extrema.Value();
}

std::optional<double> distance(const gp_Pnt& point, const TopoDS_Edge& edge) {
    return distance(BRepBuilderAPI_MakeVertex(point).Vertex(), edge);
}

bool touches(const TopoDS_Edge& a, const TopoDS_Edge& b) {
    TopoDS_Vertex common;
    return TopExp::CommonVertex(a, b, common);
}

std::string stripName(const Strip& strip, const ChamferRequest& request) {
    const std::string name = referenceName(strip.reference, request.edges[strip.reference]);
    return strip.referencedEdge ? name : std::format("an edge that joins {} smoothly", name);
}

/// How far @p face reaches from the strip's edge, as far as it matters: the
/// largest sampled distance, or the first one of at least @p needed.
std::optional<double> reach(const Strip& strip, double needed) {
    double farthest = 0.0;
    const auto consider = [&](const gp_Pnt& point) {
        const auto d = distance(point, strip.edge);
        if (!d) {
            return false;
        }
        farthest = std::max(farthest, *d);
        return true;
    };
    for (TopExp_Explorer it(strip.face, TopAbs_EDGE); it.More(); it.Next()) {
        const TopoDS_Edge& boundary = TopoDS::Edge(it.Current());
        if (BRep_Tool::Degenerated(boundary)) {
            continue;
        }
        const BRepAdaptor_Curve curve(boundary);
        const double first = curve.FirstParameter();
        const double last = curve.LastParameter();
        for (int i = 0; i <= kBoundarySamples; ++i) {
            if (!consider(curve.Value(first + (last - first) * i / kBoundarySamples))) {
                return std::nullopt;
            }
            if (farthest >= needed) {
                return farthest;
            }
        }
    }
    // A face bounded by its edge alone (a disc) reaches farthest inside.
    double u0 = 0.0;
    double u1 = 0.0;
    double v0 = 0.0;
    double v1 = 0.0;
    BRepTools::UVBounds(strip.face, u0, u1, v0, v1);
    const BRepAdaptor_Surface surface(strip.face);
    for (int i = 0; i <= kInteriorSamples; ++i) {
        for (int j = 0; j <= kInteriorSamples; ++j) {
            const gp_Pnt2d uv(u0 + (u1 - u0) * i / kInteriorSamples, v0 + (v1 - v0) * j / kInteriorSamples);
            const BRepClass_FaceClassifier classifier(strip.face, uv, Precision::Confusion());
            if (classifier.State() != TopAbs_IN) {
                continue;
            }
            if (!consider(surface.Value(uv.X(), uv.Y()))) {
                return std::nullopt;
            }
            if (farthest >= needed) {
                return farthest;
            }
        }
    }
    return farthest;
}

/// Refuses chamfers that do not fit, before the kernel sees them. Each strip
/// must stay clear of its face's other edges (those not meeting its edge),
/// must not run across the face, and must not meet another strip on the
/// same face; each by at least kMinimumRemainderMm. Distances are straight
/// lines, never longer than the distances along a curved face, so the check
/// errs towards refusing.
Result<void> checkRoom(const std::vector<Strip>& strips, const ChamferRequest& request) {
    const auto unmeasurable = [&](const Strip& strip) {
        return makeError(ErrorCode::Internal,
                         std::format("chamfer: cannot measure the room next to {}", stripName(strip, request)));
    };
    const auto tooWide = [&](const Strip& strip, double room) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("chamfer: {} does not fit: its chamfer needs {:.6g} mm on a face next to the "
                                     "edge, which leaves only {:.6g} mm (a chamfer must leave at least {:g} mm)",
                                     stripName(strip, request), strip.width, room, kMinimumRemainderMm));
    };
    for (const Strip& strip : strips) {
        const double needed = strip.width + kMinimumRemainderMm;
        for (TopExp_Explorer it(strip.face, TopAbs_EDGE); it.More(); it.Next()) {
            const TopoDS_Edge& other = TopoDS::Edge(it.Current());
            if (other.IsSame(strip.edge) || BRep_Tool::Degenerated(other) || touches(strip.edge, other)) {
                continue;
            }
            const auto gap = distance(strip.edge, other);
            if (!gap) {
                return unmeasurable(strip);
            }
            if (*gap < needed) {
                return tooWide(strip, *gap);
            }
        }
        const auto room = reach(strip, needed);
        if (!room) {
            return unmeasurable(strip);
        }
        if (*room < needed) {
            return tooWide(strip, *room);
        }
    }
    for (std::size_t i = 0; i < strips.size(); ++i) {
        for (std::size_t j = i + 1; j < strips.size(); ++j) {
            const Strip& a = strips[i];
            const Strip& b = strips[j];
            if (!a.face.IsSame(b.face) || a.edge.IsSame(b.edge) || touches(a.edge, b.edge)) {
                continue;
            }
            const auto gap = distance(a.edge, b.edge);
            if (!gap) {
                return unmeasurable(a);
            }
            if (*gap < a.width + b.width + kMinimumRemainderMm) {
                return makeError(ErrorCode::FailedPrecondition,
                                 std::format("chamfer: {} and {} do not fit together: their chamfers need {:.6g} mm "
                                             "and {:.6g} mm on a face they share, where the edges are only {:.6g} mm "
                                             "apart (a chamfer must leave at least {:g} mm)",
                                             stripName(a, request), stripName(b, request), a.width, b.width, *gap,
                                             kMinimumRemainderMm));
            }
        }
    }
    return {};
}

/// The strips of every edge the kernel will chamfer for reference @p index:
/// the referenced edge with its exact widths, and edges the kernel adds along
/// a smooth chain with the larger width on both faces.
void addStrips(const BRepFilletAPI_MakeChamfer& maker, const std::vector<occt::KernelEdge>& edges, const Plan& plan,
               std::size_t index, const ChamferRequest& request, std::vector<Strip>& strips) {
    const double d1 = occt::toModel(request.distance);
    const double d2 = request.mode == ChamferMode::TwoDistance     ? occt::toModel(request.distance2)
                      : request.mode == ChamferMode::DistanceAngle ? d1 * std::tan(request.angle.si())
                                                                   : d1;
    const int contour = maker.Contour(plan.edge->edge);
    for (int j = 1; j <= maker.NbEdges(contour); ++j) {
        const TopoDS_Edge& chained = maker.Edge(contour, j);
        const auto found = std::ranges::find_if(edges, [&](const occt::KernelEdge& e) { return e.edge.IsSame(chained); });
        if (found == edges.end()) {
            continue;
        }
        const bool referenced = chained.IsSame(plan.edge->edge);
        for (const TopoDS_Face& face : found->faces) {
            double width = std::max(d1, d2);
            if (referenced) {
                width = !plan.referenceFace || face.IsSame(*plan.referenceFace) ? d1 : d2;
            }
            strips.push_back({chained, face, width, index, referenced});
        }
    }
}

} // namespace

Result<Body> chamferEdges(const Body& body, const ChamferRequest& request) {
    const TopoDS_Shape* shape = occt::BodyAccess::shape(body);
    if (shape == nullptr) {
        return makeError(ErrorCode::FailedPrecondition, "chamfer: the body is empty");
    }
    if (auto valid = validate(request); !valid) {
        return makeError(ErrorCode::InvalidArgument, std::format("chamfer: {}", valid.error().message));
    }
    const std::size_t solids = body.topology().solids;

    return occt::guardKernelCall("chamfer", [&]() -> Result<Body> {
        const std::vector<occt::KernelEdge> edges = occt::kernelEdges(*shape);
        BRepFilletAPI_MakeChamfer maker(*shape);
        const double d1 = occt::toModel(request.distance);
        std::vector<Plan> plans;
        for (std::size_t i = 0; i < request.edges.size(); ++i) {
            const EdgeSignature& signature = request.edges[i];
            const auto matches = occt::matchingEdges(edges, signature);
            if (matches.empty()) {
                return makeError(ErrorCode::NotFound,
                                 std::format("chamfer: {} matches no edge of the body", referenceName(i, signature)));
            }
            if (matches.size() > 1) {
                return makeError(ErrorCode::FailedPrecondition,
                                 std::format("chamfer: {} is ambiguous: {} edges of the body lie on it",
                                             referenceName(i, signature), matches.size()));
            }
            const occt::KernelEdge& edge = *matches.front();
            if (edge.faces.size() != 2) {
                return makeError(ErrorCode::FailedPrecondition,
                                 std::format("chamfer: {} bounds {} face(s); a chamfer needs an edge between two faces",
                                             referenceName(i, signature), edge.faces.size()));
            }
            if (maker.Contour(edge.edge) != 0) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("chamfer: {} is already chamfered by an earlier reference (the edges "
                                             "join smoothly)",
                                             referenceName(i, signature)));
            }
            Plan plan{.edge = &edge, .referenceFace = std::nullopt};
            switch (request.mode) {
            case ChamferMode::EqualDistance:
                maker.Add(d1, edge.edge);
                break;
            case ChamferMode::TwoDistance:
            case ChamferMode::DistanceAngle: {
                auto face = referenceFace(edge, *request.referenceSide, i, signature);
                if (!face) {
                    return std::unexpected(face.error());
                }
                plan.referenceFace = *face;
                if (request.mode == ChamferMode::TwoDistance) {
                    maker.Add(d1, occt::toModel(request.distance2), edge.edge, *face);
                } else {
                    maker.AddDA(d1, request.angle.si(), edge.edge, *face);
                }
                break;
            }
            }
            plans.push_back(plan);
        }

        std::vector<Strip> strips;
        for (std::size_t i = 0; i < plans.size(); ++i) {
            addStrips(maker, edges, plans[i], i, request, strips);
        }
        if (auto fits = checkRoom(strips, request); !fits) {
            return std::unexpected(fits.error());
        }

        // After the room check the kernel should succeed; if it still fails
        // or throws, that is reported, never retried.
        try {
            maker.Build();
        } catch (const Standard_Failure& failure) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("chamfer: the kernel cannot build the chamfer on this geometry; kernel: "
                                         "{}: {}",
                                         failure.ExceptionType(), failure.what()));
        }
        if (!maker.IsDone()) {
            return makeError(ErrorCode::FailedPrecondition,
                             "chamfer: the kernel cannot build the chamfer on this geometry");
        }

        Body result = occt::BodyAccess::makeBody(maker.Shape());
        if (result.isEmpty() || !result.isValid() || result.topology().solids != solids) {
            return makeError(ErrorCode::Internal, "chamfer: the kernel produced an invalid solid");
        }
        const auto properties = result.massProperties();
        if (!properties || !isFinite(properties->volume) || !(properties->volume > Volume{}) ||
            !isFinite(properties->surfaceArea)) {
            return makeError(ErrorCode::Internal,
                             "chamfer: the kernel produced a solid without finite positive volume and area");
        }
        return result;
    });
}

} // namespace bettercad::geometry
