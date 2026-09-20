#include <bettercad/core/document/Configurations.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace bettercad {

namespace {

const ParameterOverrides kNoOverrides{};

std::unexpected<Error> notFound(ConfigurationId id) {
    return makeError(ErrorCode::NotFound, std::format("{} does not exist", id));
}

} // namespace

// --- Configuration ---------------------------------------------------------

Configuration::Configuration(ConfigurationId id, std::string name) : id_(id), name_(std::move(name)) {}

Result<Configuration> Configuration::create(ConfigurationId id, std::string name) {
    if (!id.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "a configuration needs a valid ID");
    }
    if (auto valid = validateConfigurationName(name); !valid) {
        return std::unexpected(valid.error());
    }
    return Configuration(id, std::move(name));
}

std::optional<DimensionedValue> Configuration::overrideFor(ParameterId parameter) const noexcept {
    const auto found = overrides_.find(parameter);
    if (found == overrides_.end()) {
        return std::nullopt;
    }
    return found->second;
}

Result<bool> Configuration::setOverride(ParameterId parameter, const DimensionedValue& value) {
    if (!parameter.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "an override needs a valid parameter");
    }
    if (!std::isfinite(value.siValue)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the value of {} in configuration '{}' must be finite", parameter, name_));
    }
    const auto found = overrides_.find(parameter);
    if (found != overrides_.end() && found->second == value) {
        return false;
    }
    overrides_[parameter] = value;
    return true;
}

Result<bool> Configuration::clearOverride(ParameterId parameter) {
    return overrides_.erase(parameter) > 0;
}

Result<bool> Configuration::rename(std::string name) {
    if (auto valid = validateConfigurationName(name); !valid) {
        return std::unexpected(valid.error());
    }
    if (name == name_) {
        return false;
    }
    name_ = std::move(name);
    return true;
}

std::optional<bool> Configuration::suppressionFor(ComponentId component) const noexcept {
    const auto found = components_.find(component);
    return found == components_.end() ? std::nullopt : std::optional<bool>{found->second};
}

std::optional<bool> Configuration::suppressionFor(MateId mate) const noexcept {
    const auto found = mates_.find(mate);
    return found == mates_.end() ? std::nullopt : std::optional<bool>{found->second};
}

Result<bool> Configuration::setSuppressed(ComponentId component, bool suppressed) {
    if (!component.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "a suppression override needs a valid component");
    }
    const auto found = components_.find(component);
    if (found != components_.end() && found->second == suppressed) {
        return false;
    }
    components_[component] = suppressed;
    return true;
}

Result<bool> Configuration::setSuppressed(MateId mate, bool suppressed) {
    if (!mate.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "a suppression override needs a valid mate");
    }
    const auto found = mates_.find(mate);
    if (found != mates_.end() && found->second == suppressed) {
        return false;
    }
    mates_[mate] = suppressed;
    return true;
}

Result<bool> Configuration::clearSuppression(ComponentId component) {
    return components_.erase(component) > 0;
}

Result<bool> Configuration::clearSuppression(MateId mate) { return mates_.erase(mate) > 0; }

bool equivalent(const Configuration& a, const Configuration& b) noexcept {
    return a.id() == b.id() && a.name() == b.name() && a.overrides() == b.overrides() &&
           a.componentSuppression() == b.componentSuppression() && a.mateSuppression() == b.mateSuppression();
}

// --- ConfigurationTable ----------------------------------------------------

Result<void> ConfigurationTable::requireNameAvailable(std::string_view name, ConfigurationId except) const {
    for (const auto& [id, configuration] : configurations_) {
        if (id != except && configuration.name() == name) {
            return makeError(ErrorCode::AlreadyExists,
                             std::format("a configuration named '{}' already exists", name));
        }
    }
    return {};
}

