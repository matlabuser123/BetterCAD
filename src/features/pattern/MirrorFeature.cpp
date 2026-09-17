#include <bettercad/features/MirrorFeature.hpp>

#include <format>
#include <utility>

namespace bettercad::features {

namespace {

/// Negative zero as 0, for messages.
double tidy(double value) {
    return value == 0.0 ? 0.0 : value;
}

std::string format(const Vector3D& v) {
    return std::format("({:.6g}, {:.6g}, {:.6g})", tidy(v.x), tidy(v.y), tidy(v.z));
}

} // namespace

std::string_view toString(MirrorScope scope) noexcept {
    switch (scope) {
    case MirrorScope::Feature:
        return "feature";
    case MirrorScope::Body:
        return "body";
    }
    return "unknown";
}

Result<void> validate(const MirrorDefinition& definition) {
    const auto invalid = [](const std::string& message) { return makeError(ErrorCode::InvalidArgument, message); };
    if (!definition.source.isValid()) {
        return invalid("a mirror needs a source feature");
    }
    const MirrorPlane& plane = definition.plane;
    if (plane.offsetParameter && !plane.offsetParameter->isValid()) {
        return invalid("the mirror's parameter IDs must be valid");
    }
    if (plane.reference) {
        if (plane.origin != Point3D{} || plane.normal != MirrorPlane{}.normal) {
            return invalid("a mirror plane given by a reference has no origin or normal of its own");
        }
        if (plane.reference->object && !plane.reference->object->isValid()) {
            return invalid("the mirror plane's reference must name a valid object");
        }
        if (plane.reference->face) {
            return invalid("a mirror plane refers to a datum plane or a coordinate system, not to a face");
        }
    }
    if (!isFinite(plane.origin.x) || !isFinite(plane.origin.y) || !isFinite(plane.origin.z)) {
        return invalid("the plane's origin must be finite");
    }
    if (!Direction3D::fromComponents(plane.normal.x, plane.normal.y, plane.normal.z)) {
        return invalid(std::format("the plane's normal must be a finite, non-zero vector, got {}", format(plane.normal)));
    }
    if (!plane.offsetParameter && !isFinite(plane.offset)) {
        return invalid(std::format("the plane's offset must be finite, got {:.6g} mm", plane.offset.in(units::mm)));
    }
    if (!definition.keepOriginal && definition.scope == MirrorScope::Feature) {
        return invalid("a feature mirror keeps its source: the mirrored operation is added to the body the source "
                       "made (mirror the body to keep only the mirror image)");
    }
    return {};
}

MirrorReflection mirrorReflection(const Point3D& point, const Direction3D& normal) {
    return {.point = point, .normal = normal, .motion = RigidTransform3D::reflection(point, normal)};
}

MirrorFeature::MirrorFeature(std::string name, const MirrorDefinition& definition)
    : SolidFeature(std::move(name)), definition_(definition) {}

Result<std::unique_ptr<MirrorFeature>> MirrorFeature::create(std::string name, const MirrorDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    return std::unique_ptr<MirrorFeature>(new MirrorFeature(std::move(name), definition));
}

std::unique_ptr<DocumentObject> MirrorFeature::clone() const {
    return std::unique_ptr<DocumentObject>(new MirrorFeature(*this));
}

bool MirrorFeature::contentEquals(const DocumentObject& other) const {
    return definition_ == static_cast<const MirrorFeature&>(other).definition_;
}

std::vector<ObjectId> MirrorFeature::dependencies() const {
    std::vector<ObjectId> result{ObjectId{definition_.source}};
    if (definition_.plane.offsetParameter) {
        result.push_back(ObjectId{*definition_.plane.offsetParameter});
    }
    if (definition_.plane.reference && definition_.plane.reference->object) {
        result.push_back(*definition_.plane.reference->object);
    }
    return result;
}

Result<bool> MirrorFeature::setDefinition(const MirrorDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    if (definition == definition_) {
        return false;
    }
    definition_ = definition;
    return true;
}

} // namespace bettercad::features
