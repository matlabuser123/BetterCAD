#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Export.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Configurations.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/parameters/ParameterTable.hpp>
#include <bettercad/core/units/DimensionedValue.hpp>

#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bettercad {

/// Descriptive information about a document; not geometry.
struct DocumentMetadata {
    std::string description{};
    std::string author{};
    std::map<std::string, std::string> properties{};

    friend bool operator==(const DocumentMetadata&, const DocumentMetadata&) = default;
};

/// A CAD document: parameters, objects (sketches, features, bodies) and
/// metadata, plus identity and change tracking. There is no global document
/// state; everything lives in Document instances.
///
/// - IDs of parameters and objects come from one allocator and are never
///   reused.
/// - Names are unique across parameters and objects, so lookup by name is
///   unambiguous.
/// - revision() increments on every effective change. isDirty() reports
///   changes since the last markClean().
/// - All changes go through Document methods; contained items are exposed
///   only as const. Failed operations leave the document unchanged.
class BETTERCAD_CORE_EXPORT Document {
public:
    /// New document with a random DocumentId.
    explicit Document(std::string name = "Untitled");
    /// Document with a known identity (loading, tests).
    Document(DocumentId id, std::string name);

    Document(Document&&) noexcept;
    Document& operator=(Document&&) noexcept;
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    ~Document();

    /// Deep copy with identical identity and content.
    [[nodiscard]] Document clone() const;

    // --- Identity and metadata --------------------------------------------
    [[nodiscard]] DocumentId id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    /// Document names are free text but must be non-empty and have no control characters.
    Result<bool> setName(std::string name);
    [[nodiscard]] const DocumentMetadata& metadata() const noexcept { return metadata_; }
    Result<bool> setMetadata(DocumentMetadata metadata);

    // --- Parameters -------------------------------------------------------
    [[nodiscard]] const ParameterTable& parameters() const noexcept { return parameters_; }

    template <Dimension D>
    Result<ParameterId> createParameter(std::string name, const Quantity<D>& value,
                                        const Unit<D>& displayUnit) {
        return createParameter(std::move(name), value.si(), describe(displayUnit));
    }
    Result<ParameterId> createParameter(std::string name, double siValue,
                                        const UnitDescriptor& displayUnit);
    /// Inserts a parameter that already has an ID (undo/redo, loading). Its
    /// expression, if any, must parse; its names need not exist yet.
    Result<void> insertParameter(Parameter parameter);
    Result<Parameter> removeParameter(ParameterId id);

    /// Sets the value of a parameter. A driven parameter (one with an
    /// expression) is refused with FailedPrecondition: its value comes from
    /// its expression (see ParameterExpressions.hpp).
    template <Dimension D>
    Result<bool> setParameterValue(ParameterId id, const Quantity<D>& value) {
        return setParameterSiValue(id, D, value.si());
    }
    Result<bool> setParameterSiValue(ParameterId id, Dimension dimension, double siValue);
    Result<bool> setParameterValue(ParameterId id, double value, const UnitDescriptor& unit);
    Result<bool> setParameterDisplayUnit(ParameterId id, const UnitDescriptor& unit);
    /// Sets or clears (std::nullopt) the expression that drives a parameter.
    /// The text must parse (ParseError or InvalidArgument otherwise; see
    /// Expression::parse()). Its names are resolved, and its value computed,
    /// when expressions are evaluated: evaluateParameterExpressions(), which
    /// regeneration runs. Until then the parameter keeps its value.
    ///
    /// Fails with FailedPrecondition if a configuration overrides the
    /// parameter: a driven parameter's value comes from its expression, so
    /// it cannot also be set by a configuration. Clear the overrides first
    /// (P12-PARAM-002).
    Result<bool> setParameterExpression(ParameterId id, std::optional<std::string> expression);
    /// Makes the parameter's name, value, unit and expression equal to @p state
    /// (same ID). Used by undo/redo; revisions keep increasing. The state's
    /// expression must parse.
    Result<bool> restoreParameter(const Parameter& state);

    // --- Configurations ---------------------------------------------------
    // A configuration overrides the values of free parameters; the equations
    // and features are shared (P12-PARAM-002, see Configurations.hpp).
    //
    // A parameter's own value is its *base* value and no configuration
    // changes it, so setParameterValue() always edits the base. What the
    // model is built from is effectiveParameterValue(), the base with the
    // active configuration's override applied.

    [[nodiscard]] const ConfigurationTable& configurations() const noexcept { return configurations_; }

    /// New configuration with no overrides; its name must be free.
    Result<ConfigurationId> createConfiguration(std::string name);
    /// Inserts a configuration that already has an ID (undo/redo, loading).
    /// Every parameter it overrides must exist, be free and have the
    /// override's dimension.
    Result<void> insertConfiguration(Configuration configuration);
    Result<Configuration> removeConfiguration(ConfigurationId id);

