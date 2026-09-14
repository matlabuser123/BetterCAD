#include "io/json/JsonReader.hpp"

#include <algorithm>
#include <format>
#include <limits>

namespace bettercad::io::detail {

std::string childPath(std::string_view parent, std::string_view key) {
    return parent.empty() ? std::string{key} : std::format("{}.{}", parent, key);
}

std::string indexPath(std::string_view parent, std::size_t index) {
    return std::format("{}[{}]", parent, index);
}

std::unexpected<Error> parseError(std::string_view path, std::string_view problem) {
    const std::string where = path.empty() ? std::string{"document"} : std::string{path};
    return makeError(ErrorCode::ParseError, std::format("{}: {}", where, problem));
}

std::unexpected<Error> atPath(std::string_view path, const Error& error) {
    return makeError(error.code, std::format("{}: {}", path, error.message));
}

Result<Json> parseJson(std::string_view text) {
    try {
        return Json::parse(text);
    } catch (const Json::exception& e) {
        // parse_error for syntax, out_of_range for numbers that overflow a double.
        return makeError(ErrorCode::ParseError, std::format("invalid JSON: {}", e.what()));
    }
}

Result<std::string> dumpJson(const Json& value) {
    try {
        return value.dump(2) + '\n';
    } catch (const Json::type_error&) {
        return makeError(ErrorCode::InvalidArgument, "text in the document is not valid UTF-8");
    }
}

Result<void> requireObject(const Json& value, std::string_view path,
                           std::initializer_list<std::string_view> allowedKeys) {
    if (!value.is_object()) {
        return parseError(path, "expected an object");
    }
    for (const auto& item : value.items()) {
        const std::string& key = item.key();
        if (std::ranges::find(allowedKeys, std::string_view{key}) == allowedKeys.end()) {
            return parseError(childPath(path, key), "unknown field");
        }
    }
    return {};
}

Result<const Json*> requireField(const Json& object, std::string_view key, std::string_view path) {
    const auto it = object.find(std::string{key});
    if (it == object.end()) {
        return parseError(childPath(path, key), "missing required field");
    }
    return &*it;
}

Result<std::string> readString(const Json& object, std::string_view key, std::string_view path) {
    auto field = requireField(object, key, path);
    if (!field) {
        return std::unexpected(field.error());
    }
    if (!(*field)->is_string()) {
        return parseError(childPath(path, key), "expected a string");
    }
    return (*field)->get<std::string>();
}

Result<std::optional<std::string>> readOptionalString(const Json& object, std::string_view key,
                                                      std::string_view path) {
    if (!object.contains(std::string{key})) {
        return std::optional<std::string>{};
    }
    auto value = readString(object, key, path);
    if (!value) {
        return std::unexpected(value.error());
    }
    return std::optional<std::string>{std::move(*value)};
}

Result<double> readNumber(const Json& object, std::string_view key, std::string_view path) {
    auto field = requireField(object, key, path);
    if (!field) {
        return std::unexpected(field.error());
    }
    if (!(*field)->is_number()) {
        return parseError(childPath(path, key), "expected a number");
    }
    return (*field)->get<double>();
}

Result<std::uint64_t> readUnsigned(const Json& object, std::string_view key,
                                   std::string_view path) {
    auto field = requireField(object, key, path);
    if (!field) {
        return std::unexpected(field.error());
    }
    if (!(*field)->is_number_unsigned()) {
        return parseError(childPath(path, key), "expected a non-negative integer");
    }
    return (*field)->get<std::uint64_t>();
}

Result<int> readInt(const Json& object, std::string_view key, std::string_view path) {
    auto field = requireField(object, key, path);
    if (!field) {
        return std::unexpected(field.error());
    }
    const Json& value = **field;
    if (value.is_number_unsigned()) {
        const auto v = value.get<std::uint64_t>();
        if (v <= static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            return static_cast<int>(v);
        }
    } else if (value.is_number_integer()) {
        const auto v = value.get<std::int64_t>();
        if (v >= std::numeric_limits<int>::min() && v <= std::numeric_limits<int>::max()) {
            return static_cast<int>(v);
        }
    } else {
        return parseError(childPath(path, key), "expected an integer");
    }
    return parseError(childPath(path, key), "integer out of range");
}

Result<bool> readBool(const Json& object, std::string_view key, std::string_view path) {
    auto field = requireField(object, key, path);
    if (!field) {
        return std::unexpected(field.error());
    }
    if (!(*field)->is_boolean()) {
        return parseError(childPath(path, key), "expected true or false");
    }
    return (*field)->get<bool>();
}

Result<std::uint64_t> readId(const Json& value, std::string_view path) {
    if (!value.is_number_unsigned()) {
        return parseError(path, "expected an ID (a non-negative integer)");
    }
    const auto id = value.get<std::uint64_t>();
    if (id > kMaxFileId) {
        return parseError(path, std::format("ID {} exceeds the maximum {}", id, kMaxFileId));
    }
    return id;
}

Result<std::uint64_t> readId(const Json& object, std::string_view key, std::string_view path) {
    auto field = requireField(object, key, path);
    if (!field) {
        return std::unexpected(field.error());
    }
    return readId(**field, childPath(path, key));
}

Result<std::optional<std::uint64_t>> readOptionalId(const Json& object, std::string_view key,
                                                    std::string_view path) {
    if (!object.contains(std::string{key})) {
        return std::optional<std::uint64_t>{};
    }
    auto value = readId(object, key, path);
    if (!value) {
        return std::unexpected(value.error());
    }
    return std::optional<std::uint64_t>{*value};
}

Result<std::vector<double>> readNumbers(const Json& object, std::string_view key, std::string_view path,
                                        std::size_t count) {
    auto field = requireArray(object, key, path);
    if (!field) {
        return std::unexpected(field.error());
    }
    const Json& array = **field;
    if (array.size() != count) {
        return parseError(childPath(path, key), std::format("expected {} numbers, got {}", count, array.size()));
    }
    std::vector<double> values;
    for (std::size_t i = 0; i < count; ++i) {
        if (!array[i].is_number()) {
            return parseError(indexPath(childPath(path, key), i), "expected a number");
        }
        values.push_back(array[i].get<double>());
    }
    return values;
}

Result<const Json*> requireArray(const Json& object, std::string_view key, std::string_view path) {
    auto field = requireField(object, key, path);
    if (!field) {
        return std::unexpected(field.error());
    }
    if (!(*field)->is_array()) {
        return parseError(childPath(path, key), "expected an array");
    }
    return *field;
}

} // namespace bettercad::io::detail
