#include "features/SolidSupport.hpp"

#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/FaceReferences.hpp>
#include <bettercad/features/Profiles.hpp>

#include <optional>
#include <utility>

namespace bettercad::features::detail {

Result<geometry::Body> applyToTargetBody(std::string_view featureName, std::string_view operation,
                                         const geometry::Body* target,
                                         const std::function<Result<geometry::Body>(const geometry::Body&)>& apply,
                                         std::string_view role) {
    if (target == nullptr || target->isEmpty()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{}: a {} needs the body of its {} feature", featureName, operation, role));
    }
    auto body = apply(*target);
    if (!body) {
        return makeError(body.error().code, std::format("{}: {}", featureName, body.error().message));
    }
    return body;
}

Result<const sketch::Sketch*> requireProfileSketch(const Document& document, SketchId profile,
                                                   std::string_view featureName) {
    const auto* sketch = document.findObjectAs<sketch::Sketch>(ObjectId{profile});
    if (sketch == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("{}: profile {} is not a sketch in this document", featureName, profile));
    }
    return sketch;
}

Result<std::vector<geometry::PlanarRegion>> profileRegions(const sketch::Sketch& sketch,
                                                           std::string_view featureName) {
    auto regions = extractRegions(sketch);
    if (!regions) {
        return makeError(regions.error().code, std::format("{}: {}", featureName, regions.error().message));
    }
    return regions;
}

Result<std::vector<LabelledRegion>> labelledProfileRegions(const sketch::Sketch& sketch,
                                                           std::string_view featureName) {
    auto regions = extractLabelledRegions(sketch);
    if (!regions) {
        return makeError(regions.error().code, std::format("{}: {}", featureName, regions.error().message));
    }
    return regions;
}

geometry::SweptFaceNamer sweptFaceNamer(ObjectId feature, const LabelledRegion& region, FaceRole first,
                                        FaceRole last, PathEdgeOf along) {
    return [feature, &region, first, last, along = std::move(along)](
               const geometry::SweptFace& face) -> std::optional<FaceName> {
        switch (face.kind) {
        case geometry::SweptFace::Kind::First:
            return FaceName{feature, FaceSelector{.role = first}};
        case geometry::SweptFace::Kind::Last:
            return FaceName{feature, FaceSelector{.role = last}};
        case geometry::SweptFace::Kind::Side:
            break;
        }
        const std::vector<EntityId>* entities =
            face.loop == 0 ? &region.outer
                           : (face.loop <= region.holes.size() ? &region.holes[face.loop - 1] : nullptr);
        if (entities == nullptr || face.segment >= entities->size()) {
            return std::nullopt;
        }
        FaceSelector selector{.role = FaceRole::Side, .entity = (*entities)[face.segment]};
        if (along) {
            const auto edge = along(face.pathSegment);
            if (!edge) {
                return std::nullopt;
            }
            selector.along = *edge;
        }
        return FaceName{feature, std::move(selector)};
    };
}

Result<void> requireNamedFaces(const Document& document, std::span<const FaceName> names,
                               const geometry::Body& body, FeatureId target, std::string_view noun) {
    for (std::size_t i = 0; i < names.size(); ++i) {
        const FaceName& name = names[i];
        if (auto valid = checkFaceName(document, name); !valid) {
            return makeError(valid.error().code, std::format("{} {}: {}", noun, i + 1, valid.error().message));
        }
        auto faces = geometry::findNamedFaces(body, name);
        if (!faces) {
            return makeError(faces.error().code, std::format("{} {}: {}", noun, i + 1, faces.error().message));
        }
        if (faces->empty()) {
            const ObjectId targetId{target};
            const auto targetName = document.nameOf(targetId);
            return makeError(ErrorCode::NotFound,
                             std::format("{} {}, {}, is not a face of the body of {}", noun, i + 1,
                                         describe(document, name),
                                         targetName ? std::format("{} ({})", *targetName, targetId)
                                                    : std::format("{}", targetId)));
        }
    }
    return {};
}

geometry::FaceRenamer appendCopy(const FaceCopy& copy) {
    return appendCopies({copy});
}

geometry::FaceRenamer appendCopies(std::vector<FaceCopy> copies) {
    return [copies = std::move(copies)](const FaceName& name) -> std::optional<FaceName> {
        FaceName copied = name;
        copied.face.copies.insert(copied.face.copies.end(), copies.begin(), copies.end());
        return copied;
    };
}

namespace {

template <typename Region>
Result<geometry::Body> unite(const std::vector<Region>& regions,
                             const std::function<Result<geometry::Body>(const Region&)>& build) {
    geometry::Body solid;
    for (const Region& region : regions) {
        auto piece = build(region);
        if (!piece) {
            return std::unexpected(piece.error());
        }
        if (solid.isEmpty()) {
            solid = *piece;
            continue;
        }
        auto united = geometry::booleanUnion(solid, *piece);
        if (!united) {
            return std::unexpected(united.error());
        }
        solid = *united;
    }
    return solid;
}

} // namespace

Result<geometry::Body> uniteRegionSolids(
    const std::vector<geometry::PlanarRegion>& regions,
    const std::function<Result<geometry::Body>(const geometry::PlanarRegion&)>& build) {
    return unite(regions, build);
}

Result<geometry::Body> uniteRegionSolids(const std::vector<LabelledRegion>& regions,
                                         const std::function<Result<geometry::Body>(const LabelledRegion&)>& build) {
    return unite(regions, build);
}

} // namespace bettercad::features::detail
