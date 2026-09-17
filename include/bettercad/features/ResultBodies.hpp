#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/features/Export.hpp>

#include <string>
#include <vector>

namespace bettercad::features {

/// Features whose bodies are results of the model, in ascending ID order:
/// every feature that produces a body, except those whose body another
/// feature consumes (SolidFeature::consumedFeatures(): a Join/Cut/Intersect
/// target, a pattern's source, a combine's target and tools).
///
/// Example: Pad, then Pocket cutting Pad, then a new-body Slot gives
/// {Pocket, Slot}.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::vector<ObjectId> resultFeatures(const Document& document);

struct ResultBody {
    ObjectId feature{};
    std::string name{};
    geometry::Body body{};
};

/// Regenerates a copy of @p document (which is not modified) and returns the
/// bodies of its result features. Fails with FailedPrecondition, naming the
/// failed items, if regeneration does not fully succeed.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<std::vector<ResultBody>> regenerateResultBodies(
    const Document& document);

} // namespace bettercad::features
