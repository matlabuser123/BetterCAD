#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Export.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/parameters/Parameter.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>

namespace bettercad {

/// The parameters of a document, keyed by ID with unique names.
///
/// All modifications go through the table so that name uniqueness and the
/// table revision stay correct; parameters are only exposed as const.
/// revision() increments on every effective change (add, remove, modify).
class BETTERCAD_CORE_EXPORT ParameterTable {
public:
    /// Adds a parameter; fails if its ID or name is already used.
    Result<void> add(Parameter parameter);
    /// Removes and returns the parameter.
    Result<Parameter> remove(ParameterId id);

    [[nodiscard]] const Parameter* find(ParameterId id) const noexcept;
    [[nodiscard]] const Parameter* findByName(std::string_view name) const noexcept;
    [[nodiscard]] bool contains(ParameterId id) const noexcept { return find(id) != nullptr; }
    [[nodiscard]] std::size_t size() const noexcept { return parameters_.size(); }
    [[nodiscard]] bool empty() const noexcept { return parameters_.empty(); }

    /// All parameters in ascending ID order (creation order).
    [[nodiscard]] auto all() const { return std::views::values(parameters_); }

    /// Largest parameter ID value in the table, 0 if empty.
    [[nodiscard]] std::uint64_t highestIdValue() const noexcept;

    template <Dimension D>
    Result<bool> setValue(ParameterId id, const Quantity<D>& value) {
        return setSiValue(id, D, value.si());
    }
    Result<bool> setValue(ParameterId id, double value, const UnitDescriptor& unit);
    Result<bool> setSiValue(ParameterId id, Dimension dimension, double siValue);
    Result<bool> setDisplayUnit(ParameterId id, const UnitDescriptor& unit);
    Result<bool> setExpression(ParameterId id, std::optional<std::string> expression);
    /// Renames; fails if another parameter already has @p name.
    Result<bool> rename(ParameterId id, std::string name);

    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    template <typename Mutation>
    Result<bool> modify(ParameterId id, Mutation&& mutation);

    std::map<ParameterId, Parameter> parameters_;
    std::map<std::string, ParameterId, std::less<>> idsByName_;
    std::uint64_t revision_ = 0;
};

/// Same parameters (see equivalent(const Parameter&, const Parameter&)).
[[nodiscard]] BETTERCAD_CORE_EXPORT bool equivalent(const ParameterTable& a,
                                                    const ParameterTable& b) noexcept;

} // namespace bettercad
