#pragma once

// A small STRICT JSON reader for the CLI tests (P18-CLI-001).
//
// WHY IT EXISTS. The CLI writes JSON through `JsonValue`, so a test that read the payload back with string searches would be
// checking the writer against itself: a stray `nan`, a trailing banner, a doubled value or a key that is not where the test
// looked would all pass. The process-level workflow script reads payloads with CMake's `string(JSON)`, which is a real parser. This
// is the same discipline for the in-process tests: an INDEPENDENT reader that follows RFC 8259 and refuses everything else.
//
// STRICT, which is the point. It rejects: anything after the one top-level value (a banner, a second object, a trailing comma), a
// leading zero, a bare `nan`, `inf` or `Infinity`, a single-quoted string, an unescaped control character, an unterminated string
// or container, and a duplicate key (a payload that repeats a key has two answers).
//
// NOT A DOM FOR PRODUCTION. Numbers are doubles (the CLI never writes an integer beyond 2^53), objects keep insertion order so a test
// can assert the order the writer promises, and the accessors REQUIRE (Catch2) so a missing key names itself.

#include <catch2/catch_test_macros.hpp>

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bettercad::test {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    [[nodiscard]] Type type() const noexcept { return type_; }
    [[nodiscard]] bool isNull() const noexcept { return type_ == Type::Null; }
    [[nodiscard]] bool isObject() const noexcept { return type_ == Type::Object; }
    [[nodiscard]] bool isArray() const noexcept { return type_ == Type::Array; }
    [[nodiscard]] bool isNumber() const noexcept { return type_ == Type::Number; }
    [[nodiscard]] bool isString() const noexcept { return type_ == Type::String; }
    [[nodiscard]] bool isBool() const noexcept { return type_ == Type::Bool; }

    [[nodiscard]] double number() const {
        REQUIRE(type_ == Type::Number);
        return number_;
    }
    [[nodiscard]] const std::string& text() const {
        REQUIRE(type_ == Type::String);
        return text_;
    }
    [[nodiscard]] bool boolean() const {
        REQUIRE(type_ == Type::Bool);
        return bool_;
    }
    [[nodiscard]] std::size_t size() const {
        REQUIRE((type_ == Type::Array || type_ == Type::Object));
        return type_ == Type::Array ? items_.size() : members_.size();
    }

    [[nodiscard]] bool has(std::string_view key) const {
        if (type_ != Type::Object) {
            return false;
        }
        for (const auto& member : members_) {
            if (member.first == key) {
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] const Json& operator[](std::string_view key) const {
        INFO("key '" << key << "'");
        REQUIRE(type_ == Type::Object);
        for (const auto& member : members_) {
            if (member.first == key) {
                return member.second;
            }
        }
        FAIL("no such key: " << key);
        return *this;
    }
    [[nodiscard]] const Json& operator[](std::size_t index) const {
        REQUIRE(type_ == Type::Array);
        REQUIRE(index < items_.size());
        return items_[index];
    }
    /// The keys of an object, in the order the writer wrote them.
    [[nodiscard]] std::vector<std::string> keys() const {
        REQUIRE(type_ == Type::Object);
        std::vector<std::string> out;
        for (const auto& member : members_) {
            out.push_back(member.first);
        }
        return out;
    }

    /// `{"value": v, "unit": u}` -> v, requiring the unit to be @p unit. A quantity is never a bare number.
    [[nodiscard]] double quantity(std::string_view unit) const {
        REQUIRE(type_ == Type::Object);
        CHECK((*this)["unit"].text() == unit);
        return (*this)["value"].number();
    }

private:
    friend class JsonReader;
    Type type_ = Type::Null;
    double number_ = 0.0;
    bool bool_ = false;
    std::string text_{};
    std::vector<Json> items_{};
    std::vector<std::pair<std::string, Json>> members_{};
};

class JsonReader {
public:
    /// The one top-level value of @p text, or nullopt with @p why saying where it stopped being JSON.
    [[nodiscard]] static std::optional<Json> parse(std::string_view text, std::string* why = nullptr) {
        JsonReader reader{text};
        std::optional<Json> value = reader.value(0);
        if (value) {
            reader.skipSpace();
            if (reader.at_ != text.size()) {
                reader.fail("unexpected text after the top-level value");
                value.reset();
            }
        }
        if (!value && why != nullptr) {
            *why = reader.why_;
        }
        return value;
    }

private:
    explicit JsonReader(std::string_view text) : text_(text) {}

    void fail(std::string message) {
        if (why_.empty()) {
            why_ = std::move(message) + " at offset " + std::to_string(at_);
        }
    }
    void skipSpace() {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\n' || text_[at_] == '\r' || text_[at_] == '\t')) {
            ++at_;
        }
    }
    [[nodiscard]] bool consume(std::string_view literal) {
        if (text_.substr(at_, literal.size()) == literal) {
            at_ += literal.size();
            return true;
        }
        return false;
    }

    [[nodiscard]] std::optional<Json> value(std::size_t depth) {
        if (depth > 64U) {
            fail("nested too deeply");
            return std::nullopt;
        }
        skipSpace();
        if (at_ >= text_.size()) {
            fail("unexpected end");
            return std::nullopt;
        }
        Json json;
        const char c = text_[at_];
        if (c == '{') {
            return object(depth);
        }
        if (c == '[') {
            return array(depth);
        }
        if (c == '"') {
            auto s = string();
            if (!s) {
                return std::nullopt;
            }
            json.type_ = Json::Type::String;
            json.text_ = std::move(*s);
            return json;
        }
        if (consume("true")) {
            json.type_ = Json::Type::Bool;
            json.bool_ = true;
            return json;
        }
        if (consume("false")) {
            json.type_ = Json::Type::Bool;
            return json;
        }
        if (consume("null")) {
            return json;
        }
        return number();
    }

    [[nodiscard]] std::optional<Json> number() {
        const std::size_t start = at_;
        if (at_ < text_.size() && text_[at_] == '-') {
            ++at_;
        }
        if (at_ >= text_.size() || text_[at_] < '0' || text_[at_] > '9') {
            at_ = start;
            fail("not a JSON value");
            return std::nullopt;
        }
        if (text_[at_] == '0' && at_ + 1 < text_.size() && text_[at_ + 1] >= '0' && text_[at_ + 1] <= '9') {
            fail("leading zero");
            return std::nullopt;
        }
        while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') {
            ++at_;
        }
        if (at_ < text_.size() && text_[at_] == '.') {
            ++at_;
            const std::size_t digits = at_;
            while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') {
                ++at_;
            }
            if (at_ == digits) {
                fail("no digits after the decimal point");
                return std::nullopt;
            }
        }
        if (at_ < text_.size() && (text_[at_] == 'e' || text_[at_] == 'E')) {
            ++at_;
            if (at_ < text_.size() && (text_[at_] == '+' || text_[at_] == '-')) {
                ++at_;
            }
            const std::size_t digits = at_;
            while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') {
                ++at_;
            }
            if (at_ == digits) {
                fail("no digits in the exponent");
                return std::nullopt;
            }
        }
        double parsed = 0.0;
        const auto [end, error] = std::from_chars(text_.data() + start, text_.data() + at_, parsed);
        if (error != std::errc{} || end != text_.data() + at_ || !std::isfinite(parsed)) {
            fail("number out of range");
            return std::nullopt;
        }
        Json json;
        json.type_ = Json::Type::Number;
        json.number_ = parsed;
        return json;
    }

    [[nodiscard]] std::optional<std::string> string() {
        ++at_;   // opening quote
        std::string out;
        while (at_ < text_.size()) {
            const char c = text_[at_++];
            if (c == '"') {
                return out;
            }
            if (static_cast<unsigned char>(c) < 0x20U) {
                fail("unescaped control character in a string");
                return std::nullopt;
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at_ >= text_.size()) {
                break;
            }
            const char e = text_[at_++];
            switch (e) {
            case '"':
            case '\\':
            case '/':
                out += e;
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u': {
                if (at_ + 4U > text_.size()) {
                    fail("short \\u escape");
                    return std::nullopt;
                }
                unsigned code = 0;
                const auto [end, error] = std::from_chars(text_.data() + at_, text_.data() + at_ + 4, code, 16);
                if (error != std::errc{} || end != text_.data() + at_ + 4) {
                    fail("bad \\u escape");
                    return std::nullopt;
                }
                at_ += 4U;
                out += code < 0x80U ? std::string(1, static_cast<char>(code)) : std::string{"?"};
                break;
            }
            default:
                fail("bad escape");
                return std::nullopt;
            }
        }
        fail("unterminated string");
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Json> array(std::size_t depth) {
        ++at_;
        Json json;
        json.type_ = Json::Type::Array;
        skipSpace();
        if (at_ < text_.size() && text_[at_] == ']') {
            ++at_;
            return json;
        }
        for (;;) {
            auto item = value(depth + 1U);
            if (!item) {
                return std::nullopt;
            }
            json.items_.push_back(std::move(*item));
            skipSpace();
            if (at_ >= text_.size()) {
                fail("unterminated array");
                return std::nullopt;
            }
            if (text_[at_] == ',') {
                ++at_;
                continue;
            }
            if (text_[at_] == ']') {
                ++at_;
                return json;
            }
            fail("expected , or ] in an array");
            return std::nullopt;
        }
    }

    [[nodiscard]] std::optional<Json> object(std::size_t depth) {
        ++at_;
        Json json;
        json.type_ = Json::Type::Object;
        skipSpace();
        if (at_ < text_.size() && text_[at_] == '}') {
            ++at_;
            return json;
        }
        for (;;) {
            skipSpace();
            if (at_ >= text_.size() || text_[at_] != '"') {
                fail("expected a string key");
                return std::nullopt;
            }
            auto key = string();
            if (!key) {
                return std::nullopt;
            }
            for (const auto& existing : json.members_) {
                if (existing.first == *key) {
                    fail("duplicate key '" + *key + "'");
                    return std::nullopt;
                }
            }
            skipSpace();
            if (at_ >= text_.size() || text_[at_] != ':') {
                fail("expected :");
                return std::nullopt;
            }
            ++at_;
            auto member = value(depth + 1U);
            if (!member) {
                return std::nullopt;
            }
            json.members_.emplace_back(std::move(*key), std::move(*member));
            skipSpace();
            if (at_ >= text_.size()) {
                fail("unterminated object");
                return std::nullopt;
            }
            if (text_[at_] == ',') {
                ++at_;
                continue;
            }
            if (text_[at_] == '}') {
                ++at_;
                return json;
            }
            fail("expected , or } in an object");
            return std::nullopt;
        }
    }

    std::string_view text_;
    std::size_t at_ = 0;
    std::string why_{};
};

/// The payload as one parsed top-level OBJECT, or a failed REQUIRE naming where it stopped being JSON.
[[nodiscard]] inline Json parseObject(const std::string& payload) {
    std::string why;
    const std::optional<Json> json = JsonReader::parse(payload, &why);
    INFO("payload:\n" << payload);
    INFO("parser: " << why);
    REQUIRE(json.has_value());
    REQUIRE(json->isObject());
    return *json;
}

} // namespace bettercad::test
