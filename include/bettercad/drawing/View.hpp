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
// A view is one of two things and never both:
//
//     base       names a source and a standard orientation
//     projected  names a parent and a direction, and takes its orientation
//                from the parent (ADR-018). It has no orientation of its own
//                to contradict the parent with.
namespace bettercad::drawing {

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
    /// The sheet this view sits on.
    SheetId sheet{};
    /// What it looks at. A base view names it; a projected view leaves it
    /// invalid and inherits its parent's, so two views of one thing cannot
    /// disagree about what that thing is.
    ObjectReference source{};
    /// A base view's orientation. Exactly one of this and `parent` is set.
    std::optional<StandardView> orientation{};
    /// A projected view's parent.
    std::optional<ViewId> parent{};
    /// A projected view's direction from its parent.
    std::optional<ProjectedDirection> direction{};
    /// How far from the parent, along the alignment axis. Projected views
    /// only; the placement itself is derived from the parent's.
    Length spacing{};
    /// Absent means "the sheet's scale". A view may override it.
    std::optional<DrawingScale> scale{};
    /// Where the view's projected bounding-box CENTRE sits on the sheet, in
    /// sheet coordinates. Base views only: a projected view's placement is
    /// derived from its parent's and the convention.
    Point2D placement{};

    friend bool operator==(const ViewDefinition&, const ViewDefinition&) = default;
};

/// Checks a definition on its own: a valid sheet, exactly one of orientation
/// and parent, a direction iff there is a parent, a source iff there is not,
/// a valid scale if one is given, and finite placement and spacing.
///
/// Whether the sheet, the source and the parent exist is checked against the
/// document by checkView(), because a definition alone cannot know.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> validate(const ViewDefinition& definition);

/// Whether @p definition describes a base view (as opposed to a projected one).
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

/// What a view draws: points in SHEET coordinates, and their bounds.
///
/// Derived on every request and never stored. The segments are the straight
/// edges of the source, projected; a curved edge contributes its sampled
/// endpoints and midpoint only, because turning a curve into a drawn curve
/// -- and deciding which parts of it are visible -- is P14-HLR-001.
struct ProjectedGeometry {
    /// Projected straight edges, as pairs of sheet-space points.
    std::vector<std::pair<Point2D, Point2D>> segments{};
    /// Every projected point, including the sampled points of curved edges.
    std::vector<Point2D> points{};
    /// Bounds of `points`, in sheet coordinates.
    BoundingBox2D bounds{};
};

} // namespace bettercad::drawing
