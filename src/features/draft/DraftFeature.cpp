#include <bettercad/features/DraftFeature.hpp>

#include <bettercad/core/geometry/Draft.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace bettercad::features {

Result<void> validate(const DraftDefinition& definition) {
    if (!definition.target.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "a draft needs a target feature");
    }
    if (auto valid = validate(definition.neutralPlane); !valid) {
        return makeError(valid.error().code, std::format("the neutral plane: {}", valid.error().message));
    }
    if (definition.angleParameter && !definition.angleParameter->isValid()) {
        return makeError(ErrorCode::InvalidArgument, "the angle parameter ID must be valid");
    }
    // The rest is the geometry request's contract. A driven angle is checked
    // when the parameter's value is known, at regeneration.
    const geometry::DraftRequest request{
        .faces = definition.faces,
        .neutralPlane = Frame3D::xy(),
        .angle = definition.angleParameter ? Angle{} : definition.angle,
    };
    return geometry::validate(request);
}

DraftFeature::DraftFeature(std::string name, const DraftDefinition& definition)
    : SolidFeature(std::move(name)), definition_(definition) {}

Result<std::unique_ptr<DraftFeature>> DraftFeature::create(std::string name, const DraftDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    return std::unique_ptr<DraftFeature>(new DraftFeature(std::move(name), definition));
}

std::unique_ptr<DocumentObject> DraftFeature::clone() const {
    return std::unique_ptr<DocumentObject>(new DraftFeature(*this));
}

bool DraftFeature::contentEquals(const DocumentObject& other) const {
    return definition_ == static_cast<const DraftFeature&>(other).definition_;
}

std::vector<ObjectId> DraftFeature::dependencies() const {
    std::vector<ObjectId> result{ObjectId{definition_.target}};
    const auto add = [&](ObjectId id) {
        if (std::ranges::find(result, id) == result.end()) {
            result.push_back(id);
        }
    };
    if (definition_.angleParameter) {
        add(ObjectId{*definition_.angleParameter});
    }
    for (const ObjectId id : referencedObjects(definition_.neutralPlane)) {
        add(id);
    }
    for (const FaceName& name : definition_.faces) {
        add(name.feature);
        for (const FaceCopy& copy : name.face.copies) {
            add(copy.feature);
        }
    }
    return result;
}

Result<bool> DraftFeature::setDefinition(const DraftDefinition& definition) {
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
