// The OCCT presentation of an engineering mesh (P16-VIZ-001).

#include "OcctMeshPresentation.hpp"

#include "core/geometry/occt/OcctBody.hpp"

#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_MaterialAspect.hxx>
#include <Prs3d_Presentation.hxx>
#include <PrsMgr_PresentationManager.hxx>
#include <NCollection_IncAllocator.hxx>
#include <NCollection_Vec3.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <SelectMgr_Selection.hxx>

#include <cmath>

namespace bettercad::renderer::occt {
namespace {

/// OCCT 8.0.1 DEPRECATES ITS OWN SPELLINGS, and -Werror turns each one into an
// error. Found the hard way here: Graphic3d_Vec3 wants NCollection_Vec3<float>,
// and SuppressBackFace/SetDistinguishOff want SetFaceCulling. INFRA-VIEWER-001
// hit the same wall with Standard_True, Standard_Integer and
// GetMessageString().

// The modes this presentation computes. Only one: a mesh has no wireframe
/// mode distinct from its shaded mode at the AIS level, because the style is a
/// property of the object and switching it recomputes.
constexpr int kDisplayMode = 0;

/// SI metres into the kernel's model length.
///
/// THE DEFECT THIS EXISTS TO FIX, and it would have made the whole milestone
/// useless in the GUI. MeshView::positions() are SI metres, because a
/// meshing::Node holds a Length and SI is what the project computes in. OCCT
/// MODEL SPACE IS MILLIMETRES -- core/geometry/occt/OcctBody.hpp:
/// "OCCT model space uses millimetres: its default precision (1e-7) and
/// tolerances are designed for millimetre-scale models". A mesh uploaded in
/// metres beside a CAD body in millimetres is therefore 1000 TIMES TOO SMALL.
///
/// How it showed up: a mesh displayed ALONE looked perfect, because FitAll
/// framed the only thing in the scene. Displayed beside its own CAD solid and
/// then fitted, it became a sub-pixel speck -- 0 covered pixels -- and the
/// test that caught it was the one asking whether hiding the CAD leaves the
/// mesh visible. The clue was that the same box measured 32674 pixels as a
/// mesh and 54120 with the solid added: one silhouette, two scales.
///
/// The conversion is `geometry::occt::toModel`, the project's only one, and it
/// belongs HERE rather than in MeshView: the adapter is unit-honest SI and is
/// tested headlessly, and millimetres are a fact about the kernel.
[[nodiscard]] double toModelLength(double metres) noexcept {
    return geometry::occt::toModel(Length::fromSi(metres));
}

// Not constexpr: Quantity_Color is not a literal type. Functions rather than
// namespace-scope objects, so there is no static initialisation order to
// reason about in a shared library.
[[nodiscard]] Quantity_Color facetColour() {
    return Quantity_Color{0.55, 0.62, 0.72, Quantity_TOC_sRGB};
}
[[nodiscard]] Quantity_Color edgeColour() {
    return Quantity_Color{0.12, 0.14, 0.18, Quantity_TOC_sRGB};
}
[[nodiscard]] Quantity_Color highlightColour() {
    return Quantity_Color{0.95, 0.62, 0.15, Quantity_TOC_sRGB};
}
/// A stale mesh's facets. Desaturated and much darker than the current
/// colour, so the difference survives a small viewport and a bad monitor.
[[nodiscard]] Quantity_Color staleFacetColour() {
    return Quantity_Color{0.33, 0.30, 0.26, Quantity_TOC_sRGB};
}
[[nodiscard]] Quantity_Color staleEdgeColour() {
    return Quantity_Color{0.52, 0.34, 0.10, Quantity_TOC_sRGB};
}

} // namespace

MeshPresentation::MeshPresentation(const MeshView& view, MeshDrawStyle style)
    : stamp_(view.stamp()), source_(view.source()), style_(style),
      triangleCount_(view.triangleCount()) {
    const std::span<const double> positions = view.positions();
    const std::span<const std::uint32_t> indices = view.triangleIndices();

    corners_.reserve(triangleCount_ * 9U);
    normals_.reserve(triangleCount_ * 3U);
    for (std::size_t triangle = 0U; triangle < triangleCount_; ++triangle) {
        std::array<std::array<double, 3>, 3> corner{};
        for (std::size_t which = 0U; which < 3U; ++which) {
            const std::size_t vertex = indices[(triangle * 3U) + which];
            corner[which] = {positions[vertex * 3U], positions[(vertex * 3U) + 1U],
                             positions[(vertex * 3U) + 2U]};
            corners_.push_back(toModelLength(corner[which][0]));
            corners_.push_back(toModelLength(corner[which][1]));
            corners_.push_back(toModelLength(corner[which][2]));
        }
        // n = (b - a) x (c - a), following the canonical winding. A degenerate
        // facet gets a zero normal rather than a NaN: P16 refuses degenerate
        // elements upstream, and a renderer that divided by zero here would
        // turn a refusal into a crash.
        const std::array<double, 3> u{corner[1][0] - corner[0][0], corner[1][1] - corner[0][1],
                                      corner[1][2] - corner[0][2]};
        const std::array<double, 3> v{corner[2][0] - corner[0][0], corner[2][1] - corner[0][1],
                                      corner[2][2] - corner[0][2]};
        std::array<double, 3> n{(u[1] * v[2]) - (u[2] * v[1]), (u[2] * v[0]) - (u[0] * v[2]),
                                (u[0] * v[1]) - (u[1] * v[0])};
        const double length = std::sqrt((n[0] * n[0]) + (n[1] * n[1]) + (n[2] * n[2]));
        if (length > 0.0) {
            n[0] /= length;
            n[1] /= length;
            n[2] /= length;
        }
        normals_.push_back(n[0]);
        normals_.push_back(n[1]);
        normals_.push_back(n[2]);
    }

    const std::span<const std::uint32_t> edgeIndices = view.edgeIndices();
    edgeCorners_.reserve(edgeIndices.size() * 3U);
    for (const std::uint32_t vertex : edgeIndices) {
        edgeCorners_.push_back(toModelLength(positions[vertex * 3U]));
        edgeCorners_.push_back(toModelLength(positions[(vertex * 3U) + 1U]));
        edgeCorners_.push_back(toModelLength(positions[(vertex * 3U) + 2U]));
    }
}

void MeshPresentation::setStyle(MeshDrawStyle style) {
    if (style == style_) {
        return;
    }
    style_ = style;
    SetToUpdate();
}

void MeshPresentation::setStale(bool stale) {
    if (stale == stale_) {
        return;
    }
    stale_ = stale;
    SetToUpdate();
}

void MeshPresentation::setHighlighted(std::span<const std::size_t> triangles) {
    highlighted_.assign(triangles.begin(), triangles.end());
    SetToUpdate();
}

int MeshPresentation::lastDetectedTriangle() const {
    if (sensitive_.IsNull()) {
        return -1;
    }
    return sensitive_->LastDetectedElement();
}

Handle(Graphic3d_ArrayOfTriangles) MeshPresentation::facets(
    std::span<const std::size_t> which) const {
    if (which.empty()) {
        return {};
    }
    Handle(Graphic3d_ArrayOfTriangles) array = new Graphic3d_ArrayOfTriangles(
        static_cast<int>(which.size() * 3U), 0, Graphic3d_ArrayFlags_VertexNormal);
    for (const std::size_t triangle : which) {
        if (triangle >= triangleCount_) {
            continue;
        }
        const gp_Dir normal{normals_[triangle * 3U], normals_[(triangle * 3U) + 1U],
                            normals_[(triangle * 3U) + 2U]};
        for (std::size_t corner = 0U; corner < 3U; ++corner) {
            const std::size_t at = (triangle * 9U) + (corner * 3U);
            array->AddVertex(gp_Pnt{corners_[at], corners_[at + 1U], corners_[at + 2U]}, normal);
        }
    }
    return array;
}

Handle(Graphic3d_ArrayOfSegments) MeshPresentation::edges() const {
    if (edgeCorners_.empty()) {
        return {};
    }
    const std::size_t points = edgeCorners_.size() / 3U;
    Handle(Graphic3d_ArrayOfSegments) array =
        new Graphic3d_ArrayOfSegments(static_cast<int>(points));
    for (std::size_t point = 0U; point < points; ++point) {
        array->AddVertex(gp_Pnt{edgeCorners_[point * 3U], edgeCorners_[(point * 3U) + 1U],
                                edgeCorners_[(point * 3U) + 2U]});
    }
    return array;
}

void MeshPresentation::Compute(const Handle(PrsMgr_PresentationManager)& /*manager*/,
                               const Handle(Prs3d_Presentation)& presentation, const int mode) {
    if (mode != kDisplayMode) {
        return;
    }
    presentation->Clear();

    const bool drawFacets =
        style_ == MeshDrawStyle::Shaded || style_ == MeshDrawStyle::ShadedWithEdges;
    const bool drawEdges =
        style_ == MeshDrawStyle::Wireframe || style_ == MeshDrawStyle::ShadedWithEdges;

    if (drawFacets) {
        // Everything except the highlighted facets, so the highlight is not
        // drawn twice and does not fight the base colour in the depth buffer.
        std::vector<std::size_t> plain;
        plain.reserve(triangleCount_);
        std::vector<bool> lit(triangleCount_, false);
        for (const std::size_t triangle : highlighted_) {
            if (triangle < triangleCount_) {
                lit[triangle] = true;
            }
        }
        for (std::size_t triangle = 0U; triangle < triangleCount_; ++triangle) {
            if (!lit[triangle]) {
                plain.push_back(triangle);
            }
        }

        const auto fillAspect = [](const Quantity_Color& colour) {
            Graphic3d_MaterialAspect material{Graphic3d_NameOfMaterial_Plastified};
            material.SetColor(colour);
            Handle(Graphic3d_AspectFillArea3d) aspect = new Graphic3d_AspectFillArea3d();
            aspect->SetInteriorStyle(Aspect_IS_SOLID);
            aspect->SetInteriorColor(colour);
            aspect->SetFrontMaterial(material);
            aspect->SetBackMaterial(material);
            // DOUBLE-SIDED, and not back-face culled. A boundary facet seen
            // from inside -- which is exactly what interior inspection does --
            // must still be lit rather than black. SetFaceCulling replaces the
            // deprecated SuppressBackFace/SetDistinguishOff pair.
            aspect->SetFaceCulling(Graphic3d_TypeOfBackfacingModel_DoubleSided);
            return aspect;
        };

        if (const Handle(Graphic3d_ArrayOfTriangles) array = facets(plain); !array.IsNull()) {
            const Handle(Graphic3d_Group) group = presentation->NewGroup();
            group->SetGroupPrimitivesAspect(
                fillAspect(stale_ ? staleFacetColour() : facetColour()));
            group->AddPrimitiveArray(array);
        }
        if (const Handle(Graphic3d_ArrayOfTriangles) array = facets(highlighted_);
            !array.IsNull()) {
            const Handle(Graphic3d_Group) group = presentation->NewGroup();
            group->SetGroupPrimitivesAspect(fillAspect(highlightColour()));
            group->AddPrimitiveArray(array);
        }
    }

    if (drawEdges) {
        if (const Handle(Graphic3d_ArrayOfSegments) array = edges(); !array.IsNull()) {
            const Handle(Graphic3d_Group) group = presentation->NewGroup();
            Handle(Graphic3d_AspectLine3d) aspect =
                new Graphic3d_AspectLine3d(stale_ ? staleEdgeColour() : edgeColour(),
                                           Aspect_TOL_SOLID, 1.0);
            group->SetGroupPrimitivesAspect(aspect);
            group->AddPrimitiveArray(array);
        }
    }
}

void MeshPresentation::ComputeSelection(const Handle(SelectMgr_Selection)& selection,
                                        const int mode) {
    if (mode != kDisplayMode || triangleCount_ == 0U) {
        return;
    }

    // PER-PRIMITIVE SELECTION, which is the whole reason a mesh is not an
    // AIS_Shape here. SetDetectElements makes the sensitive array record WHICH
    // triangle a pick hit, and LastDetectedElement reports it -- a render
    // index, which the caller translates through the MeshView. Without this,
    // picking a mesh would answer "the mesh", and node and element inspection
    // would have nothing to work from.
    const Handle(SelectMgr_EntityOwner) owner = new SelectMgr_EntityOwner(this, 0);
    sensitive_ = new Select3D_SensitivePrimitiveArray(owner);
    sensitive_->SetDetectElements(true);
    sensitive_->SetDetectElementMap(true);

    Handle(Graphic3d_Buffer) vertices =
        new Graphic3d_Buffer(new NCollection_IncAllocator());
    Graphic3d_Attribute attribute{Graphic3d_TOA_POS, Graphic3d_TOD_VEC3};
    if (!vertices->Init(static_cast<int>(triangleCount_ * 3U), &attribute, 1)) {
        return;
    }
    for (std::size_t corner = 0U; corner < triangleCount_ * 3U; ++corner) {
        auto* position = reinterpret_cast<NCollection_Vec3<float>*>(vertices->changeValue(static_cast<int>(corner)));
        position->x() = static_cast<float>(corners_[corner * 3U]);
        position->y() = static_cast<float>(corners_[(corner * 3U) + 1U]);
        position->z() = static_cast<float>(corners_[(corner * 3U) + 2U]);
    }
    if (!sensitive_->InitTriangulation(vertices, Handle(Graphic3d_IndexBuffer)(), TopLoc_Location{},
                                       true)) {
        return;
    }
    selection->Add(sensitive_);
}

} // namespace bettercad::renderer::occt
