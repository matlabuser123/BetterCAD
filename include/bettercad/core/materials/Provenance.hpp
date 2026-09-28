#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Export.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>

#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Where a property value claims to have come from (P15-PROV-001, ADR-028).
//
// ADR-028 fixed the shape and this implements it: provenance is PER PROPERTY,
// with a MATERIAL-LEVEL DEFAULT for properties that state none. Both, and neither
// alone -- per-property because that is how data is actually gathered (a density
// from a supplier datasheet, a conductivity from a handbook, a yield from a test
// report), and a material default so that an entry transcribed wholly from one
// datasheet does not repeat the citation nine times.
//
// PROVENANCE IS METADATA. It never participates in identity, in equality of
// engineering meaning, or in derivation (ADR-028). Two materials with identical
// values and different sources are still two materials, and changing a citation
// changes no number anywhere.
//
// A VALUE MAY BE KNOWN WHILE ITS SOURCE IS NOT. `SourceKind::Unspecified` is the
// default and is not a defect: most values a user types have no citation, and a
// material with no provenance at all still computes. What it loses is the ability
// to justify itself, which is a reviewable engineering matter rather than a
// crash. So an absent citation must never block a solver -- see Completeness.hpp,
// where engineering completeness and traceability are separate questions.
//
// WHY THIS IS NOT A FIELD ON MaterialProperty. Provenance needs strings, and
// MaterialProperty's factories are `constexpr`; adding a std::string member would
// silently strip that from three already-qualified milestones' API surface for no
// benefit. Keeping provenance in ordered maps beside the values also makes it
// sparse by nature and deterministic to enumerate. The cost is that a value and
// its provenance are separable, so they can fall out of step -- which is why
// removing a property removes its provenance, editing a value clears the
// provenance that described the old number, and an orphan citation is reported as
// an issue rather than left to mislead.
namespace bettercad::materials {

/// A calendar date, as a plain value.
///
/// Its own type rather than std::chrono::year_month_day, following the
/// repository's habit of small explicit value types (Quantity, PoissonRatio,
/// Hardness). The reason that matters here is determinism: toString() is ISO 8601
/// and is assembled digit by digit, so it cannot pick up a locale, a time zone or
/// a clock. Nothing in BetterCAD reads the current date, and no test may.
class BETTERCAD_CORE_EXPORT Date {
public:
    constexpr Date() = default;

    /// A date, or std::nullopt if it is not one. Validated including leap years,
    /// so "2026-02-30" cannot be stored and then formatted as if it were real.
    [[nodiscard]] static std::optional<Date> of(int year, int month, int day) noexcept;

    [[nodiscard]] constexpr int year() const noexcept { return year_; }
    [[nodiscard]] constexpr int month() const noexcept { return month_; }
    [[nodiscard]] constexpr int day() const noexcept { return day_; }

    friend constexpr bool operator==(const Date&, const Date&) noexcept = default;
    /// Chronological, so a caller may sort by date. Comparing two dates is
    /// ordinary; comparing two REVISIONS is not -- see PropertyProvenance.
    friend constexpr auto operator<=>(const Date&, const Date&) noexcept = default;

private:
    constexpr Date(int year, int month, int day) noexcept
        : year_(year), month_(month), day_(day) {}

