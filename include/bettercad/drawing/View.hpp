#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/document/ObjectReference.hpp>
#include <bettercad/core/math/BoundingBox.hpp>
#include <bettercad/core/math/Frame.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/drawing/Export.hpp>
#include <bettercad/drawing/HiddenLine.hpp>
#include <bettercad/drawing/Section.hpp>
#include <bettercad/drawing/Sheet.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A drawing view (P14-VIEW-001, implementing ADR-011, ADR-013, ADR-017 and
// ADR-018).
//
// A view is a DocumentObject that says WHAT to look at, FROM WHERE, HOW BIG
// and WHERE ON THE SHEET. Everything it draws is derived from that and the
// model, and none of it is persisted (ADR-011).
//
// A view is exactly one KIND, and never two:
//
//     base       names a source and a standard orientation
//     projected  names a parent and a direction, and takes its orientation
//                from the parent (ADR-018). It has no orientation of its own
//                to contradict the parent with.
//     section    names a parent and a cutting plane, and takes its
//                orientation from the plane: the plane's own frame IS the
//                basis it looks through, so the two cannot disagree.
//     detail     names a parent and a region OF THE PARENT'S SHEET, and
//                looks the same way the parent does, only closer.
//     auxiliary  names a parent and a direction of its own, for a face that
//                none of the six standard orientations faces squarely.
//
// Every kind but base derives its source from its parent, so no two views of
// one thing can disagree about what that thing is (P14-VIEW-001).
namespace bettercad::drawing {

/// What a view looks at: one object, or the whole assembly.
///
/// There is no assembly OBJECT to name -- ADR-002 puts the assembly in the
/// document itself -- so "the whole assembly" cannot be said with an
/// ObjectReference. It is said here instead, explicitly, rather than by
/// leaving the reference invalid and hoping every reader agrees what that
/// meant (ADR-021).
///
/// WHICH occurrences an assembly view draws is not stored: it is
/// assembly::activeComponents() at the moment the view is drawn, so the
/// document's active configuration and both layers of suppression have one
/// implementation and a drawing cannot come to disagree with the solver about
/// what is in the assembly.
enum class ViewSubject : std::uint8_t {
    /// The object `source` names: a feature, or one component occurrence.
    Object,
    /// Every active component occurrence of this document.
    Assembly,
};

/// "object", "assembly".
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::string_view toString(ViewSubject subject) noexcept;
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::optional<ViewSubject> viewSubjectFromString(
    std::string_view text) noexcept;

/// One of the six standard orthographic orientations, or the isometric.
///
/// Under ADR-013 a view frame's normal points from the model toward the
/// viewer, which makes Front, Right and Top exactly the three principal
/// frames the codebase already has, and the other three their reverses.
enum class StandardView : std::uint8_t {
    Front,
    Rear,
    Left,
    Right,
    Top,
    Bottom,
    Isometric,
};

/// "front", "rear", "left", "right", "top", "bottom", "isometric".
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::string_view toString(StandardView view) noexcept;
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::optional<StandardView> standardViewFromString(
    std::string_view text) noexcept;

/// Which view a projected view is, relative to its parent.
///
/// This names the VIEW, not the side of the sheet it lands on: a Top view is
/// a view of the parent's top, and whether it is placed above or below the
/// parent is the sheet's projection convention (ADR-018).
enum class ProjectedDirection : std::uint8_t {
    Top,
    Bottom,
    Left,
    Right,
};

/// "top", "bottom", "left", "right".
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::string_view toString(ProjectedDirection direction) noexcept;
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::optional<ProjectedDirection> projectedDirectionFromString(
    std::string_view text) noexcept;

/// What kind of view this is. Exactly one, always.
enum class ViewKind : std::uint8_t {
    Base,
    Projected,
    Section,
    Detail,
    Auxiliary,
};

/// "base", "projected", "section", "detail", "auxiliary".
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::string_view toString(ViewKind kind) noexcept;
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::optional<ViewKind> viewKindFromString(
    std::string_view text) noexcept;

/// The part of a parent view a detail view enlarges.
///
/// The centre and radius are in the PARENT'S SHEET coordinates, because that
/// is where the engineer draws the detail circle -- on the drawing, around
/// something already visible -- and not in model space, where they would have
/// to work out where the feature had landed.
struct DetailRegion {
    Point2D centre{};
    Length radius{};
    /// Whether the detail is cropped to the circle. A cropped detail shows
    /// only what falls inside it; an uncropped one keeps whatever the circle
    /// touched, which is what a "partial view" is.
    bool cropped = true;

