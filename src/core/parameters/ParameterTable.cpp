#include <bettercad/core/parameters/ParameterTable.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace bettercad {

namespace {

std::unexpected<Error> notFound(ParameterId id) {
    return makeError(ErrorCode::NotFound, std::format("{} does not exist", id));
}

std::unexpected<Error> nameTaken(std::string_view name) {
    return makeError(ErrorCode::AlreadyExists,
                     std::format("a parameter named '{}' already exists", name));
}

} // namespace

Result<void> ParameterTable::add(Parameter parameter) {
    const ParameterId id = parameter.id();
    if (parameters_.contains(id)) {
        return makeError(ErrorCode::AlreadyExists, std::format("{} already exists", id));
    }
    if (idsByName_.contains(parameter.name())) {
        return nameTaken(parameter.name());
    }

    const auto it = parameters_.emplace(id, std::move(parameter)).first;
    try {
        idsByName_.emplace(it->second.name(), id);
    } catch (...) {
        parameters_.erase(it);
        throw;
    }
    ++revision_;
    return {};
}

Result<Parameter> ParameterTable::remove(ParameterId id) {
    auto node = parameters_.extract(id);
    if (node.empty()) {
        return notFound(id);
    }
    idsByName_.erase(node.mapped().name());
    ++revision_;
    return std::move(node.mapped());
}

const Parameter* ParameterTable::find(ParameterId id) const noexcept {
    const auto it = parameters_.find(id);
    return it == parameters_.end() ? nullptr : &it->second;
}

const Parameter* ParameterTable::findByName(std::string_view name) const noexcept {
    const auto it = idsByName_.find(name);
    return it == idsByName_.end() ? nullptr : find(it->second);
}

std::uint64_t ParameterTable::highestIdValue() const noexcept {
    return parameters_.empty() ? 0 : parameters_.rbegin()->first.value();
}

template <typename Mutation>
Result<bool> ParameterTable::modify(ParameterId id, Mutation&& mutation) {
    const auto it = parameters_.find(id);
    if (it == parameters_.end()) {
        return notFound(id);
    }
    Result<bool> changed = std::forward<Mutation>(mutation)(it->second);
    if (changed && *changed) {
        ++revision_;
    }
    return changed;
}

Result<bool> ParameterTable::setValue(ParameterId id, double value, const UnitDescriptor& unit) {
    return modify(id, [&](Parameter& p) { return p.setValue(value, unit); });
}

Result<bool> ParameterTable::setSiValue(ParameterId id, Dimension dimension, double siValue) {
    return modify(id, [&](Parameter& p) { return p.setSiValue(dimension, siValue); });
}

Result<bool> ParameterTable::setDisplayUnit(ParameterId id, const UnitDescriptor& unit) {
    return modify(id, [&](Parameter& p) { return p.setDisplayUnit(unit); });
}

Result<bool> ParameterTable::setExpression(ParameterId id, std::optional<std::string> expression) {
    return modify(id, [&](Parameter& p) { return p.setExpression(std::move(expression)); });
}

Result<bool> ParameterTable::rename(ParameterId id, std::string name) {
    return modify(id, [&](Parameter& p) -> Result<bool> {
        if (name == p.name()) {
            return false;
        }
        if (auto valid = validateParameterName(name); !valid) {
            return std::unexpected(valid.error());
        }
        if (idsByName_.contains(name)) {
            return nameTaken(name);
        }
        // Everything that can throw happens before the parameter changes.
        std::string oldName = p.name();
        idsByName_.emplace(name, id);
        auto renamed = p.rename(std::move(name)); // validated above; cannot fail
        idsByName_.erase(oldName);
        return renamed;
    });
}

bool equivalent(const ParameterTable& a, const ParameterTable& b) noexcept {
    return std::ranges::equal(a.all(), b.all(), [](const Parameter& x, const Parameter& y) {
        return equivalent(x, y);
    });
}

} // namespace bettercad