    int year_ = 0;
    int month_ = 0;
    int day_ = 0;
};

/// "2026-09-28", ISO 8601, locale-independent by construction.
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string toString(const Date& date);

/// What KIND of source a value came from.
///
/// The categories are the ones that change engineering meaning, and no others. A
/// measured yield strength and a handbook nominal of the same number are not the
/// same claim: one is a property of the material in hand, the other of the alloy
/// in general. That distinction is the reason this enumeration exists.
///
/// Deliberately ABSENT: a generic "Imported". Where something was imported from is
/// what the other kinds already say, and a category that means "from somewhere"
/// carries no semantic value (ADR-028 rejects provenance that cannot express what
/// it is for).
enum class SourceKind : std::uint8_t {
    /// Nobody said. The default, and NOT a defect -- see the file comment.
    Unspecified,
    /// Imported from a BetterCAD library entry. The entry and its revision are
    /// recorded separately as the material's `origin` (ADR-025).
    LibraryReference,
    /// A supplier's or manufacturer's published data for their own product.
    ManufacturerData,
    /// A published standard, e.g. EN 10025-2. `standard` should name it.
    Standard,
    /// A reference handbook or textbook: nominal values for the alloy in general.
    Handbook,
    /// A test of THIS material. The strongest claim available, and the one that
    /// justifies using a value outside a handbook's nominal range.
    Measured,
    /// Computed by the user from other measurements, e.g. a density from a mass
    /// and a volume. NOT what BetterCAD's own derivation produces: a derived G is
    /// never stored at all (ADR-027), and reports its inputs' provenance instead.
    Calculated,
    /// Typed in by the user with no external citation. Honest, and distinct from
    /// Unspecified: this says a person chose the number, not that nobody knows.
    UserEntered,
};

/// The kinds, in reporting order.
[[nodiscard]] BETTERCAD_CORE_EXPORT std::span<const SourceKind> sourceKinds() noexcept;
/// "unspecified", "library reference", "manufacturer data", ...
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string_view toString(SourceKind kind) noexcept;

/// Whether @p kind is a measurement of the material in hand.
///
/// The measured-versus-reference distinction, as a predicate rather than a switch
/// in every caller. Note that it is NOT inferred from whether a source string was
/// given: a citation is not a measurement, and an uncited measurement is still one.
[[nodiscard]] BETTERCAD_CORE_EXPORT bool isMeasured(SourceKind kind) noexcept;
/// Whether @p kind is published reference data for the material class rather than
/// a measurement of this material: a standard, a handbook, a manufacturer's data
/// or a BetterCAD library entry.
[[nodiscard]] BETTERCAD_CORE_EXPORT bool isReferenceData(SourceKind kind) noexcept;

/// Where one value came from.
///
/// Every field is optional. Sparse metadata is the normal case and is not a
/// defect: a user who knows only "Rev C of the datasheet" should be able to record
/// exactly that without inventing the rest.
struct PropertyProvenance {
    SourceKind kind = SourceKind::Unspecified;
    /// Free text naming the source, e.g. "Alcoa 6061-T6 datasheet".
    std::string source;
    /// The standard the value is taken from, e.g. "ASTM B221".
    std::string standard;
    /// Where in the source, e.g. "Table 4" or "p. 212".
    std::string reference;
    /// The source's own revision, e.g. "Rev C" or "2nd edition".
    ///
    /// METADATA, NOT VERSION LOGIC. It is never parsed, never ordered and never
    /// compared for precedence: "Rev C" is not greater than "Rev B" as far as
    /// BetterCAD is concerned, and a revision string is equal only to itself.
    /// Ordering revisions would need a scheme every supplier agrees on, and there
    /// is none.
    std::string revision;
    /// When the source was issued or the measurement made.
    std::optional<Date> date;
    /// The material condition or temper the value applies to, e.g. "T6",
    /// "as-rolled", "annealed" (ADR-028's "condition/temper").
    std::string condition;
    /// Anything else worth recording about where this number came from.
    std::string notes;

    /// Whether nothing at all has been stated. An empty provenance and an absent
    /// one mean the same thing, which is what makes the material-level default
    /// work.
    ///
    /// INLINE, and that is not a style choice. Every data struct in
    /// core/materials/ -- MechanicalProperties, ThermalProperties,
    /// MaterialLibraryKey, LinearElasticConstants -- carries no export macro and
    /// defines no member out of line; the module's only out-of-line member belongs
    /// to LibraryMaterial, which IS an exported class. Defining this one in the
    /// .cpp instead left it hidden under -fvisibility=hidden and unresolvable
    /// across the DLL boundary, which only the debug-shared preset revealed.
    [[nodiscard]] bool empty() const noexcept {
        return kind == SourceKind::Unspecified && source.empty() && standard.empty() &&
               reference.empty() && revision.empty() && !date.has_value() && condition.empty() &&
               notes.empty();
    }