    friend bool operator==(const DetailRegion&, const DetailRegion&) = default;
};

/// Finite centre, and a radius greater than zero.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> validate(const DetailRegion& region);

/// The direction an auxiliary view looks from.
///
/// The normal points from the model toward the viewer, as every view basis
/// does (ADR-013); the reference fixes which way up the view sits and is
/// projected into the view plane the way Frame3D::create does.
struct ViewDirection {
    Direction3D normal = Direction3D::unitY().reversed();
    Direction3D reference = Direction3D::unitX();

    friend bool operator==(const ViewDirection&, const ViewDirection&) = default;
};

/// The two directions usable and not parallel.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> validate(const ViewDirection& direction);

/// Which way, and how far, a view is displaced from its parent on the sheet.
///
/// @p normal is the child's view normal expressed in the PARENT'S sheet
/// axes -- (right . n, up . n). The view is placed on the far side of the
/// parent in first angle and the near side in third (ADR-018), which is the
/// whole of the difference, exactly as placementStep says for the four
/// orthogonal directions.
///
/// Returns (0, 0) when the child looks the same way as its parent, or exactly
/// opposite: neither has a side of the sheet to be on.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::pair<double, double> sheetDisplacement(
    const std::pair<double, double>& normal, ProjectionConvention convention) noexcept;

/// The orthonormal basis a view looks through.
///
/// Right and up are the sheet's axes; the normal points from the model toward
/// the viewer (ADR-013). Stored as a Frame3D so the orthonormality and
/// handedness checks are the qualified ones rather than a second copy.
using ViewBasis = Frame3D;

/// The basis of @p view, with its origin at the model origin.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<ViewBasis> basisOf(StandardView view);

/// The basis a projected view in @p direction has, given its parent's.
///
/// Derived, never stored: a projected view that carried its own orientation
/// could contradict its parent, and ADR-018 removes the possibility rather
/// than testing for it.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<ViewBasis> projectedBasis(const ViewBasis& parent,
                                                                        ProjectedDirection direction);

/// Which way a projected view is displaced from its parent on the sheet,
/// as a unit step in sheet coordinates: (0,-1) for a first-angle Top view.
///
/// This is the whole of the first-angle/third-angle difference (ADR-018).
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::pair<double, double> placementStep(
    ProjectedDirection direction, ProjectionConvention convention) noexcept;

/// What a view is: the intent, and nothing derived.
struct ViewDefinition {
    /// Which kind of view this is. Every other field is either required by
    /// that kind or must be left alone, and validate() says which.
    ViewKind kind = ViewKind::Base;
    /// The sheet this view sits on.
    SheetId sheet{};
    /// Whether this view draws one object or the whole assembly. A base
    /// view says; every other kind inherits its parent's, for the same reason
    /// it inherits the source.
    ViewSubject subject = ViewSubject::Object;
    /// What it looks at, when `subject` is Object. A base view names it; a
    /// projected view leaves it invalid and inherits its parent's, so two
    /// views of one thing cannot disagree about what that thing is. An
    /// assembly view leaves it invalid and must: there is no one object it
    /// draws.
    ObjectReference source{};
    /// A base view's orientation. Exactly one of this and `parent` is set.
    std::optional<StandardView> orientation{};
    /// The parent of every kind but base.
    std::optional<ViewId> parent{};
    /// A projected view's direction from its parent.
    std::optional<ProjectedDirection> direction{};
    /// A section view's cutting plane. The plane's frame is also the basis
    /// the section looks through, so a section cannot be oriented against
    /// its own cut.
    std::optional<CuttingPlane> section{};
    /// How a section view is hatched. Ignored by every other kind.
    HatchSettings hatch{};
    /// A detail view's region of its parent's sheet.
    std::optional<DetailRegion> detail{};
    /// An auxiliary view's own direction.
    std::optional<ViewDirection> auxiliary{};
    /// How far from the parent, along the alignment axis. Projected views
    /// only; the placement itself is derived from the parent's.
    Length spacing{};
    /// What this view does with the lines the solid hides, and with the
    /// lines where a blend runs into its face. Intent, so it is stored;
    /// every kind of view has it.
    HiddenLineSettings hiddenLine{};
    /// Absent means "the sheet's scale". A view may override it.
    std::optional<DrawingScale> scale{};
    /// Where the view's projected bounding-box CENTRE sits on the sheet, in
    /// sheet coordinates. Base and detail views only; every other kind
    /// derives its placement from its parent and the convention, so storing
    /// one would be storing derived state that could contradict it.
    Point2D placement{};

