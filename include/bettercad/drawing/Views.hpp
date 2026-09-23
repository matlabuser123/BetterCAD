#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/math/RigidTransform.hpp>
#include <bettercad/drawing/Export.hpp>
#include <bettercad/drawing/View.hpp>

#include <functional>
#include <string>
#include <vector>

// The document-facing operations on views (P14-VIEW-001).
//
// Free functions, not Document members, for the reason Sheets.hpp gives:
// Document is layer 0 and must not know drawings exist (ADR-015).
//
// Everything here that computes rather than stores is named `effective`,
// because a projected view's orientation, scale and placement are DERIVED
// from its parent and its sheet (ADR-011, ADR-018). Nothing caches them.
namespace bettercad {
class Document;
}

namespace bettercad::drawing {

/// The body a view's source produced in the current regeneration, or nullptr.
///
/// The same type face resolution and the assembly solver already take, rather
/// than a second one that would have to be converted at every call.
using BodyLookup = std::function<const geometry::Body*(ObjectId object)>;

/// The SOLVED transform of a component in the current regeneration, or
/// nullptr.
///
/// The same shape features::Regenerator::transform() already has. A view of a
/// component must use this and never the component's canonical placement: the
/// placement is where the engineer asked for it to go, the solved transform
/// is where the mates actually put it (ADR-005), and a drawing that projected
/// the first would show a machine that was never assembled.
using TransformLookup = std::function<const RigidTransform3D*(ComponentId component)>;

/// Checks a definition against @p document: the sheet exists, a base view's
/// source is an object of this document that produces a body, and a projected
/// view's parent exists, is a view, and sits on the same sheet.
///
/// A projected view whose parent is on another sheet would align against
/// something that is not there, so it is refused rather than left to look
/// wrong.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> checkView(const Document& document,
                                                              const ViewDefinition& definition);

/// Adds a view and returns its ID. Fails, changing nothing, if the definition
/// is malformed or does not check out; no ID is consumed by a failed call.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<ViewId> createView(Document& document, std::string name,
                                                                 const ViewDefinition& definition);

/// Replaces the definition of the view with @p id, after the same checks
/// createView runs. Returns whether anything changed.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<bool> setViewDefinition(Document& document, ViewId id,
                                                                      const ViewDefinition& definition);

/// The view with @p id, or nullptr.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT const View* findView(const Document& document,
                                                            ViewId id) noexcept;

/// Every view in @p document, in ascending ID order.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::vector<ViewId> views(const Document& document);

/// Every view on @p sheet, in ascending ID order.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::vector<ViewId> viewsOn(const Document& document,
                                                                   SheetId sheet);

/// Removes the view with @p id. Fails if there is none, or if a projected
/// view still names it as its parent -- removing it would leave that view
/// with nothing to derive from, which is a broken drawing rather than a
/// smaller one.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> removeView(Document& document, ViewId id);

// --- Derived (ADR-011). Computed on every call, never stored. ---

/// What the view actually looks at: its own source, or its parent's.
///
/// Walks up the chain, so a view projected from a projected view still
/// resolves to the one source the chain is rooted in. Fails on a chain that
/// does not terminate in a base view.
/// What @p id draws: its own subject, or its parent's.
///
/// Every kind but base takes this from its parent along with the source, so
/// a section of an assembly view is a section OF THE ASSEMBLY and cannot be
/// made to mean something else.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<ViewSubject> effectiveSubject(const Document& document,
                                                                            ViewId id);

/// The active occurrences an assembly view draws, in ascending ComponentId
/// order, as assembly::activeComponents() gives them.
///
/// Fails for a view whose subject is a single object -- ask effectiveSource()
/// for that -- and for an assembly with no active components, because a
/// drawing of nothing is not a drawing.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<std::vector<ComponentId>> drawnOccurrences(
    const Document& document, ViewId id);

[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<ObjectReference> effectiveSource(const Document& document,
                                                                               ViewId id);

/// The basis the view looks through: its own if it is a base view, otherwise
/// its parent's turned by its direction (ADR-018).
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<ViewBasis> effectiveBasis(const Document& document,
                                                                        ViewId id);

/// The scale in force: the view's own if it sets one, otherwise its sheet's.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<DrawingScale> effectiveScale(const Document& document,
                                                                           ViewId id);

/// Where the view's centre sits on the sheet, in sheet millimetres.
///
/// A base view stores it. A projected view derives it from its parent's, its
/// own spacing, and the sheet's projection convention -- which is what makes
/// the alignment exact rather than maintained: a Top view shares its parent's
/// x because the derivation gives it the same number, not because something
/// keeps them equal.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<Point2D> effectivePlacement(const Document& document,
                                                                          ViewId id);

/// What the view draws, in sheet coordinates.
///
/// The source's body is projected through the view's basis, centred on its
/// projected bounding box, scaled, and placed. @p bodies supplies the body of
/// the object the view's source names; without it, or with a source that
/// produced none, this fails rather than drawing an empty view.
/// @p transforms supplies a component's solved transform, and is required
/// only when the view's source is a component.
/// A section view draws the CUT solid, so what it projects is what is left
/// after its plane has removed the material between it and the viewer. A
/// detail view draws its parent's projection, cropped to its region and
/// enlarged about the region's centre.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<ProjectedGeometry> projectedGeometry(
    const Document& document, ViewId id, const BodyLookup& bodies,
    const TransformLookup& transforms = {});

/// Where a model point lands on this view's sheet, in sheet millimetres.
///
/// The SAME mapping the view draws its own geometry with -- the basis, the
/// centring on the projected bounds, the scale and the placement -- so that
/// anything put on the sheet by this lands on the geometry it belongs to
/// rather than near it. An annotation pointing at a hole is only pointing at
/// the hole if it uses the projection the hole was drawn with.
///
/// Needs @p bodies because the centring is the projected bounding box of what
/// the view draws, and that cannot be known without the body.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<Point2D> toSheet(const Document& document, ViewId id,
                                                               const Point3D& point,
                                                               const BodyLookup& bodies,
                                                               const TransformLookup& transforms = {});

/// The cut faces of a section view, hatched, in sheet coordinates.
///
/// Fails with InvalidArgument for a view that is not a section: a view with
/// no cutting plane has no cut faces, and returning an empty set would make
/// "nothing was cut" and "this is not a section" the same answer.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<SectionGeometry> sectionOf(
    const Document& document, ViewId id, const BodyLookup& bodies,
    const TransformLookup& transforms = {});

} // namespace bettercad::drawing
