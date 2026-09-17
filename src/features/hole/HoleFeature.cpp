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

/// The diameter the definition gives the hole unless a parameter drives it:
/// a thread's basic minor diameter, a standard clearance or the literal
/// value.
std::optional<Length> literalDiameter(const HoleDefinition& d) {
    if (d.thread) {
        return standards::basicDiameters(d.thread->size).minor;
    }
    if (d.clearance) {
        return standards::clearanceHoleDiameter(d.clearance->bolt, d.clearance->series);
    }
    if (d.diameterParameter) {
        return std::nullopt;
    }
    return d.diameter;
}

/// The request with the literal values; a driven thread length is left out
/// (the whole hole), and a driven diameter is the literal field.
geometry::HoleRequest literalRequest(const HoleDefinition& d) {
    geometry::HoleRequest request{
        .face = d.face,
        .center = d.center,
        .type = d.type,
        .extent = d.extent,
        .diameter = literalDiameter(d).value_or(d.diameter),
        .depth = d.depth,
        .counterboreDiameter = d.counterboreDiameter,
        .counterboreDepth = d.counterboreDepth,
        .countersinkDiameter = d.countersinkDiameter,
        .countersinkAngle = d.countersinkAngle,
        .spotfaceDiameter = d.spotfaceDiameter,
        .spotfaceDepth = d.spotfaceDepth,
    };
    if (d.thread) {
        request.thread = geometry::CosmeticThread{
            .majorDiameter = d.thread->size.diameter(),
            .length = d.thread->lengthParameter ? Length{} : d.thread->length,
        };
    }
    return request;
}