Result<void> ConfigurationTable::add(Configuration configuration) {
    if (configurations_.contains(configuration.id())) {
        return makeError(ErrorCode::AlreadyExists, std::format("{} already exists", configuration.id()));
    }
    if (auto available = requireNameAvailable(configuration.name(), configuration.id()); !available) {
        return std::unexpected(available.error());
    }
    const ConfigurationId id = configuration.id();
    configurations_.emplace(id, std::move(configuration));
    return {};
}

Result<Configuration> ConfigurationTable::remove(ConfigurationId id) {
    const auto found = configurations_.find(id);
    if (found == configurations_.end()) {
        return notFound(id);
    }
    Configuration removed = std::move(found->second);
    configurations_.erase(found);
    if (active_ == id) {
        active_.reset();
    }
    return removed;
}

const Configuration* ConfigurationTable::find(ConfigurationId id) const noexcept {
    const auto found = configurations_.find(id);
    return found == configurations_.end() ? nullptr : &found->second;
}

Configuration* ConfigurationTable::findMutable(ConfigurationId id) noexcept {
    const auto found = configurations_.find(id);
    return found == configurations_.end() ? nullptr : &found->second;
}

const Configuration* ConfigurationTable::findByName(std::string_view name) const noexcept {
    for (const auto& [id, configuration] : configurations_) {
        if (configuration.name() == name) {
            return &configuration;
        }
    }
    return nullptr;
}

std::uint64_t ConfigurationTable::highestIdValue() const noexcept {
    return configurations_.empty() ? 0 : configurations_.rbegin()->first.value();
}

const Configuration* ConfigurationTable::activeConfiguration() const noexcept {
    return active_ ? find(*active_) : nullptr;
}

Result<bool> ConfigurationTable::setActive(std::optional<ConfigurationId> id) {
    if (id && !configurations_.contains(*id)) {
        return notFound(*id);
    }
    if (active_ == id) {
        return false;
    }
    active_ = id;
    return true;
}

const ParameterOverrides& ConfigurationTable::activeOverrides() const noexcept {
    const Configuration* configuration = activeConfiguration();
    return configuration == nullptr ? kNoOverrides : configuration->overrides();
}

Result<bool> ConfigurationTable::rename(ConfigurationId id, std::string name) {
    Configuration* configuration = findMutable(id);
    if (configuration == nullptr) {
        return notFound(id);
    }
    if (auto available = requireNameAvailable(name, id); !available) {
        return std::unexpected(available.error());
    }
    return configuration->rename(std::move(name));
}

Result<bool> ConfigurationTable::setOverride(ConfigurationId id, ParameterId parameter,
                                             const DimensionedValue& value) {
    Configuration* configuration = findMutable(id);
    if (configuration == nullptr) {
        return notFound(id);
    }
    return configuration->setOverride(parameter, value);
}

Result<bool> ConfigurationTable::clearOverride(ConfigurationId id, ParameterId parameter) {
    Configuration* configuration = findMutable(id);
    if (configuration == nullptr) {
        return notFound(id);
    }
    return configuration->clearOverride(parameter);
}

const ComponentSuppression& ConfigurationTable::activeComponentSuppression() const noexcept {
    static const ComponentSuppression kNone;
    const Configuration* configuration = activeConfiguration();
    return configuration == nullptr ? kNone : configuration->componentSuppression();
}

const MateSuppression& ConfigurationTable::activeMateSuppression() const noexcept {
    static const MateSuppression kNone;
    const Configuration* configuration = activeConfiguration();
    return configuration == nullptr ? kNone : configuration->mateSuppression();
}

std::optional<bool> ConfigurationTable::activeSuppressionFor(ComponentId id) const noexcept {
    const Configuration* configuration = activeConfiguration();
    return configuration == nullptr ? std::nullopt : configuration->suppressionFor(id);
}

std::optional<bool> ConfigurationTable::activeSuppressionFor(MateId id) const noexcept {
    const Configuration* configuration = activeConfiguration();
    return configuration == nullptr ? std::nullopt : configuration->suppressionFor(id);
}

