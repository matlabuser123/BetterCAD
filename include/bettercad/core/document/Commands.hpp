#pragma once

#include <bettercad/core/Export.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Configurations.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/parameters/Parameter.hpp>
#include <bettercad/core/units/DimensionedValue.hpp>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Core document commands. Modules that define object kinds add their own
// commands (for example CreateSketchCommand in the sketch module), typically
// built on AddObjectCommand.
namespace bettercad {

/// Creates a parameter; redo recreates it with the same ID.
class BETTERCAD_CORE_EXPORT CreateParameterCommand final : public Command {
public:
    template <Dimension D>
    CreateParameterCommand(std::string name, const Quantity<D>& value, const Unit<D>& displayUnit)
        : CreateParameterCommand(std::move(name), value.si(), describe(displayUnit)) {}
    CreateParameterCommand(std::string name, double siValue, const UnitDescriptor& displayUnit);

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

    /// ID of the created parameter; invalid before execute().
    [[nodiscard]] ParameterId parameterId() const noexcept;

private:
    std::string name_;
    double siValue_;
    UnitDescriptor displayUnit_;
    std::optional<Parameter> created_;
};

/// Requested changes to a parameter; unset fields stay as they are.
/// Designed for designated initializers: ParameterChanges{.name = "w"}.
///
/// A value is a DimensionedValue, so the change stays dimension-checked when
/// the parameter's dimension is only known at run time. The value of a
/// parameter driven by an expression can only change together with its
/// expression (see Document::setParameterSiValue()).
struct ParameterChanges {
    std::optional<std::string> name{};
    std::optional<DimensionedValue> value{};
    std::optional<UnitDescriptor> displayUnit{};
    /// Engaged with std::nullopt inside to clear the expression.
    std::optional<std::optional<std::string>> expression{};
};

/// Changes any combination of a parameter's name, value, display unit and
/// expression. All changes are validated before any is applied, so the edit
/// is atomic.
class BETTERCAD_CORE_EXPORT ModifyParameterCommand final : public Command {
public:
    ModifyParameterCommand(ParameterId id, ParameterChanges changes);

    template <Dimension D>
    [[nodiscard]] static std::unique_ptr<ModifyParameterCommand> setValue(ParameterId id,
                                                                          const Quantity<D>& value) {
        ParameterChanges changes;
        changes.value = DimensionedValue::of(value);
        return std::make_unique<ModifyParameterCommand>(id, std::move(changes));
    }

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    ParameterId id_;
    ParameterChanges changes_;
    std::optional<Parameter> before_;
    std::optional<Parameter> after_;
};

/// Adds a new object (sketch, feature, ...); redo re-inserts it with the same ID.
class BETTERCAD_CORE_EXPORT AddObjectCommand final : public Command {
public:
    /// @param object  New object without an ID; the command keeps it as a prototype.
    explicit AddObjectCommand(std::unique_ptr<DocumentObject> object);

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

    /// ID of the added object; invalid before execute().
    [[nodiscard]] ObjectId objectId() const noexcept { return id_; }

private:
    std::unique_ptr<DocumentObject> prototype_;
    std::unique_ptr<DocumentObject> removed_;
    ObjectId id_;
};

/// Deletes a parameter or an object; undo restores it with the same ID and name.
class BETTERCAD_CORE_EXPORT DeleteObjectCommand final : public Command {
public:
    explicit DeleteObjectCommand(ObjectId id) noexcept : id_(id) {}

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    ObjectId id_;
    std::string name_;
    std::optional<Parameter> parameter_;
    std::unique_ptr<DocumentObject> object_;
    /// What the configurations said about this object before it went. The
    /// deletion clears it; undo must put it back, or the object returns and
    /// the intent about it does not (P13-CMD-001).
    ObjectOverrides overrides_;
};

/// Renames a parameter or an object.
class BETTERCAD_CORE_EXPORT RenameObjectCommand final : public Command {
public:
    RenameObjectCommand(ObjectId id, std::string newName);

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    ObjectId id_;
    std::string newName_;
    std::optional<std::string> oldName_;
};

/// Creates a configuration; redo recreates it with the same ID and
/// overrides (P12-PARAM-002).
class BETTERCAD_CORE_EXPORT CreateConfigurationCommand final : public Command {
public:
    explicit CreateConfigurationCommand(std::string name);

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

    /// ID of the created configuration; invalid before execute().
    [[nodiscard]] ConfigurationId configurationId() const noexcept;

private:
    std::string name_;
    std::optional<Configuration> created_;
};

/// Requested changes to a configuration; unset fields stay as they are.
/// Designed for designated initializers:
/// ConfigurationChanges{.overrides = {{width, DimensionedValue::of(80_mm)}}}.
struct ConfigurationChanges {
    std::optional<std::string> name{};
    /// Overrides to set, replacing any the configuration has for those
    /// parameters. Parameters not named here keep the overrides they have.
    std::map<ParameterId, DimensionedValue> overrides{};
    /// Overrides to remove, so those parameters take their base values.
    std::vector<ParameterId> cleared{};
};

/// Renames a configuration and edits its overrides. Undo restores the whole
/// configuration, so a failed edit leaves nothing half applied.
class BETTERCAD_CORE_EXPORT ModifyConfigurationCommand final : public Command {
public:
    ModifyConfigurationCommand(ConfigurationId id, ConfigurationChanges changes);

    /// Sets one override.
    template <Dimension D>
    [[nodiscard]] static std::unique_ptr<ModifyConfigurationCommand>
    setOverride(ConfigurationId id, ParameterId parameter, const Quantity<D>& value) {
        return std::make_unique<ModifyConfigurationCommand>(
            id, ConfigurationChanges{.overrides = {{parameter, DimensionedValue::of(value)}}});
    }

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    ConfigurationId id_;
    ConfigurationChanges changes_;
    std::optional<Configuration> before_;
    std::optional<Configuration> after_;
};

/// Deletes a configuration; undo restores it with the same ID, overrides and
/// place as the active one.
class BETTERCAD_CORE_EXPORT DeleteConfigurationCommand final : public Command {
public:
    explicit DeleteConfigurationCommand(ConfigurationId id) noexcept : id_(id) {}

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    ConfigurationId id_;
    std::optional<Configuration> removed_;
    bool wasActive_ = false;
};

/// Selects the configuration whose overrides are in force; std::nullopt is
/// the base configuration.
class BETTERCAD_CORE_EXPORT SetActiveConfigurationCommand final : public Command {
public:
    explicit SetActiveConfigurationCommand(std::optional<ConfigurationId> id) noexcept : id_(id) {}

    [[nodiscard]] std::string description() const override;
    [[nodiscard]] Result<void> execute(Document& document) override;
    [[nodiscard]] Result<void> undo(Document& document) override;
    [[nodiscard]] Result<void> redo(Document& document) override;

private:
    std::optional<ConfigurationId> id_;
    std::optional<std::optional<ConfigurationId>> before_;
};

} // namespace bettercad
