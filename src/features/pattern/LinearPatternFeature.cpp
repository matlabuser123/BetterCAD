#include <bettercad/features/LinearPatternFeature.hpp>

#include <bettercad/core/units/Format.hpp>

#include <format>
#include <utility>

namespace bettercad::features {

namespace {

bool validId(const std::optional<ParameterId>& id) {
    return !id || id->isValid();
}

/// Negative zero (as in a reversed vector) as 0, for messages.
double tidy(double value) {
    return value == 0.0 ? 0.0 : value;
}

std::string format(const Vector3D& v) {
    return std::format("({:.6g}, {:.6g}, {:.6g})", tidy(v.x), tidy(v.y), tidy(v.z));
}

std::optional<Direction3D> normalized(const Vector3D& v) {
    return Direction3D::fromComponents(v.x, v.y, v.z);
}

Result<void> validateDirection(const PatternDirection& direction, std::string_view label) {
    const auto invalid = [&](const std::string& message) {
        return makeError(ErrorCode::InvalidArgument, std::format("{}: {}", label, message));
    };
    if (!normalized(direction.direction)) {
        return invalid(
            std::format("the direction must be a finite, non-zero vector, got {}", format(direction.direction)));
    }
    if (!direction.countParameter && direction.count < 1) {
        return invalid(std::format("the count must be at least 1, got {}", direction.count));
    }
    if (!direction.spacingParameter && (!isFinite(direction.spacing) || !(direction.spacing > Length{}))) {
        return invalid(std::format("the spacing must be positive and finite, got {}",
                                   toString(direction.spacing, units::mm)));
    }
    return {};
}

} // namespace

Result<void> validate(const LinearPatternDefinition& definition) {
    if (!definition.source.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "a linear pattern needs a source feature");
    }
    const auto directions = {&definition.first, definition.second ? &*definition.second : nullptr};
    for (const PatternDirection* direction : directions) {
        if (direction != nullptr && (!validId(direction->countParameter) || !validId(direction->spacingParameter))) {
            return makeError(ErrorCode::InvalidArgument, "the pattern's parameter IDs must be valid");
        }
    }
    const PatternDirection& first = definition.first;
    if (auto valid = validateDirection(first, "direction 1"); !valid) {
        return valid;
    }
    const PatternDirection* second = definition.second ? &*definition.second : nullptr;
    if (second != nullptr) {
        if (auto valid = validateDirection(*second, "direction 2"); !valid) {
            return valid;
        }
        if (!normalized(first.direction)->cross(*normalized(second->direction))) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("the two directions must not be parallel, got {} and {}",
                                         format(first.direction), format(second->direction)));
        }
    }
    // The instances the literal counts give; a driven count adds at least 1.
    const bool driven = first.countParameter || (second != nullptr && second->countParameter);
    const std::uint64_t along1 = first.countParameter ? 1 : first.count;
    const std::uint64_t along2 = second == nullptr || second->countParameter ? 1 : second->count;
    if (along1 * along2 > kMaxPatternInstances) {
        const std::string count = driven             ? std::format("at least {}", along1 * along2)
                                  : second != nullptr ? std::format("{} ({} x {})", along1 * along2, along1, along2)
                                                      : std::format("{}", along1);
        return makeError(ErrorCode::InvalidArgument, std::format("a linear pattern may have at most {} instances, got {}",
                                                                 kMaxPatternInstances, count));
    }
    return {};
}

std::vector<PatternInstance> patternInstances(const PatternStep& first, const std::optional<PatternStep>& second) {
    const std::size_t rows = second ? second->count : 1;
    std::vector<PatternInstance> instances;
    instances.reserve(first.count * rows);
    for (std::size_t j = 0; j < rows; ++j) {
        for (std::size_t i = 0; i < first.count; ++i) {
            // From the source: i s1 d1 (+ j s2 d2), each product rounded once.
            Translation3D offset = Translation3D::along(first.direction, first.spacing * static_cast<double>(i));
            if (second) {
                offset = offset + Translation3D::along(second->direction, second->spacing * static_cast<double>(j));
            }
            instances.push_back({.index = instances.size(), .first = i, .second = j, .offset = offset});
        }
    }
    return instances;
}

LinearPatternFeature::LinearPatternFeature(std::string name, const LinearPatternDefinition& definition)
    : SolidFeature(std::move(name)), definition_(definition) {}

Result<std::unique_ptr<LinearPatternFeature>> LinearPatternFeature::create(std::string name,
                                                                           const LinearPatternDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    return std::unique_ptr<LinearPatternFeature>(new LinearPatternFeature(std::move(name), definition));
}

std::unique_ptr<DocumentObject> LinearPatternFeature::clone() const {
    return std::unique_ptr<DocumentObject>(new LinearPatternFeature(*this));
}

bool LinearPatternFeature::contentEquals(const DocumentObject& other) const {
    return definition_ == static_cast<const LinearPatternFeature&>(other).definition_;
}

std::vector<ObjectId> LinearPatternFeature::dependencies() const {
    std::vector<ObjectId> result{ObjectId{definition_.source}};
    const auto add = [&](const PatternDirection& direction) {
        for (const auto& parameter : {direction.countParameter, direction.spacingParameter}) {
            if (parameter) {
                result.push_back(ObjectId{*parameter});
            }
        }
    };
    add(definition_.first);
    if (definition_.second) {
        add(*definition_.second);
    }
    return result;
}

Result<bool> LinearPatternFeature::setDefinition(const LinearPatternDefinition& definition) {
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
