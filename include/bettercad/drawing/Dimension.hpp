#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/drawing/Export.hpp>
#include <bettercad/drawing/Tolerance.hpp>
#include <bettercad/drawing/Tolerance.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Dimensions on a drawing (P14-DIM-001, on ADR-011, ADR-012 and ADR-017).
//
// WHAT IS STORED AND WHAT IS NOT. A dimension stores what to measure, in
// which view, and how to write the answer. It does NOT store the answer. The
// number comes from the model every time it is asked for, so a dimension can
// never be out of date with the part it dimensions -- which is the one
// failure a drawing must not have.
//
//     canonical     the view; the kind of measurement; the references;
//                   the display unit and precision; where the text sits
//     derived       the measured Length or Angle; the text; anything drawn
//
// WHAT A DIMENSION MAY POINT AT is settled by ADR-012, and this file adds
// nothing to it: a datum or principal plane, a datum or principal axis, or a
// NAMED face of a feature. Never an edge, a topology index, a kernel face, or
// anything whose identity is the kernel's enumeration order. A dimension an
// engineer would call "between these two edges" is expressed between the two
// named faces that meet at them -- the edge is where the faces cross, and the
// faces are what the model can name.
//
// Note for readers: `bettercad::drawing::Dimension` shares its name with
// `bettercad::Dimension`, the units dimensional-analysis type. ADR-017 records
// the collision; the namespace resolves it, and no drawing header writes
// `using namespace bettercad`.
namespace bettercad::drawing {

/// What a dimension measures.
enum class DimensionType : std::uint8_t {
    /// The true distance in the MODEL, whatever the view.
    Linear,
    /// The part of that distance along the view's own X axis.
    Horizontal,
    /// The part along the view's own Y axis.
    Vertical,
    /// The distance as DRAWN: the separation projected into the view plane.
    /// Equal to Linear only when the separation lies in that plane.
    Aligned,
    /// The angle a protractor reads: between two PLANES (measured through
    /// the material, so two faces of a slab read 0 and a wedge reads its own
    /// included angle), or between two axes as lines, in [0, 90].
    Angular,
    /// The radius of a named cylindrical face.
    Radius,
    /// Twice that radius, by the one path.
    Diameter,
    /// A signed coordinate along one of the view's axes, from a datum.
    Ordinate,
};

/// "linear", "horizontal", "vertical", "aligned", "angular", "radius",
/// "diameter", "ordinate".
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::string_view toString(DimensionType type) noexcept;
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::optional<DimensionType> dimensionTypeFromString(
    std::string_view text) noexcept;

/// Which of a view's axes an ordinate dimension measures along.
enum class OrdinateAxis : std::uint8_t {
    X,
    Y,
};

/// "x", "y".
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::string_view toString(OrdinateAxis axis) noexcept;
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::optional<OrdinateAxis> ordinateAxisFromString(
    std::string_view text) noexcept;

/// One end of a measurement: exactly the vocabulary ADR-012 permits, and
/// exactly one of the three at a time.
///
/// `plane` covers the principal planes, a datum plane, and a named PLANAR
/// face -- `PlaneReference` already spells all three. `axis` covers the
/// principal axes and datum axes. `cylinder` names a cylindrical face, and
/// exists because a radius is not a plane and not an axis.
struct DimensionTarget {
    std::optional<PlaneReference> plane{};
    std::optional<AxisReference> axis{};
    std::optional<FaceName> cylinder{};

    friend bool operator==(const DimensionTarget&, const DimensionTarget&) = default;
};

/// Exactly one of the three set, and that one valid on its own.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> validate(const DimensionTarget& target);

/// Whether @p target names anything at all.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT bool isEmpty(const DimensionTarget& target) noexcept;

/// The document objects @p target depends on, so a dimension rebuilds when
/// any of them moves.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::vector<ObjectId> referencedObjects(
    const DimensionTarget& target);

/// How a measured value is written.
///
/// The unit is a SYMBOL from the unit catalogue ("mm", "cm", "in", "deg"),
/// not a scale factor, so a file says what it means rather than carrying a
/// number whose meaning has to be remembered.
struct DimensionFormat {
    /// Digits after the point, 0 to 6. More than 6 says nothing a model built
    /// in double precision can support.
    std::uint8_t decimals = 2;
    /// Whether "100" is written as "100.00" or as "100". Drawing offices
    /// differ, and ISO 129 does not settle it.
    bool trailingZeros = true;
    /// Display unit symbol. A length dimension takes a length unit and an
    /// angular one takes an angle unit; mixing them is refused rather than
    /// silently reinterpreted.
    std::string unit = "mm";
    /// Whether the unit symbol is written after the number.
    bool showUnit = false;

    friend bool operator==(const DimensionFormat&, const DimensionFormat&) = default;
};

/// A known unit symbol, decimals in range. Whether the unit SUITS the
/// dimension's type is checked with the whole definition, because the format
/// alone does not know the type.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> validate(const DimensionFormat& format);