    /// Sets the value @p parameter takes in @p configuration. Fails with
    /// NotFound if either does not exist, FailedPrecondition if the parameter
    /// is driven by an expression, and DimensionMismatch if the value is not
    /// of the parameter's dimension.
    Result<bool> setConfigurationOverride(ConfigurationId configuration, ParameterId parameter,
                                          const DimensionedValue& value);
    template <Dimension D>
    Result<bool> setConfigurationOverride(ConfigurationId configuration, ParameterId parameter,
                                          const Quantity<D>& value) {
        return setConfigurationOverride(configuration, parameter, DimensionedValue::of(value));
    }
    /// Removes an override, so the parameter takes its base value again.
    Result<bool> clearConfigurationOverride(ConfigurationId configuration, ParameterId parameter);

    /// Sets whether a component or a mate is suppressed while @p configuration
    /// is active (P13-CONF-001, ADR-007). Fails with NotFound if the
    /// configuration, or an object with that ID, does not exist.
    ///
    /// That the object is a *component*, or a *mate*, is checked by the
    /// assembly module: a document knows its objects, not what a mate is.
    /// Use assembly::suppressComponent() and assembly::suppressMate().
    Result<bool> setConfigurationSuppression(ConfigurationId configuration, ComponentId component,
                                             bool suppressed);
    Result<bool> setConfigurationSuppression(ConfigurationId configuration, MateId mate, bool suppressed);
    /// Removes the override, so the object takes its base state again.
    Result<bool> clearConfigurationSuppression(ConfigurationId configuration, ComponentId component);
    Result<bool> clearConfigurationSuppression(ConfigurationId configuration, MateId mate);
    Result<bool> renameConfiguration(ConfigurationId id, std::string name);
    /// Makes the configuration with @p state's ID equal to it: same name and
    /// same overrides. Used by undo/redo, and validated exactly as the
    /// piecewise edits are.
    Result<bool> restoreConfiguration(const Configuration& state);
    /// Whether this document would accept @p value as an override of
    /// @p parameter: it exists, is free, and the value has its dimension.
    /// Lets a command check before it changes anything.
    [[nodiscard]] Result<void> checkOverride(ParameterId parameter, const DimensionedValue& value) const {
        return requireOverridable(parameter, value);
    }

    /// Everything the configurations say about @p object, and the means to
    /// put it back (P13-CMD-001).
    ///
    /// Deleting an object clears the overrides naming it, because a
    /// configuration must never name something that is gone. A command that
    /// deletes therefore captures these first and restores them on undo --
    /// without which the object would come back and the intent about it
    /// would not.
    [[nodiscard]] ObjectOverrides configurationOverridesFor(ObjectId object) const {
        return configurations_.overridesFor(object);
    }
    void restoreConfigurationOverrides(ObjectId object, const ObjectOverrides& overrides) {
        configurations_.restoreOverridesFor(object, overrides);
        ++revision_;
    }

    /// Selects the configuration whose overrides are in force; std::nullopt
    /// is the base configuration. Fails with NotFound for an unknown ID.
    Result<bool> setActiveConfiguration(std::optional<ConfigurationId> id);
    [[nodiscard]] std::optional<ConfigurationId> activeConfiguration() const noexcept {
        return configurations_.active();
    }
    /// The overrides in force; empty for the base configuration.
    [[nodiscard]] const ParameterOverrides& activeOverrides() const noexcept {
        return configurations_.activeOverrides();
    }

    /// The value @p id has under the active configuration: its override if
    /// the configuration has one, otherwise the parameter's own value.
    /// std::nullopt if the parameter does not exist.
    ///
    /// This is what the model is built from -- expressions, driven sketch
    /// constraints and feature parameters all read it -- so that a
    /// configuration reaches everything without any of them knowing that
    /// configurations exist.
    [[nodiscard]] std::optional<DimensionedValue> effectiveParameterValue(ParameterId id) const noexcept;

    // --- Objects ----------------------------------------------------------
    /// Adds a new object (whose ID must still be invalid) and assigns its ID.
    Result<ObjectId> addObject(std::unique_ptr<DocumentObject> object);
    /// Inserts an object that already has an ID (undo/redo).
    Result<void> insertObject(std::unique_ptr<DocumentObject> object);
    /// Adds a new object (ID still invalid) under the given ID, e.g. when
    /// loading a file. The ID must not be in use; it is reserved.
    Result<void> restoreObject(ObjectId id, std::unique_ptr<DocumentObject> object);
    Result<std::unique_ptr<DocumentObject>> removeObject(ObjectId id);

