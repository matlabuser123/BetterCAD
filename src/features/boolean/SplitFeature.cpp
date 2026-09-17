#include <bettercad/features/SplitFeature.hpp>

#include <format>
#include <utility>

namespace bettercad::features {

Result<void> validate(const SplitDefinition& definition) {
    if (!definition.target.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "a split needs a target feature");
    }
    if (auto valid = validate(definition.plane); !valid) {
        return makeError(valid.error().code, std::format("the split plane: {}", valid.error().message));
    }
    switch (definition.keep) {
    case geometry::SplitKeep::Front:
    case geometry::SplitKeep::Back:
    case geometry::SplitKeep::Both:
        return {};
    }
    return makeError(ErrorCode::InvalidArgument, "a split keeps the front, the back or both");
}

SplitFeature::SplitFeature(std::string name, const SplitDefinition& definition)
    : SolidFeature(std::move(name)), definition_(definition) {}

Result<std::unique_ptr<SplitFeature>> SplitFeature::create(std::string name, const SplitDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    return std::unique_ptr<SplitFeature>(new SplitFeature(std::move(name), definition));
}

std::unique_ptr<DocumentObject> SplitFeature::clone() const {
    return std::unique_ptr<DocumentObject>(new SplitFeature(*this));
}

bool SplitFeature::contentEquals(const DocumentObject& other) const {
    return definition_ == static_cast<const SplitFeature&>(other).definition_;
}

std::vector<ObjectId> SplitFeature::dependencies() const {
    std::vector<ObjectId> result{ObjectId{definition_.target}};
    for (const ObjectId id : referencedObjects(definition_.plane)) {
        if (id != result.front()) {
            result.push_back(id);
        }
    }
    return result;
}

Result<bool> SplitFeature::setDefinition(const SplitDefinition& definition) {
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
