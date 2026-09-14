#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/FeatureCommands.hpp>

#include <format>
#include <utility>

namespace bettercad::features {

// --- CreateExtrudeCommand ------------------------------------------------------------

CreateExtrudeCommand::CreateExtrudeCommand(std::string name, const ExtrudeDefinition& definition)
    : name_(std::move(name)), definition_(definition) {}

std::string CreateExtrudeCommand::description() const {
    return std::format("Create extrude '{}'", name_);
}

FeatureId CreateExtrudeCommand::featureId() const noexcept {
    return add_ ? FeatureId::fromValue(add_->objectId().value()) : FeatureId{};
}

Result<void> CreateExtrudeCommand::execute(Document& document) {
    auto feature = ExtrudeFeature::create(name_, definition_);
    if (!feature) {
        return std::unexpected(feature.error());
    }
    add_.emplace(std::move(*feature));
    auto executed = add_->execute(document);
    if (!executed) {
        add_.reset();
    }
    return executed;
}

Result<void> CreateExtrudeCommand::undo(Document& document) {
    if (!add_) {
        return makeError(ErrorCode::FailedPrecondition, "cannot undo: the command has not been executed");
    }
    return add_->undo(document);
}

Result<void> CreateExtrudeCommand::redo(Document& document) {
    if (!add_) {
        return makeError(ErrorCode::FailedPrecondition, "cannot redo: the command has not been executed");
    }
    return add_->redo(document);
}

// --- ModifyExtrudeCommand ------------------------------------------------------------

ModifyExtrudeCommand::ModifyExtrudeCommand(FeatureId feature, const ExtrudeDefinition& definition)
    : feature_(feature), after_(definition) {}

std::string ModifyExtrudeCommand::description() const {
    return std::format("Modify {}", feature_);
}

Result<void> ModifyExtrudeCommand::apply(Document& document, const ExtrudeDefinition& definition) {
    auto changed = document.modifyObject<ExtrudeFeature>(
        ObjectId{feature_}, [&](ExtrudeFeature& feature) { return feature.setDefinition(definition); });
    if (!changed) {
        return std::unexpected(changed.error());
    }
    return {};
}

Result<void> ModifyExtrudeCommand::execute(Document& document) {
    const auto* feature = document.findObjectAs<ExtrudeFeature>(ObjectId{feature_});
    if (feature == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("{} is not an extrude feature", feature_));
    }
    const ExtrudeDefinition before = feature->definition();
    if (auto applied = apply(document, after_); !applied) {
        return applied;
    }
    before_ = before;
    return {};
}

Result<void> ModifyExtrudeCommand::undo(Document& document) {
    if (!before_) {
        return makeError(ErrorCode::FailedPrecondition, "cannot undo: the command has not been executed");
    }
    return apply(document, *before_);
}

Result<void> ModifyExtrudeCommand::redo(Document& document) {
    return apply(document, after_);
}

} // namespace bettercad::features
