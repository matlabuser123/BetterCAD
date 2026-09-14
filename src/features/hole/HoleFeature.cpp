#include <bettercad/features/HoleFeature.hpp>

#include <bettercad/core/units/Format.hpp>

#include <cmath>
#include <format>
#include <numbers>
#include <string_view>
#include <utility>

namespace bettercad::features {

namespace {

bool validId(const std::optional<ParameterId>& id) {
    return !id || id->isValid();
}

bool positiveAndFinite(Length value) {
    return isFinite(value) && value > Length{};
}

std::string mm(Length value) {
    return toString(value, units::mm);
}

geometry::HoleRequest literalRequest(const HoleDefinition& d) {
    return {
        .face = d.face,
        .center = d.center,
        .type = d.type,
        .extent = d.extent,
        .diameter = d.diameter,
        .depth = d.depth,
        .counterboreDiameter = d.counterboreDiameter,
        .counterboreDepth = d.counterboreDepth,
        .countersinkDiameter = d.countersinkDiameter,
        .countersinkAngle = d.countersinkAngle,
    };
}

/// The checks of geometry::validate(HoleRequest) that do not involve a
/// driven value; the messages are the same. A head's diameter checked
/// without a literal hole diameter must at least be positive and finite.
Result<void> validateWithDrivenValues(const HoleDefinition& d) {
    using geometry::HoleExtent;
    using geometry::HoleType;
    const auto invalid = [](std::string message) { return makeError(ErrorCode::InvalidArgument, std::move(message)); };
    const auto checkHeadDiameter = [&](std::string_view head, Length value) -> Result<void> {
        if (!d.diameterParameter) {
            if (!isFinite(value) || !(value > d.diameter)) {
                return invalid(std::format("the {} diameter must be larger than the hole diameter ({}), got {}", head,
                                           mm(d.diameter), mm(value)));
            }
        } else if (!positiveAndFinite(value)) {
            return invalid(std::format("the {} diameter must be positive and finite, got {}", head, mm(value)));
        }
        return {};
    };
    if (auto face = geometry::validate(d.face); !face) {
        return invalid(std::format("placement face: {}", face.error().message));
    }
    if (!isFinite(d.center.x) || !isFinite(d.center.y)) {
        return invalid("the hole centre must be finite");
    }
    const bool literalDiameter = !d.diameterParameter;
    const bool blind = d.extent == HoleExtent::Blind;
    const bool literalDepth = blind && !d.depthParameter;
    if (literalDiameter && !positiveAndFinite(d.diameter)) {
        return invalid(std::format("the hole diameter must be positive and finite, got {}", mm(d.diameter)));
    }
    if (literalDepth && !positiveAndFinite(d.depth)) {
        return invalid(std::format("the hole depth must be positive and finite, got {}", mm(d.depth)));
    }
    if (!blind && d.depth != Length{}) {
        return invalid("a through hole takes no depth; it goes through all material");
    }
    const bool counterbore = d.type == HoleType::Counterbore;
    const bool countersink = d.type == HoleType::Countersink;
    if (!counterbore && (d.counterboreDiameter != Length{} || d.counterboreDepth != Length{})) {
        return invalid("only a counterbore hole takes counterbore dimensions");
    }
    if (!countersink && (d.countersinkDiameter != Length{} || d.countersinkAngle != Angle{})) {
        return invalid("only a countersink hole takes countersink dimensions");
    }
    if (counterbore) {
        if (auto head = checkHeadDiameter("counterbore", d.counterboreDiameter); !head) {
            return head;
        }
        if (!positiveAndFinite(d.counterboreDepth)) {
            return invalid(std::format("the counterbore depth must be positive and finite, got {}",
                                       mm(d.counterboreDepth)));
        }
        if (literalDepth && !(d.counterboreDepth < d.depth)) {
            return invalid(std::format("the counterbore ({} deep) must be shallower than the blind hole ({} deep)",
                                       mm(d.counterboreDepth), mm(d.depth)));
        }
    }
    if (countersink) {
        if (auto head = checkHeadDiameter("countersink", d.countersinkDiameter); !head) {
            return head;
        }
        const double angle = d.countersinkAngle.si();
        if (!std::isfinite(angle) || !(angle > 0.0) || !(angle < std::numbers::pi)) {
            return invalid(std::format("the countersink angle must be in (0, 180) deg, got {}",
                                       toString(d.countersinkAngle, units::deg)));
        }
        if (literalDiameter && literalDepth && !(geometry::countersinkDepth(literalRequest(d)) < d.depth)) {
            return invalid(std::format("the countersink ({:.6g} mm deep) must be shallower than the blind hole ({} "
                                       "deep)",
                                       geometry::countersinkDepth(literalRequest(d)).in(units::mm), mm(d.depth)));
        }
    }
    return {};
}

} // namespace

Result<void> validate(const HoleDefinition& definition) {
    if (!definition.target.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "a hole needs a target feature");
    }
    if (!validId(definition.diameterParameter) || !validId(definition.depthParameter) ||
        !validId(definition.centerUParameter) || !validId(definition.centerVParameter)) {
        return makeError(ErrorCode::InvalidArgument, "the hole's parameter IDs must be valid");
    }
    if (definition.extent == geometry::HoleExtent::Through && definition.depthParameter) {
        return makeError(ErrorCode::InvalidArgument, "a through hole takes no depth parameter");
    }
    // With every value literal, the geometry request's own contract applies
    // in full. Relations with a driven value wait for regeneration.
    if (!definition.diameterParameter && !definition.depthParameter) {
        return geometry::validate(literalRequest(definition));
    }
    return validateWithDrivenValues(definition);
}

HoleFeature::HoleFeature(std::string name, const HoleDefinition& definition)
    : SolidFeature(std::move(name)), definition_(definition) {}

Result<std::unique_ptr<HoleFeature>> HoleFeature::create(std::string name, const HoleDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    return std::unique_ptr<HoleFeature>(new HoleFeature(std::move(name), definition));
}

std::unique_ptr<DocumentObject> HoleFeature::clone() const {
    return std::unique_ptr<DocumentObject>(new HoleFeature(*this));
}

bool HoleFeature::contentEquals(const DocumentObject& other) const {
    return definition_ == static_cast<const HoleFeature&>(other).definition_;
}

std::vector<ObjectId> HoleFeature::dependencies() const {
    std::vector<ObjectId> result{ObjectId{definition_.target}};
    for (const auto& parameter : {definition_.diameterParameter, definition_.depthParameter,
                                  definition_.centerUParameter, definition_.centerVParameter}) {
        if (parameter) {
            result.push_back(ObjectId{*parameter});
        }
    }
    return result;
}

Result<bool> HoleFeature::setDefinition(const HoleDefinition& definition) {
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
