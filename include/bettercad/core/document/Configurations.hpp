#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Export.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/Naming.hpp>
#include <bettercad/core/units/DimensionedValue.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

// Named configurations: one parametric document describing a family of parts
// (P12-PARAM-002).
//
// The semantics, in one line: a configuration overrides the values of free
// parameters, and everything else follows from the equations that were
// already there.
//
//     base values -> the active configuration's overrides
//                 -> equation evaluation -> feature regeneration
//
// So a configuration that sets `width` moves `height = width / 2` with it;
// derived values are never duplicated per configuration, and a document with
// no configurations behaves exactly as it did before this milestone.
//
// What a configuration never does is change the document's canonical state.
// A free parameter's stored value is its *base* value and no configuration
// touches it; the value in force is the base value with the active
// configuration's override applied on top (Document::effectiveParameterValue).
// That is why switching Small -> Large -> Small restores Small exactly: the
// base values never moved.
namespace bettercad {

/// Configuration names are identifiers, like parameter and object names, so
/// that they can be typed on a command line without quoting.
[[nodiscard]] inline Result<void> validateConfigurationName(std::string_view name) {
    return validateIdentifier(name, "configuration");
}

/// The overrides in force: the value each overridden parameter takes. An
/// empty set is the base configuration, in which every parameter has its own
/// value.
using ParameterOverrides = std::map<ParameterId, DimensionedValue>;

/// Whether a component or a mate is suppressed, for the ones a configuration
/// overrides (P13-CONF-001, ADR-007).
///
/// An absent entry means the object keeps its own `suppressed` flag -- its
/// *base* state, which no configuration edits. A present entry is the value
/// that flag takes while this configuration is active.
///
/// A map to a value rather than a set of suppressed objects, for the same
/// reason a parameter override is a value: a component whose base state is
/// suppressed must be able to be turned back on by a configuration, and a set
/// could only ever turn things off.
using ComponentSuppression = std::map<ComponentId, bool>;
using MateSuppression = std::map<MateId, bool>;

/// A named set of intentional overrides to a document's parameters, e.g.
/// "Small" with `width = 80 mm`.
///
/// Only a *free* parameter can be overridden. A driven parameter's value
/// comes from its expression, and an override would be a second answer to
/// the same question; Document refuses it. Overriding what a driven
/// parameter names is how a configuration moves it.
///
/// An override must have the parameter's own dimension. Configuration alone
/// cannot check that -- it does not know the parameters -- so Document does,
/// and a Configuration reached through a Document has always been checked.
class BETTERCAD_CORE_EXPORT Configuration {
public:
    [[nodiscard]] static Result<Configuration> create(ConfigurationId id, std::string name);

    [[nodiscard]] ConfigurationId id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const ParameterOverrides& overrides() const noexcept { return overrides_; }
    [[nodiscard]] const ComponentSuppression& componentSuppression() const noexcept {
        return components_;
    }
    [[nodiscard]] const MateSuppression& mateSuppression() const noexcept { return mates_; }
    /// The value this configuration gives @p parameter, if it overrides it.
    [[nodiscard]] std::optional<DimensionedValue> overrideFor(ParameterId parameter) const noexcept;
    [[nodiscard]] bool overrides(ParameterId parameter) const noexcept {
        return overrides_.contains(parameter);
    }
    [[nodiscard]] std::size_t size() const noexcept { return overrides_.size(); }
    /// Whether this configuration says anything at all. The base
    /// configuration is the absence of overrides, so an empty one behaves
    /// exactly as no configuration does.
    [[nodiscard]] bool empty() const noexcept {
        return overrides_.empty() && components_.empty() && mates_.empty();
    }
    /// Whether this configuration suppresses @p component, or nullopt if it
    /// leaves it at its base state.
    [[nodiscard]] std::optional<bool> suppressionFor(ComponentId component) const noexcept;
    [[nodiscard]] std::optional<bool> suppressionFor(MateId mate) const noexcept;

    // Mutators return whether the configuration changed.

    /// Sets the value @p parameter takes in this configuration. The dimension
    /// is not checked here; see the class notes.
    Result<bool> setOverride(ParameterId parameter, const DimensionedValue& value);
    /// Removes the override, so the parameter takes its base value again.
    /// Removing one that is not there is not a change.
    Result<bool> clearOverride(ParameterId parameter);
    /// Sets whether @p component is suppressed while this configuration is
    /// active. Whether the component exists, and is a component, is checked
    /// by the document and by the assembly module respectively -- a
    /// configuration does not know what a component is.
    Result<bool> setSuppressed(ComponentId component, bool suppressed);
    Result<bool> setSuppressed(MateId mate, bool suppressed);
    /// Removes the override, so the object takes its base state again.
    /// Removing one that is not there is not a change.
    Result<bool> clearSuppression(ComponentId component);
    Result<bool> clearSuppression(MateId mate);
    /// Validates the name syntax only; uniqueness is the owning table's job.
    Result<bool> rename(std::string name);

private:
    Configuration(ConfigurationId id, std::string name);

