#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/parameters/ParameterTable.hpp>
#include <bettercad/io/Export.hpp>

#include <string>
#include <string_view>

namespace bettercad::io {

/// Format identifier and version of standalone parameter documents.
inline constexpr std::string_view kParameterFormat = "bettercad-parameters";
inline constexpr int kParameterFormatVersion = 1;

/// Serializes @p table as JSON. Values are stored in SI units with a
/// round-trip exact decimal representation; the display unit and the
/// dimension are stored alongside. Revision counters are not persisted.
/// Fails with InvalidArgument if an expression is not valid UTF-8.
[[nodiscard]] BETTERCAD_IO_EXPORT Result<std::string> parametersToJson(const ParameterTable& table);

/// Parses a document written by parametersToJson(). The input is validated
/// strictly (structure, types, unknown fields, unit/dimension consistency,
/// names, unique IDs); errors name the offending JSON path.
[[nodiscard]] BETTERCAD_IO_EXPORT Result<ParameterTable> parametersFromJson(std::string_view json);

} // namespace bettercad::io
