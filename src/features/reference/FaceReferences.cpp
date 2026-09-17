#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/FaceReferences.hpp>
#include <bettercad/features/Feature.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>

namespace bettercad::features {

namespace {

// Named faces lie on one plane when their normals agree within this (radians)
// and their points lie within kLengthTolerance of the first face's plane: the
// face-matching tolerances (geometry::findFaces()).
constexpr double kAngularTolerance = 1e-9;
constexpr double kLengthTolerance = 1e-10; // metres

std::string label(const Document& document, ObjectId id) {
    const auto name = document.nameOf(id);
    return name ? std::format("{} ({})", *name, id) : std::format("{}", id);
}

std::string kindName(std::string_view typeName) {
    std::string text{typeName};
    std::ranges::replace(text, '_', ' ');
    return text;
}

std::string article(std::string_view word) {
    return std::format("{} {}", std::string_view{"aeiou"}.find(word.front()) == std::string_view::npos ? "a" : "an",
                       word);
}

std::string_view roleText(FaceRole role) {
    switch (role) {
    case FaceRole::StartCap:
        return "the start cap";
    case FaceRole::EndCap:
        return "the end cap";
    case FaceRole::Side:
        return "the side";
    }
    return "the face";
}

bool onOnePlane(const geometry::FaceSignature& a, const geometry::FaceSignature& b) {
    const Direction3D& na = a.normal;
    const Direction3D& nb = b.normal;
    // The sine from the cross product: 1 - cos^2 would lose it to rounding.
    const double sine = std::hypot(na.y() * nb.z() - na.z() * nb.y(), na.z() * nb.x() - na.x() * nb.z(),
                                   na.x() * nb.y() - na.y() * nb.x());
    if (na.dot(nb) <= 0.0 || sine > kAngularTolerance) {
        return false;
    }
    const double offset = (b.point.x - a.point.x).si() * na.x() + (b.point.y - a.point.y).si() * na.y() +
                          (b.point.z - a.point.z).si() * na.z();
    return std::abs(offset) <= kLengthTolerance;
}

} // namespace

bool namesFaces(std::string_view typeName) noexcept {
    return typeName == ExtrudeFeature::kTypeName;
}

std::string describe(const Document& document, const FaceName& name) {
    if (name.face.role == FaceRole::Side && name.face.entity) {
        return std::format("the side from {} of {}", *name.face.entity, label(document, name.feature));
    }
    return std::format("{} of {}", roleText(name.face.role), label(document, name.feature));
}

Result<void> checkFaceName(const Document& document, const FaceName& name) {
    const DocumentObject* object = document.findObject(name.feature);
    if (object == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("{} does not exist", name.feature));
    }
    const std::string who = label(document, name.feature);
    if (dynamic_cast<const SolidFeature*>(object) == nullptr) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} is {}, not a feature, and has no faces", who,
                                     article(kindName(object->typeName()))));
    }
    if (!namesFaces(object->typeName())) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} is {}, whose faces cannot be referenced yet (extrudes name their faces)", who,
                                     article(kindName(object->typeName()))));
    }
    if (auto valid = validate(name.face); !valid) {
        return makeError(ErrorCode::InvalidArgument, std::format("{}: {}", who, valid.error().message));
    }
    if (name.face.role != FaceRole::Side) {
        return {};
    }
    const auto* extrude = dynamic_cast<const ExtrudeFeature*>(object);
    const ObjectId sketchId{extrude->definition().profile};
    const auto* sketch = document.findObjectAs<sketch::Sketch>(sketchId);
    if (sketch == nullptr) {
        return {}; // the extrude itself fails
    }
    const sketch::Entity* entity = sketch->findEntity(*name.face.entity);
    if (entity == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("{}: {} is not an entity of its profile {}", who,
                                                          *name.face.entity, label(document, sketchId)));
    }
    if (entity->type() == sketch::EntityType::Point) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{}: {} of its profile is a point, which sweeps no face", who, entity->id));
    }
    if (entity->construction) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{}: {} of its profile is construction geometry, which sweeps no face", who,
                                     entity->id));
    }
    return {};
}

Result<Frame3D> resolveFacePlane(const Document& document, const FaceName& name, const BodyLookup& bodies) {
    if (auto valid = checkFaceName(document, name); !valid) {
        return std::unexpected(valid.error());
    }
    const std::string what = describe(document, name);
    const geometry::Body* body = bodies ? bodies(name.feature) : nullptr;
    if (!bodies) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{} is found in its feature's regenerated body, which is not available here",
                                     what));
    }
    if (body == nullptr || body->isEmpty()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{} cannot be found: {} has no body", what, label(document, name.feature)));
    }
    auto faces = geometry::findNamedFaces(*body, name);
    if (!faces) {
        return makeError(faces.error().code, std::format("{}: {}", what, faces.error().message));
    }
    if (faces->empty()) {
        return makeError(ErrorCode::NotFound,
                         std::format("{} is not a face of its body (the feature's operation left no such face)",
                                     what));
    }
    for (const geometry::FaceInfo& face : *faces) {
        if (!face.signature) {
            return makeError(ErrorCode::InvalidArgument, std::format("{} is {}, not a plane", what,
                                                                     article(toString(face.surface))));
        }
    }
    const geometry::FaceSignature& first = *faces->front().signature;
    for (const geometry::FaceInfo& face : *faces) {
        if (!onOnePlane(first, *face.signature)) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("{} is {} faces that do not lie on one plane", what, faces->size()));
        }
    }
    auto frame = geometry::faceFrame(first);
    if (!frame) {
        return makeError(frame.error().code, std::format("{}: {}", what, frame.error().message));
    }
    return frame;
}

} // namespace bettercad::features
