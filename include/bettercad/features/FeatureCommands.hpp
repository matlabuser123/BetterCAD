#pragma once

#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Commands.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Export.hpp>

#include <optional>
#include <string>

namespace bettercad::features {

/// Creates an extrude feature; redo recreates it with the same ID.
class BETTERCAD_FEATURES_EXPORT CreateExtrudeCommand final : public Command {
public:
    CreateExtrudeCommand(std::string name, const ExtrudeDefinition& definition);

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

    /// ID of the created feature; invalid before execute().
    [[nodiscard]] FeatureId featureId() const noexcept;

private:
    std::string name_;
    ExtrudeDefinition definition_;
    std::optional<AddObjectCommand> add_;
};

/// Replaces the definition of an extrude feature (depth, direction, ...).
class BETTERCAD_FEATURES_EXPORT ModifyExtrudeCommand final : public Command {
public:
    ModifyExtrudeCommand(FeatureId feature, const ExtrudeDefinition& definition);

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    Result<void> apply(Document& document, const ExtrudeDefinition& definition);

    FeatureId feature_;
    ExtrudeDefinition after_;
    std::optional<ExtrudeDefinition> before_;
};

} // namespace bettercad::features
