#include <bettercad/features/SweepFeature.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace bettercad::features {

std::string_view toString(SweepOrientation orientation) noexcept {
    switch (orientation) {
    case SweepOrientation::FollowPath:
        return "follow path";
    }
    return "unknown";
}

Result<void> validate(const SweepDefinition& definition) {
    const auto invalid = [](const std::string& message) { return makeError(ErrorCode::InvalidArgument, message); };
    if (!definition.profile.isValid()) {
        return invalid("a sweep needs a profile sketch");
    }
    if (!definition.path.sketch.isValid()) {
        return invalid("a sweep needs a path sketch");
    }
    if (definition.path.sketch == definition.profile) {
        return invalid("the path must be in another sketch than the profile: it leaves the profile's plane at right "
                       "angles");
    }
    const std::vector<EntityId>& edges = definition.path.edges;
    if (edges.empty()) {
        return invalid("a sweep path needs at least one edge");
    }
    for (std::size_t i = 0; i < edges.size(); ++i) {
        if (!edges[i].isValid()) {
            return invalid("the path's edge IDs must be valid");
        }
        if (std::find(edges.begin(), edges.begin() + static_cast<std::ptrdiff_t>(i), edges[i]) !=
            edges.begin() + static_cast<std::ptrdiff_t>(i)) {
            return invalid(std::format("{} is listed twice in the path", edges[i]));
        }
    }
    return validateOperation(definition.operation, definition.target);
}

SweepFeature::SweepFeature(std::string name, const SweepDefinition& definition)
    : SolidFeature(std::move(name)), definition_(definition) {}

Result<std::unique_ptr<SweepFeature>> SweepFeature::create(std::string name, const SweepDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    return std::unique_ptr<SweepFeature>(new SweepFeature(std::move(name), definition));
}

std::unique_ptr<DocumentObject> SweepFeature::clone() const {
    return std::unique_ptr<DocumentObject>(new SweepFeature(*this));
}

bool SweepFeature::contentEquals(const DocumentObject& other) const {
    return definition_ == static_cast<const SweepFeature&>(other).definition_;
}

std::vector<ObjectId> SweepFeature::dependencies() const {
    std::vector<ObjectId> result{ObjectId{definition_.profile}, ObjectId{definition_.path.sketch}};
    if (definition_.target) {
        result.push_back(ObjectId{*definition_.target});
    }
    return result;
}

Result<bool> SweepFeature::setDefinition(const SweepDefinition& definition) {
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
