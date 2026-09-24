#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// How the writers put TEXT on paper (P14-EXPORT-001).
//
// The scene holds text as UTF-8, because that is what a note, a part name and
// a GD&T symbol all are. Two of the three formats cannot take UTF-8:
//
//   SVG    declares encoding="UTF-8" and takes the bytes as they are. Nothing
//          here is needed, and SvgWriter does not use this header.
//   PDF    a simple font has a SINGLE-BYTE encoding. Helvetica is written with
//          /WinAnsiEncoding, so a code point must be turned into the ONE byte
//          that encoding gives it.
//   DXF    R12 stores text in the drawing's code page, named by $DWGCODEPAGE.
//          ANSI_1252 is the same repertoire as WinAnsi, so one transcoder
//          serves both.
//
// WHY THIS EXISTS AT ALL: escaping the UTF-8 BYTES instead of transcoding the
// CHARACTERS is a silent, plausible-looking corruption. "Ø20" is two bytes
// C3 98, and under WinAnsi those are two glyphs: the drawing reads "Ã˜20". A
// diameter callout that says something else is exactly the failure this
// milestone's gate is about -- a writer must TRANSCRIBE the scene.
//
// WHAT CANNOT BE CARRIED: WinAnsi has 224 printable characters and no GD&T
// symbols. Straightness, flatness, cylindricity, position and runout have no
// glyph in any of PDF's 14 standard fonts, so no encoding choice reaches them;
// only an embedded font would, which is a font subsystem and not this
// milestone. Those characters are replaced by '?', which is VISIBLY missing
// rather than quietly wrong: nobody reads '?' as a tolerance. SVG carries them
// correctly today. Recorded as a known limitation.
namespace bettercad::io::detail {

/// The one WinAnsi byte for a code point, or 0 when the encoding has none.
///
/// WinAnsi is Latin-1 with the C1 control range replaced by printable
/// characters -- the quotes, dashes and bullet that a typed note really does
/// contain. That block is the only part that needs a table.
[[nodiscard]] inline std::uint8_t winAnsiByte(char32_t code) noexcept {
    if (code >= 0x20 && code <= 0x7E) {
        return static_cast<std::uint8_t>(code); // ASCII, unchanged
    }
    if (code >= 0xA0 && code <= 0xFF) {
        return static_cast<std::uint8_t>(code); // Latin-1, unchanged: Ø ± ° µ
    }
    switch (code) {
    case 0x20AC: return 0x80; // euro
    case 0x201A: return 0x82; // single low quote
    case 0x0192: return 0x83; // florin
    case 0x201E: return 0x84; // double low quote
    case 0x2026: return 0x85; // ellipsis
    case 0x2020: return 0x86; // dagger
    case 0x2021: return 0x87; // double dagger
    case 0x02C6: return 0x88; // circumflex
    case 0x2030: return 0x89; // per mille
    case 0x0160: return 0x8A; // S caron
    case 0x2039: return 0x8B; // single left angle quote
    case 0x0152: return 0x8C; // OE
    case 0x017D: return 0x8E; // Z caron
    case 0x2018: return 0x91; // left single quote
    case 0x2019: return 0x92; // right single quote
    case 0x201C: return 0x93; // left double quote
    case 0x201D: return 0x94; // right double quote
    case 0x2022: return 0x95; // bullet
    case 0x2013: return 0x96; // en dash
    case 0x2014: return 0x97; // em dash
    case 0x02DC: return 0x98; // small tilde
    case 0x2122: return 0x99; // trade mark
    case 0x0161: return 0x9A; // s caron
    case 0x203A: return 0x9B; // single right angle quote
    case 0x0153: return 0x9C; // oe
    case 0x017E: return 0x9E; // z caron
    case 0x0178: return 0x9F; // Y diaeresis
    default: break;
    }
    return 0;
}

/// UTF-8 in, WinAnsi bytes out. One byte per character, '?' where the
/// encoding has none.
///
/// A control character has no WinAnsi byte either, so it becomes '?' as well.
/// That is not incidental: DXF is a LINE-BASED format, and a newline inside a
/// text value would not corrupt the text but the file.
///
/// Malformed UTF-8 yields '?' too and never a partial sequence: a writer must
/// produce a well-formed file from any input, and text reaching here has
/// already passed through a user-facing API.
[[nodiscard]] inline std::string toWinAnsi(std::string_view utf8) {
    std::string out;
    out.reserve(utf8.size());
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(utf8[i]); };
    for (std::size_t i = 0; i < utf8.size();) {
        const unsigned char lead = byte(i);
        char32_t code = 0;
        std::size_t length = 0;
        if (lead < 0x80) {
            code = lead;
            length = 1;
        } else if ((lead & 0xE0) == 0xC0) {
            code = lead & 0x1FU;
            length = 2;
        } else if ((lead & 0xF0) == 0xE0) {
            code = lead & 0x0FU;
            length = 3;
        } else if ((lead & 0xF8) == 0xF0) {
            code = lead & 0x07U;
            length = 4;
        } else {
            out += '?'; // a stray continuation or an invalid lead
            ++i;
            continue;
        }
        if (i + length > utf8.size()) {
            out += '?'; // truncated at the end of the string
            break;
        }
        bool wellFormed = true;
        for (std::size_t k = 1; k < length; ++k) {
            if ((byte(i + k) & 0xC0) != 0x80) {
                wellFormed = false;
                break;
            }
            code = (code << 6) | (byte(i + k) & 0x3FU);
        }
        if (!wellFormed) {
            out += '?';
            ++i; // resynchronise one byte at a time
            continue;
        }
        const std::uint8_t mapped = winAnsiByte(code);
        out += mapped != 0 ? static_cast<char>(mapped) : '?';
        i += length;
    }
    return out;
}

} // namespace bettercad::io::detail
