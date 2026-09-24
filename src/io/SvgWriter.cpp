#include <bettercad/io/DrawingExport.hpp>

#include "FileIo.hpp"
#include "SceneNumbers.hpp"

#include <bettercad/core/Units.hpp>

#include <format>
#include <string>

// SVG from a drawing scene (P14-EXPORT-001).
//
// THE ONE TRANSFORM. The scene's origin is the sheet's bottom-left with +Y UP;
// SVG's is the top-left with +Y DOWN. So every y becomes `height - y`, here,
// in `flip()`, and nowhere else. It is the only arithmetic in this file that
// is not a unit conversion, and it is the mistake this milestone is most
// likely to make -- so the asymmetric fixture puts a line near the TOP-LEFT
// and a circle near the BOTTOM-RIGHT, where a forgotten flip cannot hide.
//
// The page is declared in millimetres and the viewBox is in the same units, so
// the drawing is 420 mm wide because it says so, not because a viewer guessed
// a pixel density.
namespace bettercad::io {
namespace {

using detail::dashPattern;
using detail::mm;

/// The scene's y, in SVG's downward y.
[[nodiscard]] double flip(const drawing::DrawingScene& scene, Length y) {
    return scene.height.in(units::mm) - y.in(units::mm);
}

/// `stroke-dasharray` for a style, or nothing for a continuous one.
[[nodiscard]] std::string dashAttribute(drawing::LineStyle style) {
    const std::vector<double> pattern = dashPattern(style);
    if (pattern.empty()) {
        return {};
    }
    std::string text = R"( stroke-dasharray=")";
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        text += (i == 0 ? "" : " ") + mm(pattern[i]);
    }
    return text + '"';
}

/// XML text, with the five characters that cannot appear raw replaced.
[[nodiscard]] std::string escaped(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&apos;";
            break;
        default:
            out += c;
        }
    }
    return out;
}

/// SVG's own word for where the text sits horizontally.
[[nodiscard]] std::string_view anchorWord(drawing::TextAnchor anchor) {
    switch (anchor) {
    case drawing::TextAnchor::BaselineLeft:
    case drawing::TextAnchor::MiddleLeft:
        return "start";
    case drawing::TextAnchor::BaselineCentre:
    case drawing::TextAnchor::MiddleCentre:
        return "middle";
    case drawing::TextAnchor::BaselineRight:
    case drawing::TextAnchor::MiddleRight:
        return "end";
    }
    return "start";
}

/// Whether the anchor centres the text on the point vertically.
[[nodiscard]] bool middleVertically(drawing::TextAnchor anchor) noexcept {
    return anchor == drawing::TextAnchor::MiddleLeft ||
           anchor == drawing::TextAnchor::MiddleCentre ||
           anchor == drawing::TextAnchor::MiddleRight;
}

} // namespace

Result<std::string> svgDocument(const drawing::DrawingScene& scene) {
    if (auto valid = validate(scene); !valid) {
        return std::unexpected(valid.error());
    }
    const std::string width = mm(scene.width);
    const std::string height = mm(scene.height);

    std::string out;
    out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out += std::format(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\"{}mm\" height=\"{}mm\" "
        "viewBox=\"0 0 {} {}\">\n",
        width, height, width, height);
    // Every stroke is black and nothing is filled: a technical drawing is
    // lines, and a default fill would flood every closed polyline.
    out += "<g fill=\"none\" stroke=\"black\" stroke-linecap=\"round\" "
           "stroke-linejoin=\"round\">\n";

    for (const drawing::SceneLine& line : scene.items.lines) {
        out += std::format("<polyline points=\"");
        for (std::size_t i = 0; i < line.points.size(); ++i) {
            out += std::format("{}{},{}", i == 0 ? "" : " ", mm(line.points[i].x),
                               mm(flip(scene, line.points[i].y)));
        }
        out += std::format("\" stroke-width=\"{}\"{}/>\n", mm(line.width), dashAttribute(line.style));
    }

    for (const drawing::SceneArc& arc : scene.items.arcs) {
        const double radius = arc.radius.in(units::mm);
        const double sweep = arc.sweep.in(units::deg);
        if (std::abs(sweep) >= 359.999) {
            // A whole circle is a circle, not a path that happens to close.
            out += std::format("<circle cx=\"{}\" cy=\"{}\" r=\"{}\" stroke-width=\"{}\"{}/>\n",
                               mm(arc.centre.x), mm(flip(scene, arc.centre.y)), mm(radius),
                               mm(arc.width), dashAttribute(arc.style));
            continue;
        }
        const double start = arc.start.si();
        const double end = start + arc.sweep.si();
        const auto at = [&](double angle) {
            return Point2D{arc.centre.x + Length::fromSi(arc.radius.si() * std::cos(angle)),
                           arc.centre.y + Length::fromSi(arc.radius.si() * std::sin(angle))};
        };
        const Point2D from = at(start);
        const Point2D to = at(end);
        // The flip reverses which way an arc turns, so the sweep flag is the
        // opposite of what it would be in scene coordinates.
        const int large = std::abs(sweep) > 180.0 ? 1 : 0;
        const int clockwise = sweep > 0.0 ? 0 : 1;
        out += std::format("<path d=\"M {} {} A {} {} 0 {} {} {} {}\" stroke-width=\"{}\"{}/>\n",
                           mm(from.x), mm(flip(scene, from.y)), mm(radius), mm(radius), large,
                           clockwise, mm(to.x), mm(flip(scene, to.y)), mm(arc.width),
                           dashAttribute(arc.style));
    }

    for (const drawing::SceneText& text : scene.items.texts) {
        const double x = text.at.x.in(units::mm);
        const double y = flip(scene, text.at.y);
        std::string transform;
        if (text.rotation.si() != 0.0) {
            // SVG rotates clockwise for a positive angle and the scene
            // anticlockwise, so the sign turns over with the y axis.
            transform = std::format(" transform=\"rotate({} {} {})\"",
                                    detail::degrees(Angle::fromSi(-text.rotation.si())), mm(x), mm(y));
        }
        out += std::format(
            "<text x=\"{}\" y=\"{}\" font-size=\"{}\" font-family=\"sans-serif\" "
            "text-anchor=\"{}\"{}{} fill=\"black\" stroke=\"none\">{}</text>\n",
            mm(x), mm(y), mm(text.height), anchorWord(text.anchor),
            middleVertically(text.anchor) ? " dominant-baseline=\"central\"" : "", transform,
            escaped(text.text));
    }

    out += "</g>\n</svg>\n";
    return out;
}

Result<void> exportSvg(const drawing::DrawingScene& scene, const std::filesystem::path& path) {
    auto text = svgDocument(scene);
    if (!text) {
        return std::unexpected(text.error());
    }
    return detail::writeFileAtomically(path, *text);
}

} // namespace bettercad::io