    friend bool operator==(const PropertyProvenance&, const PropertyProvenance&) = default;
};

/// One material's provenance: a default, plus per-property overrides.
///
/// The maps are ORDERED (std::map over the property-kind enumerations), so
/// enumerating provenance gives the same order in every build and on every run.
/// A kind absent from a map has no provenance of its own and takes the material
/// default -- which is the two-level model ADR-028 chose, and the only inheritance
/// anywhere in the material model. Property VALUES never inherit (ADR-025,
/// P15-CUSTOM-001); provenance does, deliberately and visibly.
struct MaterialProvenance {
    /// The default for properties that state none.
    PropertyProvenance material;
    std::map<MechanicalPropertyKind, PropertyProvenance> mechanical;
    std::map<ThermalPropertyKind, PropertyProvenance> thermal;

    /// Inline, for the reason PropertyProvenance::empty() is.
    [[nodiscard]] bool empty() const noexcept {
        return material.empty() && mechanical.empty() && thermal.empty();
    }

    friend bool operator==(const MaterialProvenance&, const MaterialProvenance&) = default;
};

/// The provenance in force for @p kind: its own if it has any, otherwise the
/// material default.
///
/// Always answerable, which is ADR-028's requirement. For a DERIVED kind -- the
/// shear and bulk moduli -- this returns `Calculated` and nothing else, because a
/// derived value has no source of its own; ask derivedInputProvenance() for the
/// inputs it came from.
[[nodiscard]] BETTERCAD_CORE_EXPORT PropertyProvenance
effectiveProvenance(const MaterialProvenance& provenance, MechanicalPropertyKind kind);
[[nodiscard]] BETTERCAD_CORE_EXPORT PropertyProvenance
effectiveProvenance(const MaterialProvenance& provenance, ThermalPropertyKind kind);

/// Whether @p kind has provenance of its own, as opposed to taking the material
/// default. Lets a caller show "inherited from the material" without comparing
/// two PropertyProvenance values and guessing.
[[nodiscard]] BETTERCAD_CORE_EXPORT bool hasOwnProvenance(const MaterialProvenance& provenance,
                                                          MechanicalPropertyKind kind);
[[nodiscard]] BETTERCAD_CORE_EXPORT bool hasOwnProvenance(const MaterialProvenance& provenance,
                                                          ThermalPropertyKind kind);

/// The inputs a derived property is computed from, with the provenance in force
/// for each -- ADR-028's "a derived property reports the provenance of the inputs
/// it was derived from".
///
/// So a shear modulus traces to the Young's modulus and the Poisson ratio it came
/// from, and each of those to its own source. Empty for a kind that is not
/// derived: a stored property's provenance is its own, and asking this about one
/// would be a category error rather than an absence.
///
/// The order is the property-kind enumeration order, so it is deterministic.
[[nodiscard]] BETTERCAD_CORE_EXPORT std::vector<std::pair<MechanicalPropertyKind, PropertyProvenance>>
derivedInputProvenance(const MaterialProvenance& provenance, MechanicalPropertyKind kind);

/// Problems with @p provenance considered on its own, in reporting order.
///
/// These are WARNINGS about metadata, never reasons to refuse data. The model
/// permits sparse provenance on purpose, so this reports only claims that
/// contradict themselves -- saying a value comes from a standard without naming
/// the standard, for instance. It says nothing about whether a value is present:
/// that is Completeness.hpp's question.
[[nodiscard]] BETTERCAD_CORE_EXPORT std::vector<std::string>
provenanceProblems(const PropertyProvenance& provenance);

} // namespace bettercad::materials
