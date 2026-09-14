#include <bettercad/features/FilletFeature.hpp>

#include <bettercad/core/geometry/Fillet.hpp>

#include <utility>

namespace bettercad::features {

Result<void> validate(const FilletDefinition& definition) {
    if (!definition.target.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "a fillet needs a target feature");
    }
    if (definition.radiusParameter && !definition.radiusParameter->isValid()) {
        return makeError(ErrorCode::InvalidArgument, "the radius parameter ID must be valid");
    }
    // The rest is the geometry request's contract. A driven radius is checked
    // when the parameter's value is known, at regeneration.
    const geometry::FilletRequest request{
        .edges = definition.edges,
        .radius = definition.radiusParameter ? Length::fromSi(1.0) : definition.radius,
    };
    return geometry::validate(request);
}

FilletFeature::FilletFeature(std::string name, const FilletDefinition& definition)
    : SolidFeature(std::move(name)), definition_(definition) {}

Result<std::unique_ptr<FilletFeature>> FilletFeature::create(std::string name, const FilletDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    return std::unique_ptr<FilletFeature>(new FilletFeature(std::move(name), definition));
}

std::unique_ptr<DocumentObject> FilletFeature::clone() const {
    return std::unique_ptr<DocumentObject>(new FilletFeature(*this));
}

bool FilletFeature::contentEquals(const DocumentObject& other) const {
    return definition_ == static_cast<const FilletFeature&>(other).definition_;
}

std::vector<ObjectId> FilletFeature::dependencies() const {
    std::vector<ObjectId> result{ObjectId{definition_.target}};
    if (definition_.radiusParameter) {
        result.push_back(ObjectId{*definition_.radiusParameter});
    }
    return result;
}

Result<bool> FilletFeature::setDefinition(const FilletDefinition& definition) {
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
