#pragma once

#include "io/json/JsonReader.hpp"

#include <bettercad/core/parameters/ParameterTable.hpp>
#include <bettercad/core/units/Dimension.hpp>

#include <string_view>

// JSON mapping of parameters, shared by the standalone parameter format and
// (later) the native document format.
namespace bettercad::io::detail {

/// Only non-zero exponents are written: {"length": 1}; dimensionless is {}.
[[nodiscard]] Json dimensionToJson(const Dimension& dimension);
[[nodiscard]] Result<Dimension> dimensionFromJson(const Json& value, std::string_view path);

[[nodiscard]] Json parameterToJson(const Parameter& parameter);
[[nodiscard]] Result<Parameter> parameterFromJson(const Json& value, std::string_view path);

/// JSON array of parameters in ID order.
[[nodiscard]] Json parameterTableToJson(const ParameterTable& table);
[[nodiscard]] Result<ParameterTable> parameterTableFromJson(const Json& value,
                                                            std::string_view path);

} // namespace bettercad::io::detail
