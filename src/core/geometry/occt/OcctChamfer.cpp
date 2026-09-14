#include "core/geometry/occt/OcctBlend.hpp"
#include "core/geometry/occt/OcctBody.hpp"
#include "core/geometry/occt/OcctGuard.hpp"
#include "core/geometry/occt/OcctTopology.hpp"

#include <bettercad/core/geometry/Chamfer.hpp>

#include <BRepFilletAPI_MakeChamfer.hxx>

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>

namespace bettercad::geometry {

namespace {

constexpr occt::BlendNames kNames{"chamfer", "chamfered"};

// Faces are told apart by the reference side when the cosines of their
// normals with it differ by more than this.
constexpr double kSideTolerance = 1e-6;

/// Of the edge's two faces, the one whose outward normal is closer to @p side.
Result<TopoDS_Face> referenceFace(const occt::KernelEdge& edge, const Direction3D& side, std::size_t index,
                                  const EdgeSignature& signature) {
    auto first = occt::outwardNormalAt(edge.faces[0], edge.edge);
    auto second = occt::outwardNormalAt(edge.faces[1], edge.edge);
    if (!first || !second) {
        return makeError(ErrorCode::Internal, std::format("chamfer: {}: {}", occt::referenceName(index, signature),
                                                          (!first ? first : second).error().message));
    }
    const double a = first->dot(side);
    const double b = second->dot(side);
    if (std::abs(a - b) <= kSideTolerance) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("chamfer: the reference side does not tell the two faces of {} apart",
                                     occt::referenceName(index, signature)));
    }
    return a > b ? edge.faces[0] : edge.faces[1];
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
        // Width on the other face: the second distance, or the leg opposite
        // the angle; the same for an equal-distance chamfer.
        const double d2 = request.mode == ChamferMode::TwoDistance     ? occt::toModel(request.distance2)
                          : request.mode == ChamferMode::DistanceAngle ? d1 * std::tan(request.angle.si())
                                                                       : d1;
        std::vector<occt::BlendStrip> strips;
        for (std::size_t i = 0; i < request.edges.size(); ++i) {
            auto resolved = occt::resolveBlendEdge(kNames, edges, request.edges, i, maker);
            if (!resolved) {
                return std::unexpected(resolved.error());
            }
            const occt::KernelEdge& edge = **resolved;
            std::optional<TopoDS_Face> reference;
            switch (request.mode) {
            case ChamferMode::EqualDistance:
                maker.Add(d1, edge.edge);
                break;
            case ChamferMode::TwoDistance:
            case ChamferMode::DistanceAngle: {
                auto face = referenceFace(edge, *request.referenceSide, i, request.edges[i]);
                if (!face) {
                    return std::unexpected(face.error());
                }
                reference = *face;
                if (request.mode == ChamferMode::TwoDistance) {
                    maker.Add(d1, d2, edge.edge, *face);
                } else {
                    maker.AddDA(d1, request.angle.si(), edge.edge, *face);
                }
                break;
            }
            }
            // The referenced edge has its exact widths; edges the kernel adds
            // along a smooth chain get the larger one on both faces.
            const auto width = [&](const occt::KernelEdge&, const TopoDS_Face& face, bool referenced) -> Result<double> {
                if (!referenced) {
                    return std::max(d1, d2);
                }
                return !reference || face.IsSame(*reference) ? d1 : d2;
            };
            if (auto added = occt::addChainStrips(maker, edges, edge, i, width, strips); !added) {
                return std::unexpected(added.error());
            }
        }
        if (auto fits = occt::checkRoom(kNames, strips, request.edges); !fits) {
            return std::unexpected(fits.error());
        }
        return occt::buildBlend(kNames, maker, solids);
    });
}

} // namespace bettercad::geometry
