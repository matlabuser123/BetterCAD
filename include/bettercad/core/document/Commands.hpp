#pragma once

#include <bettercad/core/Export.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/parameters/Parameter.hpp>

#include <memory>
#include <optional>
#include <string>
#include <utility>

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

/// A value with its dimension, so that value changes stay dimension-checked
/// when the target parameter's dimension is only known at run time.
struct DimensionedValue {
    Dimension dimension{};
    double siValue = 0.0;

    template <Dimension D>
    [[nodiscard]] static constexpr DimensionedValue of(const Quantity<D>& value) noexcept {
        return {D, value.si()};
    }
};

/// Requested changes to a parameter; unset fields stay as they are.
/// Designed for designated initializers: ParameterChanges{.name = "w"}.
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

} // namespace bettercad
