#pragma once

#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Machine-readable CLI output (P16-CLI-001).
//
// WHY THIS EXISTS, AND WHY IT IS THIS SMALL.
//
// Until now bettercad-cli has been machine-readable in three ways, all
// deliberate and all recorded by P15-CLI-001: the EXIT CODE carries the gate
// ("a report that gates is only usable from a script if the gate reaches the
// exit status"), a failure carries a STABLE MACHINE CODE, and the streams are
// disciplined so that "could not answer" prints nothing to stdout. Success
// output is prose for a person.
//
// Prose is enough for "which materials are defined". It is not enough for a
// mesh: a pipeline needs the element count, the volume and the validation
// status as values, and scraping them out of sentences makes the sentences an
// interface nobody can reword. So the mesh reports take `--json`.
//
// IT IS GENERAL ON PURPOSE AND ADOPTED NARROWLY ON PURPOSE. Any command can
// use this; only the mesh reports do. Converting P13's, P14's and P15's
// commands would mean rewriting three qualified output surfaces, which is a
// refactor and does not belong in a feature milestone. The cost is an honest
// inconsistency -- structured output for meshing, prose elsewhere -- and it is
// recorded as a known limitation rather than presented as a finished CLI-wide
// format.
//
// FIELD NAMES ARE AN INTERFACE. They are lowerCamelCase, they name BetterCAD
// concepts, and they never name a backend one: `elementCount`, not `nTets`.
//
// ORDER IS INSERTION ORDER, which makes the output deterministic without any
// sorting: a caller writes fields in a fixed sequence and gets the same bytes
// every run. Nothing here iterates an unordered container.
namespace bettercad::cli {

/// A JSON value being built, as text.
///
/// Deliberately not a DOM. It writes what it is given, in the order given,
/// and the only thing it knows how to do is escape a string correctly --
/// which is the one part of emitting JSON that must not be hand-rolled per
/// call site.
class JsonValue {
public:
    [[nodiscard]] static JsonValue object() { return JsonValue{Kind::Object}; }
    [[nodiscard]] static JsonValue array() { return JsonValue{Kind::Array}; }

    /// A quoted, escaped string.
    [[nodiscard]] static JsonValue text(std::string_view value) {
        return JsonValue{Kind::Raw, quote(value)};
    }
    /// A number, written through std::format so the round trip is the
    /// formatter's and not a locale's. `{}` on a double gives the shortest
    /// representation that reads back exactly.
    [[nodiscard]] static JsonValue number(double value) {
        return JsonValue{Kind::Raw, std::format("{}", value)};
    }
    [[nodiscard]] static JsonValue number(std::size_t value) {
        return JsonValue{Kind::Raw, std::format("{}", value)};
    }
    [[nodiscard]] static JsonValue boolean(bool value) {
        return JsonValue{Kind::Raw, value ? "true" : "false"};
    }
    [[nodiscard]] static JsonValue null() { return JsonValue{Kind::Raw, "null"}; }

    /// A quantity, as a value and the unit it is in: `{"value": 0.01,
    /// "unit": "m"}`.
    ///
    /// NEVER A BARE NUMBER. A size written as `0.01` with no unit anywhere is
    /// the shape of a factor-of-1000 mistake that nothing can catch later, and
    /// the brief asks for the unit to be part of the contract.
    [[nodiscard]] static JsonValue quantity(double value, std::string_view unit) {
        JsonValue json = object();
        json.set("value", number(value));
        json.set("unit", text(unit));
        return json;
    }

    /// Appends a field. The caller's order is the output's order.
    ///
    /// Renders the child WITHOUT the trailing newline the public render()
    /// adds: using the public one put a newline before every comma and a pad
    /// before every closing bracket. Valid JSON, unreadable output -- and
    /// found by looking at it rather than by trusting it.
    JsonValue& set(std::string_view key, const JsonValue& value) {
        fields_.emplace_back(quote(key), value.render(0));
        return *this;
    }
    /// Appends an element to an array.
    JsonValue& push(const JsonValue& value) {
        fields_.emplace_back(std::string{}, value.render(0));
        return *this;
    }

    [[nodiscard]] bool empty() const noexcept { return fields_.empty(); }

    /// The JSON text. Two-space indent and a trailing newline, matching the
    /// document format's own `dump(2)` so that a reader of one is not
    /// surprised by the other.
    [[nodiscard]] std::string render() const { return render(0) + "\n"; }

private:
    enum class Kind { Object, Array, Raw };

    explicit JsonValue(Kind kind) : kind_(kind) {}
    JsonValue(Kind kind, std::string raw) : kind_(kind), raw_(std::move(raw)) {}

public:
    /// The value at @p depth, with no trailing newline. Public so a nested
    /// value can be rendered by its parent.
    [[nodiscard]] std::string render(std::size_t depth) const {
        if (kind_ == Kind::Raw) {
            return raw_;
        }
        const std::string_view open = kind_ == Kind::Object ? "{" : "[";
        const std::string_view close = kind_ == Kind::Object ? "}" : "]";
        if (fields_.empty()) {
            return std::string{open} + std::string{close};
        }
        const std::string pad(2 * (depth + 1), ' ');
        std::string text{open};
        text += '\n';
        for (std::size_t i = 0; i < fields_.size(); ++i) {
            text += pad;
            if (!fields_[i].first.empty()) {
                text += fields_[i].first;
                text += ": ";
            }
            text += indent(fields_[i].second, depth + 1);
            if (i + 1 < fields_.size()) {
                text += ',';
            }
            text += '\n';
        }
        text += std::string(2 * depth, ' ');
        text += close;
        return text;
    }

private:
    /// Re-indents an already-rendered child so nesting reads correctly.
    [[nodiscard]] static std::string indent(const std::string& rendered, std::size_t depth) {
        const std::string pad(2 * depth, ' ');
        std::string out;
        for (const char c : rendered) {
            out += c;
            if (c == '\n') {
                out += pad;
            }
        }
        return out;
    }

    /// The one thing that must not be hand-rolled: a correctly escaped JSON
    /// string. Control characters go out as \\u00XX, because a document name
    /// is user text and a raw control byte would make the output unparseable.
    [[nodiscard]] static std::string quote(std::string_view value) {
        std::string out{'"'};
        for (const char c : value) {
            switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20U) {
                    out += std::format("\\u{:04x}", static_cast<unsigned int>(
                                                        static_cast<unsigned char>(c)));
                } else {
                    // UTF-8 bytes pass through: the document format is UTF-8
                    // and JSON strings are UTF-8, so a name needs no
                    // transcoding here.
                    out += c;
                }
                break;
            }
        }
        out += '"';
        return out;
    }

    Kind kind_;
    std::string raw_{};
    /// Rendered children, in insertion order. A vector and not a map, because
    /// a map would sort the output and the order is the caller's statement.
    std::vector<std::pair<std::string, std::string>> fields_{};
};

/// The rendered value, or an empty object when a command has nothing to say.
[[nodiscard]] inline std::string renderJson(const JsonValue& value) { return value.render(); }

} // namespace bettercad::cli
