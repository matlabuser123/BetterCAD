#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Export.hpp>
#include <bettercad/core/units/Units.hpp>

#include <string>
#include <string_view>

/// Engineering standards (P12-HOLE-001): tabulated data from published
/// standards, with no geometry. Every table is transcribed from the sources
/// recorded in docs/verification/P12-HOLE-001/ and checked there against
/// independent copies.
namespace bettercad::standards {

/// The fundamental deviations of holes (ISO 286-1) that BetterCAD knows: the
/// ones that place a hole's tolerance interval at or above its nominal size,
/// with the lower limit deviation EI given by a table and the upper one
/// EI + IT. D, E, F and G leave a clearance to an h shaft; H starts at the
/// nominal size.
///
/// JS is not known: copies of ISO 286-2 disagree on whether its odd
/// tolerances of grades 7 to 11 are rounded to whole micrometres, which
/// BetterCAD cannot settle from the sources it has. The positions below D
/// (A, B, C, CD) and between them (EF, FG) and the transition and
/// interference positions (J to ZC) are not known either.
enum class HoleDeviation {
    D,
    E,
    F,
    G,
    H,
};

/// "D", "E", "F", "G" or "H".
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string_view toString(HoleDeviation deviation) noexcept;

/// A tolerance class of a hole (ISO 286-1), e.g. H7: a fundamental deviation
/// and a standard tolerance grade. The known classes are the ones ISO 286-2
/// tabulates for nominal sizes up to 500 mm with the known deviations: D6 to
/// D13, E5 to E10, F3 to F10, G3 to G10 and H1 to H18.
struct HoleToleranceClass {
    HoleDeviation deviation = HoleDeviation::H;
    int grade = 7;

    friend constexpr bool operator==(const HoleToleranceClass&, const HoleToleranceClass&) = default;
};

/// "H7", "D10".
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string toString(const HoleToleranceClass& tolerance);

/// Checks that the class is a known one (see HoleToleranceClass); fails with
/// InvalidArgument otherwise.
[[nodiscard]] BETTERCAD_CORE_EXPORT Result<void> validate(const HoleToleranceClass& tolerance);

/// Parses a known class from its designation: a capital letter and the grade
/// without leading zeros ("H7", "D10"). Fails with InvalidArgument for
/// anything else, naming the text.
[[nodiscard]] BETTERCAD_CORE_EXPORT Result<HoleToleranceClass> parseHoleToleranceClass(std::string_view text);

/// The standard tolerance ITn (ISO 286-1, Table 1) of a nominal size in
/// (0, 500] mm, for a grade from 1 to 18. The size ranges include their upper
/// limit ("above 3 up to and including 6"); a size within 1e-9 mm of a limit
/// is taken as that limit. Grades 14 to 18 are not used for sizes up to
/// 1 mm (Table 1, footnote). Fails with InvalidArgument for a size or grade
/// out of range.
[[nodiscard]] BETTERCAD_CORE_EXPORT Result<Length> standardTolerance(Length nominalSize, int grade);

/// The limit deviations of a hole: its lower (EI) and upper (ES) limits of
/// size less the nominal size.
struct LimitDeviations {
    Length lower{};
    Length upper{};

    friend constexpr bool operator==(const LimitDeviations&, const LimitDeviations&) = default;
};

/// The limit deviations of a hole of @p nominalSize in @p tolerance
/// (ISO 286-1): EI is the fundamental deviation of the size's range (zero
/// for H) and ES = EI + ITn. Fails with InvalidArgument for an unknown class
/// and as standardTolerance() does.
[[nodiscard]] BETTERCAD_CORE_EXPORT Result<LimitDeviations> limitDeviations(Length nominalSize,
                                                                           const HoleToleranceClass& tolerance);

} // namespace bettercad::standards
