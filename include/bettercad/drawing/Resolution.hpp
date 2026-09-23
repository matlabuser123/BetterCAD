#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/drawing/Export.hpp>
#include <bettercad/drawing/Views.hpp>

#include <cstdint>
#include <string>
#include <string_view>

// What state a drawing's reference to the model is in (P14-STREF-001, on
// ADR-004, ADR-012 and ADR-022).
//
// THIS RESOLVES NOTHING OF ITS OWN. Every answer here comes from the one
// resolver the drawing already uses -- measure() for a dimension, draw() for
// an annotation, projectedGeometry() for a view -- read through the error
// code it already returns. A second resolution path would be a second answer
// to "where is this", and the two could differ.
//
// THREE STATES, AND THE DIFFERENCE BETWEEN THE LAST TWO MATTERS.
//
//     Resolved     the target exists and the reference found it
//     Unresolved   the reference is well formed and its target is not there
//                  NOW -- suppressed, deleted, or in a configuration that
//                  does not have it. The INTENT is intact, so the reference
//                  is kept and resolves again if the target comes back
//     Invalid      the reference itself is incoherent: a malformed selector,
//                  an object of a kind that cannot be pointed at, a chain
//                  that does not reach a base view
//
// An Unresolved reference is NOT a deleted one. Nothing here removes a
// reference or rewrites it to point somewhere else; a drawing that could
// quietly repoint itself at whatever geometry happened to survive would be a
// drawing nobody could trust.
namespace bettercad::drawing {

/// The state of one reference from a drawing object to the model.
enum class ResolutionState : std::uint8_t {
    Resolved,
    Unresolved,
    Invalid,
};

/// "resolved", "unresolved", "invalid".
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::string_view toString(ResolutionState state) noexcept;

/// What a reference resolved to, and why not when it did not.
struct Resolution {
    ResolutionState state = ResolutionState::Resolved;
    /// Empty when resolved; otherwise the diagnostic the resolver gave,
    /// unchanged. A caller that shows this to an engineer shows the same
    /// words the drawing itself would have failed with.
    std::string diagnostic{};

    [[nodiscard]] bool resolved() const noexcept { return state == ResolutionState::Resolved; }

    friend bool operator==(const Resolution&, const Resolution&) = default;
};

/// How @p id's references to the model stand now.
///
/// A view resolves when it can be projected; a dimension when it can be
/// measured; an annotation when it can be drawn. That is deliberate: the
/// state a drawing reports is the state its own output is in, so a reference
/// cannot be called Resolved by one route and fail by another.
///
/// The bodies and transforms are the same lookups the drawing takes, so a
/// caller asking during a regeneration sees what that regeneration sees.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Resolution viewResolution(
    const Document& document, ViewId id, const BodyLookup& bodies,
    const TransformLookup& transforms = {});
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Resolution dimensionResolution(
    const Document& document, DimensionId id, const BodyLookup& bodies,
    const TransformLookup& transforms = {});
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Resolution annotationResolution(
    const Document& document, AnnotationId id, const BodyLookup& bodies,
    const TransformLookup& transforms = {});

} // namespace bettercad::drawing