    friend bool operator==(const ViewDefinition&, const ViewDefinition&) = default;
};

/// Checks a definition on its own: a valid sheet, the fields the kind needs
/// and none it does not, a valid scale if one is given, and finite numbers
/// throughout.
///
/// A field belonging to another kind is an error rather than something
/// quietly ignored: a definition carrying a cutting plane AND a detail region
/// was built by something confused about which it was making, and the second
/// one to be read would silently win.
///
/// Whether the sheet, the source and the parent exist is checked against the
/// document by checkView(), because a definition alone cannot know.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> validate(const ViewDefinition& definition);

/// Whether @p definition describes a base view (as opposed to any kind that
/// derives from a parent).
[[nodiscard]] BETTERCAD_DRAWING_EXPORT bool isBaseView(const ViewDefinition& definition) noexcept;

/// One view on a drawing sheet (type name "view").
class BETTERCAD_DRAWING_EXPORT View final : public DocumentObject {
public:
    using Definition = ViewDefinition;
    static constexpr std::string_view kTypeName = "view";

    [[nodiscard]] static Result<std::unique_ptr<View>> create(std::string name,
                                                              const ViewDefinition& definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    /// The sheet it sits on, the source it draws, and its parent if it has
    /// one -- so a view rebuilds when any of them moves.
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;

    [[nodiscard]] ViewId viewId() const noexcept { return ViewId::fromValue(id().value()); }
    [[nodiscard]] const ViewDefinition& definition() const noexcept { return definition_; }
    Result<bool> setDefinition(const ViewDefinition& definition);

private:
    View(std::string name, const ViewDefinition& definition);

    ViewDefinition definition_;
};

// --- Derived geometry (ADR-011). Computed, never stored, never written. ---

/// A model point projected into view-plane coordinates, in model units.
///
///     x = (P - O) . right      y = (P - O) . up
///
/// which is exactly Frame3D::toLocal -- orthographic projection onto the
/// view plane, qualified since P0 and not reimplemented here.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Point2D projectToViewPlane(const ViewBasis& basis,
                                                                  const Point3D& point) noexcept;

/// How far @p point lies in front of the view plane. Positive is toward the
/// viewer. Not used for placement; kept because a later milestone's
/// hidden-line pass needs exactly this and it belongs with the projection.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Length depthInView(const ViewBasis& basis,
                                                          const Point3D& point) noexcept;

/// What a view draws: classified lines in SHEET coordinates, and their
/// bounds.
///
/// Derived on every request and never stored. Each edge says whether the
/// solid hides it and what kind of line it is (P14-HLR-001), so a renderer
/// can give it the right style without deciding the geometry again.
struct ProjectedGeometry {
    /// Every line the view draws, after coincident lines have been merged
    /// and the view's own settings have dropped the classes it does not show.
    std::vector<DrawnEdge> edges{};
    /// Every drawn point, the sampled points of curves included.
    std::vector<Point2D> points{};
    /// Bounds of the view BEFORE its settings dropped anything, so that
    /// turning hidden lines off does not move the drawing on the sheet.
    BoundingBox2D bounds{};
    /// How many lines were dropped because another drew the same line. A
    /// detail view reports its PARENT's count: the merge happened there, on
    /// the whole view, before this one cropped it.
    std::size_t merged = 0;
    /// How many were dropped because this view does not show their class.
    std::size_t suppressed = 0;
};

} // namespace bettercad::drawing
