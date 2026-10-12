// The OCCT presentation of a structural overlay (P17-VIZ-001, ADR-045).

#include "OcctOverlayPresentation.hpp"

#include "core/geometry/occt/OcctBody.hpp"

#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Prs3d_Presentation.hxx>
#include <PrsMgr_PresentationManager.hxx>
#include <SelectMgr_Selection.hxx>

namespace bettercad::renderer::occt {
namespace {

constexpr int kDisplayMode = 0;

/// THE SAME SINGLE CONVERSION the mesh presentation uses, and for the same
/// reason: millimetres are a fact about the kernel, and an overlay that
/// reached model space through a second expression could disagree with the
/// mesh it is drawn over by a factor nobody would see until the two were
/// displayed together.
[[nodiscard]] double toModelLength(double metres) noexcept {
    return geometry::occt::toModel(Length::fromSi(metres));
}

/// Not constexpr: Quantity_Color is not a literal type.
[[nodiscard]] Quantity_Color loadColour() {
    return Quantity_Color{0.90, 0.35, 0.10, Quantity_TOC_sRGB};
}
[[nodiscard]] Quantity_Color reactionColour() {
    return Quantity_Color{0.20, 0.55, 0.95, Quantity_TOC_sRGB};
}
[[nodiscard]] Quantity_Color restraintColour() {
    return Quantity_Color{0.20, 0.75, 0.35, Quantity_TOC_sRGB};
}

/// A COLOUR PER ROLE, so a load, a reaction and a support are told apart
/// without a legend. The three are far apart in hue rather than in shade,
/// because the alternative is a user reading a reaction as a load.
[[nodiscard]] Quantity_Color colourOf(OverlayRole role) {
    switch (role) {
    case OverlayRole::LoadArrows:
        return loadColour();
    case OverlayRole::ReactionArrows:
        return reactionColour();
    case OverlayRole::RestraintSymbols:
        return restraintColour();
    }
    return loadColour();
}

} // namespace

OverlayPresentation::OverlayPresentation(const StructuralOverlay& overlay)
    : role_(overlay.role), segmentCount_(overlay.segments.size()) {
    endpoints_.reserve(segmentCount_ * 6U);
    for (const OverlaySegment& segment : overlay.segments) {
        endpoints_.push_back(toModelLength(segment.from.x.si()));
        endpoints_.push_back(toModelLength(segment.from.y.si()));
        endpoints_.push_back(toModelLength(segment.from.z.si()));
        endpoints_.push_back(toModelLength(segment.to.x.si()));
        endpoints_.push_back(toModelLength(segment.to.y.si()));
        endpoints_.push_back(toModelLength(segment.to.z.si()));
    }
}

void OverlayPresentation::Compute(const Handle(PrsMgr_PresentationManager)& /*manager*/,
                                  const Handle(Prs3d_Presentation)& presentation,
                                  const int mode) {
    if (mode != kDisplayMode || segmentCount_ == 0U) {
        return;
    }
    presentation->Clear();

    Handle(Graphic3d_ArrayOfSegments) array =
        new Graphic3d_ArrayOfSegments(static_cast<int>(segmentCount_ * 2U));
    for (std::size_t segment = 0U; segment < segmentCount_; ++segment) {
        const std::size_t at = segment * 6U;
        array->AddVertex(gp_Pnt{endpoints_[at], endpoints_[at + 1U], endpoints_[at + 2U]});
        array->AddVertex(gp_Pnt{endpoints_[at + 3U], endpoints_[at + 4U], endpoints_[at + 5U]});
    }

    const Handle(Graphic3d_Group) group = presentation->NewGroup();
    // WIDTH 2, so an overlay reads over a shaded mesh rather than
    // disappearing into its edges, which are drawn at 1.
    Handle(Graphic3d_AspectLine3d) aspect =
        new Graphic3d_AspectLine3d(colourOf(role_), Aspect_TOL_SOLID, 2.0);
    group->SetGroupPrimitivesAspect(aspect);
    group->AddPrimitiveArray(array);
}

void OverlayPresentation::ComputeSelection(const Handle(SelectMgr_Selection)& /*selection*/,
                                           const int /*mode*/) {
    // NOTHING, DELIBERATELY. An overlay is an annotation drawn over a mesh;
    // making it pickable would put an arrow between the user and the element
    // they were trying to click.
}

} // namespace bettercad::renderer::occt
