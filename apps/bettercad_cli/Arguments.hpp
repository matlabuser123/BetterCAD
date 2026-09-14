#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/units/Units.hpp>

#include <filesystem>
#include <initializer_list>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::cli {

using Args = std::span<const std::string_view>;

struct OptionSpec {
    std::string_view name; ///< e.g. "--name"
    bool takesValue = false;
};

/// A command's arguments: positional arguments in order, and options.
/// Options are "--flag", "--option value" or "--option=value"; "--" ends
/// option parsing, so later arguments are positional even if they start
/// with '-'.
class ParsedArguments {
public:
    [[nodiscard]] const std::vector<std::string_view>& positional() const noexcept { return positional_; }
    [[nodiscard]] bool has(std::string_view option) const { return options_.contains(option); }
    [[nodiscard]] std::optional<std::string_view> value(std::string_view option) const;

private:
    friend Result<ParsedArguments> parseArguments(Args args, std::initializer_list<OptionSpec> options);

    std::vector<std::string_view> positional_;
    std::map<std::string_view, std::string_view, std::less<>> options_;
};

/// Fails with InvalidArgument ("unexpected argument '--x'", "option '--name'
/// needs a value", ...) for anything @p options does not allow.
[[nodiscard]] Result<ParsedArguments> parseArguments(Args args, std::initializer_list<OptionSpec> options);

/// Command-line arguments are UTF-8 on every platform (see main.cpp).
[[nodiscard]] std::filesystem::path pathFromArgument(std::string_view argument);
/// UTF-8 form of a path for output.
[[nodiscard]] std::string displayPath(const std::filesystem::path& path);

/// "0.05mm", "0.05 mm", "0.002in" or a plain number in @p defaultUnit.
[[nodiscard]] Result<Length> parseLength(std::string_view text, const Unit<dimensions::length>& defaultUnit);
/// "15deg", "0.2 rad" or a plain number in @p defaultUnit.
[[nodiscard]] Result<Angle> parseAngle(std::string_view text, const Unit<dimensions::angle>& defaultUnit);

} // namespace bettercad::cli