    [[nodiscard]] const DocumentObject* findObject(ObjectId id) const noexcept;
    [[nodiscard]] const DocumentObject* findObjectByName(std::string_view name) const noexcept;
    /// The object if it exists and is a T; nullptr otherwise.
    template <typename T>
    [[nodiscard]] const T* findObjectAs(ObjectId id) const noexcept {
        return dynamic_cast<const T*>(findObject(id));
    }
    [[nodiscard]] std::size_t objectCount() const noexcept { return objects_.size(); }
    /// All objects in ascending ID order.
    [[nodiscard]] auto objects() const {
        return objects_ | std::views::transform([](const auto& entry) -> const DocumentObject& {
                   return *entry.second;
               });
    }

    /// Applies @p mutation to the object, which must be a T. The mutation
    /// returns whether it changed anything; changes bump the object and
    /// document revisions.
    template <typename T, typename Mutation>
    Result<bool> modifyObject(ObjectId id, Mutation&& mutation) {
        DocumentObject* object = findMutableObject(id);
        if (object == nullptr) {
            return objectNotFound(id);
        }
        T* typed = dynamic_cast<T*>(object);
        if (typed == nullptr) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("{} is a '{}', which this operation cannot modify", id,
                                         object->typeName()));
        }
        Result<bool> changed = std::forward<Mutation>(mutation)(*typed);
        if (changed && *changed) {
            ++object->revision_;
            ++revision_;
        }
        return changed;
    }

    // --- Items of either kind ---------------------------------------------
    /// True if a parameter or object has this ID.
    [[nodiscard]] bool contains(ObjectId id) const noexcept;
    /// The ID as a ParameterId if it names a parameter (checked narrowing).
    [[nodiscard]] std::optional<ParameterId> asParameter(ObjectId id) const noexcept;
    /// Name of the parameter or object with this ID.
    [[nodiscard]] std::optional<std::string_view> nameOf(ObjectId id) const noexcept;
    /// ID of the parameter or object with this name.
    [[nodiscard]] std::optional<ObjectId> findByName(std::string_view name) const noexcept;
    /// Renames a parameter or object.
    Result<bool> rename(ObjectId id, std::string name);
    /// "<base>1", "<base>2", ...: the first such name not in use.
    [[nodiscard]] std::string uniqueName(std::string_view base) const;

    // --- Change tracking --------------------------------------------------
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] bool isDirty() const noexcept { return revision_ != cleanRevision_; }
    /// Declares the current state saved.
    void markClean() noexcept { cleanRevision_ = revision_; }
    /// Highest ID value allocated so far (persisted so IDs are never reused).
    [[nodiscard]] std::uint64_t lastAllocatedId() const noexcept { return ids_.lastValue(); }
    /// Makes later allocations return values above @p value (loading).
    void reserveIdsThrough(std::uint64_t value) noexcept { ids_.reserveThrough(value); }
    /// Revision of the parameter or object with this ID.
    [[nodiscard]] std::optional<std::uint64_t> revisionOf(ObjectId id) const noexcept;
    /// IDs of all parameters and objects, ascending.
    [[nodiscard]] std::vector<ObjectId> itemIds() const;

private:
    friend class ParameterExpressionWriter;

    /// Stores the value a driven parameter's expression evaluated to.
    Result<bool> storeExpressionValue(ParameterId id, const DimensionedValue& value);

    [[nodiscard]] DocumentObject* findMutableObject(ObjectId id) noexcept;
    [[nodiscard]] static std::unexpected<Error> objectNotFound(ObjectId id);
    [[nodiscard]] Result<void> requireNameAvailable(std::string_view name) const;
    [[nodiscard]] Result<void> requireNotDriven(ParameterId id) const;
    /// The parameter exists, is free and has @p value's dimension: what an
    /// override needs.
    [[nodiscard]] Result<void> requireOverridable(ParameterId id, const DimensionedValue& value) const;
    /// No configuration overrides the parameter: what it takes for it to be
    /// allowed an expression.
    [[nodiscard]] Result<void> requireNotOverridden(ParameterId id) const;
    [[nodiscard]] static Result<void> checkExpressionSyntax(std::string_view parameter,
                                                            const std::optional<std::string>& expression);
    Result<bool> bump(Result<bool> changed) noexcept;

    DocumentId id_;
    std::string name_;
    DocumentMetadata metadata_;
    IdAllocator ids_;
    ParameterTable parameters_;
    ConfigurationTable configurations_;
    std::map<ObjectId, std::unique_ptr<DocumentObject>> objects_;
    std::map<std::string, ObjectId, std::less<>> objectIdsByName_;
    std::uint64_t revision_ = 0;
    std::uint64_t cleanRevision_ = 0;
};

/// Same identity, name, metadata, parameters, configurations and objects.
/// Revisions, dirty state and the ID allocator position are change-tracking
/// details and are ignored.
[[nodiscard]] BETTERCAD_CORE_EXPORT bool equivalent(const Document& a, const Document& b);

} // namespace bettercad
