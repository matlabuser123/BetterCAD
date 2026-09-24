#include <bettercad/io/DrawingExport.hpp>

#include "FileIo.hpp"
#include "SceneNumbers.hpp"
#include "TextEncoding.hpp"

#include <bettercad/core/Units.hpp>

#include <format>
#include <string>
#include <vector>

// PDF from a drawing scene (P14-EXPORT-001).
//
// TRUE VECTOR PDF, written here rather than through a dependency. Nothing is
// rasterised: a line is `m`/`l`/`S`, an arc is Béziers, text is `Tj`. A PDF
// containing a picture of a drawing is not a drawing, and a test asserts the
// file contains no image object at all.
//
// THE ONE CONVERSION: PDF measures in points, 72 to the inch, so
//
//     pt = mm * 72 / 25.4
//
// and it happens in `pt()` and nowhere else. 210 mm is 595.2756 pt and 297 mm
// is 841.8898 pt, which is A4 -- the arithmetic is checked against those two
// figures by a test that does not call this function.
//
// COORDINATES AGREE WITH THE SCENE: PDF's origin is the bottom-left with +Y
// up, which is the scene's own. Nothing is flipped.
//
// DETERMINISM: there is no creation date, no producer, no document ID and no
// compression. The same scene gives the same bytes. That is a deliberate
// choice about what goes in the file, not a normalisation applied afterwards.
namespace bettercad::io {
namespace {

using detail::dashPattern;
using detail::toWinAnsi;

/// Millimetres to PDF points, in the one place.
[[nodiscard]] double pt(double millimetres) noexcept { return millimetres * 72.0 / 25.4; }
[[nodiscard]] double pt(Length length) noexcept { return pt(length.in(units::mm)); }

/// A number in a content stream or a page box. Fixed, four decimals, no
/// locale, no negative zero -- the same rules as everywhere else, in points
/// rather than millimetres.
[[nodiscard]] std::string num(double value) {
    if (value == 0.0) {
        value = 0.0;
    }
    return std::format("{:.4f}", value);
}

/// A PDF string literal: parentheses and backslashes escaped, and anything
/// outside printable ASCII written as an octal escape so the file stays
/// 7-bit and a byte-for-byte comparison means the same thing everywhere.
/// A PDF string literal, from the scene's UTF-8.
///
/// TRANSCODED, not escaped byte by byte: the font is Helvetica with
/// /WinAnsiEncoding, so a character is the ONE byte that encoding gives it.
/// Escaping the UTF-8 bytes instead would write "Ø" as C3 98 and a reader
/// would show two glyphs, "Ã˜" -- the scene's text, silently changed.
[[nodiscard]] std::string literal(std::string_view text) {
    const std::string encoded = toWinAnsi(text);
    std::string out = "(";
    for (const char raw : encoded) {
        const auto c = static_cast<unsigned char>(raw);
        if (c == '(' || c == ')' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c < 32 || c > 126) {
            out += std::format("\\{:03o}", c);
        } else {
            out += static_cast<char>(c);
        }
    }
    return out + ")";
}

/// Appends an arc as Béziers, which is what a PDF has instead of a circle.
///
/// A quarter turn at a time, with the standard control-point distance
/// k = 4/3 * tan(sweep/4). The error of that approximation over 90 degrees is
/// under 3 parts in 10,000 of the radius -- for a 50 mm circle, 15 micrometres,
/// which is far below what a plotter or a printer resolves.
void appendArc(std::string& content, const drawing::SceneArc& arc, bool startNewPath) {
    const double cx = arc.centre.x.in(units::mm);
    const double cy = arc.centre.y.in(units::mm);
    const double r = arc.radius.in(units::mm);
    const double start = arc.start.si();
    const double total = arc.sweep.si();
    const int quarters = std::max(1, static_cast<int>(std::ceil(std::abs(total) / (std::numbers::pi / 2.0))));
    const double step = total / static_cast<double>(quarters);

    const auto at = [&](double angle) {
        return std::pair{cx + r * std::cos(angle), cy + r * std::sin(angle)};
    };
    if (startNewPath) {
        const auto [x, y] = at(start);
        content += std::format("{} {} m\n", num(pt(x)), num(pt(y)));
    }
    const double k = 4.0 / 3.0 * std::tan(step / 4.0);
    for (int i = 0; i < quarters; ++i) {
        const double from = start + step * static_cast<double>(i);
        const double to = from + step;
        const auto [x0, y0] = at(from);
        const auto [x1, y1] = at(to);
        // The control points are along the tangents at each end.
        const double c1x = x0 - k * r * std::sin(from);
        const double c1y = y0 + k * r * std::cos(from);
        const double c2x = x1 + k * r * std::sin(to);
        const double c2y = y1 - k * r * std::cos(to);
        content += std::format("{} {} {} {} {} {} c\n", num(pt(c1x)), num(pt(c1y)), num(pt(c2x)),
                               num(pt(c2y)), num(pt(x1)), num(pt(y1)));
    }
}

/// The graphics state a style needs: width, and a dash array in points.
[[nodiscard]] std::string strokeState(drawing::LineStyle style, Length width) {
    std::string out = std::format("{} w\n", num(pt(width)));
    const std::vector<double> pattern = dashPattern(style);
    if (pattern.empty()) {
        out += "[] 0 d\n";
        return out;
    }
    out += "[";
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        out += (i == 0 ? "" : " ") + num(pt(pattern[i]));
    }
    out += "] 0 d\n";
    return out;
}

} // namespace

