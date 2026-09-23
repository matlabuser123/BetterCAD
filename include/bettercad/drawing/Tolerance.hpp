#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/standards/HoleTolerances.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/drawing/Export.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Tolerances and the GD&T foundation (P14-TOL-001, on ADR-011, ADR-012,
// ADR-016 and ADR-017).
//
// WHAT IS CANONICAL AND WHAT IS NOT.
//
//     canonical   the interval a feature is made to; the fit designation;
//                 the characteristic, its zone, its magnitude and its
//                 ordered datums; which way it is shown
//     derived     every string, every frame, every cell
//
// A tolerance stores the ENGINEERING MEANING and never the text. "20.05 /
// 19.95" and "20 ±0.05" are the same interval shown two ways, and they are
// one stored fact with a display mode -- so they cannot come to disagree.
//
// SIGNS, STATED ONCE. Both deviations are SIGNED and measured from the
// nominal: ±0.05 is lower = -0.05 and upper = +0.05; +0.10/-0.02 is
// lower = -0.02 and upper = +0.10. The one invariant is `lower < upper`:
// strictly, because an interval of no width admits a single size that
// nothing can be made to.
//
// The nominal is NOT required to lie inside the interval, and that is
// deliberate rather than an oversight: ISO 286 puts a whole class of
// intervals above the nominal size. H7 on 20 mm is EI = 0, ES = +0.021, so
// the smallest acceptable hole IS the nominal and every other one is larger.
// A contract that demanded the nominal be interior could not express H7.
namespace bettercad::drawing {

/// Whether a tolerance is written as a deviation pair or as two limits.
///
/// The same interval either way. This is presentation, and changing it
/// cannot change what the part is made to.
enum class ToleranceDisplay : std::uint8_t {
    /// `20 ±0.05`, or `20 +0.10 -0.02` when the deviations differ.
    PlusMinus,
    /// The two limits, larger first: `20.05` over `19.95`.
    Limits,
};

/// "plus_minus", "limits".
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::string_view toString(ToleranceDisplay display) noexcept;
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::optional<ToleranceDisplay> toleranceDisplayFromString(
    std::string_view text) noexcept;

/// Whether a fit designation names a hole or a shaft.
///
/// ISO 286 writes a hole's letter as a capital and a shaft's in lower case,
/// so the case is DERIVED from this and is not stored: a stored case could
/// disagree with the role, and then `H7` on a shaft would mean nothing.
enum class FitRole : std::uint8_t {
    Hole,
    Shaft,
};

/// "hole", "shaft".
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::string_view toString(FitRole role) noexcept;
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::optional<FitRole> fitRoleFromString(
    std::string_view text) noexcept;

/// A fit designation: a fundamental deviation and a tolerance grade, against
/// a role. `H7`, `g6`.
///
/// The letter is stored as a CAPITAL whatever the role, and written in the
/// case the role calls for.
struct FitDesignation {
    FitRole role = FitRole::Hole;
    /// The fundamental deviation's letter, A to Z, stored capitalised.
    char letter = 'H';
    /// The standard tolerance grade, 1 to 18 (ISO 286-1 Table 1).
    int grade = 7;

    friend bool operator==(const FitDesignation&, const FitDesignation&) = default;
};

/// A letter A to Z and a grade from 1 to 18.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> validate(const FitDesignation& fit);

/// `H7` for a hole, `g6` for a shaft.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::string toString(const FitDesignation& fit);

/// The deviations @p fit gives at @p nominalSize, from the tabulated data.
///
/// **This resolves for HOLES of the positions ISO 286-2 is transcribed for in
/// this build -- D, E, F, G and H -- and for nothing else.** A shaft's
/// fundamental deviations are not tabulated here, and neither are the hole
/// positions below D or above H.
///
/// Where it cannot resolve it FAILS, naming what is missing. It never
/// estimates: a fabricated limit is a number somebody would machine to.
/// `fitWidth()` still gives the interval's width for any grade, because the
/// standard tolerance IT is shared by holes and shafts alike -- so a shaft's
/// tolerance is known even when its position is not.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<standards::LimitDeviations> fitDeviations(
    Length nominalSize, const FitDesignation& fit);

/// The width of @p fit's tolerance interval at @p nominalSize: the standard
/// tolerance ITn, which ISO 286 gives per grade and size and which does not
/// depend on the fundamental deviation or the role.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<Length> fitWidth(Length nominalSize,
                                                               const FitDesignation& fit);

/// What a feature is made to.
///
/// Either a deviation pair or a fit designation -- never both, because a fit
/// IS a deviation pair and two of them could disagree. A fit's deviations are
/// resolved from the tables when they are asked for, so a drawing that cites
/// `H7` follows the standard rather than a copy of it.
struct DimensionTolerance {
    /// Signed, from the nominal, and `lower < upper`. Both must be zero
    /// when `fit` is set, which is how the two are kept from disagreeing.
    Length lower{};
    Length upper{};
    /// A fit designation instead of a deviation pair.
    std::optional<FitDesignation> fit{};
    ToleranceDisplay display = ToleranceDisplay::PlusMinus;

    friend bool operator==(const DimensionTolerance&, const DimensionTolerance&) = default;
};

/// Finite deviations with `lower < upper`, or a valid fit; never both.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> validate(const DimensionTolerance& tolerance);

