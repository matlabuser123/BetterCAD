#pragma once

// The OCCT presentation of an engineering mesh (P16-VIZ-001).
//
// A PRIVATE header beside its source: nothing outside the renderer's OCCT
// adapter may see an AIS type, which is what rule 1 of the architecture check
// enforces.
//
// ONE AIS OBJECT FOR A WHOLE MESH, not one per element. A mesh of ten thousand
// tetrahedra displayed as ten thousand interactive objects would spend its time
// in the presentation manager and would be unusable; the brief forbids it
// outright. So the facets go into batched Graphic3d arrays and the whole mesh
// is one selectable object whose selection reports WHICH primitive was hit.
//
// IT HOLDS NO MESH. It is constructed from a renderer::MeshView -- itself a
// derived, disposable render cache -- and keeps vertex and index buffers plus
// the view's stamp. Every engineering question is answered by asking the
// canonical mesh, which this class has no reference to.

#if !defined(_WIN32)
#error "The viewer's window is WNT_Window; a non-Windows port needs its own Aspect_Window."
#endif

// Guarded, because the viewer's own translation unit defines these before
// including this header, and -Werror treats a redefinition as an error.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <AIS_InteractiveObject.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Select3D_SensitivePrimitiveArray.hxx>
#include <Standard_Type.hxx>

#include <bettercad/renderer/MeshView.hpp>
#include <bettercad/renderer/Viewer.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace bettercad::renderer::occt {

/// How a mesh presentation draws itself.
enum class MeshDrawStyle : std::uint8_t {
    /// Flat-shaded facets only.
    Shaded,
    /// Flat-shaded facets with their edges drawn over them: the default for
    /// inspection, because a shaded mesh with no edges is just a solid.
    ShadedWithEdges,
    /// Edges only.
    Wireframe,
};

/// One batched, selectable presentation of one mesh view.
class MeshPresentation : public AIS_InteractiveObject {
    DEFINE_STANDARD_RTTI_INLINE(MeshPresentation, AIS_InteractiveObject)

public:
    /// Takes the buffers from @p view. The view may be discarded afterwards:
    /// what is kept is a copy of its arrays and its stamp, so this object
    /// cannot outlive its meaning silently -- `stamp()` is what a caller
    /// compares against the current mesh.
    ///
    /// @p deformed OVERRIDES the vertex positions, and is empty for the
    /// undeformed mesh. It is in SI METRES, in exactly the layout of
    /// `MeshView::positions()`, and the caller has already checked its length
    /// -- `Viewer::displayDeformedMesh` refuses a mismatch.
    ///
    /// THE POINT OF TAKING IT HERE rather than converting it elsewhere
    /// (ADR-043): the single metres-to-millimetres conversion is in this
    /// constructor, so a deformed buffer and an undeformed one reach the
    /// kernel through the SAME call and are consistent by construction rather
    /// than by two call sites agreeing. P16-VIZ-001 recorded what the second
    /// site costs -- a mesh in metres beside a body in millimetres is 1000x
    /// too small, and alone in the scene that is invisible as an error.
    ///
    /// The normals and the selection array are rebuilt from the overridden
    /// corners, so a deformed facet is lit by its own normal and a pick
    /// resolves against what is on screen.
    MeshPresentation(const MeshView& view, MeshDrawStyle style,
                     std::span<const double> deformed = {});

    [[nodiscard]] const meshing::MeshStamp& stamp() const noexcept { return stamp_; }
    [[nodiscard]] MeshSource source() const noexcept { return source_; }
    [[nodiscard]] std::size_t triangleCount() const noexcept { return triangleCount_; }

    void setStyle(MeshDrawStyle style);

    /// Draws this mesh as STALE: it does not describe the model any more.
    ///
    /// A VISUAL DISTINCTION, so that "current" and "stale" cannot look the
    /// same. What it must not be is a quiet one -- the colour change is
    /// deliberately large, because the alternative is a user trusting a mesh
    /// of geometry that has since changed.
    void setStale(bool stale);
    [[nodiscard]] bool isStale() const noexcept { return stale_; }
    [[nodiscard]] MeshDrawStyle style() const noexcept { return style_; }

