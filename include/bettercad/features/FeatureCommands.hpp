#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/CircularPatternFeature.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/FilletFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/LoftFeature.hpp>
#include <bettercad/features/MirrorFeature.hpp>
#include <bettercad/features/RevolveFeature.hpp>
#include <bettercad/features/SweepFeature.hpp>

#include <format>
#include <optional>
#include <string>
#include <utility>

// Undoable creation and editing of features. One implementation serves every
// feature kind F that provides:
//   using Definition = ...;
//   static constexpr std::string_view kTypeName = "...";
//   static Result<std::unique_ptr<F>> create(std::string name, const Definition&);
//   const Definition& definition() const;
//   Result<bool> setDefinition(const Definition&);
namespace bettercad::features {

/// Creates a feature; redo recreates it with the same ID.
template <typename F>
class CreateFeatureCommand final : public Command {
public:
    using Definition = typename F::Definition;

    CreateFeatureCommand(std::string name, const Definition& definition)
        : name_(std::move(name)), definition_(definition) {}

    [[nodiscard]] std::string description() const override {
        return std::format("Create {} '{}'", F::kTypeName, name_);
    }

    [[nodiscard]] Result<void> execute(Document& document) override {
        auto feature = F::create(name_, definition_);
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

    [[nodiscard]] Result<void> undo(Document& document) override {
        if (!add_) {
            return makeError(ErrorCode::FailedPrecondition, "cannot undo: the command has not been executed");
        }
        return add_->undo(document);
    }

    [[nodiscard]] Result<void> redo(Document& document) override {
        if (!add_) {
            return makeError(ErrorCode::FailedPrecondition, "cannot redo: the command has not been executed");
        }
        return add_->redo(document);
    }

    /// ID of the created feature; invalid before execute().
    [[nodiscard]] FeatureId featureId() const noexcept {
        return add_ ? FeatureId::fromValue(add_->objectId().value()) : FeatureId{};
    }

private:
    std::string name_;
    Definition definition_;
    std::optional<AddObjectCommand> add_;
};

/// Replaces the definition of a feature (depth, angle, direction, ...).
template <typename F>
class ModifyFeatureCommand final : public Command {
public:
    using Definition = typename F::Definition;

    ModifyFeatureCommand(FeatureId feature, const Definition& definition) : feature_(feature), after_(definition) {}

    [[nodiscard]] std::string description() const override { return std::format("Modify {}", feature_); }

    [[nodiscard]] Result<void> execute(Document& document) override {
        const auto* feature = document.findObjectAs<F>(ObjectId{feature_});
        if (feature == nullptr) {
            return makeError(ErrorCode::NotFound,
                             std::format("{} is not a feature of type '{}'", feature_, F::kTypeName));
        }
        const Definition before = feature->definition();
        if (auto applied = apply(document, after_); !applied) {
            return applied;
        }
        before_ = before;
        return {};
    }

    [[nodiscard]] Result<void> undo(Document& document) override {
        if (!before_) {
            return makeError(ErrorCode::FailedPrecondition, "cannot undo: the command has not been executed");
        }
        return apply(document, *before_);
    }

    [[nodiscard]] Result<void> redo(Document& document) override { return apply(document, after_); }

private:
    Result<void> apply(Document& document, const Definition& definition) {
        auto changed = document.modifyObject<F>(ObjectId{feature_},
                                                [&](F& feature) { return feature.setDefinition(definition); });
        if (!changed) {
            return std::unexpected(changed.error());
        }
        return {};
    }

    FeatureId feature_;
    Definition after_;
    std::optional<Definition> before_;
};

using CreateExtrudeCommand = CreateFeatureCommand<ExtrudeFeature>;
using ModifyExtrudeCommand = ModifyFeatureCommand<ExtrudeFeature>;
using CreateRevolveCommand = CreateFeatureCommand<RevolveFeature>;
using ModifyRevolveCommand = ModifyFeatureCommand<RevolveFeature>;
using CreateChamferCommand = CreateFeatureCommand<ChamferFeature>;
using ModifyChamferCommand = ModifyFeatureCommand<ChamferFeature>;
using CreateFilletCommand = CreateFeatureCommand<FilletFeature>;
using ModifyFilletCommand = ModifyFeatureCommand<FilletFeature>;
using CreateHoleCommand = CreateFeatureCommand<HoleFeature>;
using ModifyHoleCommand = ModifyFeatureCommand<HoleFeature>;
using CreateLinearPatternCommand = CreateFeatureCommand<LinearPatternFeature>;
using ModifyLinearPatternCommand = ModifyFeatureCommand<LinearPatternFeature>;
using CreateCircularPatternCommand = CreateFeatureCommand<CircularPatternFeature>;
using ModifyCircularPatternCommand = ModifyFeatureCommand<CircularPatternFeature>;
using CreateMirrorCommand = CreateFeatureCommand<MirrorFeature>;
using ModifyMirrorCommand = ModifyFeatureCommand<MirrorFeature>;
using CreateSweepCommand = CreateFeatureCommand<SweepFeature>;
using ModifySweepCommand = ModifyFeatureCommand<SweepFeature>;
using CreateLoftCommand = CreateFeatureCommand<LoftFeature>;
using ModifyLoftCommand = ModifyFeatureCommand<LoftFeature>;

} // namespace bettercad::features
