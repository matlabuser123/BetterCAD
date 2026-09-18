#include <bettercad/features/SweepFeature.hpp>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bettercad::features {

std::string_view toString(SweepOrientation orientation) noexcept {
    switch (orientation) {
    case SweepOrientation::FollowPath:
        return "follow path";
    }
    return "unknown";
}

namespace {

/// The edges of one run: at least one, each valid, and none listed twice
/// within the run. Entity IDs are numbered per sketch, so two runs on
/// different sketches may hold the same ID and mean different edges; only
/// repeats inside one run are a mistake. @p what names it in messages.
Result<void> validateRun(const SketchId sketch, const std::vector<EntityId>& edges, std::string_view what) {
    const auto invalid = [](const std::string& message) { return makeError(ErrorCode::InvalidArgument, message); };
    if (!sketch.isValid()) {
        return invalid(std::format("{} needs a sketch", what));
    }
    if (edges.empty()) {
        return invalid(std::format("{} needs at least one edge", what));
    }
    std::vector<EntityId> seen;
    for (const EntityId edge : edges) {
        if (!edge.isValid()) {
            return invalid(std::format("{}'s edge IDs must be valid", what));
        }
        // One edge of a run cannot be travelled twice.
        if (std::ranges::find(seen, edge) != seen.end()) {
            return invalid(std::format("{} is listed twice in the path", edge));
        }
        seen.push_back(edge);
    }
    return {};
}

/// A whole path: the first run, then each further one.
Result<void> validatePath(const SweepPath& path, std::string_view what, std::string_view runsCalled) {
    if (auto valid = validateRun(path.sketch, path.edges, what); !valid) {
        return valid;
    }
    for (std::size_t i = 0; i < path.runs.size(); ++i) {
        const std::string name = std::format("{} run {}", runsCalled, i + 2);
        if (auto valid = validateRun(path.runs[i].sketch, path.runs[i].edges, name); !valid) {
            return valid;
        }
    }
    return {};
}

} // namespace

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
    if (definition.path.edges.empty()) {
        return invalid("a sweep path needs at least one edge");
    }
    if (auto valid = validatePath(definition.path, "the path", "path"); !valid) {
        return valid;
    }
    if (!isFinite(definition.twist)) {
        return invalid(std::format("the twist must be finite, got {}", toString(definition.twist, units::deg)));
    }
    if (definition.twistParameter && !definition.twistParameter->isValid()) {
        return invalid("the twist parameter ID must be valid");
    }
    if (definition.guide) {
        const bool twisted = definition.twist != Angle{} || definition.twistParameter.has_value();
        if (twisted) {
            return invalid("a sweep takes a twist or a guide curve, not both: a guide already says how the section "
                           "turns");
        }
        if (auto valid = validatePath(*definition.guide, "the guide", "guide"); !valid) {
            return valid;
        }
        if (definition.guide->sketch == definition.profile) {
            return invalid("the guide must be in another sketch than the profile");
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
    const auto add = [&result](ObjectId id) {
        if (std::ranges::find(result, id) == result.end()) {
            result.push_back(id);
        }
    };
    for (const SweepPathRun& run : definition_.path.runs) {
        add(ObjectId{run.sketch});
    }
    if (definition_.guide) {
        add(ObjectId{definition_.guide->sketch});
        for (const SweepPathRun& run : definition_.guide->runs) {
            add(ObjectId{run.sketch});
        }
    }
    if (definition_.twistParameter) {
        add(ObjectId{*definition_.twistParameter});
    }
    if (definition_.target) {
        add(ObjectId{*definition_.target});
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
