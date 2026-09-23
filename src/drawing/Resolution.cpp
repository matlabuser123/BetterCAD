#include <bettercad/drawing/Resolution.hpp>

#include <bettercad/drawing/Annotations.hpp>
#include <bettercad/drawing/Dimensions.hpp>

#include <utility>

namespace bettercad::drawing {
namespace {

/// Which state an error code means.
///
/// The split is between "the reference is fine and its target is not here"
/// and "the reference itself does not make sense", because only the first is
/// recoverable and only the first should be kept waiting for its target.
[[nodiscard]] ResolutionState stateFor(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::NotFound:
    case ErrorCode::FailedPrecondition:
        // The target is not there NOW. A suppressed component, a deleted
        // feature, a face this configuration does not produce, an assembly
        // that did not solve. The intent is intact.
        return ResolutionState::Unresolved;
    case ErrorCode::InvalidArgument:
        // The reference cannot be right whatever the model does: a malformed
        // selector, an object of a kind that cannot be pointed at, an angular
        // dimension between parallel faces.
        return ResolutionState::Invalid;
    default:
        // Internal and anything else: the resolver broke rather than the
        // reference. Reported as Invalid rather than swallowed, because a
        // drawing must not call it Resolved.
        return ResolutionState::Invalid;
    }
}

template <typename T>
[[nodiscard]] Resolution from(Result<T>&& result) {
    if (result) {
        return Resolution{};
    }
    return Resolution{stateFor(result.error().code), std::move(result.error().message)};
}

} // namespace

std::string_view toString(ResolutionState state) noexcept {
    switch (state) {
    case ResolutionState::Resolved:
        return "resolved";
    case ResolutionState::Unresolved:
        return "unresolved";
    case ResolutionState::Invalid:
        return "invalid";
    }
    return "unknown";
}

Resolution viewResolution(const Document& document, ViewId id, const BodyLookup& bodies,
                          const TransformLookup& transforms) {
    return from(projectedGeometry(document, id, bodies, transforms));
}

Resolution dimensionResolution(const Document& document, DimensionId id, const BodyLookup& bodies,
                               const TransformLookup& transforms) {
    return from(measure(document, id, bodies, transforms));
}

Resolution annotationResolution(const Document& document, AnnotationId id, const BodyLookup& bodies,
                                const TransformLookup& transforms) {
    return from(draw(document, id, bodies, transforms));
}

} // namespace bettercad::drawing
