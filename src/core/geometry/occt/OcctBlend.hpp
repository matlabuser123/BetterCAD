#pragma once

// Shared machinery of the edge blends built on the kernel's fillet builder
// (chamfer and fillet): resolving edge references, checking that the blend
// fits before the kernel runs, and building and validating the result.

#include "core/geometry/occt/OcctTopology.hpp"

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Edges.hpp>

#include <BRepFilletAPI_LocalOperation.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::geometry::occt {

/// How messages name the operation: {"chamfer", "chamfered"}.
struct BlendNames {
    std::string_view noun;
    std::string_view participle;
};

/// Room every blend must leave on the faces it cuts, in model units (mm).
/// OCCT 8.0.1 cannot be trusted with blends that do not fit: in this
/// toolchain it can run for seconds and then crash the process instead of
/// failing (docs/verification/P11-FEAT-002 and P11-FEAT-003). Near the limit
/// it built blends leaving 1e-4 mm and failed or crashed below that, so
/// 1e-3 mm is required.
inline constexpr double kMinimumRemainderMm = 1e-3;

/// "edge reference 2 (line through ...)".
[[nodiscard]] std::string referenceName(std::size_t index, const EdgeSignature& signature);

/// The one edge that reference @p index matches. Fails with NotFound for no
/// match, FailedPrecondition for several or for an edge not between two
/// faces, and InvalidArgument for an edge that an earlier reference already
/// blends (the two join smoothly).
[[nodiscard]] Result<const KernelEdge*> resolveBlendEdge(const BlendNames& names,
                                                         const std::vector<KernelEdge>& edges,
                                                         const std::vector<EdgeSignature>& references,
                                                         std::size_t index,
                                                         const BRepFilletAPI_LocalOperation& maker);

/// The part of a face a blend removes or adds: points within `width` of
/// `edge` on `face`.
struct BlendStrip {
    TopoDS_Edge edge;
    TopoDS_Face face;
    double width = 0.0; // model units
    std::size_t reference = 0;
    bool referencedEdge = true; // false for an edge the kernel added along a smooth chain
};

/// Width of the blend of @p edge on @p face; @p referenced tells a selected
/// edge from one the kernel added along a smooth chain.
using StripWidth = std::function<Result<double>(const KernelEdge& edge, const TopoDS_Face& face, bool referenced)>;

/// Adds the strips of every edge the kernel will blend for reference
/// @p index: the referenced edge and the edges joining it smoothly.
[[nodiscard]] Result<void> addChainStrips(const BRepFilletAPI_LocalOperation& maker,
                                          const std::vector<KernelEdge>& edges, const KernelEdge& referenced,
                                          std::size_t index, const StripWidth& width,
                                          std::vector<BlendStrip>& strips);

/// Refuses blends that do not fit, before the kernel sees them. Each strip
/// must stay clear of its face's other edges (those not meeting its edge),
/// must not run across the face, and must not meet another strip on the
/// same face; each by at least kMinimumRemainderMm. Distances are straight
/// lines, never longer than the distances along a curved face, so the check
/// errs towards refusing.
[[nodiscard]] Result<void> checkRoom(const BlendNames& names, const std::vector<BlendStrip>& strips,
                                     const std::vector<EdgeSignature>& references);

/// Runs the kernel and validates its result: one valid solid for each of
/// the @p solids input solids, with finite, positive volume and finite area.
/// Kernel failures and exceptions become FailedPrecondition, invalid
/// results Internal.
[[nodiscard]] Result<Body> buildBlend(const BlendNames& names, BRepFilletAPI_LocalOperation& maker,
                                      std::size_t solids);

} // namespace bettercad::geometry::occt
