#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/core/geometry/Sweeps.hpp>
#include <bettercad/core/parameters/Parameter.hpp>
#include <bettercad/features/Profiles.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <format>
#include <functional>
#include <string_view>
#include <vector>

// Steps shared by the profile-based solid features (extrude, revolve, ...)
// and the edge operations on another feature's body (chamfer, fillet).
namespace bettercad::features::detail {

/// Applies an operation to the target feature's body. Fails with
/// FailedPrecondition "<featureName>: a <operation> needs the body of its
/// <role> feature" if there is no non-empty target body; errors of @p apply
/// come back prefixed with @p featureName. @p apply never modifies the
/// target body (bodies are immutable), so a failure leaves it intact.
[[nodiscard]] Result<geometry::Body> applyToTargetBody(
    std::string_view featureName, std::string_view operation, const geometry::Body* target,
    const std::function<Result<geometry::Body>(const geometry::Body&)>& apply, std::string_view role = "target");

/// The feature's profile sketch; NotFound if @p profile is not a sketch of
/// the document. Errors are prefixed with @p featureName.
[[nodiscard]] Result<const sketch::Sketch*> requireProfileSketch(const Document& document, SketchId profile,
                                                                 std::string_view featureName);

/// Closed regions of the profile sketch; errors are prefixed with @p featureName.
[[nodiscard]] Result<std::vector<geometry::PlanarRegion>> profileRegions(const sketch::Sketch& sketch,
                                                                         std::string_view featureName);

/// profileRegions() with the entity of each segment; errors are prefixed
/// with @p featureName.
[[nodiscard]] Result<std::vector<LabelledRegion>> labelledProfileRegions(const sketch::Sketch& sketch,
                                                                         std::string_view featureName);

/// The path edge a sweep's path segment comes from, or none.
using PathEdgeOf = std::function<std::optional<EntityId>(std::size_t pathSegment)>;

/// Names for the faces @p feature sweeps from @p region (which must outlive
/// the namer): the region where the sweep starts is @p first, where it ends
/// @p last, and each segment's side is named by its entity (and, with
/// @p along, by its path edge).
[[nodiscard]] geometry::SweptFaceNamer sweptFaceNamer(ObjectId feature, const LabelledRegion& region, FaceRole first,
                                                      FaceRole last, PathEdgeOf along = {});

/// The copy step @p copy appended to every face name (patterns and mirrors).
[[nodiscard]] geometry::FaceRenamer appendCopy(const FaceCopy& copy);

/// One solid per region, made by @p build, united into one body (disjoint
/// regions give a body with several solids). Face names on the solids are
/// carried into the body.
[[nodiscard]] Result<geometry::Body> uniteRegionSolids(
    const std::vector<geometry::PlanarRegion>& regions,
    const std::function<Result<geometry::Body>(const geometry::PlanarRegion&)>& build);
[[nodiscard]] Result<geometry::Body> uniteRegionSolids(
    const std::vector<LabelledRegion>& regions,
    const std::function<Result<geometry::Body>(const LabelledRegion&)>& build);

/// Value of a driving parameter as quantity Q: NotFound if it does not
/// exist, DimensionMismatch if it is not a Q. @p role names it in messages,
/// e.g. "depth parameter".
template <QuantityType Q>
[[nodiscard]] Result<Q> drivingValue(const Document& document, ParameterId parameter, std::string_view role) {
    const Parameter* found = document.parameters().find(parameter);
    if (found == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("{} {} does not exist", role, parameter));
    }
    return found->as<Q>();
}

} // namespace bettercad::features::detail
