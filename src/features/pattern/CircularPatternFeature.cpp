#include <bettercad/features/CircularPatternFeature.hpp>

#include "features/pattern/PatternSupport.hpp"

#include <format>
#include <numbers>
#include <utility>

namespace bettercad::features {

namespace {

bool validId(const std::optional<ParameterId>& id) {
    return !id || id->isValid();
}

/// Negative zero as 0, for messages.
double tidy(double value) {
    return value == 0.0 ? 0.0 : value;
}

std::string format(const Vector3D& v) {
    return std::format("({:.6g}, {:.6g}, {:.6g})", tidy(v.x), tidy(v.y), tidy(v.z));
}

} // namespace

std::string_view toString(CircularSpacing spacing) noexcept {
    switch (spacing) {
    case CircularSpacing::FullCircle:
        return "full circle";
    case CircularSpacing::IncludedAngle:
        return "included angle";
    case CircularSpacing::AngleStep:
        return "angle step";
    }
    return "unknown";
}

std::string_view toString(RotationDirection direction) noexcept {
    switch (direction) {
    case RotationDirection::Positive:
        return "positive";
    case RotationDirection::Negative:
        return "negative";
    }
    return "unknown";
}

Result<void> validate(const CircularPatternDefinition& definition) {
    const auto invalid = [](const std::string& message) { return makeError(ErrorCode::InvalidArgument, message); };
    if (!definition.source.isValid()) {
        return invalid("a circular pattern needs a source feature");
    }
    if (!validId(definition.countParameter) || !validId(definition.angleParameter)) {
        return invalid("the pattern's parameter IDs must be valid");
    }
    if (const auto& reference = definition.axis.reference) {
        if (definition.axis.origin != Point3D{} || definition.axis.direction != PatternAxis{}.direction) {
            return invalid("a pattern axis given by a reference has no origin or direction of its own");
        }
        if (reference->object && !reference->object->isValid()) {
            return invalid("the pattern axis' reference must name a valid object");
        }
    }
    const Point3D& origin = definition.axis.origin;
    if (!isFinite(origin.x) || !isFinite(origin.y) || !isFinite(origin.z)) {
        return invalid("the axis origin must be finite");
    }
    const Vector3D& direction = definition.axis.direction;
    if (!Direction3D::fromComponents(direction.x, direction.y, direction.z)) {
        return invalid(
            std::format("the axis direction must be a finite, non-zero vector, got {}", format(direction)));
    }
    if (!definition.countParameter) {
        if (definition.count < 1) {
            return invalid(std::format("the count must be at least 1, got {}", definition.count));
        }
        if (definition.count > kMaxPatternInstances) {
            return invalid(std::format("a circular pattern may have at most {} instances, got {}",
                                       kMaxPatternInstances, definition.count));
        }
    }
    if (definition.spacing == CircularSpacing::FullCircle) {
        if (definition.angle != Angle{} || definition.angleParameter) {
            return invalid("a full-circle pattern takes no angle: its instances are 360 deg / count apart");
        }
        return {};
    }
    if (definition.angleParameter) {
        return {}; // checked with the parameter's value, at regeneration
    }
    const std::optional<std::size_t> count =
        definition.countParameter ? std::nullopt : std::optional<std::size_t>{definition.count};
    return detail::checkCircularAngle(definition.spacing, count, definition.angle);
}

std::vector<CircularPatternInstance> circularPatternInstances(const Axis3D& axis, std::size_t count, Angle step) {
    std::vector<CircularPatternInstance> instances;
    instances.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        // From the source: i × step, rounded once, and its own rotation.
        const Angle angle = step * static_cast<double>(i);
        instances.push_back({.index = i, .angle = angle, .motion = RigidTransform3D::rotation(axis, angle)});
    }
    return instances;
}

Angle circularPatternStep(CircularSpacing spacing, std::size_t count, Angle angle, RotationDirection direction) {
    Angle step{};
    switch (spacing) {
    case CircularSpacing::FullCircle:
        step = count > 0 ? Angle::fromSi(2.0 * std::numbers::pi / static_cast<double>(count)) : Angle{};
        break;
    case CircularSpacing::IncludedAngle:
        step = count > 1 ? angle / static_cast<double>(count - 1) : Angle{};
        break;
    case CircularSpacing::AngleStep:
        step = angle;
        break;
    }
    return direction == RotationDirection::Negative ? -step : step;
}

CircularPatternFeature::CircularPatternFeature(std::string name, const CircularPatternDefinition& definition)
    : SolidFeature(std::move(name)), definition_(definition) {}

Result<std::unique_ptr<CircularPatternFeature>> CircularPatternFeature::create(
    std::string name, const CircularPatternDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    return std::unique_ptr<CircularPatternFeature>(new CircularPatternFeature(std::move(name), definition));
}

std::unique_ptr<DocumentObject> CircularPatternFeature::clone() const {
    return std::unique_ptr<DocumentObject>(new CircularPatternFeature(*this));
}

bool CircularPatternFeature::contentEquals(const DocumentObject& other) const {
    return definition_ == static_cast<const CircularPatternFeature&>(other).definition_;
}

std::vector<ObjectId> CircularPatternFeature::dependencies() const {
    std::vector<ObjectId> result{ObjectId{definition_.source}};
    for (const auto& parameter : {definition_.countParameter, definition_.angleParameter}) {
        if (parameter) {
            result.push_back(ObjectId{*parameter});
        }
    }
    if (definition_.axis.reference && definition_.axis.reference->object) {
        result.push_back(*definition_.axis.reference->object);
    }
    return result;
}

Result<bool> CircularPatternFeature::setDefinition(const CircularPatternDefinition& definition) {
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