/// Whether @p tolerance is written the same both ways: a symmetric pair.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT bool isSymmetric(const DimensionTolerance& tolerance) noexcept;

/// The interval @p tolerance allows about @p nominal, in model units.
///
/// One function, so the `±` text and the limits text can never describe
/// different intervals: both are written from what this returns. A fit
/// resolves through `fitDeviations()` and fails where that fails.
struct ToleranceInterval {
    Length lower{};
    Length upper{};

    friend bool operator==(const ToleranceInterval&, const ToleranceInterval&) = default;
};

[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<ToleranceInterval> intervalOf(
    Length nominal, const DimensionTolerance& tolerance);

/// The signed deviations @p tolerance gives at @p nominal: the pair it stores,
/// or the pair its fit resolves to.
///
/// The one place a fit becomes numbers, so the interval and the precision the
/// text needs are worked out from the same pair rather than from each other.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<standards::LimitDeviations> deviationsOf(
    Length nominal, const DimensionTolerance& tolerance);

// --- The GD&T foundation ---------------------------------------------------

/// The geometric characteristics this build represents.
///
/// The fourteen of ISO 1101 less the three profile and symmetry ones, which
/// need a profile or a median-feature contract this foundation does not have.
/// A characteristic is here only when it has a value, validation rules, a
/// symbol, persistence and tests -- listing one without those would be
/// claiming support for it.
enum class GeometricCharacteristic : std::uint8_t {
    Straightness,
    Flatness,
    Circularity,
    Cylindricity,
    Parallelism,
    Perpendicularity,
    Angularity,
    Position,
    CircularRunout,
    TotalRunout,
};

/// "straightness", "flatness", ... "total_runout".
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::string_view toString(
    GeometricCharacteristic characteristic) noexcept;
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::optional<GeometricCharacteristic>
geometricCharacteristicFromString(std::string_view text) noexcept;

/// The ISO 1101 symbol, as the Unicode codepoint that names it.
///
/// Unicode's Miscellaneous Technical block carries most of them by name --
/// U+23E4 STRAIGHTNESS, U+23E5 FLATNESS, U+232D CYLINDRICITY, U+2316
/// POSITION INDICATOR, U+2330 TOTAL RUNOUT. Where it does not, the
/// conventional substitute is used and said to be one: circular runout is
/// written with a single arrow (U+2197), because Unicode has no codepoint for
/// it.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::string_view symbolOf(
    GeometricCharacteristic characteristic) noexcept;

/// The shape of the zone a tolerance holds a feature within.
enum class ToleranceZone : std::uint8_t {
    /// Between two parallel lines or planes, the given distance apart. The
    /// usual zone, and the one written with no prefix.
    Width,
    /// Inside a cylinder of the given diameter, written with the diameter
    /// sign. A position tolerance on a hole's axis is the common case.
    Cylindrical,
};

/// "width", "cylindrical".
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::string_view toString(ToleranceZone zone) noexcept;
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::optional<ToleranceZone> toleranceZoneFromString(
    std::string_view text) noexcept;

/// One datum cited by a feature-control frame, by the letter a datum feature
/// symbol gives it.
///
/// A letter, not a reference to geometry: that is how GD&T works and how
/// P14-ANNO-001 already models a datum feature symbol. There is no second
/// datum type here.
struct DatumReference {
    char letter = 'A';

    friend bool operator==(const DatumReference&, const DatumReference&) = default;
};

/// A capital A to Z, and not I, O or Q.
///
/// ISO 5459 skips those three because they read as 1 and 0. This is THE rule,
/// used by the datum feature symbol and by every datum a frame cites, so the
/// two cannot come to disagree about what a datum may be called.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> validateDatumLetter(char letter);

/// What a feature-control frame requires of a feature.
///
/// The cells of the frame, as meaning rather than as text: a characteristic,
/// a zone of a size, and the datums it is held against IN ORDER. The order is
/// the whole of what makes A|B|C different from B|A|C, so it is a vector and
/// never a set.
struct FeatureControlFrame {
    GeometricCharacteristic characteristic = GeometricCharacteristic::Position;
    ToleranceZone zone = ToleranceZone::Width;
    Length tolerance{};
    /// Primary, then secondary, then tertiary. At most three.
    std::vector<DatumReference> datums{};

    friend bool operator==(const FeatureControlFrame&, const FeatureControlFrame&) = default;
};

/// Checks a frame for the things that are incoherent whatever the standard
/// being followed:
///
///     a finite tolerance greater than zero
///     a form characteristic with datums -- a flat face is flat by itself,
///         and citing a datum says the frame's author meant orientation
///     an orientation, position or runout characteristic with NO datum --
///         parallel to WHAT?
///     a cylindrical zone on a characteristic that cannot have one: a
///         flatness zone is two planes and is never a cylinder
///     more than three datums, or the same letter twice
///     a datum letter ISO 5459 does not use
///
/// It does NOT check everything a standard requires. What is checked and what
/// is deferred is recorded with the milestone.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> validate(const FeatureControlFrame& frame);

/// Whether @p characteristic is a form characteristic, which is held against
/// no datum because it is a property of the feature by itself.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT bool isForm(
    GeometricCharacteristic characteristic) noexcept;

/// Whether @p characteristic may hold a feature inside a cylinder.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT bool allowsCylindricalZone(
    GeometricCharacteristic characteristic) noexcept;

} // namespace bettercad::drawing
