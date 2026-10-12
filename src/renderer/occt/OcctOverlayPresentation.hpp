#pragma once

// The OCCT presentation of a structural overlay (P17-VIZ-001, ADR-045).
//
// A PRIVATE header beside its source: nothing outside the renderer's OCCT
// adapter may see an AIS type, which is what rule 1 of the architecture check
// enforces.
//
// IT HOLDS NO MESH, NO RESULT AND NO IDENTITY. It is constructed from a
// `StructuralOverlay` -- a list of segments in SI metres -- and keeps a copy of
// those segments in millimetres plus the role they depict. Every engineering
// question was answered before the geometry got here.
//
// SEGMENTS ONLY, and deliberately: the arrowheads are line segments produced
// headlessly rather than shaded cones produced here, so the geometry that
// decides whether an arrow points the right way is testable without a GPU.
//
// NOT SELECTABLE. An overlay is an annotation drawn over a mesh, and making it
// pickable would put an arrow between the user and the element they were
// trying to click. `ComputeSelection` adds nothing.

#if !defined(_WIN32)
#error "The viewer's window is WNT_Window; a non-Windows port needs its own Aspect_Window."
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <AIS_InteractiveObject.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Standard_Type.hxx>

#include <bettercad/renderer/StructuralOverlay.hpp>

#include <cstddef>
#include <vector>

namespace bettercad::renderer::occt {

/// One batched presentation of one structural overlay.
class OverlayPresentation : public AIS_InteractiveObject {
    DEFINE_STANDARD_RTTI_INLINE(OverlayPresentation, AIS_InteractiveObject)

public:
    /// Takes the segments from @p overlay, converting metres to millimetres
    /// through the adapter's single conversion. The overlay may be discarded
    /// afterwards.
    explicit OverlayPresentation(const StructuralOverlay& overlay);

    [[nodiscard]] OverlayRole role() const noexcept { return role_; }
    [[nodiscard]] std::size_t segmentCount() const noexcept { return segmentCount_; }

protected:
    void Compute(const Handle(PrsMgr_PresentationManager)& manager,
                 const Handle(Prs3d_Presentation)& presentation, const int mode) override;
    void ComputeSelection(const Handle(SelectMgr_Selection)& selection, const int mode) override;

private:
    OverlayRole role_ = OverlayRole::LoadArrows;
    std::size_t segmentCount_ = 0U;
    /// Flattened endpoints in MILLIMETRES: six doubles per segment.
    std::vector<double> endpoints_{};
};

} // namespace bettercad::renderer::occt