/// What a dimension is: the intent, and nothing measured.
struct DimensionDefinition {
    /// The view it is measured in and drawn on. Every dimension has one: a
    /// horizontal distance is horizontal in a view, and a dimension with no
    /// view would have no axes to be horizontal against.
    ViewId view{};
    DimensionType type = DimensionType::Linear;
    /// What it measures from. A radius or diameter uses this alone.
    DimensionTarget from{};
    /// What it measures to. Left empty by radius and diameter.
    DimensionTarget to{};
    /// Which view axis an ordinate runs along. Ignored by every other type.
    OrdinateAxis ordinate = OrdinateAxis::X;
    DimensionFormat format{};
    /// What the feature is made to, if anything. The interval is canonical
    /// and the text is not: a fit resolves its deviations from the standard
    /// every time it is asked for (P14-TOL-001).
    std::optional<DimensionTolerance> tolerance{};
    /// Where the text sits, in sheet coordinates. Intent: the engineer puts
    /// it where it reads well, and nothing derives it.
    Point2D placement{};

    friend bool operator==(const DimensionDefinition&, const DimensionDefinition&) = default;
};

/// Checks a definition on its own: a valid view; the targets the type needs
/// and none it does not; a valid format whose unit suits the type; a finite
/// placement.
///
/// Whether the view and the referenced objects exist is checked against the
/// document by checkDimension(), because a definition alone cannot know.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> validate(const DimensionDefinition& definition);

/// Whether @p type measures an angle rather than a length.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT bool isAngular(DimensionType type) noexcept;

/// Whether @p type measures one target rather than two.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT bool isSingleTarget(DimensionType type) noexcept;

/// One dimension on a drawing view (type name "dimension").
class BETTERCAD_DRAWING_EXPORT Dimension final : public DocumentObject {
public:
    using Definition = DimensionDefinition;
    static constexpr std::string_view kTypeName = "dimension";

    [[nodiscard]] static Result<std::unique_ptr<Dimension>> create(std::string name,
                                                                   const DimensionDefinition& definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    /// Its view and everything its references name, so it rebuilds when any
    /// of them moves.
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;

    [[nodiscard]] DimensionId dimensionId() const noexcept {
        return DimensionId::fromValue(id().value());
    }
    [[nodiscard]] const DimensionDefinition& definition() const noexcept { return definition_; }
    Result<bool> setDefinition(const DimensionDefinition& definition);

private:
    Dimension(std::string name, const DimensionDefinition& definition);

    DimensionDefinition definition_;
};

// --- Derived (ADR-011). Computed on every call, never stored. ---

/// What a dimension measures, and how that reads.
///
/// Exactly one of `length` and `angle` is set: which one is decided by the
/// dimension's type, not by what happened to resolve.
struct MeasuredDimension {
    std::optional<Length> length{};
    std::optional<Angle> angle{};
    /// The value written out under the dimension's format. With a tolerance
    /// shown as a deviation pair this carries the whole thing -- "20 +/-0.05";
    /// shown as limits it carries the UPPER limit alone, and `lowerText`
    /// carries the other.
    std::string text{};
    /// The lower limit, for a tolerance shown as limits. Empty otherwise.
    std::string lowerText{};
    /// What the tolerance admits, in model units. Absent when the dimension
    /// carries no tolerance.
    std::optional<ToleranceInterval> interval{};
};

/// @p value written under @p format: rounded half away from zero to the
/// format's decimals, in its display unit, with the unit symbol when asked
/// for.
///
/// Half away from zero, not the "round half to even" std::format would give:
/// a drawing office rounds 0.125 to 0.13, and a dimension that rounded it to
/// 0.12 because the digit before was even would be right by a rule nobody
/// applies with a micrometer. The formatting is locale-independent.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<std::string> formatLength(Length value,
                                                                        const DimensionFormat& format);
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<std::string> formatAngle(Angle value,
                                                                       const DimensionFormat& format);

/// How many decimals @p value needs to be written without being rounded,
/// never fewer than @p atLeast and never more than six.
///
/// A dimension's `decimals` is a presentation choice about the NOMINAL. It is
/// not a choice about what the part is made to, and applied to a tolerance it
/// stops being one: +/-0.05 written to no decimals is "+/-0", and the two
/// limits of an H7 hole written to two decimals are 20.02 and 20.00 -- an
/// interval a fifth narrower than the standard's, on the face of the drawing.
/// So the tolerance side of a dimension, and the magnitude in a
/// feature-control frame, are written to at least what they need.
///
/// Six decimals is a nanometre, below any drawing; a value needing more is
/// written to six.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::uint8_t decimalsWithoutRounding(
    Length value, std::uint8_t atLeast = 0) noexcept;

/// A length written with an explicit sign, as a deviation is: `+0.10`,
/// `-0.02`.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<std::string> formatDeviation(
    Length value, const DimensionFormat& format);

/// The tolerance part of a dimension's text, with no nominal in front of it:
/// `+/-0.05`, `+0.10 -0.02`, or a fit's designation.
///
/// Shown as limits there is no single string, so this is not what builds one;
/// measure() writes the two limits from the ONE interval instead, which is
/// what stops the two presentations describing different parts.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<std::string> formatTolerance(
    const DimensionTolerance& tolerance, const DimensionFormat& format);

} // namespace bettercad::drawing
