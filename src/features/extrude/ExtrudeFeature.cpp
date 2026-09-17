#include <bettercad/core/units/Format.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>

#include <format>
#include <utility>

namespace bettercad::features {

std::string_view toString(ExtrudeDirection direction) noexcept {
    switch (direction) {
    case ExtrudeDirection::Normal:
        return "normal";
    case ExtrudeDirection::Reversed:
        return "reversed";
    case ExtrudeDirection::Symmetric:
        return "symmetric";
    }
    return "unknown";
}

std::string_view toString(ExtrudeTermination termination) noexcept {
    switch (termination) {
    case ExtrudeTermination::Blind:
        return "blind";
    case ExtrudeTermination::ThroughAll:
        return "through all";
    }
    return "unknown";
}

Result<void> validate(const ExtrudeDefinition& definition) {
    if (!definition.profile.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "an extrude needs a profile sketch");
    }
    if (definition.termination == ExtrudeTermination::ThroughAll) {
        if (definition.depthParameter || definition.depth != Length{}) {
            return makeError(ErrorCode::InvalidArgument,
                             "a through-all extrude has no depth: it reaches through its target");
        }
        if (definition.operation != FeatureOperation::Cut) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("a through-all extrude must be a cut, got {}",
                                         toString(definition.operation)));
        }
        return validateOperation(definition.operation, definition.target);
    }
    if (definition.termination != ExtrudeTermination::Blind) {
        return makeError(ErrorCode::InvalidArgument, "unknown extrude termination");
    }
    if (definition.depthParameter) {
        if (!definition.depthParameter->isValid()) {
            return makeError(ErrorCode::InvalidArgument, "the depth parameter ID must be valid");
        }
    } else if (!isFinite(definition.depth) || definition.depth <= Length{}) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("extrude depth must be positive, got {}",
                                     toString(definition.depth, units::mm)));
    }
    return validateOperation(definition.operation, definition.target);
}

ExtrudeFeature::ExtrudeFeature(std::string name, const ExtrudeDefinition& definition)
    : SolidFeature(std::move(name)), definition_(definition) {}

Result<std::unique_ptr<ExtrudeFeature>> ExtrudeFeature::create(std::string name,
                                                               const ExtrudeDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    return std::unique_ptr<ExtrudeFeature>(new ExtrudeFeature(std::move(name), definition));
}

std::unique_ptr<DocumentObject> ExtrudeFeature::clone() const {
    return std::unique_ptr<DocumentObject>(new ExtrudeFeature(*this));
}

bool ExtrudeFeature::contentEquals(const DocumentObject& other) const {
    return definition_ == static_cast<const ExtrudeFeature&>(other).definition_;
}

std::vector<ObjectId> ExtrudeFeature::dependencies() const {
    std::vector<ObjectId> result{ObjectId{definition_.profile}};
    if (definition_.depthParameter) {
        result.push_back(ObjectId{*definition_.depthParameter});
    }
    if (definition_.target) {
        result.push_back(ObjectId{*definition_.target});
    }
    return result;
}

Result<bool> ExtrudeFeature::setDefinition(const ExtrudeDefinition& definition) {
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
