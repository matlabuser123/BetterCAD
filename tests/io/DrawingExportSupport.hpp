#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/math/Point.hpp>

#include <catch2/catch_test_macros.hpp>

#include <charconv>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Independent readers for the three drawing export formats (P14-EXPORT-001).
//
// A writer TRANSCRIBES a scene, so a test must read the GENERATED FILE with
// something that is not the writer. A writer agreeing with itself proves
// nothing; the mistakes worth catching are serialization mistakes, and those
// are only visible from outside.
//
// These lived in an anonymous namespace inside DrawingExportTests.cpp until
// P14-REFMOD-001 needed them too. They are here rather than copied so that the
// reference models and the export milestone read their files with the SAME
// reader: two copies could drift, and a second copy written from the same
// misunderstanding would share its blind spot without anyone noticing.
//
// They remain deliberately small, and they share NO code with the writers:
// a group-code reader for DXF, an attribute scraper for SVG, an object and
// content-stream reader for PDF. They are not general-purpose parsers and are
// not meant to become any.
//
// They call REQUIRE, so they belong to a test translation unit.
namespace bettercad::test::drawex {

inline constexpr double kMm = 1e-6; ///< a nanometre on paper; the writers print 4 decimals of a mm

/// The message of a failed Result, or nothing. INFO builds its text with
/// operator<<, so a conditional of two different types cannot be handed to it.
template <typename T>
[[nodiscard]] std::string why(const Result<T>& result) {
    return result.has_value() ? std::string{} : result.error().message;
}

inline std::string bytesOf(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    REQUIRE(stream.good());
    return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}


// --- An independent DXF reader -------------------------------------------------------------------
//
// DXF is a list of (group code, value) pairs, one per two lines. That is the
// whole format, which is why forty lines can read it honestly.

struct DxfPair {
    int code;
    std::string value;
};

inline std::vector<DxfPair> readDxf(std::string_view text) {
    std::vector<DxfPair> pairs;
    std::size_t at = 0;
    const auto nextLine = [&]() -> std::optional<std::string_view> {
        if (at >= text.size()) {
            return std::nullopt;
        }
        const std::size_t end = text.find('\n', at);
        const std::string_view line = text.substr(at, end == std::string_view::npos ? end : end - at);
        at = end == std::string_view::npos ? text.size() : end + 1;
        return line;
    };
    while (true) {
        const auto codeLine = nextLine();
        if (!codeLine) {
            break;
        }
        const auto valueLine = nextLine();
        if (!valueLine) {
            break;
        }
        int code = 0;
        const auto trimmed = codeLine->substr(codeLine->find_first_not_of(" \t"));
        const auto [stop, ec] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), code);
        if (ec != std::errc{}) {
            continue;
        }
        pairs.push_back(DxfPair{code, std::string{*valueLine}});
    }
    return pairs;
}

/// Every entity in a DXF, as the pairs that follow its `0` code.
struct DxfEntity {
    std::string type;
    std::vector<DxfPair> pairs;

    [[nodiscard]] std::optional<std::string> value(int code) const {
        for (const DxfPair& pair : pairs) {
            if (pair.code == code) {
                return pair.value;
            }
        }
        return std::nullopt;
    }
    [[nodiscard]] double number(int code) const {
        const auto text = value(code);
        REQUIRE(text.has_value());
        return std::stod(*text);
    }
};

inline std::vector<DxfEntity> dxfEntities(std::string_view text) {
    const std::vector<DxfPair> pairs = readDxf(text);
    std::vector<DxfEntity> entities;
    bool inEntities = false;
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        if (pairs[i].code == 2 && pairs[i].value == "ENTITIES") {
            inEntities = true;
            continue;
        }
        if (!inEntities) {
            continue;
        }
        if (pairs[i].code == 0) {
            if (pairs[i].value == "ENDSEC" || pairs[i].value == "EOF") {
                break;
            }
            entities.push_back(DxfEntity{pairs[i].value, {}});
            continue;
        }
        if (!entities.empty()) {
            entities.back().pairs.push_back(pairs[i]);
        }
    }
    return entities;
}

/// A header variable's value, e.g. `$INSUNITS`.
inline std::optional<std::string> dxfHeader(std::string_view text, std::string_view name) {
    const std::vector<DxfPair> pairs = readDxf(text);
    for (std::size_t i = 0; i + 1 < pairs.size(); ++i) {
        if (pairs[i].code == 9 && pairs[i].value == name) {
            return pairs[i + 1].value;
        }
    }
    return std::nullopt;
}

// --- An independent SVG reader -------------------------------------------------------------------
//
// Not a general XML parser: an element scraper, which is what is needed to
// check that a drawing has the elements and the attributes it should.

struct SvgElement {
    std::string name;
    std::map<std::string, std::string> attributes;
    std::string body;
};

