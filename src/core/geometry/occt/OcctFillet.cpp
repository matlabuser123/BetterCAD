#include "core/geometry/occt/OcctBlend.hpp"
#include "core/geometry/occt/OcctBody.hpp"
#include "core/geometry/occt/OcctGuard.hpp"
#include "core/geometry/occt/OcctTopology.hpp"

#include <bettercad/core/geometry/Fillet.hpp>

#include <BRepFilletAPI_MakeFillet.hxx>

#include <cmath>
#include <format>
#include <numbers>

namespace bettercad::geometry {

namespace {

constexpr occt::BlendNames kNames{"fillet", "filleted"};

// Faces whose outward normals are closer than this (radians) join smoothly:
// there is no corner to round.
constexpr double kSmoothAngle = 1e-6;
// Faces whose normals are this close to opposite fold back on each other: a
// fillet between them would be unboundedly wide.
constexpr double kKnifeAngle = std::numbers::pi - 1e-6;

} // namespace

Result<Body> filletEdges(const Body& body, const FilletRequest& request) {
    const TopoDS_Shape* shape = occt::BodyAccess::shape(body);
    if (shape == nullptr) {
        return makeError(ErrorCode::FailedPrecondition, "fillet: the body is empty");
    }
    if (auto valid = validate(request); !valid) {
        return makeError(ErrorCode::InvalidArgument, std::format("fillet: {}", valid.error().message));
    }
    const std::size_t solids = body.topology().solids;

    return occt::guardKernelCall("fillet", [&]() -> Result<Body> {
        const std::vector<occt::KernelEdge> edges = occt::kernelEdges(*shape);
        BRepFilletAPI_MakeFillet maker(*shape);
        const double r = occt::toModel(request.radius);
        std::vector<occt::BlendStrip> strips;
        for (std::size_t i = 0; i < request.edges.size(); ++i) {
            auto resolved = occt::resolveBlendEdge(kNames, edges, request.edges, i, maker);
            if (!resolved) {
                return std::unexpected(resolved.error());
            }
            const occt::KernelEdge& edge = **resolved;
            const std::string name = occt::referenceName(i, request.edges[i]);
            auto angle = occt::largestFaceAngle(edge);
            if (!angle) {
                return makeError(ErrorCode::Internal, std::format("fillet: {}: {}", name, angle.error().message));
            }
            if (*angle < kSmoothAngle) {
                return makeError(ErrorCode::FailedPrecondition,
                                 std::format("fillet: {} joins its two faces smoothly; there is no corner to round",
                                             name));
            }
            maker.Add(r, edge.edge);

            // The fillet touches each face at r tan(angle / 2) from the edge,
            // for every edge of the smooth chain the kernel rounds.
            const auto width = [&](const occt::KernelEdge& chained, const TopoDS_Face&, bool) -> Result<double> {
                auto chainedAngle = occt::largestFaceAngle(chained);
                if (!chainedAngle) {
                    return makeError(ErrorCode::Internal,
                                     std::format("fillet: {}: {}", name, chainedAngle.error().message));
                }
                if (*chainedAngle > kKnifeAngle) {
                    return makeError(ErrorCode::FailedPrecondition,
                                     std::format("fillet: {}: the faces fold back onto each other; they cannot be "
                                                 "rounded",
                                                 name));
                }
                return r * std::tan(*chainedAngle / 2.0);
            };
            if (auto added = occt::addChainStrips(maker, edges, edge, i, width, strips); !added) {
                return std::unexpected(added.error());
            }
        }
        if (auto fits = occt::checkRoom(kNames, strips, request.edges); !fits) {
            return std::unexpected(fits.error());
        }
        return occt::buildBlend(kNames, maker, solids, body);
    });
}

} // namespace bettercad::geometry
