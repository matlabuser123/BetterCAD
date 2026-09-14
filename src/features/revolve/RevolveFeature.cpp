#include <bettercad/core/units/Format.hpp>
#include <bettercad/features/RevolveFeature.hpp>

#include <format>
#include <utility>

namespace bettercad::features {

namespace {

constexpr Angle kFullTurn = Angle::fromSi(2.0 * std::numbers::pi);
// Same allowance as geometry::makeRevolution for "exactly 360 degrees".
constexpr double kFullTurnTolerance = 1e-12;

} // namespace

std::string_view toString(RevolveAxisKind kind) noexcept {
    switch (kind) {
    case RevolveAxisKind::SketchX:
        return "sketch X axis";
    case RevolveAxisKind::SketchY:
        return "sketch Y axis";
    case RevolveAxisKind::Line:
        return "line";
    }
    return "unknown";
}

std::string_view toString(RevolveDirection direction) noexcept {
    switch (direction) {
    case RevolveDirection::Positive:
        return "positive";
    case RevolveDirection::Negative:
        return "negative";
    case RevolveDirection::Symmetric:
        return "symmetric";
    }
    return "unknown";
}

Result<void> validateRevolveAngle(Angle angle) {
    if (!isFinite(angle) || angle <= Angle{} || angle.si() > kFullTurn.si() + kFullTurnTolerance) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("revolve angle must be in (0, 360] deg, got {}", toString(angle, units::deg)));
    }
    return {};
}

Result<void> validate(const RevolveDefinition& definition) {
    if (!definition.profile.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "a revolve needs a profile sketch");
    }
    if (definition.axis.kind == RevolveAxisKind::Line && !definition.axis.line.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "a revolve about a line needs a valid line entity");
    }
    if (definition.axis.kind != RevolveAxisKind::Line && definition.axis.line.isValid()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("a revolve about the {} takes no line entity", toString(definition.axis.kind)));
    }
    if (definition.angleParameter) {
        if (!definition.angleParameter->isValid()) {
            return makeError(ErrorCode::InvalidArgument, "the angle parameter ID must be valid");
        }
    } else if (auto valid = validateRevolveAngle(definition.angle); !valid) {
        return valid;
    }
    return validateOperation(definition.operation, definition.target);
}

RevolveFeature::RevolveFeature(std::string name, const RevolveDefinition& definition)
    : SolidFeature(std::move(name)), definition_(definition) {}

Result<std::unique_ptr<RevolveFeature>> RevolveFeature::create(std::string name,
                                                               const RevolveDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    return std::unique_ptr<RevolveFeature>(new RevolveFeature(std::move(name), definition));
}

std::unique_ptr<DocumentObject> RevolveFeature::clone() const {
    return std::unique_ptr<DocumentObject>(new RevolveFeature(*this));
}

bool RevolveFeature::contentEquals(const DocumentObject& other) const {
    return definition_ == static_cast<const RevolveFeature&>(other).definition_;
}

std::vector<ObjectId> RevolveFeature::dependencies() const {
    std::vector<ObjectId> result{ObjectId{definition_.profile}};
    if (definition_.angleParameter) {
        result.push_back(ObjectId{*definition_.angleParameter});
    }
    if (definition_.target) {
        result.push_back(ObjectId{*definition_.target});
    }
    return result;
}

Result<bool> RevolveFeature::setDefinition(const RevolveDefinition& definition) {
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
