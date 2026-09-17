#include "core/geometry/occt/OcctBlend.hpp"
#include "core/geometry/occt/OcctFaceNames.hpp"

#include "core/geometry/occt/OcctBody.hpp"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepClass_FaceClassifier.hxx>
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
#include <format>
#include <optional>

namespace bettercad::geometry::occt {

namespace {

// Sample counts for finding how far a face reaches from an edge.
constexpr int kBoundarySamples = 16; // intervals per boundary edge
constexpr int kInteriorSamples = 8;  // intervals per surface parameter

bool touches(const TopoDS_Edge& a, const TopoDS_Edge& b) {
    TopoDS_Vertex common;
    return TopExp::CommonVertex(a, b, common);
}

std::string stripName(const BlendStrip& strip, const std::vector<EdgeSignature>& references) {
    const std::string name = referenceName(strip.reference, references[strip.reference]);
    return strip.referencedEdge ? name : std::format("an edge that joins {} smoothly", name);
}

/// How far the strip's face reaches from its edge, as far as it matters: the
/// largest sampled distance, or the first one of at least @p needed.
std::optional<double> reach(const BlendStrip& strip, double needed) {
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

} // namespace

std::string referenceName(std::size_t index, const EdgeSignature& signature) {
    return std::format("edge reference {} ({})", index + 1, describe(signature));
}

Result<const KernelEdge*> resolveBlendEdge(const BlendNames& names, const std::vector<KernelEdge>& edges,
                                           const std::vector<EdgeSignature>& references, std::size_t index,
                                           const BRepFilletAPI_LocalOperation& maker) {
    const std::string name = referenceName(index, references[index]);
    const auto matches = matchingEdges(edges, references[index]);
    if (matches.empty()) {
        return makeError(ErrorCode::NotFound, std::format("{}: {} matches no edge of the body", names.noun, name));
    }
    if (matches.size() > 1) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{}: {} is ambiguous: {} edges of the body lie on it", names.noun, name,
                                     matches.size()));
    }
    const KernelEdge& edge = *matches.front();
    if (edge.faces.size() != 2) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{}: {} bounds {} face(s); a {} needs an edge between two faces", names.noun,
                                     name, edge.faces.size(), names.noun));
    }
    if (maker.Contour(edge.edge) != 0) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{}: {} is already {} by an earlier reference (the edges join smoothly)",
                                     names.noun, name, names.participle));
    }
    return &edge;
}

Result<void> addChainStrips(const BRepFilletAPI_LocalOperation& maker, const std::vector<KernelEdge>& edges,
                            const KernelEdge& referenced, std::size_t index, const StripWidth& width,
                            std::vector<BlendStrip>& strips) {
    const int contour = maker.Contour(referenced.edge);
    for (int j = 1; j <= maker.NbEdges(contour); ++j) {
        const TopoDS_Edge& chained = maker.Edge(contour, j);
        const auto found = std::ranges::find_if(edges, [&](const KernelEdge& e) { return e.edge.IsSame(chained); });
        if (found == edges.end()) {
            continue;
        }
        const bool isReferenced = chained.IsSame(referenced.edge);
        for (const TopoDS_Face& face : found->faces) {
            auto w = width(*found, face, isReferenced);
            if (!w) {
                return std::unexpected(w.error());
            }
            strips.push_back({chained, face, *w, index, isReferenced});
        }
    }
    return {};
}

Result<void> checkRoom(const BlendNames& names, const std::vector<BlendStrip>& strips,
                       const std::vector<EdgeSignature>& references) {
    const auto unmeasurable = [&](const BlendStrip& strip) {
        return makeError(ErrorCode::Internal, std::format("{}: cannot measure the room next to {}", names.noun,
                                                          stripName(strip, references)));
    };
    const auto tooWide = [&](const BlendStrip& strip, double room) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{}: {} does not fit: its {} needs {:.6g} mm on a face next to the edge, which "
                                     "leaves only {:.6g} mm (a {} must leave at least {:g} mm)",
                                     names.noun, stripName(strip, references), names.noun, strip.width, room,
                                     names.noun, kMinimumRemainderMm));
    };
    for (const BlendStrip& strip : strips) {
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
            const BlendStrip& a = strips[i];
            const BlendStrip& b = strips[j];
            if (!a.face.IsSame(b.face) || a.edge.IsSame(b.edge) || touches(a.edge, b.edge)) {
                continue;
            }
            const auto gap = distance(a.edge, b.edge);
            if (!gap) {
                return unmeasurable(a);
            }
            if (*gap < a.width + b.width + kMinimumRemainderMm) {
                return makeError(ErrorCode::FailedPrecondition,
                                 std::format("{}: {} and {} do not fit together: their {}s need {:.6g} mm and "
                                             "{:.6g} mm on a face they share, where the edges are only {:.6g} mm "
                                             "apart (a {} must leave at least {:g} mm)",
                                             names.noun, stripName(a, references), stripName(b, references),
                                             names.noun, a.width, b.width, *gap, names.noun, kMinimumRemainderMm));
            }
        }
    }
    return {};
}

Result<Body> buildBlend(const BlendNames& names, BRepFilletAPI_LocalOperation& maker, std::size_t solids,
                        const Body& input, const std::vector<GeneratedName>& generated) {
    // After the room check the kernel should succeed; if it still fails or
    // throws, that is reported, never retried.
    try {
        maker.Build();
    } catch (const Standard_Failure& failure) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{}: the kernel cannot build the {} on this geometry; kernel: {}: {}",
                                     names.noun, names.noun, failure.ExceptionType(), failure.what()));
    }
    if (!maker.IsDone()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{}: the kernel cannot build the {} on this geometry", names.noun, names.noun));
    }
    const TopoDS_Shape shape = maker.Shape();
    std::vector<NamedFace> faceNames = carriedNames(maker, shape, {&input});
    ShapeMap faces;
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    for (const GeneratedName& entry : generated) {
        for (const TopoDS_Shape& face : maker.Generated(entry.edge)) {
            if (face.ShapeType() == TopAbs_FACE && faces.Contains(face)) {
                faceNames.push_back({TopoDS::Face(face), entry.name});
            }
        }
    }
    Body result = BodyAccess::makeBody(shape, canonicalNames(shape, std::move(faceNames)));
    if (result.isEmpty() || !result.isValid() || result.topology().solids != solids) {
        return makeError(ErrorCode::Internal, std::format("{}: the kernel produced an invalid solid", names.noun));
    }
    const auto properties = result.massProperties();
    if (!properties || !isFinite(properties->volume) || !(properties->volume > Volume{}) ||
        !isFinite(properties->surfaceArea)) {
        return makeError(ErrorCode::Internal,
                         std::format("{}: the kernel produced a solid without finite positive volume and area",
                                     names.noun));
    }
    return result;
}

} // namespace bettercad::geometry::occt
