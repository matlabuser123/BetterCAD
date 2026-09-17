#include "features/SolidSupport.hpp"

#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Shell.hpp>
#include <bettercad/features/FaceReferences.hpp>
#include <bettercad/features/Regeneration.hpp>

#include <format>

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
        for (std::size_t i = 0; i < definition.openFaces.size(); ++i) {
            const FaceName& name = definition.openFaces[i];
            if (auto valid = checkFaceName(document, name); !valid) {
                return makeError(valid.error().code,
                                 std::format("open face {}: {}", i + 1, valid.error().message));
            }
            auto faces = geometry::findNamedFaces(body, name);
            if (!faces) {
                return makeError(faces.error().code, std::format("open face {}: {}", i + 1, faces.error().message));
            }
            if (faces->empty()) {
                const ObjectId targetId{definition.target};
                const auto targetName = document.nameOf(targetId);
                return makeError(ErrorCode::NotFound,
                                 std::format("open face {}, {}, is not a face of the body of {}", i + 1,
                                             describe(document, name),
                                             targetName ? std::format("{} ({})", *targetName, targetId)
                                                        : std::format("{}", targetId)));
            }
        }
        return geometry::shellBody(body, {.openFaces = definition.openFaces,
                                          .thickness = *thickness,
                                          .side = definition.side});
    };
    return detail::applyToTargetBody(feature.name(), "shell", target, shell);
}

} // namespace bettercad::features
