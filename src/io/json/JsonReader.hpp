#pragma once

#include <bettercad/core/Error.hpp>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Strict, path-aware reading of JSON documents. Every failure becomes an
// Error whose message names the offending location, e.g.
// "parameters[2].unit: expected a string".
namespace bettercad::io::detail {

/// Insertion-ordered JSON, so written files keep a stable, readable key order.
using Json = nlohmann::ordered_json;

[[nodiscard]] std::string childPath(std::string_view parent, std::string_view key);
[[nodiscard]] std::string indexPath(std::string_view parent, std::size_t index);

[[nodiscard]] std::unexpected<Error> parseError(std::string_view path, std::string_view problem);
/// Keeps the error code and prefixes the message with @p path.
[[nodiscard]] std::unexpected<Error> atPath(std::string_view path, const Error& error);

[[nodiscard]] Result<Json> parseJson(std::string_view text);
/// Pretty-printed text (2-space indent, trailing newline). Fails with
/// InvalidArgument if a string is not valid UTF-8.
[[nodiscard]] Result<std::string> dumpJson(const Json& value);

/// @p value must be an object whose keys are all in @p allowedKeys.
[[nodiscard]] Result<void> requireObject(const Json& value, std::string_view path,
                                         std::initializer_list<std::string_view> allowedKeys);

[[nodiscard]] Result<const Json*> requireField(const Json& object, std::string_view key,
                                               std::string_view path);
[[nodiscard]] Result<std::string> readString(const Json& object, std::string_view key,
                                             std::string_view path);
/// Absent key gives std::nullopt; a present key must hold a string.
[[nodiscard]] Result<std::optional<std::string>>
readOptionalString(const Json& object, std::string_view key, std::string_view path);
/// Any JSON number, as double.
[[nodiscard]] Result<double> readNumber(const Json& object, std::string_view key,
                                        std::string_view path);
/// A non-negative JSON integer.
[[nodiscard]] Result<std::uint64_t> readUnsigned(const Json& object, std::string_view key,
                                                 std::string_view path);
/// A JSON integer that fits in int.
[[nodiscard]] Result<int> readInt(const Json& object, std::string_view key, std::string_view path);
[[nodiscard]] Result<bool> readBool(const Json& object, std::string_view key, std::string_view path);

/// Largest ID value a file may contain: 2^53 - 1, the largest integer that
/// every JSON implementation represents exactly. The cap also keeps loaded
/// documents far from exhausting the 64-bit ID space.
inline constexpr std::uint64_t kMaxFileId = (std::uint64_t{1} << 53) - 1;

/// An ID or ID counter: an integer in [0, kMaxFileId]. Whether 0 (the
/// invalid ID) is acceptable is up to the caller.
[[nodiscard]] Result<std::uint64_t> readId(const Json& value, std::string_view path);
[[nodiscard]] Result<std::uint64_t> readId(const Json& object, std::string_view key, std::string_view path);
/// Absent key gives std::nullopt; a present key must hold an ID.
[[nodiscard]] Result<std::optional<std::uint64_t>> readOptionalId(const Json& object, std::string_view key,
                                                                  std::string_view path);
/// An array of exactly @p count numbers.
[[nodiscard]] Result<std::vector<double>> readNumbers(const Json& object, std::string_view key,
                                                      std::string_view path, std::size_t count);
/// The field, which must be an array.
[[nodiscard]] Result<const Json*> requireArray(const Json& object, std::string_view key,
                                               std::string_view path);

} // namespace bettercad::io::detail
