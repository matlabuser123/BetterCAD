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

bool equivalent(const Configuration& a, const Configuration& b) noexcept {
    return a.id() == b.id() && a.name() == b.name() && a.overrides() == b.overrides();
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

bool equivalent(const ConfigurationTable& a, const ConfigurationTable& b) noexcept {
    if (a.size() != b.size() || a.active() != b.active()) {
        return false;
    }
    return std::ranges::equal(a.all(), b.all(),
                              [](const Configuration& x, const Configuration& y) { return equivalent(x, y); });
}

} // namespace bettercad
