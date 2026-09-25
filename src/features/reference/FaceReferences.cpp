#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/CircularPatternFeature.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/FaceReferences.hpp>
#include <bettercad/features/Feature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/LoftFeature.hpp>
#include <bettercad/features/MirrorFeature.hpp>
#include <bettercad/features/RevolveFeature.hpp>
#include <bettercad/features/RibFeature.hpp>
#include <bettercad/features/SweepFeature.hpp>
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

/// "the end cap", "the side from entity:4 along entity:9", "the face of chamfer edge 2".
std::string faceText(const FaceSelector& face) {
    switch (face.role) {
    case FaceRole::StartCap:
        return "the start cap";
    case FaceRole::EndCap:
        return "the end cap";
    case FaceRole::Side:
        if (!face.entity) {
            return "a side";
        }
        if (!face.along) {
            return std::format("the side from {}", *face.entity);
        }
        return face.alongSketch
                   ? std::format("the side from {} along {} of {}", *face.entity, *face.along, *face.alongSketch)
                   : std::format("the side from {} along {}", *face.entity, *face.along);
    case FaceRole::HoleBottom:
        return "the bottom";
    case FaceRole::CounterboreFloor:
        return "the counterbore floor";
    case FaceRole::Chamfer:
        return face.edge ? std::format("the face of chamfer edge {}", face.edge->value()) : "a chamfer face";
    case FaceRole::SpotfaceFloor:
        return "the spotface floor";
    }
    return "a face";
}

/// "a hole bottom", for messages about roles a feature does not have.
std::string_view roleName(FaceRole role) {
    switch (role) {
    case FaceRole::StartCap:
        return "start cap";
    case FaceRole::EndCap:
        return "end cap";
    case FaceRole::Side:
        return "side face";
    case FaceRole::HoleBottom:
        return "hole bottom";
    case FaceRole::CounterboreFloor:
        return "counterbore floor";
    case FaceRole::Chamfer:
        return "chamfer face";
    case FaceRole::SpotfaceFloor:
        return "spotface floor";
    }
    return "face";
}

