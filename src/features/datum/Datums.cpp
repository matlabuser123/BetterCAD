#include <bettercad/features/Datums.hpp>

#include <format>
#include <utility>

namespace bettercad::features {

namespace {

std::unexpected<Error> invalid(std::string message) {
    return makeError(ErrorCode::InvalidArgument, std::move(message));
}

bool finite(const Point3D& p) {
    return isFinite(p.x) && isFinite(p.y) && isFinite(p.z);
}

Result<void> checkId(const std::optional<ParameterId>& id, std::string_view what) {
    if (id && !id->isValid()) {
        return invalid(std::format("the {} parameter ID must be valid", what));
    }
    return {};
}

Result<void> checkObject(const std::optional<ObjectId>& id, std::string_view what) {
    if (id && !id->isValid()) {
        return invalid(std::format("the {} must be a valid object ID", what));
    }
    return {};
}

Result<void> checkPlane(const PlaneReference& reference, std::string_view what) {
    return checkObject(reference.object, what);
}

Result<void> checkAxis(const AxisReference& reference, std::string_view what) {
    return checkObject(reference.object, what);
}

void pushObject(std::vector<ObjectId>& out, const std::optional<ObjectId>& id) {
    if (id) {
        out.push_back(*id);
    }
}

void pushParameter(std::vector<ObjectId>& out, const std::optional<ParameterId>& id) {
    if (id) {
        out.push_back(ObjectId{*id});
    }
}

} // namespace

// --- Datum planes ------------------------------------------------------------------------------------

std::string_view toString(DatumPlaneKind kind) noexcept {
    switch (kind) {
    case DatumPlaneKind::Fixed:
        return "fixed";
    case DatumPlaneKind::Offset:
        return "offset";
    case DatumPlaneKind::Angled:
        return "angled";
    }
    return "unknown";
}

Result<void> validate(const DatumPlaneDefinition& d) {
    const DatumPlaneDefinition defaults{.kind = d.kind};
    if (!finite(d.frame.origin())) {
        return invalid("the plane's origin must be finite");
    }
    if (auto valid = checkPlane(d.base, "base plane"); !valid) {
        return valid;
    }
    if (auto valid = checkAxis(d.axis, "axis"); !valid) {
        return valid;
    }
    if (auto valid = checkId(d.offsetParameter, "offset"); !valid) {
        return valid;
    }
    if (auto valid = checkId(d.angleParameter, "angle"); !valid) {
        return valid;
    }
    if (!isFinite(d.offset) || !isFinite(d.angle)) {
        return invalid("the plane's offset and angle must be finite");
    }
    const bool usesBase = d.kind != DatumPlaneKind::Fixed;
    const bool usesOffset = d.kind == DatumPlaneKind::Offset;
    const bool usesAngle = d.kind == DatumPlaneKind::Angled;
    // "a fixed datum plane", "an offset datum plane", "an angled datum plane"
    const std::string noun =
        std::format("{} {} datum plane", d.kind == DatumPlaneKind::Fixed ? "a" : "an", toString(d.kind));
    if (usesBase && d.frame != defaults.frame) {
        return invalid(std::format("{} is placed from its base plane and has no frame of its own", noun));
    }
    if (!usesBase && d.base != defaults.base) {
        return invalid(std::format("{} has no base plane", noun));
    }
    if (!usesOffset && (d.offset != defaults.offset || d.offsetParameter)) {
        return invalid(std::format("{} has no offset", noun));
    }
    if (!usesAngle && (d.axis != defaults.axis || d.angle != defaults.angle || d.angleParameter)) {
        return invalid(std::format("{} has no axis or angle", noun));
    }
    return {};
}

DatumPlane::DatumPlane(std::string name, const DatumPlaneDefinition& definition)
    : DocumentObject(std::move(name)), definition_(definition) {}

Result<std::unique_ptr<DatumPlane>> DatumPlane::create(std::string name, const DatumPlaneDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    return std::unique_ptr<DatumPlane>(new DatumPlane(std::move(name), definition));
}

std::unique_ptr<DocumentObject> DatumPlane::clone() const {
    return std::unique_ptr<DocumentObject>(new DatumPlane(*this));
}

bool DatumPlane::contentEquals(const DocumentObject& other) const {
    return definition_ == static_cast<const DatumPlane&>(other).definition_;
}

std::vector<ObjectId> DatumPlane::dependencies() const {
    std::vector<ObjectId> result;
    pushObject(result, definition_.base.object);
    pushParameter(result, definition_.offsetParameter);
    pushObject(result, definition_.axis.object);
    pushParameter(result, definition_.angleParameter);
    return result;
}

Result<bool> DatumPlane::setDefinition(const DatumPlaneDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    if (definition == definition_) {
        return false;
    }
    definition_ = definition;
    return true;
}

// --- Datum axes -----------------------------------------------------------------------------------------

std::string_view toString(DatumAxisKind kind) noexcept {
    switch (kind) {
    case DatumAxisKind::Fixed:
        return "fixed";
    case DatumAxisKind::Intersection:
        return "intersection";
    }
    return "unknown";
}

Result<void> validate(const DatumAxisDefinition& d) {
    const DatumAxisDefinition defaults{.kind = d.kind};
    if (!finite(d.axis.origin)) {
        return invalid("the axis' origin must be finite");
    }
    if (auto valid = checkPlane(d.first, "first plane"); !valid) {
        return valid;
    }
    if (auto valid = checkPlane(d.second, "second plane"); !valid) {
        return valid;
    }
    if (d.kind == DatumAxisKind::Fixed && (d.first != defaults.first || d.second != defaults.second)) {
        return invalid("a fixed datum axis has no planes");
    }
    if (d.kind == DatumAxisKind::Intersection) {
        if (d.axis != defaults.axis) {
            return invalid("an intersection datum axis is where its planes meet and has no line of its own");
        }
        if (d.first == d.second) {
            return invalid("an intersection datum axis needs two different planes");
        }
    }
    return {};
}

DatumAxis::DatumAxis(std::string name, const DatumAxisDefinition& definition)
    : DocumentObject(std::move(name)), definition_(definition) {}

Result<std::unique_ptr<DatumAxis>> DatumAxis::create(std::string name, const DatumAxisDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    return std::unique_ptr<DatumAxis>(new DatumAxis(std::move(name), definition));
}

std::unique_ptr<DocumentObject> DatumAxis::clone() const {
    return std::unique_ptr<DocumentObject>(new DatumAxis(*this));
}

bool DatumAxis::contentEquals(const DocumentObject& other) const {
    return definition_ == static_cast<const DatumAxis&>(other).definition_;
}

std::vector<ObjectId> DatumAxis::dependencies() const {
    std::vector<ObjectId> result;
    pushObject(result, definition_.first.object);
    pushObject(result, definition_.second.object);
    return result;
}

Result<bool> DatumAxis::setDefinition(const DatumAxisDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    if (definition == definition_) {
        return false;
    }
    definition_ = definition;
    return true;
}

// --- Coordinate systems -------------------------------------------------------------------------------------

std::string_view toString(CoordinateSystemKind kind) noexcept {
    switch (kind) {
    case CoordinateSystemKind::Fixed:
        return "fixed";
    case CoordinateSystemKind::Offset:
        return "offset";
    }
    return "unknown";
}

Result<void> validate(const CoordinateSystemDefinition& d) {
    const CoordinateSystemDefinition defaults{.kind = d.kind};
    if (!finite(d.frame.origin())) {
        return invalid("the coordinate system's origin must be finite");
    }
    if (auto valid = checkObject(d.base, "base coordinate system"); !valid) {
        return valid;
    }
    for (std::size_t i = 0; i < 3; ++i) {
        if (!isFinite(d.translation[i]) || !isFinite(d.rotation[i])) {
            return invalid("the coordinate system's translation and rotation must be finite");
        }
        if (auto valid = checkId(d.translationParameters[i], "translation"); !valid) {
            return valid;
        }
        if (auto valid = checkId(d.rotationParameters[i], "rotation"); !valid) {
            return valid;
        }
    }
    if (d.kind == CoordinateSystemKind::Fixed) {
        if (d.base != defaults.base || d.translation != defaults.translation ||
            d.translationParameters != defaults.translationParameters || d.rotation != defaults.rotation ||
            d.rotationParameters != defaults.rotationParameters) {
            return invalid("a fixed coordinate system has no base, translation or rotation");
        }
    } else if (d.frame != defaults.frame) {
        return invalid("an offset coordinate system is placed from its base and has no frame of its own");
    }
    return {};
}

CoordinateSystem::CoordinateSystem(std::string name, const CoordinateSystemDefinition& definition)
    : DocumentObject(std::move(name)), definition_(definition) {}

Result<std::unique_ptr<CoordinateSystem>> CoordinateSystem::create(std::string name,
                                                                   const CoordinateSystemDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    return std::unique_ptr<CoordinateSystem>(new CoordinateSystem(std::move(name), definition));
}

std::unique_ptr<DocumentObject> CoordinateSystem::clone() const {
    return std::unique_ptr<DocumentObject>(new CoordinateSystem(*this));
}

bool CoordinateSystem::contentEquals(const DocumentObject& other) const {
    return definition_ == static_cast<const CoordinateSystem&>(other).definition_;
}

std::vector<ObjectId> CoordinateSystem::dependencies() const {
    std::vector<ObjectId> result;
    pushObject(result, definition_.base);
    for (const auto& parameter : definition_.translationParameters) {
        pushParameter(result, parameter);
    }
    for (const auto& parameter : definition_.rotationParameters) {
        pushParameter(result, parameter);
    }
    return result;
}

Result<bool> CoordinateSystem::setDefinition(const CoordinateSystemDefinition& definition) {
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
