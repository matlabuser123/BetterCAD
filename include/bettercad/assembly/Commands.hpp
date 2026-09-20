#pragma once

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Export.hpp>
#include <bettercad/assembly/Mate.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/document/Placement.hpp>

#include <memory>
#include <optional>
#include <string>

// Assembly edits as commands (P13-CMD-001).
//
// There is one command system and one history; these are commands in it, not
// a second mechanism. `core` already provides AddObjectCommand and
// DeleteObjectCommand, which work on any DocumentObject and so already create
// and delete components and mates with the same-ID guarantee -- and
// DeleteObjectCommand now also restores the configuration overrides a
// deletion clears, which it did not before this milestone.
//
// What core's generic commands cannot do is VALIDATE. assembly::
// createComponent() checks that the part exists and is a part;
// createMate() checks ADR-004's reference rules, that a mate relates two
// different components, and that its value has the right dimension. A raw
// AddObjectCommand bypasses all of it. So the commands here wrap the generic
// ones with that validation rather than replacing them: two ways to add a
// component, one checked and one not, ends with the wrong one being used.
//
// Every command mutates canonical intent only. Derived state -- solved
// transforms, solver diagnostics -- is recomputed by regeneration
// (P13-REGEN-001) and is never stored as undo payload: undoing to a
// placement means restoring the placement, not the position it solved to.
namespace bettercad::assembly {

/// Creates a component placing @p part, validated as createComponent() does.
///
/// Redo restores the same ComponentId, so a mate that named it before an undo
/// still names it after the redo.
class BETTERCAD_ASSEMBLY_EXPORT CreateComponentCommand final : public Command {
public:
    CreateComponentCommand(std::string name, ComponentDefinition definition);

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

    /// The component created; invalid before execute().
    [[nodiscard]] ComponentId componentId() const noexcept { return id_; }

private:
    std::string name_;
    ComponentDefinition definition_;
    ComponentId id_{};
    /// Held between undo and redo so that redo restores the SAME object,
    /// with the ID anything referring to it still names.
    std::unique_ptr<DocumentObject> kept_{};
};

/// Moves a component, by replacing its placement intent.
///
/// The placement is intent and the solve is derived (ADR-005), so this edits
/// where the engineer said the component sits -- not where it ended up.
class BETTERCAD_ASSEMBLY_EXPORT SetComponentPlacementCommand final : public Command {
public:
    SetComponentPlacementCommand(ComponentId component, ComponentPlacement placement);

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    ComponentId component_;
    ComponentPlacement placement_;
    std::optional<ComponentPlacement> before_{};
};

/// Creates a mate, validated as createMate() does.
class BETTERCAD_ASSEMBLY_EXPORT CreateMateCommand final : public Command {
public:
    CreateMateCommand(std::string name, MateDefinition definition);

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

    /// The mate created; invalid before execute().
    [[nodiscard]] MateId mateId() const noexcept { return id_; }

private:
    std::string name_;
    MateDefinition definition_;
    MateId id_{};
    /// Held between undo and redo so that redo restores the SAME object,
    /// with the ID anything referring to it still names.
    std::unique_ptr<DocumentObject> kept_{};
};

/// Edits a mate: its targets, its value, its kind, or its own suppression.
class BETTERCAD_ASSEMBLY_EXPORT SetMateDefinitionCommand final : public Command {
public:
    SetMateDefinitionCommand(MateId mate, MateDefinition definition);

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    MateId mate_;
    MateDefinition definition_;
    std::optional<MateDefinition> before_{};
};

/// Sets whether a component is suppressed in a configuration, or clears the
/// override so it takes its base state again (P13-CONF-001).
///
/// `suppressed` empty means "clear the override", which is a different thing
/// from setting it false: false says "in this build, present"; cleared says
/// "this build has no opinion".
class BETTERCAD_ASSEMBLY_EXPORT SuppressComponentCommand final : public Command {
public:
    SuppressComponentCommand(ConfigurationId configuration, ComponentId component,
                             std::optional<bool> suppressed);

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    ConfigurationId configuration_;
    ComponentId component_;
    std::optional<bool> suppressed_;
    std::optional<std::optional<bool>> before_{};
};

/// The same, for a mate.
class BETTERCAD_ASSEMBLY_EXPORT SuppressMateCommand final : public Command {
public:
    SuppressMateCommand(ConfigurationId configuration, MateId mate, std::optional<bool> suppressed);

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    ConfigurationId configuration_;
    MateId mate_;
    std::optional<bool> suppressed_;
    std::optional<std::optional<bool>> before_{};
};

} // namespace bettercad::assembly
