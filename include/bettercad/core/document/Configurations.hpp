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
    /// The value this configuration gives @p parameter, if it overrides it.
    [[nodiscard]] std::optional<DimensionedValue> overrideFor(ParameterId parameter) const noexcept;
    [[nodiscard]] bool overrides(ParameterId parameter) const noexcept {
        return overrides_.contains(parameter);
    }
    [[nodiscard]] std::size_t size() const noexcept { return overrides_.size(); }

    // Mutators return whether the configuration changed.

    /// Sets the value @p parameter takes in this configuration. The dimension
    /// is not checked here; see the class notes.
    Result<bool> setOverride(ParameterId parameter, const DimensionedValue& value);
    /// Removes the override, so the parameter takes its base value again.
    /// Removing one that is not there is not a change.
    Result<bool> clearOverride(ParameterId parameter);
    /// Validates the name syntax only; uniqueness is the owning table's job.
    Result<bool> rename(std::string name);

private:
    Configuration(ConfigurationId id, std::string name);

    ConfigurationId id_;
    std::string name_;
    ParameterOverrides overrides_;
};

/// Same identity and content (ID, name, overrides).
[[nodiscard]] BETTERCAD_CORE_EXPORT bool equivalent(const Configuration& a, const Configuration& b) noexcept;

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

    Result<bool> rename(ConfigurationId id, std::string name);
    Result<bool> setOverride(ConfigurationId id, ParameterId parameter, const DimensionedValue& value);
    Result<bool> clearOverride(ConfigurationId id, ParameterId parameter);
    /// Removes @p parameter from every configuration, e.g. because it was
    /// deleted. Returns how many configurations changed.
    std::size_t forgetParameter(ParameterId parameter);

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