Result<std::string> pdfDocument(const drawing::DrawingScene& scene) {
    if (auto valid = validate(scene); !valid) {
        return std::unexpected(valid.error());
    }

    // --- the page content, in points.
    std::string content;
    content += "1 J 1 j\n"; // round caps and joins, as a pen draws
    for (const drawing::SceneLine& line : scene.items.lines) {
        content += strokeState(line.style, line.width);
        for (std::size_t i = 0; i < line.points.size(); ++i) {
            content += std::format("{} {} {}\n", num(pt(line.points[i].x)),
                                   num(pt(line.points[i].y)), i == 0 ? "m" : "l");
        }
        content += "S\n";
    }
    for (const drawing::SceneArc& arc : scene.items.arcs) {
        content += strokeState(arc.style, arc.width);
        appendArc(content, arc, true);
        content += "S\n";
    }
    for (const drawing::SceneText& text : scene.items.texts) {
        const double size = pt(text.height);
        // The anchor is applied by shifting the text, because PDF has no
        // alignment of its own. Widths are estimated from the standard font's
        // average advance, which is the one place this writer approximates --
        // and it moves text, never geometry. Recorded as a limitation.
        // The GLYPH count, not the byte count: "Ø20" is four UTF-8 bytes and
        // three characters, and centring it on four would push it left.
        const double estimated = 0.5 * size * static_cast<double>(toWinAnsi(text.text).size());
        double dx = 0.0;
        double dy = 0.0;
        switch (text.anchor) {
        case drawing::TextAnchor::BaselineCentre:
        case drawing::TextAnchor::MiddleCentre:
            dx = -estimated / 2.0;
            break;
        case drawing::TextAnchor::BaselineRight:
        case drawing::TextAnchor::MiddleRight:
            dx = -estimated;
            break;
        default:
            break;
        }
        if (text.anchor == drawing::TextAnchor::MiddleLeft ||
            text.anchor == drawing::TextAnchor::MiddleCentre ||
            text.anchor == drawing::TextAnchor::MiddleRight) {
            dy = -size * 0.35; // half a cap height, so the point is the middle
        }
        const double x = pt(text.at.x);
        const double y = pt(text.at.y);
        content += "BT\n";
        content += std::format("/F1 {} Tf\n", num(size));
        if (text.rotation.si() != 0.0) {
            const double c = std::cos(text.rotation.si());
            const double s = std::sin(text.rotation.si());
            // The rotation is about the anchor point, so the offset turns with it.
            const double ox = dx * c - dy * s;
            const double oy = dx * s + dy * c;
            content += std::format("{} {} {} {} {} {} Tm\n", num(c), num(s), num(-s), num(c),
                                   num(x + ox), num(y + oy));
        } else {
            content += std::format("1 0 0 1 {} {} Tm\n", num(x + dx), num(y + dy));
        }
        content += literal(text.text) + " Tj\n";
        content += "ET\n";
    }

    // --- the file: five objects, then an xref built from where each landed.
    const std::string pageWidth = num(pt(scene.width));
    const std::string pageHeight = num(pt(scene.height));
    std::vector<std::string> objects;
    objects.push_back("<< /Type /Catalog /Pages 2 0 R >>");
    objects.push_back("<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    objects.push_back(std::format("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {} {}] "
                                  "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
                                  pageWidth, pageHeight));
    objects.push_back(std::format("<< /Length {} >>\nstream\n{}endstream", content.size(), content));
    // Helvetica: one of the fourteen fonts every PDF reader has, so nothing is
    // embedded, nothing is licensed and nothing depends on the host's fonts.
    objects.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
                      "/Encoding /WinAnsiEncoding >>");

    std::string out = "%PDF-1.4\n";
    // A comment of high bytes, which tells every tool the file is binary.
    out += "%\xE2\xE3\xCF\xD3\n";
    std::vector<std::size_t> offsets;
    offsets.reserve(objects.size());
    for (std::size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(out.size());
        out += std::format("{} 0 obj\n{}\nendobj\n", i + 1, objects[i]);
    }
    const std::size_t xref = out.size();
    out += std::format("xref\n0 {}\n", objects.size() + 1);
    out += "0000000000 65535 f \n";
    for (const std::size_t offset : offsets) {
        out += std::format("{:010} 00000 n \n", offset);
    }
    // No /Info, no /ID, no dates: nothing that would differ between two runs.
    out += std::format("trailer\n<< /Size {} /Root 1 0 R >>\nstartxref\n{}\n%%EOF\n",
                       objects.size() + 1, xref);
    return out;
}

Result<void> exportPdf(const drawing::DrawingScene& scene, const std::filesystem::path& path) {
    auto text = pdfDocument(scene);
    if (!text) {
        return std::unexpected(text.error());
    }
    return detail::writeFileAtomically(path, *text);
}

} // namespace bettercad::io
