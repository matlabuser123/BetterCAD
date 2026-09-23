#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/drawing/Export.hpp>
#include <bettercad/drawing/Scene.hpp>
#include <bettercad/drawing/Tolerance.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Annotations on a drawing (P14-ANNO-001, on ADR-011, ADR-012, ADR-016 and
// ADR-017).
//
// WHAT IS STORED AND WHAT IS NOT.
//
//     canonical   the view; the kind; what it points at; the words an
//                 engineer chose; the paper text height; where it sits on
//                 the sheet; the symbol's own values
//     derived     the resolved text of anything model-driven; every line,
//                 arrow, arm and frame; the scene items
//
// A hole callout stores which hole, never "10". Change the hole and the
// callout changes with it, because there is no number in the file to go
// stale. A NOTE, by contrast, stores its words: "DEBURR ALL EDGES" is not
// derivable from geometry and is intent like any other.
//
// WHAT AN ANNOTATION MAY POINT AT is ADR-012's list and nothing wider: a
// datum or principal plane, a datum or principal axis, a NAMED face, or a
// document object. Never an edge, a topology index or a kernel handle.
//
// TWO COORDINATE SPACES, AND THEY DO NOT MIX.
//
//     the TARGET lives in the model, and reaches the sheet through the
//     view's projection -- so it moves when the view moves or its scale
//     changes, as the geometry it labels does
//
//     the ANNOTATION lives on the PAPER: its placement, its text height,
//     its arrowheads, its centre-mark arms are sheet millimetres and are
//     never multiplied by a view scale
//
// That is the invariant the whole milestone turns on. A 3.5 mm note is
// 3.5 mm at 1:1 and 3.5 mm at 1:10; what changes is where the thing it
// points at has got to.
namespace bettercad::drawing {

/// What kind of annotation this is.
enum class AnnotationType : std::uint8_t {
    /// Free text placed on the sheet. Points at nothing.
    Note,
    /// Free text with a leader from it to what it is about.
    Leader,
    /// The long-dash-dot line through an axis of something round.
    Centreline,
    /// The small cross at the centre of something round.
    Centremark,
    /// Text derived from a hole feature: its diameter, and whether it goes
    /// through or to a depth.
    HoleCallout,
    /// A roughness requirement against a face.
    SurfaceFinish,
    /// A datum letter against a face or axis, for a feature-control frame to
    /// cite.
    Datum,
    /// A feature-control frame: what a feature must be held to, and against
    /// which datums (P14-TOL-001).
    FeatureControlFrame,
};

/// "note", "leader", "centreline", "centremark", "hole_callout",
/// "surface_finish", "datum".
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::string_view toString(AnnotationType type) noexcept;
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::optional<AnnotationType> annotationTypeFromString(
    std::string_view text) noexcept;

/// What an annotation is about: exactly ADR-012's vocabulary, one at a time.
///
/// `object` names a document object -- a hole feature for a callout. It is on
/// ADR-012's list, and for a hole it is the right level: a hole's bore is not
/// a named face (P12 names its bottom and floors, not its wall), and the
/// feature knows its own diameter, depth and extent.
struct AnnotationTarget {
    std::optional<PlaneReference> plane{};
    std::optional<AxisReference> axis{};
    std::optional<FaceName> cylinder{};
    std::optional<ObjectId> object{};

    friend bool operator==(const AnnotationTarget&, const AnnotationTarget&) = default;
};

/// At most one of the four, and that one valid on its own.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> validate(const AnnotationTarget& target);

/// Whether @p target names anything.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT bool isEmpty(const AnnotationTarget& target) noexcept;

/// The document objects @p target depends on.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::vector<ObjectId> referencedObjects(
    const AnnotationTarget& target);

/// How text is lettered. ON PAPER, always.
struct TextStyle {
    /// Cap height in sheet millimetres. ISO 3098 gives 2.5, 3.5, 5, 7, 10;
    /// 3.5 is the usual body height on an A3 drawing. Nothing scales it.
    Length height = Length::fromSi(0.0035);

    friend bool operator==(const TextStyle&, const TextStyle&) = default;
};

/// Finite and greater than zero.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> validate(const TextStyle& style);

