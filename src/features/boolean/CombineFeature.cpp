#include <bettercad/features/CombineFeature.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace bettercad::features {

Result<void> validate(const CombineDefinition& definition) {
    if (!definition.target.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "a combine needs a target feature");
    }
    if (definition.tools.empty()) {
        return makeError(ErrorCode::InvalidArgument, "a combine needs one or more tool features");
    }
    for (std::size_t i = 0; i < definition.tools.size(); ++i) {
        const FeatureId tool = definition.tools[i];
        if (!tool.isValid()) {
            return makeError(ErrorCode::InvalidArgument, std::format("tool {} must be a valid feature", i + 1));
        }
        if (tool == definition.target) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("tool {} is the target: a body is not combined with itself", i + 1));
        }
        if (std::ranges::find(definition.tools.begin(), definition.tools.begin() + static_cast<std::ptrdiff_t>(i),
                              tool) != definition.tools.begin() + static_cast<std::ptrdiff_t>(i)) {
            return makeError(ErrorCode::InvalidArgument, std::format("tool {} repeats an earlier tool", i + 1));
        }
    }
    switch (definition.operation) {
    case FeatureOperation::Join:
    case FeatureOperation::Cut:
    case FeatureOperation::Intersect:
        return {};
    case FeatureOperation::NewBody:
        break;
    }
    return makeError(ErrorCode::InvalidArgument,
                     std::format("a combine joins, cuts or intersects bodies, got {}", toString(definition.operation)));
}

CombineFeature::CombineFeature(std::string name, const CombineDefinition& definition)
    : SolidFeature(std::move(name)), definition_(definition) {}

Result<std::unique_ptr<CombineFeature>> CombineFeature::create(std::string name,
                                                               const CombineDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    return std::unique_ptr<CombineFeature>(new CombineFeature(std::move(name), definition));
}

std::unique_ptr<DocumentObject> CombineFeature::clone() const {
    return std::unique_ptr<DocumentObject>(new CombineFeature(*this));
}

bool CombineFeature::contentEquals(const DocumentObject& other) const {
    return definition_ == static_cast<const CombineFeature&>(other).definition_;
}

std::vector<ObjectId> CombineFeature::dependencies() const {
    std::vector<ObjectId> result{ObjectId{definition_.target}};
    for (const FeatureId tool : definition_.tools) {
        result.push_back(ObjectId{tool});
    }
    return result;
}

std::vector<FeatureId> CombineFeature::consumedFeatures() const {
    std::vector<FeatureId> result{definition_.target};
    result.insert(result.end(), definition_.tools.begin(), definition_.tools.end());
    return result;
}

Result<bool> CombineFeature::setDefinition(const CombineDefinition& definition) {
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