inline std::vector<SvgElement> readSvg(std::string_view text) {
    std::vector<SvgElement> elements;
    std::size_t at = 0;
    while ((at = text.find('<', at)) != std::string_view::npos) {
        ++at;
        if (at < text.size() && (text[at] == '?' || text[at] == '/' || text[at] == '!')) {
            at = text.find('>', at);
            if (at == std::string_view::npos) {
                break;
            }
            continue;
        }
        const std::size_t close = text.find('>', at);
        if (close == std::string_view::npos) {
            break;
        }
        std::string_view tag = text.substr(at, close - at);
        const bool selfClosing = !tag.empty() && tag.back() == '/';
        if (selfClosing) {
            tag.remove_suffix(1);
        }
        SvgElement element;
        const std::size_t nameEnd = tag.find_first_of(" \t\n");
        element.name = std::string{tag.substr(0, nameEnd)};
        // attribute="value", repeatedly
        std::size_t scan = nameEnd == std::string_view::npos ? tag.size() : nameEnd;
        while (scan < tag.size()) {
            const std::size_t equals = tag.find('=', scan);
            if (equals == std::string_view::npos) {
                break;
            }
            const std::size_t quote = tag.find('"', equals);
            if (quote == std::string_view::npos) {
                break;
            }
            const std::size_t endQuote = tag.find('"', quote + 1);
            if (endQuote == std::string_view::npos) {
                break;
            }
            std::string_view key = tag.substr(scan, equals - scan);
            while (!key.empty() && (key.front() == ' ' || key.front() == '\n' || key.front() == '\t')) {
                key.remove_prefix(1);
            }
            element.attributes[std::string{key}] = std::string{tag.substr(quote + 1, endQuote - quote - 1)};
            scan = endQuote + 1;
        }
        if (!selfClosing) {
            const std::size_t bodyEnd = text.find('<', close);
            if (bodyEnd != std::string_view::npos) {
                element.body = std::string{text.substr(close + 1, bodyEnd - close - 1)};
            }
        }
        elements.push_back(std::move(element));
        at = close;
    }
    return elements;
}

inline const SvgElement* findSvg(const std::vector<SvgElement>& elements, std::string_view name) {
    const auto found = std::ranges::find(elements, name, &SvgElement::name);
    return found == elements.end() ? nullptr : &*found;
}

inline std::vector<const SvgElement*> allSvg(const std::vector<SvgElement>& elements,
                                                  std::string_view name) {
    std::vector<const SvgElement*> found;
    for (const SvgElement& element : elements) {
        if (element.name == name) {
            found.push_back(&element);
        }
    }
    return found;
}

// --- An independent PDF reader --------------------------------------------------------------------
//
// Enough of PDF to check a drawing: the page box, and the content stream's
// operators. The file this reads is uncompressed by design, which is what
// makes an honest read-back possible without a dependency.

inline std::string pdfContentStream(std::string_view pdf) {
    const std::size_t start = pdf.find("stream\n");
    REQUIRE(start != std::string_view::npos);
    const std::size_t end = pdf.find("endstream", start);
    REQUIRE(end != std::string_view::npos);
    return std::string{pdf.substr(start + 7, end - start - 7)};
}

/// The four numbers of `/MediaBox [a b c d]`.
inline std::vector<double> pdfMediaBox(std::string_view pdf) {
    const std::size_t at = pdf.find("/MediaBox [");
    REQUIRE(at != std::string_view::npos);
    const std::size_t close = pdf.find(']', at);
    REQUIRE(close != std::string_view::npos);
    const std::string inside{pdf.substr(at + 11, close - at - 11)};
    std::vector<double> numbers;
    std::size_t scan = 0;
    while (scan < inside.size()) {
        while (scan < inside.size() && inside[scan] == ' ') {
            ++scan;
        }
        if (scan >= inside.size()) {
            break;
        }
        std::size_t used = 0;
        numbers.push_back(std::stod(inside.substr(scan), &used));
        scan += used;
    }
    return numbers;
}

/// Every `x y m` and `x y l` in a content stream, as points.
inline std::vector<Point2D> pdfPathPoints(std::string_view content) {
    std::vector<Point2D> points;
    std::size_t at = 0;
    while (at < content.size()) {
        const std::size_t end = content.find('\n', at);
        const std::string_view line =
            content.substr(at, end == std::string_view::npos ? end : end - at);
        at = end == std::string_view::npos ? content.size() : end + 1;
        if (line.size() < 3) {
            continue;
        }
        const bool isMove = line.ends_with(" m");
        const bool isLine = line.ends_with(" l");
        if (!isMove && !isLine) {
            continue;
        }
        double x = 0.0;
        double y = 0.0;
        std::size_t used = 0;
        const std::string text{line};
        x = std::stod(text, &used);
        y = std::stod(text.substr(used));
        points.push_back(Point2D{Length::fromSi(x * 25.4 / 72.0 * 1e-3),
                                 Length::fromSi(y * 25.4 / 72.0 * 1e-3)});
    }
    return points;
}

} // namespace bettercad::test::drawex
