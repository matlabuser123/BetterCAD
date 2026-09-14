#include <bettercad/features/ChamferFeature.hpp>

#include <utility>

namespace bettercad::features {

Result<void> validate(const ChamferDefinition& definition) {
    if (!definition.target.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "a chamfer needs a target feature");
    }
    if (definition.distanceParameter && !definition.distanceParameter->isValid()) {
        return makeError(ErrorCode::InvalidArgument, "the distance parameter ID must be valid");
    }
    using geometry::ChamferMode;
    if (definition.mode != ChamferMode::TwoDistance && definition.distance2 != Length{}) {
        return makeError(ErrorCode::InvalidArgument, "only a chamfer by two distances takes a second distance");
    }
    if (definition.mode != ChamferMode::DistanceAngle && definition.angle != Angle{}) {
        return makeError(ErrorCode::InvalidArgument, "only a chamfer by distance and angle takes an angle");
    }
    // The rest is the geometry request's contract. A driven distance is
    // checked when the parameter's value is known, at regeneration.
    const geometry::ChamferRequest request{
        .edges = definition.edges,
        .mode = definition.mode,
        .distance = definition.distanceParameter ? Length::fromSi(1.0) : definition.distance,
        .distance2 = definition.distance2,
        .angle = definition.angle,
        .referenceSide = definition.referenceSide,
    };
    return geometry::validate(request);
}

ChamferFeature::ChamferFeature(std::string name, const ChamferDefinition& definition)
    : SolidFeature(std::move(name)), definition_(definition) {}

Result<std::unique_ptr<ChamferFeature>> ChamferFeature::create(std::string name,
                                                               const ChamferDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    return std::unique_ptr<ChamferFeature>(new ChamferFeature(std::move(name), definition));
}

std::unique_ptr<DocumentObject> ChamferFeature::clone() const {
    return std::unique_ptr<DocumentObject>(new ChamferFeature(*this));
}

bool ChamferFeature::contentEquals(const DocumentObject& other) const {
    return definition_ == static_cast<const ChamferFeature&>(other).definition_;
}

std::vector<ObjectId> ChamferFeature::dependencies() const {
    std::vector<ObjectId> result{ObjectId{definition_.target}};
    if (definition_.distanceParameter) {
        result.push_back(ObjectId{*definition_.distanceParameter});
    }
    return result;
}

Result<bool> ChamferFeature::setDefinition(const ChamferDefinition& definition) {
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
