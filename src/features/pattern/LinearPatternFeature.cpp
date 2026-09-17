#include <bettercad/features/LinearPatternFeature.hpp>

#include <bettercad/core/units/Format.hpp>

#include <algorithm>
#include <format>
#include <string>
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
    const bool total = direction.distribution == PatternDistribution::TotalLength;
    const std::string_view what = total ? "total length" : "spacing";
    if (!normalized(direction.direction)) {
        return invalid(
            std::format("the direction must be a finite, non-zero vector, got {}", format(direction.direction)));
    }
    if (!direction.countParameter && direction.count < 1) {
        return invalid(std::format("the count must be at least 1, got {}", direction.count));
    }
    if (!direction.spacingParameter && (!isFinite(direction.spacing) || !(direction.spacing > Length{}))) {
        return invalid(std::format("the {} must be positive and finite, got {}", what,
                                   toString(direction.spacing, units::mm)));
    }
    // The rules of the count that do not depend on a driven value
    // (P12-PATTERN-001).
    if (!direction.countParameter) {
        if (total && direction.count < 2) {
            return invalid(std::format("a total length needs at least 2 instances to divide it between, got {}",
                                       direction.count));
        }
        if (direction.symmetric && direction.count % 2 == 0) {
            return invalid(std::format("a symmetric direction needs an odd count, so the source is its middle "
                                       "instance, got {}",
                                       direction.count));
        }
    }
    return {};
}

} // namespace

Result<void> validate(const LinearPatternDefinition& definition) {
    if (!definition.source.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "a linear pattern needs a source feature");
    }
    if (auto valid = validateSuppressed(definition.suppressed, "linear pattern"); !valid) {
        return valid;
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

std::string_view toString(PatternDistribution distribution) noexcept {
    switch (distribution) {
    case PatternDistribution::Spacing:
        return "spacing";
    case PatternDistribution::TotalLength:
        return "total_length";
    }
    return "unknown";
}

double patternStepMultiple(std::size_t step, bool symmetric) noexcept {
    if (!symmetric || step == 0) {
        // The source is where it is; +0, never -0, which would reach the
        // file and the bit-for-bit comparisons as a different number.
        return static_cast<double>(step);
    }
    // 0, +1, -1, +2, -2, ...: the copies are numbered outward, the positive
    // side first, so raising the count keeps what an index means. A count of
    // 2n + 1 reaches +n and -n, spanning 2 n s centred on the source.
    const double away = static_cast<double>((step + 1) / 2);
    return step % 2 == 1 ? away : -away;
}

Result<void> validateSuppressed(const std::vector<std::uint32_t>& suppressed, std::string_view pattern) {
    for (std::size_t i = 0; i < suppressed.size(); ++i) {
        if (suppressed[i] == 0) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("a {} cannot suppress instance 0: it is the source itself", pattern));
        }
        if (i > 0 && suppressed[i] <= suppressed[i - 1]) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("a {}'s suppressed instances are listed once, by increasing index, got {} "
                                         "after {}",
                                         pattern, suppressed[i], suppressed[i - 1]));
        }
    }
    return {};
}

Result<void> checkSuppressedAgainstCount(const std::vector<std::uint32_t>& suppressed, std::size_t count,
                                         std::string_view pattern) {
    for (const std::uint32_t index : suppressed) {
        if (index >= count) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("a {} of {} instances has no instance {} to suppress", pattern, count,
                                         index));
        }
    }
    if (count > 1 && suppressed.size() + 1 == count) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("a {} cannot suppress every copy: {} of {} instances leaves the source alone",
                                     pattern, suppressed.size(), count));
    }
    return {};
}

std::vector<PatternInstance> patternInstances(const PatternStep& first, const std::optional<PatternStep>& second,
                                              const std::vector<std::uint32_t>& suppressed) {
    const std::size_t rows = second ? second->count : 1;
    std::vector<PatternInstance> instances;
    instances.reserve(first.count * rows);
    for (std::size_t j = 0; j < rows; ++j) {
        for (std::size_t i = 0; i < first.count; ++i) {
            // From the source: m(i) s1 d1 (+ m(j) s2 d2), each product
            // rounded once, so no offset depends on the instances before it.
            Translation3D offset = Translation3D::along(
                first.direction, first.spacing * patternStepMultiple(i, first.symmetric));
            if (second) {
                offset = offset + Translation3D::along(
                                      second->direction,
                                      second->spacing * patternStepMultiple(j, second->symmetric));
            }
            const std::size_t index = instances.size();
            instances.push_back({.suppressed = std::ranges::find(suppressed, static_cast<std::uint32_t>(index)) !=
                                               suppressed.end(),
                                 .index = index,
                                 .first = i,
                                 .second = j,
                                 .offset = offset});
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
