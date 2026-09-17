#include "features/SolidSupport.hpp"

#include <bettercad/core/geometry/Shell.hpp>
#include <bettercad/features/Regeneration.hpp>

namespace bettercad::features {

Result<Length> resolveShellThickness(const ShellDefinition& definition, const Document& document) {
    if (!definition.thicknessParameter) {
        return definition.thickness;
    }
    return detail::drivingValue<Length>(document, *definition.thicknessParameter, "thickness parameter");
}

Result<geometry::Body> regenerateShell(const ShellFeature& feature, const Document& document,
                                       const geometry::Body* target) {
    const ShellDefinition& definition = feature.definition();
    const auto shell = [&](const geometry::Body& body) -> Result<geometry::Body> {
        auto thickness = resolveShellThickness(definition, document);
        if (!thickness) {
            return std::unexpected(thickness.error());
        }
        // Each open face is named by a feature that names its faces, and is
        // a face of this body: nothing else is taken in its place.
        if (auto named = detail::requireNamedFaces(document, definition.openFaces, body, definition.target,
                                                   "open face");
            !named) {
            return std::unexpected(named.error());
        }
        return geometry::shellBody(body, {.openFaces = definition.openFaces,
                                          .thickness = *thickness,
                                          .side = definition.side});
    };
    return detail::applyToTargetBody(feature.name(), "shell", target, shell);
}

} // namespace bettercad::features