bool copiesFaces(std::string_view typeName) noexcept {
    return typeName == LinearPatternFeature::kTypeName || typeName == CircularPatternFeature::kTypeName ||
           typeName == MirrorFeature::kTypeName;
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

/// A profile curve of @p sketchId: NotFound if the sketch has no such
/// entity, InvalidArgument for a point or construction geometry. A missing
/// sketch is left to the feature's own regeneration.
Result<void> checkProfileEntity(const Document& document, const std::string& who, SketchId sketchId,
                                EntityId entityId) {
    const ObjectId id{sketchId};
    const auto* sketch = document.findObjectAs<sketch::Sketch>(id);
    if (sketch == nullptr) {
        return {};
    }
    const sketch::Entity* entity = sketch->findEntity(entityId);
    if (entity == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("{}: {} is not an entity of its profile {}", who, entityId,
                                                          label(document, id)));
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

/// The roles the feature @p object generates, checked against @p face.
Result<void> checkRole(const Document& document, const DocumentObject& object, const FaceSelector& face) {
    const std::string who = label(document, object.id());
    const auto noSuchRole = [&] {
        return makeError(ErrorCode::InvalidArgument, std::format("{} is {}, which has no {}", who,
                                                                 article(kindName(object.typeName())),
                                                                 roleName(face.role)));
    };
    const auto noAlong = [&]() -> Result<void> {
        if (face.along) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} is {}, whose sides are not named by a path edge", who,
                                         article(kindName(object.typeName()))));
        }
        return {};
    };
    const bool cap = face.role == FaceRole::StartCap || face.role == FaceRole::EndCap;
    if (const auto* extrude = dynamic_cast<const ExtrudeFeature*>(&object)) {
        if (cap) {
            return {};
        }
        if (face.role != FaceRole::Side) {
            return noSuchRole();
        }
        if (auto valid = noAlong(); !valid) {
            return valid;
        }
        return checkProfileEntity(document, who, extrude->definition().profile, *face.entity);
    }
    if (const auto* revolve = dynamic_cast<const RevolveFeature*>(&object)) {
        if (cap) {
            return {};
        }
        if (face.role != FaceRole::Side) {
            return noSuchRole();
        }
        if (auto valid = noAlong(); !valid) {
            return valid;
        }
        return checkProfileEntity(document, who, revolve->definition().profile, *face.entity);
    }
    if (const auto* sweep = dynamic_cast<const SweepFeature*>(&object)) {
        if (cap) {
            return {};
        }
        if (face.role != FaceRole::Side) {
            return noSuchRole();
        }
        if (!face.along) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} is a sweep, whose sides are named by a profile entity and a path edge",
                                         who));
        }
        if (auto valid = checkProfileEntity(document, who, sweep->definition().profile, *face.entity); !valid) {
            return valid;
        }
        // The edge must belong to one of the path's runs. Entity IDs are
        // numbered per sketch, so a path of several runs names the sketch
        // too and the pair must match one run exactly (P12-SWEEP-001).
        const SweepDefinition& definition = sweep->definition();
        const auto holds = [&](SketchId sketch, const std::vector<EntityId>& edges) {
            if (face.alongSketch && *face.alongSketch != sketch) {
                return false;
            }
            return std::ranges::find(edges, *face.along) != edges.end();
        };
        bool found = holds(definition.path.sketch, definition.path.edges);
        for (const SweepPathRun& run : definition.path.runs) {
            found = found || holds(run.sketch, run.edges);
        }
        if (!found) {
            return makeError(ErrorCode::NotFound, std::format("{}: {} is not an edge of its path", who, *face.along));
        }
        if (!definition.path.runs.empty() && !face.alongSketch) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{}: its path runs through several sketches, so a side names the sketch "
                                         "its path edge is drawn in as well as the edge",
                                         who));
        }
        return {};
    }
    if (dynamic_cast<const LoftFeature*>(&object) != nullptr) {
        if (cap) {
            return {};
        }
        if (face.role == FaceRole::Side) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} is a loft, whose sides are not planes and are not named", who));
        }
        return noSuchRole();
    }
    if (const auto* rib = dynamic_cast<const RibFeature*>(&object)) {
        if (cap) {
            return {};
        }
        if (face.role != FaceRole::Side) {
            return noSuchRole();
        }
        if (auto valid = noAlong(); !valid) {
            return valid;
        }
        const auto& edges = rib->definition().edges;
        if (std::ranges::find(edges, *face.entity) == edges.end()) {
            return makeError(ErrorCode::NotFound,
                             std::format("{}: {} is not an edge of its profile", who, *face.entity));
        }
        return {};
    }
    if (const auto* hole = dynamic_cast<const HoleFeature*>(&object)) {
        const HoleDefinition& d = hole->definition();
        if (face.role == FaceRole::HoleBottom) {
            if (d.extent != geometry::HoleExtent::Blind) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("{} is a through hole, which has no bottom", who));
            }
            return {};
        }
        if (face.role == FaceRole::CounterboreFloor) {
            if (d.type != geometry::HoleType::Counterbore) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("{} is not counterbored, so it has no counterbore floor", who));
            }
            return {};
        }
        if (face.role == FaceRole::SpotfaceFloor) {
            if (d.type != geometry::HoleType::Spotface) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("{} is not spotfaced, so it has no spotface floor", who));
            }
            return {};
        }
        return noSuchRole();
    }
    if (const auto* chamfer = dynamic_cast<const ChamferFeature*>(&object)) {
        if (face.role != FaceRole::Chamfer) {
            return noSuchRole();
        }
        // BY IDENTITY, not by position: the selection this reference names
        // either is still among the chamfer's edges or it is gone, and its
        // place in the list is not part of the question. A count check -- what
        // this was -- called a reference valid whenever the list was merely
        // long enough, which is how a reorder used to resolve to another face.
        const auto& edges = chamfer->definition().edges;
        const bool present =
            std::ranges::any_of(edges, [&](const ChamferEdge& edge) { return edge.id == *face.edge; });
        if (!present) {
            return makeError(ErrorCode::NotFound,
                             std::format("{} has no chamfer edge {}", who, face.edge->value()));
        }
        return {};
    }
    return makeError(ErrorCode::Internal, std::format("{}: no face roles are known for its kind", who));
}

} // namespace

bool namesFaces(std::string_view typeName) noexcept {
    return typeName == ExtrudeFeature::kTypeName || typeName == RevolveFeature::kTypeName ||
           typeName == SweepFeature::kTypeName || typeName == LoftFeature::kTypeName ||
           typeName == HoleFeature::kTypeName || typeName == ChamferFeature::kTypeName ||
           typeName == RibFeature::kTypeName;
}