/// The checks of geometry::validate(HoleRequest) that do not involve a
/// driven value; the messages are the same. A head's diameter checked
/// without a literal hole diameter must at least be positive and finite.
Result<void> validateWithDrivenValues(const HoleDefinition& d) {
    using geometry::HoleExtent;
    using geometry::HoleType;
    const auto invalid = [](std::string message) { return makeError(ErrorCode::InvalidArgument, std::move(message)); };
    const std::optional<Length> diameter = literalDiameter(d);
    const auto checkHeadDiameter = [&](std::string_view head, Length value) -> Result<void> {
        if (diameter) {
            if (!isFinite(value) || !(value > *diameter)) {
                return invalid(std::format("the {} diameter must be larger than the hole diameter ({}), got {}", head,
                                           mm(*diameter), mm(value)));
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
    const bool blind = d.extent == HoleExtent::Blind;
    const bool literalDepth = blind && !d.depthParameter;
    if (diameter && !positiveAndFinite(*diameter)) {
        return invalid(std::format("the hole diameter must be positive and finite, got {}", mm(*diameter)));
    }
    if (literalDepth && !positiveAndFinite(d.depth)) {
        return invalid(std::format("the hole depth must be positive and finite, got {}", mm(d.depth)));
    }
    if (!blind && d.depth != Length{}) {
        return invalid("a through hole takes no depth; it goes through all material");
    }
    const bool counterbore = d.type == HoleType::Counterbore;
    const bool countersink = d.type == HoleType::Countersink;
    const bool spotface = d.type == HoleType::Spotface;
    if (!counterbore && (d.counterboreDiameter != Length{} || d.counterboreDepth != Length{})) {
        return invalid("only a counterbore hole takes counterbore dimensions");
    }
    if (!countersink && (d.countersinkDiameter != Length{} || d.countersinkAngle != Angle{})) {
        return invalid("only a countersink hole takes countersink dimensions");
    }
    if (!spotface && (d.spotfaceDiameter != Length{} || d.spotfaceDepth != Length{})) {
        return invalid("only a spotface hole takes spotface dimensions");
    }
    const auto checkCylindricalHead = [&](std::string_view head, Length headDiameter, Length depth) -> Result<void> {
        if (auto valid = checkHeadDiameter(head, headDiameter); !valid) {
            return valid;
        }
        if (!positiveAndFinite(depth)) {
            return invalid(std::format("the {} depth must be positive and finite, got {}", head, mm(depth)));
        }
        if (literalDepth && !(depth < d.depth)) {
            return invalid(std::format("the {} ({} deep) must be shallower than the blind hole ({} deep)", head,
                                       mm(depth), mm(d.depth)));
        }
        return {};
    };
    if (counterbore) {
        if (auto head = checkCylindricalHead("counterbore", d.counterboreDiameter, d.counterboreDepth); !head) {
            return head;
        }
    }
    if (spotface) {
        if (auto head = checkCylindricalHead("spotface", d.spotfaceDiameter, d.spotfaceDepth); !head) {
            return head;
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
        if (diameter && literalDepth && !(geometry::countersinkDepth(literalRequest(d)) < d.depth)) {
            return invalid(std::format("the countersink ({:.6g} mm deep) must be shallower than the blind hole ({} "
                                       "deep)",
                                       geometry::countersinkDepth(literalRequest(d)).in(units::mm), mm(d.depth)));
        }
    }
    if (d.thread) {
        // A threaded hole's diameter is literal, so only its depth is driven
        // here; the thread's relations to the depth wait for regeneration.
        geometry::HoleRequest request = literalRequest(d);
        request.extent = HoleExtent::Through;
        request.depth = Length{};
        if (auto valid = geometry::validate(request); !valid) {
            return valid;
        }
    }
    return {};
}

/// The checks of the standard parts of a hole (P12-HOLE-001): a thread or a
/// clearance size, which gives the diameter, and the tolerance classes.
Result<void> validateStandards(const HoleDefinition& d) {
    const auto invalid = [](std::string message) { return makeError(ErrorCode::InvalidArgument, std::move(message)); };
    if (d.thread && d.clearance) {
        return invalid("a hole takes a thread or a clearance size, not both");
    }
    if (d.thread || d.clearance) {
        if (d.diameter != Length{} || d.diameterParameter) {
            return invalid(d.thread ? "a threaded hole's diameter comes from its thread; it takes no diameter"
                                    : "a standard clearance hole's diameter comes from ISO 273; it takes no diameter");
        }
    }
    if (d.thread) {
        if (d.tolerance) {
            return invalid(std::format("a threaded hole takes no tolerance class; its thread has one ({})",
                                       standards::toString(d.thread->tolerance)));
        }
        if (auto limits = standards::internalThreadLimits(d.thread->size, d.thread->tolerance); !limits) {
            return std::unexpected(limits.error());
        }
    }
    if (d.tolerance) {
        if (auto valid = standards::validate(*d.tolerance); !valid) {
            return valid;
        }
        if (const std::optional<Length> diameter = literalDiameter(d);
            diameter && positiveAndFinite(*diameter)) {
            if (auto deviations = standards::limitDeviations(*diameter, *d.tolerance); !deviations) {
                return std::unexpected(deviations.error());
            }
        }
    }
    return {};
}

} // namespace

Result<void> validate(const HoleDefinition& definition) {
    if (!definition.target.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "a hole needs a target feature");
    }
    const std::optional<ParameterId> threadLength =
        definition.thread ? definition.thread->lengthParameter : std::optional<ParameterId>{};
    if (!validId(definition.diameterParameter) || !validId(definition.depthParameter) ||
        !validId(definition.centerUParameter) || !validId(definition.centerVParameter) || !validId(threadLength)) {
        return makeError(ErrorCode::InvalidArgument, "the hole's parameter IDs must be valid");
    }
    if (definition.extent == geometry::HoleExtent::Through && definition.depthParameter) {
        return makeError(ErrorCode::InvalidArgument, "a through hole takes no depth parameter");
    }
    if (auto standard = validateStandards(definition); !standard) {
        return standard;
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
    const std::optional<ParameterId> threadLength =
        definition_.thread ? definition_.thread->lengthParameter : std::optional<ParameterId>{};
    for (const auto& parameter : {definition_.diameterParameter, definition_.depthParameter,
                                  definition_.centerUParameter, definition_.centerVParameter, threadLength}) {
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