    ConfigurationId id_;
    std::string name_;
    ParameterOverrides overrides_;
    ComponentSuppression components_;
    MateSuppression mates_;
};

/// Same identity and content (ID, name, and all three kinds of override).
[[nodiscard]] BETTERCAD_CORE_EXPORT bool equivalent(const Configuration& a, const Configuration& b) noexcept;

/// Everything the configurations of a document say about one object: the
/// parameter value each overrides it to, or the suppression each gives it.
///
/// Captured before a deletion so that undoing the deletion can put it back.
/// A configuration must never name an object that is gone, so deleting one
/// clears these -- and an undo that restored the object but not these would
/// leave the document almost, but not exactly, as it was (P13-CMD-001).
struct ObjectOverrides {
    std::map<ConfigurationId, DimensionedValue> parameter{};
    std::map<ConfigurationId, bool> component{};
    std::map<ConfigurationId, bool> mate{};

    [[nodiscard]] bool empty() const noexcept {
        return parameter.empty() && component.empty() && mate.empty();
    }
    friend bool operator==(const ObjectOverrides&, const ObjectOverrides&) = default;
};

/// The configurations of a document, by ID, with unique names.
///
/// One of them may be *active*: the one whose overrides are in force. None
/// active is the base configuration, the values the parameters themselves
/// hold.
class BETTERCAD_CORE_EXPORT ConfigurationTable {
public:
    /// Fails with AlreadyExists if the ID or the name is taken.
    Result<void> add(Configuration configuration);
    /// Removes it and returns it. Fails with NotFound. If it was active, the
    /// base configuration becomes active.
    Result<Configuration> remove(ConfigurationId id);

    [[nodiscard]] const Configuration* find(ConfigurationId id) const noexcept;
    [[nodiscard]] const Configuration* findByName(std::string_view name) const noexcept;
    [[nodiscard]] bool contains(ConfigurationId id) const noexcept { return find(id) != nullptr; }
    [[nodiscard]] std::size_t size() const noexcept { return configurations_.size(); }
    [[nodiscard]] bool empty() const noexcept { return configurations_.empty(); }
    /// All configurations in ascending ID order, which is creation order.
    [[nodiscard]] auto all() const { return std::views::values(configurations_); }
    [[nodiscard]] std::uint64_t highestIdValue() const noexcept;

    /// The configuration whose overrides are in force, or nullopt for the
    /// base configuration.
    [[nodiscard]] std::optional<ConfigurationId> active() const noexcept { return active_; }
    [[nodiscard]] const Configuration* activeConfiguration() const noexcept;
    /// nullopt selects the base configuration. Fails with NotFound for an
    /// unknown ID.
    Result<bool> setActive(std::optional<ConfigurationId> id);

    /// The overrides in force, empty for the base configuration.
    [[nodiscard]] const ParameterOverrides& activeOverrides() const noexcept;
    [[nodiscard]] const ComponentSuppression& activeComponentSuppression() const noexcept;
    [[nodiscard]] const MateSuppression& activeMateSuppression() const noexcept;
    /// Whether the active configuration suppresses @p id, or nullopt if it
    /// leaves it at its base state. Always nullopt for the base
    /// configuration, which overrides nothing.
    [[nodiscard]] std::optional<bool> activeSuppressionFor(ComponentId id) const noexcept;
    [[nodiscard]] std::optional<bool> activeSuppressionFor(MateId id) const noexcept;

    Result<bool> rename(ConfigurationId id, std::string name);
    Result<bool> setOverride(ConfigurationId id, ParameterId parameter, const DimensionedValue& value);
    Result<bool> clearOverride(ConfigurationId id, ParameterId parameter);
    Result<bool> setSuppressed(ConfigurationId id, ComponentId component, bool suppressed);
    Result<bool> setSuppressed(ConfigurationId id, MateId mate, bool suppressed);
    Result<bool> clearSuppression(ConfigurationId id, ComponentId component);
    Result<bool> clearSuppression(ConfigurationId id, MateId mate);
    /// Removes @p parameter from every configuration, e.g. because it was
    /// deleted. Returns how many configurations changed.
    std::size_t forgetParameter(ParameterId parameter);
    /// Removes every suppression override naming @p object, because it was
    /// deleted. A configuration must never name an object that is gone. The
    /// sibling of forgetParameter(), and called from the same place.
    ///
    /// Takes an ObjectId because a component and a mate are both document
    /// objects and the table does not know which kind this was; both maps are
    /// cleared of that ID value.
    std::size_t forgetObject(ObjectId object);
    /// Everything every configuration says about @p object, in a form
    /// restoreOverridesFor() can put back. Empty when none mentions it.
    [[nodiscard]] ObjectOverrides overridesFor(ObjectId object) const;
    /// Puts back what forgetParameter() or forgetObject() cleared. Silently
    /// skips configurations that no longer exist, so that undoing a deletion
    /// after a configuration was itself deleted restores what it can.
    void restoreOverridesFor(ObjectId object, const ObjectOverrides& overrides);

private:
    [[nodiscard]] Configuration* findMutable(ConfigurationId id) noexcept;
    [[nodiscard]] Result<void> requireNameAvailable(std::string_view name, ConfigurationId except) const;

    std::map<ConfigurationId, Configuration> configurations_;
    std::optional<ConfigurationId> active_;
};

/// Same configurations and the same active one.
[[nodiscard]] BETTERCAD_CORE_EXPORT bool equivalent(const ConfigurationTable& a,
                                                    const ConfigurationTable& b) noexcept;

} // namespace bettercad
