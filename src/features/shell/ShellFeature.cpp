#include <bettercad/features/ShellFeature.hpp>

#include <algorithm>
#include <utility>

namespace bettercad::features {

Result<void> validate(const ShellDefinition& definition) {
    if (!definition.target.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "a shell needs a target feature");
    }
    if (definition.thicknessParameter && !definition.thicknessParameter->isValid()) {
        return makeError(ErrorCode::InvalidArgument, "the thickness parameter ID must be valid");
    }
    // The rest is the geometry request's contract. A driven thickness is
    // checked when the parameter's value is known, at regeneration.
    const geometry::ShellRequest request{
        .openFaces = definition.openFaces,
        .thickness = definition.thicknessParameter ? Length::fromSi(1.0) : definition.thickness,
        .side = definition.side,
    };
    return geometry::validate(request);
}

ShellFeature::ShellFeature(std::string name, const ShellDefinition& definition)
    : SolidFeature(std::move(name)), definition_(definition) {}

Result<std::unique_ptr<ShellFeature>> ShellFeature::create(std::string name, const ShellDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    return std::unique_ptr<ShellFeature>(new ShellFeature(std::move(name), definition));
}

std::unique_ptr<DocumentObject> ShellFeature::clone() const {
    return std::unique_ptr<DocumentObject>(new ShellFeature(*this));
}

bool ShellFeature::contentEquals(const DocumentObject& other) const {
    return definition_ == static_cast<const ShellFeature&>(other).definition_;
}

std::vector<ObjectId> ShellFeature::dependencies() const {
    std::vector<ObjectId> result{ObjectId{definition_.target}};
    const auto add = [&](ObjectId id) {
        if (std::ranges::find(result, id) == result.end()) {
            result.push_back(id);
        }
    };
    if (definition_.thicknessParameter) {
        add(ObjectId{*definition_.thicknessParameter});
    }
    for (const FaceName& name : definition_.openFaces) {
        add(name.feature);
        for (const FaceCopy& copy : name.face.copies) {
            add(copy.feature);
        }
    }
    return result;
}

Result<bool> ShellFeature::setDefinition(const ShellDefinition& definition) {
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
