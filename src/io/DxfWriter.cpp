#include <bettercad/io/DrawingExport.hpp>

#include "FileIo.hpp"
#include "SceneNumbers.hpp"
#include "TextEncoding.hpp"

#include <bettercad/core/Units.hpp>

#include <format>
#include <string>

// DXF from a drawing scene (P14-EXPORT-001).
//
// AutoCAD R12 ASCII: the version every reader accepts, and one that already
// has every entity a drawing needs -- LINE, LWPOLYLINE, CIRCLE, ARC, TEXT.
// This writes DRAWINGS and nothing else: no import, no 3D, no blocks, no
// interoperability beyond getting a sheet into another package.
//
// DXF IS A LIST OF PAIRS: a group code on one line, its value on the next.
// Code 0 starts an entity, 8 is its layer, 10/20 a point, and so on. There is
// no nesting and no escaping, which is why an independent reader for it is
// forty lines and why this milestone can validate its own output honestly.
//
// COORDINATES AGREE WITH THE SCENE: millimetres, +Y up, origin bottom-left.
// Nothing is flipped or scaled. `$INSUNITS` 4 says millimetres explicitly, so
// a reader never has to guess -- leaving it out is how a 420 mm sheet arrives
// somewhere as 420 inches.
//
// TEXT IS IN THE DRAWING'S CODE PAGE, named by `$DWGCODEPAGE`. R12 predates
// Unicode and has no UTF-8: writing UTF-8 bytes into a code-1 value and saying
// nothing leaves the reader to guess, and it guesses its own locale's page --
// so "Ø20" arrives as "Ã˜20" on one machine and something else on another.
// ANSI_1252 is declared and the text is transcoded to it.
namespace bettercad::io {
namespace {

using detail::mm;
using detail::toWinAnsi;

/// One group code and its value.
void pair(std::string& out, int code, std::string_view value) {
    out += std::format("{}\n{}\n", code, value);
}
void pair(std::string& out, int code, int value) {
    out += std::format("{}\n{}\n", code, value);
}

/// The layer a style is drawn on, which is also where its linetype lives.
///
/// Layers carry the line CLASS rather than the geometry's meaning, because
/// that is what a DXF reader can act on: a receiving package turns hidden
/// lines off by turning off a layer.
[[nodiscard]] std::string_view layerFor(drawing::LineStyle style) noexcept {
    switch (style) {
    case drawing::LineStyle::Continuous:
        return "VISIBLE";
    case drawing::LineStyle::Dashed:
        return "HIDDEN";
    case drawing::LineStyle::Centre:
        return "CENTRE";
    case drawing::LineStyle::Thin:
        return "THIN";
    }
    return "VISIBLE";
}

/// The linetype name for a style. R12's own names, so a reader recognises
/// them without a table of ours.
[[nodiscard]] std::string_view linetypeFor(drawing::LineStyle style) noexcept {
    switch (drawing::LineStyle{style}) {
    case drawing::LineStyle::Dashed:
        return "DASHED";
    case drawing::LineStyle::Centre:
        return "CENTER";
    case drawing::LineStyle::Continuous:
    case drawing::LineStyle::Thin:
        break;
    }
    return "CONTINUOUS";
}

/// A width in millimetres as R12's lineweight: hundredths of a millimetre.
[[nodiscard]] int lineweight(Length width) noexcept {
    return static_cast<int>(std::lround(width.in(units::mm) * 100.0));
}

void writeLinetypeTable(std::string& out) {
    pair(out, 0, "TABLE");
    pair(out, 2, "LTYPE");
    pair(out, 70, 3);
    for (const auto& [name, description, total, dashes] :
         std::vector<std::tuple<const char*, const char*, double, std::vector<double>>>{
             {"CONTINUOUS", "Solid line", 0.0, {}},
             {"DASHED", "Dashed __ __ __ __", 6.0, {4.0, -2.0}},
             {"CENTER", "Centre ____ _ ____ _", 18.0, {12.0, -2.0, 2.0, -2.0}}}) {
        pair(out, 0, "LTYPE");
        pair(out, 2, name);
        pair(out, 70, 0);
        pair(out, 3, description);
        pair(out, 72, 65); // 'A', the only alignment R12 defines
        pair(out, 73, static_cast<int>(dashes.size()));
        pair(out, 40, mm(total));
        for (const double dash : dashes) {
            pair(out, 49, mm(dash));
        }
    }
    pair(out, 0, "ENDTAB");
}

void writeLayerTable(std::string& out) {
    pair(out, 0, "TABLE");
    pair(out, 2, "LAYER");
    pair(out, 70, 4);
    for (const drawing::LineStyle style :
         {drawing::LineStyle::Continuous, drawing::LineStyle::Dashed, drawing::LineStyle::Centre,
          drawing::LineStyle::Thin}) {
        pair(out, 0, "LAYER");
        pair(out, 2, layerFor(style));
        pair(out, 70, 0);
        pair(out, 62, 7); // white/black, whichever the reader's paper is
        pair(out, 6, linetypeFor(style));
    }
    pair(out, 0, "ENDTAB");
}

} // namespace

Result<std::string> dxfDocument(const drawing::DrawingScene& scene) {
    if (auto valid = validate(scene); !valid) {
        return std::unexpected(valid.error());
    }
    std::string out;

    // --- HEADER: the units, and the sheet's extent.
    pair(out, 0, "SECTION");
    pair(out, 2, "HEADER");
    pair(out, 9, "$ACADVER");
    pair(out, 1, "AC1009"); // R12
    pair(out, 9, "$INSUNITS");
    pair(out, 70, 4); // millimetres, said rather than assumed
    pair(out, 9, "$DWGCODEPAGE");
    pair(out, 3, "ANSI_1252"); // the encoding TEXT values are written in
    pair(out, 9, "$EXTMIN");
    pair(out, 10, mm(0.0));
    pair(out, 20, mm(0.0));
    pair(out, 9, "$EXTMAX");
    pair(out, 10, mm(scene.width));
    pair(out, 20, mm(scene.height));
    pair(out, 0, "ENDSEC");

    // --- TABLES: the linetypes and layers the entities refer to.
    pair(out, 0, "SECTION");
    pair(out, 2, "TABLES");
    writeLinetypeTable(out);
    writeLayerTable(out);
    pair(out, 0, "ENDSEC");

    // --- ENTITIES, in the scene's order.
    pair(out, 0, "SECTION");
    pair(out, 2, "ENTITIES");

    for (const drawing::SceneLine& line : scene.items.lines) {
        if (line.points.size() == 2) {
            // Two points is a LINE, which is what a reader expects to find.
            pair(out, 0, "LINE");
            pair(out, 8, layerFor(line.style));
            pair(out, 6, linetypeFor(line.style));
            pair(out, 370, lineweight(line.width));
            pair(out, 10, mm(line.points[0].x));
            pair(out, 20, mm(line.points[0].y));
            pair(out, 30, mm(0.0));
            pair(out, 11, mm(line.points[1].x));
            pair(out, 21, mm(line.points[1].y));
            pair(out, 31, mm(0.0));
            continue;
        }
        pair(out, 0, "LWPOLYLINE");
        pair(out, 8, layerFor(line.style));
        pair(out, 6, linetypeFor(line.style));
        pair(out, 370, lineweight(line.width));
        pair(out, 90, static_cast<int>(line.points.size()));
        pair(out, 70, 0); // open; a closed shape repeats its first point
        for (const Point2D& point : line.points) {
            pair(out, 10, mm(point.x));
            pair(out, 20, mm(point.y));
        }
    }

    for (const drawing::SceneArc& arc : scene.items.arcs) {
        const double sweep = arc.sweep.in(units::deg);
        if (std::abs(sweep) >= 359.999) {
            pair(out, 0, "CIRCLE");
            pair(out, 8, layerFor(arc.style));
            pair(out, 6, linetypeFor(arc.style));
            pair(out, 370, lineweight(arc.width));
            pair(out, 10, mm(arc.centre.x));
            pair(out, 20, mm(arc.centre.y));
            pair(out, 30, mm(0.0));
            pair(out, 40, mm(arc.radius));
            continue;
        }
        // A DXF ARC always runs ANTICLOCKWISE from its start to its end, so a
        // clockwise sweep is written as the same arc the other way round
        // rather than as a negative angle, which R12 has no way to say.
        const double start = arc.start.in(units::deg);
        const double end = start + sweep;
        const double from = sweep >= 0.0 ? start : end;
        const double to = sweep >= 0.0 ? end : start;
        pair(out, 0, "ARC");
        pair(out, 8, layerFor(arc.style));
        pair(out, 6, linetypeFor(arc.style));
        pair(out, 370, lineweight(arc.width));
        pair(out, 10, mm(arc.centre.x));
        pair(out, 20, mm(arc.centre.y));
        pair(out, 30, mm(0.0));
        pair(out, 40, mm(arc.radius));
        pair(out, 50, detail::degrees(Angle::fromSi(from * std::numbers::pi / 180.0)));
        pair(out, 51, detail::degrees(Angle::fromSi(to * std::numbers::pi / 180.0)));
    }

    for (const drawing::SceneText& text : scene.items.texts) {
        pair(out, 0, "TEXT");
        pair(out, 8, "TEXT");
        pair(out, 10, mm(text.at.x));
        pair(out, 20, mm(text.at.y));
        pair(out, 30, mm(0.0));
        pair(out, 40, mm(text.height));
        // Transcoded into the code page declared above -- never the scene's
        // raw UTF-8, which R12 cannot say and a reader would misread.
        pair(out, 1, toWinAnsi(text.text));
        pair(out, 50, detail::degrees(text.rotation));
        // R12's horizontal justification: 0 left, 1 centre, 2 right. The
        // second alignment point is where a justified text actually sits.
        int justify = 0;
        switch (text.anchor) {
        case drawing::TextAnchor::BaselineCentre:
        case drawing::TextAnchor::MiddleCentre:
            justify = 1;
            break;
        case drawing::TextAnchor::BaselineRight:
        case drawing::TextAnchor::MiddleRight:
            justify = 2;
            break;
        default:
            break;
        }
        pair(out, 72, justify);
        if (justify != 0) {
            pair(out, 11, mm(text.at.x));
            pair(out, 21, mm(text.at.y));
            pair(out, 31, mm(0.0));
        }
    }

    pair(out, 0, "ENDSEC");
    pair(out, 0, "EOF");
    return out;
}

Result<void> exportDxf(const drawing::DrawingScene& scene, const std::filesystem::path& path) {
    auto text = dxfDocument(scene);
    if (!text) {
        return std::unexpected(text.error());
    }
    return detail::writeFileAtomically(path, *text);
}

} // namespace bettercad::io