/// Whether a surface must be machined, must not be, or may be either.
///
/// ISO 1302's three basic symbols. The roughness value goes with it; the rest
/// of that standard -- lay direction, machining allowance, two-limit
/// requirements -- is not here, and the symbol is a foundation rather than
/// full coverage.
enum class MaterialRemoval : std::uint8_t {
    Any,
    Required,
    Prohibited,
};

/// "any", "required", "prohibited".
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::string_view toString(MaterialRemoval removal) noexcept;
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::optional<MaterialRemoval> materialRemovalFromString(
    std::string_view text) noexcept;

/// A surface-finish requirement.
struct SurfaceFinish {
    /// Arithmetic mean deviation, Ra. A LENGTH, so it cannot be confused
    /// with a bare number whose unit somebody has to remember: 3.2 um is
    /// stored as 3.2 um and written in micrometres.
    Length roughness{};
    MaterialRemoval removal = MaterialRemoval::Any;

    friend bool operator==(const SurfaceFinish&, const SurfaceFinish&) = default;
};

/// A finite roughness greater than zero, and a known removal requirement.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> validate(const SurfaceFinish& finish);

/// What an annotation is: the intent, and nothing drawn.
struct AnnotationDefinition {
    /// The view it belongs to. Every annotation has one: a leader points at
    /// geometry that only a view knows where to draw, and a note belongs on
    /// a sheet through the view it annotates.
    ViewId view{};
    AnnotationType type = AnnotationType::Note;
    /// What it is about. A note points at nothing and leaves this empty.
    AnnotationTarget target{};
    /// The words, for the kinds whose words are an engineer's choice: a note,
    /// a leader, and a datum's letter. A hole callout's text is DERIVED and
    /// this stays empty -- storing it would be storing a number that could
    /// disagree with the hole.
    std::string text{};
    TextStyle style{};
    /// Where the text sits, in SHEET millimetres. Absolute on the sheet, not
    /// an offset from the view: a note stays where it was put when the view
    /// is moved, which is what an engineer who placed it there meant.
    Point2D placement{};
    /// How far a centreline runs past the thing it belongs to, on paper.
    /// Centrelines only.
    Length extension = Length::fromSi(0.003);
    /// Half the length of a centre mark's arms, on paper. Centre marks only.
    Length armLength = Length::fromSi(0.0025);
    /// The surface-finish requirement. That kind only.
    std::optional<SurfaceFinish> finish{};
    /// What the feature is held to. Feature-control frames only. The cells a
    /// reader sees are derived from this; the frame stores the meaning.
    std::optional<FeatureControlFrame> frame{};

    friend bool operator==(const AnnotationDefinition&, const AnnotationDefinition&) = default;
};

/// Checks a definition on its own: a valid view; a target if the kind needs
/// one and none if it must not have one; text where the kind's words are
/// intent and none where they are derived; a valid style; finite paper
/// lengths that are greater than zero where they are used.
///
/// Whether the view and the target exist is checkAnnotation()'s, against the
/// document.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> validate(const AnnotationDefinition& definition);

/// Whether @p type derives its own text from the model.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT bool isModelDriven(AnnotationType type) noexcept;

/// Whether @p type must point at something.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT bool needsTarget(AnnotationType type) noexcept;

/// One annotation on a drawing view (type name "annotation").
class BETTERCAD_DRAWING_EXPORT Annotation final : public DocumentObject {
public:
    using Definition = AnnotationDefinition;
    static constexpr std::string_view kTypeName = "annotation";

    [[nodiscard]] static Result<std::unique_ptr<Annotation>> create(
        std::string name, const AnnotationDefinition& definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    /// Its view and whatever its target names.
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;

    [[nodiscard]] AnnotationId annotationId() const noexcept {
        return AnnotationId::fromValue(id().value());
    }
    [[nodiscard]] const AnnotationDefinition& definition() const noexcept { return definition_; }
    Result<bool> setDefinition(const AnnotationDefinition& definition);

private:
    Annotation(std::string name, const AnnotationDefinition& definition);

    AnnotationDefinition definition_;
};

} // namespace bettercad::drawing