    /// Colours every facet CORNER from @p rgb: 9 doubles per render triangle,
    /// three sRGB components per corner, in `[0, 1]`. Empty clears the
    /// contour and returns the mesh to its uniform facet colour.
    ///
    /// PER CORNER, so one mechanism serves both channel domains: an element
    /// channel hands all three corners of a facet the same colour and the
    /// facet draws flat, while a nodal channel hands each corner its own and
    /// the interpolation across the facet is the element's linear shape
    /// function. Deciding which is the caller's job and is done headlessly by
    /// `renderer::contourCornerColours`.
    ///
    /// A VISUAL OPERATION, like `setStyle` and `setStale`: it touches no mesh,
    /// no result and no document, and it triggers no regeneration.
    void setFacetColours(std::span<const double> rgb);
    [[nodiscard]] bool hasContour() const noexcept { return !facetColours_.empty(); }

    /// The render triangles to draw as selected. GUI state, held here and
    /// nowhere near the mesh.
    void setHighlighted(std::span<const std::size_t> triangles);
    [[nodiscard]] const std::vector<std::size_t>& highlighted() const noexcept {
        return highlighted_;
    }

    /// Which render triangle the last pick detected, or a negative value.
    ///
    /// A RENDER INDEX, and the caller must translate it through the MeshView
    /// that built this presentation. That is the only bridge, and it is why
    /// this returns an int rather than an ElementId: an AIS object has no
    /// business producing engineering identity.
    [[nodiscard]] int lastDetectedTriangle() const;

protected:
    void Compute(const Handle(PrsMgr_PresentationManager)& manager,
                 const Handle(Prs3d_Presentation)& presentation, const int mode) override;
    void ComputeSelection(const Handle(SelectMgr_Selection)& selection, const int mode) override;

private:
    /// A non-indexed triangle array: three vertices per facet, each carrying
    /// that facet's own normal.
    ///
    /// FLAT SHADING IS THE RIGHT ANSWER HERE, and the reason is not
    /// performance. Averaging normals at shared vertices would smooth the
    /// facets into a curved-looking surface, which is precisely what an
    /// engineering mesh inspection must not do: the user is looking AT the
    /// faceting. It also makes render triangle i the i-th facet of the view,
    /// so the selection index needs no second mapping.
    /// @p withColours is false for the HIGHLIGHT group: a selected facet must
    /// keep the highlight colour rather than its contour band, or selecting
    /// something during a contour display would do nothing visible.
    [[nodiscard]] Handle(Graphic3d_ArrayOfTriangles) facets(
        std::span<const std::size_t> which, bool withColours) const;
    [[nodiscard]] Handle(Graphic3d_ArrayOfSegments) edges() const;

    meshing::MeshStamp stamp_{};
    MeshSource source_ = MeshSource::VolumeBoundary;
    MeshDrawStyle style_ = MeshDrawStyle::ShadedWithEdges;
    bool stale_ = false;
    std::size_t triangleCount_ = 0U;
    /// Flattened triangle corners: 9 doubles per facet.
    std::vector<double> corners_{};
    /// One normal per facet: 3 doubles.
    std::vector<double> normals_{};
    /// Flattened edge endpoints: 6 doubles per edge.
    std::vector<double> edgeCorners_{};
    /// Nine doubles per facet: three sRGB components for each of three
    /// corners. Empty means no contour.
    std::vector<double> facetColours_{};
    std::vector<std::size_t> highlighted_{};
    Handle(Select3D_SensitivePrimitiveArray) sensitive_{};
};

DEFINE_STANDARD_HANDLE(MeshPresentation, AIS_InteractiveObject)

} // namespace bettercad::renderer::occt