std::string describe(const Document& document, const FaceName& name) {
    std::string text = std::format("{} of {}", faceText(name.face), label(document, name.feature));
    for (const FaceCopy& copy : name.face.copies) {
        text += std::format(", copy {} of {}", copy.instance, label(document, copy.feature));
    }
    return text;
}

ObjectId holderOf(const FaceName& name) noexcept {
    return name.face.copies.empty() ? name.feature : name.face.copies.back().feature;
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
    if (copiesFaces(object->typeName())) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} is {}, whose faces are copies: name the face it copies, and the copy", who,
                                     article(kindName(object->typeName()))));
    }
    if (!namesFaces(object->typeName())) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} is {}, whose faces are not named (extrudes, revolves, sweeps, lofts, holes, "
                                     "chamfers and ribs name theirs)",
                                     who, article(kindName(object->typeName()))));
    }
    if (auto valid = validate(name.face); !valid) {
        return makeError(ErrorCode::InvalidArgument, std::format("{}: {}", who, valid.error().message));
    }
    if (auto valid = checkRole(document, *object, name.face); !valid) {
        return valid;
    }
    for (const FaceCopy& copy : name.face.copies) {
        const DocumentObject* copier = document.findObject(copy.feature);
        if (copier == nullptr) {
            return makeError(ErrorCode::NotFound, std::format("{} does not exist", copy.feature));
        }
        if (!copiesFaces(copier->typeName())) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} is {}, which makes no copies (patterns and mirrors do)",
                                         label(document, copy.feature), article(kindName(copier->typeName()))));
        }
        if (copier->typeName() == MirrorFeature::kTypeName && copy.instance != 1) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} is a mirror, whose only copy is instance 1, not {}",
                                         label(document, copy.feature), copy.instance));
        }
    }
    return {};
}

namespace {

/// The named faces of a body, with the diagnostics resolveFacePlane gives.
/// Shared so a cylinder and a plane fail the same way for the same reasons.
[[nodiscard]] Result<std::vector<geometry::FaceInfo>> namedFacesOf(const Document& document,
                                                                   const FaceName& name,
                                                                   const BodyLookup& bodies,
                                                                   const std::string& what) {
    if (!bodies) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{} is found in its feature's regenerated body, which is not available here",
                                     what));
    }
    const ObjectId holder = holderOf(name);
    const geometry::Body* body = bodies(holder);
    if (body == nullptr || body->isEmpty()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{} cannot be found: {} has no body", what, label(document, holder)));
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
    return faces;
}

} // namespace

Result<geometry::CylindricalFace> resolveFaceCylinder(const Document& document, const FaceName& name,
                                                      const BodyLookup& bodies) {
    if (auto valid = checkFaceName(document, name); !valid) {
        return std::unexpected(valid.error());
    }
    const std::string what = describe(document, name);
    auto faces = namedFacesOf(document, name, bodies, what);
    if (!faces) {
        return std::unexpected(faces.error());
    }
    for (const geometry::FaceInfo& face : *faces) {
        if (!face.cylinder) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} is {}, not a cylinder", what,
                                         article(toString(face.surface))));
        }
    }
    // A name can carry several faces -- a cut can split one in two -- and
    // the parts of one cylinder all share its axis and radius. Parts of
    // DIFFERENT cylinders would leave a radius that has to be chosen between,
    // and choosing is the silent wrong answer ADR-012 exists to prevent.
    const geometry::CylindricalFace& first = *faces->front().cylinder;
    for (const geometry::FaceInfo& face : *faces) {
        const geometry::CylindricalFace& other = *face.cylinder;
        const bool sameRadius = std::abs((other.radius - first.radius).si()) <= 1e-10;
        const bool sameDirection = std::abs(std::abs(other.axis.direction.dot(first.axis.direction)) - 1.0) <= 1e-9;
        if (!sameRadius || !sameDirection) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("{} is {} faces that are not parts of one cylinder", what,
                                         faces->size()));
        }
    }
    return first;
}

Result<Frame3D> resolveFacePlane(const Document& document, const FaceName& name, const BodyLookup& bodies) {
    if (auto valid = checkFaceName(document, name); !valid) {
        return std::unexpected(valid.error());
    }
    const std::string what = describe(document, name);
    if (!bodies) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{} is found in its feature's regenerated body, which is not available here",
                                     what));
    }
    const ObjectId holder = holderOf(name);
    const geometry::Body* body = bodies(holder);
    if (body == nullptr || body->isEmpty()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{} cannot be found: {} has no body", what, label(document, holder)));
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