Result<bool> ConfigurationTable::setSuppressed(ConfigurationId id, ComponentId component, bool suppressed) {
    Configuration* configuration = findMutable(id);
    if (configuration == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("no configuration with ID {}", id));
    }
    return configuration->setSuppressed(component, suppressed);
}

Result<bool> ConfigurationTable::setSuppressed(ConfigurationId id, MateId mate, bool suppressed) {
    Configuration* configuration = findMutable(id);
    if (configuration == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("no configuration with ID {}", id));
    }
    return configuration->setSuppressed(mate, suppressed);
}

Result<bool> ConfigurationTable::clearSuppression(ConfigurationId id, ComponentId component) {
    Configuration* configuration = findMutable(id);
    if (configuration == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("no configuration with ID {}", id));
    }
    return configuration->clearSuppression(component);
}

Result<bool> ConfigurationTable::clearSuppression(ConfigurationId id, MateId mate) {
    Configuration* configuration = findMutable(id);
    if (configuration == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("no configuration with ID {}", id));
    }
    return configuration->clearSuppression(mate);
}

std::size_t ConfigurationTable::forgetObject(ObjectId object) {
    // A component and a mate are both document objects, and the table does
    // not know which kind this ID was. Clearing both by value is correct
    // either way: an ID is one object, so at most one map can hold it.
    const auto component = ComponentId::fromValue(object.value());
    const auto mate = MateId::fromValue(object.value());
    std::size_t changed = 0;
    for (auto& [id, configuration] : configurations_) {
        const Result<bool> forgotComponent = configuration.clearSuppression(component);
        const Result<bool> forgotMate = configuration.clearSuppression(mate);
        if ((forgotComponent && *forgotComponent) || (forgotMate && *forgotMate)) {
            ++changed;
        }
    }
    return changed;
}

std::size_t ConfigurationTable::forgetParameter(ParameterId parameter) {
    std::size_t changed = 0;
    for (auto& [id, configuration] : configurations_) {
        const Result<bool> cleared = configuration.clearOverride(parameter);
        if (cleared && *cleared) {
            ++changed;
        }
    }
    return changed;
}

ObjectOverrides ConfigurationTable::overridesFor(ObjectId object) const {
    const auto parameter = ParameterId::fromValue(object.value());
    const auto component = ComponentId::fromValue(object.value());
    const auto mate = MateId::fromValue(object.value());
    ObjectOverrides found;
    for (const auto& [id, configuration] : configurations_) {
        if (const auto value = configuration.overrideFor(parameter)) {
            found.parameter.emplace(id, *value);
        }
        if (const auto suppressed = configuration.suppressionFor(component)) {
            found.component.emplace(id, *suppressed);
        }
        if (const auto suppressed = configuration.suppressionFor(mate)) {
            found.mate.emplace(id, *suppressed);
        }
    }
    return found;
}

void ConfigurationTable::restoreOverridesFor(ObjectId object, const ObjectOverrides& overrides) {
    const auto parameter = ParameterId::fromValue(object.value());
    const auto component = ComponentId::fromValue(object.value());
    const auto mate = MateId::fromValue(object.value());
    for (const auto& [id, value] : overrides.parameter) {
        if (Configuration* configuration = findMutable(id)) {
            (void)configuration->setOverride(parameter, value);
        }
    }
    for (const auto& [id, suppressed] : overrides.component) {
        if (Configuration* configuration = findMutable(id)) {
            (void)configuration->setSuppressed(component, suppressed);
        }
    }
    for (const auto& [id, suppressed] : overrides.mate) {
        if (Configuration* configuration = findMutable(id)) {
            (void)configuration->setSuppressed(mate, suppressed);
        }
    }
}

bool equivalent(const ConfigurationTable& a, const ConfigurationTable& b) noexcept {
    if (a.size() != b.size() || a.active() != b.active()) {
        return false;
    }
    return std::ranges::equal(a.all(), b.all(),
                              [](const Configuration& x, const Configuration& y) { return equivalent(x, y); });
}

} // namespace bettercad
