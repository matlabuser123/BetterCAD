#include <bettercad/features/RibFeature.hpp>

#include <bettercad/core/units/Format.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <utility>

namespace bettercad::features {

Result<void> validate(const RibDefinition& definition) {
    const auto invalid = [](std::string message) { return makeError(ErrorCode::InvalidArgument, std::move(message)); };
    if (!definition.target.isValid()) {
        return invalid("a rib needs a target feature");
    }
    if (!definition.profile.isValid()) {
        return invalid("a rib needs a profile sketch");
    }
    if (definition.edges.empty()) {
        return invalid("a rib needs one or more profile edges");
    }
    for (std::size_t i = 0; i < definition.edges.size(); ++i) {
        if (!definition.edges[i].isValid()) {
            return invalid(std::format("profile edge {} must be a valid entity", i + 1));
        }
        const auto earlier = definition.edges.begin() + static_cast<std::ptrdiff_t>(i);
        if (std::find(definition.edges.begin(), earlier, definition.edges[i]) != earlier) {
            return invalid(std::format("profile edge {} repeats an earlier edge", i + 1));
        }
    }
    if (definition.thicknessParameter && !definition.thicknessParameter->isValid()) {
        return invalid("the thickness parameter ID must be valid");
    }
    if (!definition.thicknessParameter &&
        (!isFinite(definition.thickness) || !(definition.thickness > Length{}))) {
        return invalid(std::format("the rib thickness must be positive and finite, got {}",
                                   toString(definition.thickness, units::mm)));
    }
    const geometry::RibPlacement p = definition.placement;
    if (p != geometry::RibPlacement::Symmetric && p != geometry::RibPlacement::AlongNormal &&
        p != geometry::RibPlacement::AgainstNormal) {
        return invalid("a rib's thickness lies symmetric, along or against the normal");
    }
    return {};
}

RibFeature::RibFeature(std::string name, const RibDefinition& definition)
    : SolidFeature(std::move(name)), definition_(definition) {}

Result<std::unique_ptr<RibFeature>> RibFeature::create(std::string name, const RibDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    return std::unique_ptr<RibFeature>(new RibFeature(std::move(name), definition));
}

std::unique_ptr<DocumentObject> RibFeature::clone() const {
    return std::unique_ptr<DocumentObject>(new RibFeature(*this));
}

bool RibFeature::contentEquals(const DocumentObject& other) const {
    return definition_ == static_cast<const RibFeature&>(other).definition_;
}

std::vector<ObjectId> RibFeature::dependencies() const {
    std::vector<ObjectId> result{ObjectId{definition_.target}, ObjectId{definition_.profile}};
    if (definition_.thicknessParameter) {
        result.push_back(ObjectId{*definition_.thicknessParameter});
    }
    return result;
}

Result<bool> RibFeature::setDefinition(const RibDefinition& definition) {
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
