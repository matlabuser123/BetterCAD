#include <bettercad/features/LoftFeature.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace bettercad::features {

namespace {

/// The same sketch moved the same way: a repeated section.
bool sameSection(const LoftSection& a, const LoftSection& b) {
    if (a.sketch != b.sketch || a.offsetParameter.has_value() != b.offsetParameter.has_value()) {
        return false;
    }
    return a.offsetParameter ? *a.offsetParameter == *b.offsetParameter : a.offset == b.offset;
}

} // namespace

std::string_view toString(LoftInterpolation interpolation) noexcept {
    switch (interpolation) {
    case LoftInterpolation::Ruled:
        return "ruled";
    }
    return "unknown";
}

Result<void> validate(const LoftDefinition& definition) {
    const auto invalid = [](const std::string& message) { return makeError(ErrorCode::InvalidArgument, message); };
    const std::vector<LoftSection>& sections = definition.sections;
    if (sections.size() < 2) {
        return invalid(std::format("a loft needs at least two sections, got {}", sections.size()));
    }
    for (std::size_t i = 0; i < sections.size(); ++i) {
        const LoftSection& section = sections[i];
        if (!section.sketch.isValid()) {
            return invalid(std::format("section {} needs a sketch", i + 1));
        }
        if (section.offsetParameter && !section.offsetParameter->isValid()) {
            return invalid(std::format("section {}'s offset parameter ID must be valid", i + 1));
        }
        if (!section.offsetParameter && !isFinite(section.offset)) {
            return invalid(std::format("section {}'s offset must be finite, got {:.6g} mm", i + 1,
                                       section.offset.in(units::mm)));
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (sameSection(sections[j], section)) {
                return invalid(std::format("section {} repeats section {}: the same sketch at the same offset", i + 1,
                                           j + 1));
            }
        }
    }
    return validateOperation(definition.operation, definition.target);
}

LoftFeature::LoftFeature(std::string name, const LoftDefinition& definition)
    : SolidFeature(std::move(name)), definition_(definition) {}

Result<std::unique_ptr<LoftFeature>> LoftFeature::create(std::string name, const LoftDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    return std::unique_ptr<LoftFeature>(new LoftFeature(std::move(name), definition));
}

std::unique_ptr<DocumentObject> LoftFeature::clone() const {
    return std::unique_ptr<DocumentObject>(new LoftFeature(*this));
}

bool LoftFeature::contentEquals(const DocumentObject& other) const {
    return definition_ == static_cast<const LoftFeature&>(other).definition_;
}

std::vector<ObjectId> LoftFeature::dependencies() const {
    std::vector<ObjectId> result;
    const auto add = [&](ObjectId id) {
        if (std::ranges::find(result, id) == result.end()) {
            result.push_back(id);
        }
    };
    for (const LoftSection& section : definition_.sections) {
        add(ObjectId{section.sketch});
    }
    for (const LoftSection& section : definition_.sections) {
        if (section.offsetParameter) {
            add(ObjectId{*section.offsetParameter});
        }
    }
    if (definition_.target) {
        add(ObjectId{*definition_.target});
    }
    return result;
}

Result<bool> LoftFeature::setDefinition(const LoftDefinition& definition) {
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
