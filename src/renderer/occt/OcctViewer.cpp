// The OCCT side of the viewer (INFRA-VIEWER-001).
//
// THE ONLY FILE IN THE RENDERER THAT SEES OCCT. Rule 1 of
// tests/architecture/CheckLayering.cmake confines OCCT headers to an `occt/`
// adapter directory under src/, and that rule already anticipates more than
// one adapter -- this is the second, after core/geometry's.
//
// WHY IT INCLUDES core/geometry's PRIVATE ADAPTER HEADER. A `Body` deliberately
// exposes no kernel type through its public API, so the only way to reach the
// `TopoDS_Shape` behind one is `occt::BodyAccess`, whose own comment says it is
// "Access to Body internals for the adapter". This IS that adapter. The
// dependency direction is correct -- renderer is layer 6, core is layer 0 --
// and the alternative would be displaying a tessellation instead of the shape,
// which would give up `AIS_Shape`'s face, edge and vertex selection owners
// that P16-VIZ-001's CAD-to-mesh linkage needs.
//
// WINDOWS ONLY, deliberately and visibly. `WNT_Window` is the platform's
// Aspect_Window, and the project targets Windows AMD64. The guard below is a
// seam for a future port rather than decoration: a port replaces the window
// and nothing else in this file.
#if !defined(_WIN32)
#error "The viewer's window is WNT_Window; a non-Windows port needs its own Aspect_Window."
#endif

// FIRST, and it has to be: OCCT's own headers pull in a restricted windows.h,
// after which CS_OWNDC and WS_OVERLAPPEDWINDOW are not declared.
//
// WIN32_LEAN_AND_MEAN IS NOT TIDINESS. Without it windows.h pulls in rpc.h and
// rpcndr.h, which declare a global `byte`; that shadows the `byte` parameter in
// core/Uuid.hpp and the build fails under -Wshadow -Werror. NOMINMAX is the
// companion trap: the min/max macros break std::min and std::clamp, both of
// which this file uses.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "OcctMeshPresentation.hpp"
#include "core/geometry/occt/OcctBody.hpp"
#include "core/geometry/occt/OcctGuard.hpp"

#include <bettercad/renderer/Viewer.hpp>

#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <AIS_Shape.hxx>
#include <AIS_ViewCube.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Graphic3d_Camera.hxx>
#include <Image_AlienPixMap.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_Drawer.hxx>
#include <Quantity_Color.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <V3d_TypeOfOrientation.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <WNT_WClass.hxx>
#include <WNT_Window.hxx>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>
#include <vector>

namespace bettercad::renderer {
namespace {

/// The window class every viewer window shares. OCCT wants a registered
/// Win32 class, and registering one per view would exhaust the class table in
/// a long session.
[[nodiscard]] Handle(WNT_WClass) sharedWindowClass() {
    static Handle(WNT_WClass) windowClass =
        new WNT_WClass("BetterCADViewer", nullptr, CS_OWNDC);
    return windowClass;
}

[[nodiscard]] Result<void> checkSize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("viewer: a {}x{} view has no area", width, height));
    }
    return {};
}

[[nodiscard]] Quantity_Color colourOf(Rgb colour) {
    return Quantity_Color(static_cast<double>(colour.red) / 255.0,
                          static_cast<double>(colour.green) / 255.0,
                          static_cast<double>(colour.blue) / 255.0, Quantity_TOC_sRGB);
}

[[nodiscard]] V3d_TypeOfOrientation orientationOf(StandardView view) {
    switch (view) {
    case StandardView::Front:
        return V3d_Yneg;
    case StandardView::Back:
        return V3d_Ypos;
    case StandardView::Left:
        return V3d_Xneg;
    case StandardView::Right:
        return V3d_Xpos;
    case StandardView::Top:
        return V3d_Zpos;
    case StandardView::Bottom:
        return V3d_Zneg;
    case StandardView::Isometric:
        return V3d_XposYnegZpos;
    }
    return V3d_XposYnegZpos;
}

} // namespace

std::string_view toString(StandardView view) noexcept {
    switch (view) {
    case StandardView::Front:
        return "front";
    case StandardView::Back:
        return "back";
    case StandardView::Left:
        return "left";
    case StandardView::Right:
        return "right";
    case StandardView::Top:
        return "top";
    case StandardView::Bottom:
        return "bottom";
    case StandardView::Isometric:
        return "isometric";
    }
    return "unknown_standard_view";
}

Rgb RenderedImage::at(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return Rgb{};
    }
    const std::size_t offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                static_cast<std::size_t>(x)) *
                               3U;
    if (offset + 2U >= rgb.size()) {
        return Rgb{};
    }
    return Rgb{rgb[offset], rgb[offset + 1U], rgb[offset + 2U]};
}

std::size_t RenderedImage::coverage(Rgb background, int tolerance) const {
    std::size_t covered = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Rgb pixel = at(x, y);
            const int dr = std::abs(static_cast<int>(pixel.red) - static_cast<int>(background.red));
            const int dg =
                std::abs(static_cast<int>(pixel.green) - static_cast<int>(background.green));
            const int db =
                std::abs(static_cast<int>(pixel.blue) - static_cast<int>(background.blue));
            if (dr > tolerance || dg > tolerance || db > tolerance) {
                ++covered;
            }
        }
    }
    return covered;
}

/// Everything OCCT, behind the public class.
class Viewer::Impl {
public:
    Handle(OpenGl_GraphicDriver) driver;
    Handle(V3d_Viewer) viewer;
    Handle(V3d_View) view;
    Handle(AIS_InteractiveContext) context;
    Handle(WNT_Window) window;

    std::vector<DisplayedObject> displayed;
    /// Parallel to `displayed`: the presentation each entry is showing. Kept
    /// beside rather than inside `DisplayedObject`, because that is a public
    /// value type and an OCCT handle cannot appear in one.
    std::vector<Handle(AIS_InteractiveObject)> presentations;
    PresentationId::ValueType lastPresentation = 0;

    Rgb background{32, 32, 32};
    int width = 0;
    int height = 0;

    [[nodiscard]] std::optional<std::size_t> indexOf(PresentationId id) const {
        for (std::size_t i = 0; i < displayed.size(); ++i) {
            if (displayed[i].presentation == id) {
                return i;
            }
        }
        return std::nullopt;
    }

    /// Creates the driver, viewer, view and context, and attaches @p window.
    ///
    /// Shared by both factories, because the only thing that differs between
    /// an offscreen view and a windowed one IS the window.
    [[nodiscard]] static Result<std::unique_ptr<Impl>> make(Handle(WNT_Window) window, int width,
                                                            int height);
};

Result<std::unique_ptr<Viewer::Impl>> Viewer::Impl::make(Handle(WNT_Window) window, int width,
                                                          int height) {
    return geometry::occt::guardKernelCall(
        "viewer", [&]() -> Result<std::unique_ptr<Impl>> {
            auto impl = std::make_unique<Impl>();
            impl->width = width;
            impl->height = height;
            impl->window = std::move(window);

            Handle(Aspect_DisplayConnection) display = new Aspect_DisplayConnection();
            impl->driver = new OpenGl_GraphicDriver(display, false);
            if (impl->driver.IsNull()) {
                return makeError(ErrorCode::FailedPrecondition,
                                 "viewer: no OpenGL graphic driver could be created");
            }
            // Nothing presents to a swap chain: an offscreen view has no
            // surface to swap, and a windowed one is redrawn by its widget.
            impl->driver->ChangeOptions().buffersNoSwap = true;

            impl->viewer = new V3d_Viewer(impl->driver);
            impl->viewer->SetDefaultLights();
            impl->viewer->SetLightOn();

            impl->view = impl->viewer->CreateView();
            if (impl->view.IsNull()) {
                return makeError(ErrorCode::FailedPrecondition,
                                 "viewer: no view could be created");
            }
            impl->view->SetWindow(impl->window);
            impl->view->SetBackgroundColor(colourOf(impl->background));
            impl->view->MustBeResized();
            impl->view->SetProj(orientationOf(StandardView::Isometric));

            impl->context = new AIS_InteractiveContext(impl->viewer);
            return impl;
        });
}

Viewer::Viewer(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Viewer::~Viewer() = default;
Viewer::Viewer(Viewer&&) noexcept = default;
Viewer& Viewer::operator=(Viewer&&) noexcept = default;

Result<Viewer> Viewer::createOffscreen(int width, int height) {
    // THE SIZE IS CHECKED BEFORE A WINDOW IS MADE OUT OF IT. Checking inside
    // Impl::make was too late: a 0-wide WNT_Window is created happily, so the
    // caller got an Internal error from somewhere downstream instead of the
    // InvalidArgument its argument deserved.
    if (const Result<void> checked = checkSize(width, height); !checked.has_value()) {
        return std::unexpected(checked.error());
    }
    // A REAL NATIVE WINDOW THAT IS NEVER MAPPED. An Aspect_NeutralWindow is
    // not enough: OCCT asks the window for a native handle and calls
    // SetPixelFormat on its device context, so a window-less view fails at
    // creation with "SetPixelFormat failed". Measured, not assumed.
    Result<std::unique_ptr<Impl>> impl = geometry::occt::guardKernelCall(
        "viewer window", [&]() -> Result<std::unique_ptr<Impl>> {
            Handle(WNT_Window) window = new WNT_Window(
                "BetterCADOffscreenView", sharedWindowClass(), WS_OVERLAPPEDWINDOW, 0, 0,
                width, height);
            // Virtual: never mapped, so nothing appears on screen.
            window->SetVirtual(true);
            return Impl::make(window, width, height);
        });
    if (!impl.has_value()) {
        return std::unexpected(impl.error());
    }
    return Viewer(std::move(*impl));
}

Result<Viewer> Viewer::createForWindow(std::uintptr_t nativeWindow, int width, int height) {
    if (nativeWindow == 0) {
        return makeError(ErrorCode::InvalidArgument, "viewer: the native window handle is null");
    }
    if (const Result<void> checked = checkSize(width, height); !checked.has_value()) {
        return std::unexpected(checked.error());
    }
    Result<std::unique_ptr<Impl>> impl = geometry::occt::guardKernelCall(
        "viewer window", [&]() -> Result<std::unique_ptr<Impl>> {
            Handle(WNT_Window) window =
                new WNT_Window(reinterpret_cast<Aspect_Drawable>(nativeWindow));
            return Impl::make(window, width, height);
        });
    if (!impl.has_value()) {
        return std::unexpected(impl.error());
    }
    return Viewer(std::move(*impl));
}

Result<PresentationId> Viewer::display(ObjectId object, const geometry::Body& body) {
    if (!object.isValid()) {
        return makeError(ErrorCode::InvalidArgument,
                         "viewer: the invalid handle cannot name a displayed object");
    }
    const TopoDS_Shape* shape = geometry::occt::BodyAccess::shape(body);
    if (shape == nullptr) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("viewer: {} has no body to display", object));
    }
    return geometry::occt::guardKernelCall("viewer display", [&]() -> Result<PresentationId> {
        Handle(AIS_Shape) presentation = new AIS_Shape(*shape);
        // THE MODE IS SET ON THE PRESENTATION, not just passed to Display, and
        // that distinction is a defect this milestone's tests caught: an
        // erased object re-displayed with `Display(object, update)` comes back
        // in its OWN display mode, which for an AIS_Shape defaults to
        // wireframe. So hiding and showing a shaded box returned it as edges
        // only -- 1240 covered pixels instead of 32674 -- and nothing in the
        // state said so.
        presentation->SetDisplayMode(AIS_Shaded);
        // Shaded, with the whole shape selectable. Finer selection modes --
        // faces, edges, vertices -- are activated by the caller that needs
        // them; P16-VIZ-001's CAD-to-mesh linkage is what will.
        impl_->context->Display(presentation, AIS_Shaded, 0, false);

        const PresentationId id = PresentationId::fromValue(++impl_->lastPresentation);
        impl_->displayed.push_back(
            DisplayedObject{id, object, PresentationKind::CadBody, true, false});
        impl_->presentations.push_back(presentation);
        return id;
    });
}

std::string_view toString(PresentationKind kind) noexcept {
    switch (kind) {
    case PresentationKind::CadBody:
        return "a CAD body";
    case PresentationKind::EngineeringMesh:
        return "an engineering mesh";
    }
    return "an unknown presentation";
}

std::string_view toString(MeshStyle style) noexcept {
    switch (style) {
    case MeshStyle::Shaded:
        return "shaded";
    case MeshStyle::ShadedWithEdges:
        return "shaded with edges";
    case MeshStyle::Wireframe:
        return "wireframe";
    }
    return "unknown";
}

namespace {

[[nodiscard]] occt::MeshDrawStyle drawStyleOf(MeshStyle style) noexcept {
    switch (style) {
    case MeshStyle::Shaded:
        return occt::MeshDrawStyle::Shaded;
    case MeshStyle::ShadedWithEdges:
        return occt::MeshDrawStyle::ShadedWithEdges;
    case MeshStyle::Wireframe:
        return occt::MeshDrawStyle::Wireframe;
    }
    return occt::MeshDrawStyle::ShadedWithEdges;
}

[[nodiscard]] MeshStyle styleOf(occt::MeshDrawStyle style) noexcept {
    switch (style) {
    case occt::MeshDrawStyle::Shaded:
        return MeshStyle::Shaded;
    case occt::MeshDrawStyle::ShadedWithEdges:
        return MeshStyle::ShadedWithEdges;
    case occt::MeshDrawStyle::Wireframe:
        return MeshStyle::Wireframe;
    }
    return MeshStyle::ShadedWithEdges;
}

/// The mesh presentation behind @p presentation, or a refusal naming what the
/// handle actually shows.
[[nodiscard]] Result<Handle(occt::MeshPresentation)> meshPresentationAt(
    const std::optional<std::size_t>& index, PresentationId presentation,
    const std::vector<DisplayedObject>& displayed,
    const std::vector<Handle(AIS_InteractiveObject)>& presentations) {
    if (!index.has_value()) {
        return makeError(ErrorCode::NotFound,
                         std::format("viewer: presentation {} is not displayed",
                                     presentation.value()));
    }
    Handle(occt::MeshPresentation) mesh =
        Handle(occt::MeshPresentation)::DownCast(presentations[*index]);
    if (mesh.IsNull()) {
        // Refused, and it says what the presentation IS rather than only what
        // it is not.
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("viewer: presentation {} shows {}, not an engineering mesh",
                                     presentation.value(), toString(displayed[*index].kind)));
    }
    return mesh;
}

} // namespace

Result<PresentationId> Viewer::displayMesh(ObjectId source, const MeshView& view,
                                           MeshStyle style) {
    if (!source.isValid()) {
        return makeError(ErrorCode::InvalidArgument,
                         "viewer: the invalid handle cannot name the source of a mesh");
    }
    if (view.triangleCount() == 0U) {
        return makeError(ErrorCode::FailedPrecondition,
                         "viewer: a mesh view with no triangles has nothing to display");
    }
    return geometry::occt::guardKernelCall(
        "viewer mesh display", [&]() -> Result<PresentationId> {
            Handle(occt::MeshPresentation) presentation =
                new occt::MeshPresentation(view, drawStyleOf(style));
            // The display mode is pinned rather than left to the context's
            // default. MeshPresentation::Compute returns early for any mode
            // but 0, so a context whose default mode was not 0 would
            // re-display an erased mesh as nothing at all.
            //
            // A GUARD, NOT A FIX: it was tested by removal, and the
            // [meshdisplay] suite passes without it, because
            // AIS_InteractiveContext's default display mode is already 0. It
            // is kept because INFRA-VIEWER-001's Finding 1 was exactly this
            // shape for AIS_Shape -- whose own default IS wireframe -- and
            // pinning the mode costs one line and removes the dependency.
            presentation->SetDisplayMode(0);
            impl_->context->Display(presentation, 0, 0, false);

            const PresentationId id = PresentationId::fromValue(++impl_->lastPresentation);
            impl_->displayed.push_back(
                DisplayedObject{id, source, PresentationKind::EngineeringMesh, true, false});
            impl_->presentations.push_back(presentation);
            return id;
        });
}

Result<meshing::MeshStamp> Viewer::meshStampOf(PresentationId presentation) const {
    Result<Handle(occt::MeshPresentation)> mesh = meshPresentationAt(
        impl_->indexOf(presentation), presentation, impl_->displayed, impl_->presentations);
    if (!mesh.has_value()) {
        return std::unexpected(mesh.error());
    }
    return (*mesh)->stamp();
}

Result<void> Viewer::setMeshStyle(PresentationId presentation, MeshStyle style) {
    Result<Handle(occt::MeshPresentation)> mesh = meshPresentationAt(
        impl_->indexOf(presentation), presentation, impl_->displayed, impl_->presentations);
    if (!mesh.has_value()) {
        return std::unexpected(mesh.error());
    }
    return geometry::occt::guardKernelCall("viewer mesh style", [&]() -> Result<void> {
        // A VISUAL OPERATION. Nothing here touches a mesh and nothing
        // regenerates: the presentation recomputes from buffers it already
        // holds.
        (*mesh)->setStyle(drawStyleOf(style));
        impl_->context->Redisplay(*mesh, false);
        return Result<void>{};
    });
}

Result<MeshStyle> Viewer::meshStyleOf(PresentationId presentation) const {
    Result<Handle(occt::MeshPresentation)> mesh = meshPresentationAt(
        impl_->indexOf(presentation), presentation, impl_->displayed, impl_->presentations);
    if (!mesh.has_value()) {
        return std::unexpected(mesh.error());
    }
    return styleOf((*mesh)->style());
}

Result<void> Viewer::setMeshStale(PresentationId presentation, bool stale) {
    Result<Handle(occt::MeshPresentation)> mesh = meshPresentationAt(
        impl_->indexOf(presentation), presentation, impl_->displayed, impl_->presentations);
    if (!mesh.has_value()) {
        return std::unexpected(mesh.error());
    }
    return geometry::occt::guardKernelCall("viewer mesh staleness", [&]() -> Result<void> {
        (*mesh)->setStale(stale);
        impl_->context->Redisplay(*mesh, false);
        return Result<void>{};
    });
}

Result<bool> Viewer::meshIsStale(PresentationId presentation) const {
    Result<Handle(occt::MeshPresentation)> mesh = meshPresentationAt(
        impl_->indexOf(presentation), presentation, impl_->displayed, impl_->presentations);
    if (!mesh.has_value()) {
        return std::unexpected(mesh.error());
    }
    return (*mesh)->isStale();
}

Result<void> Viewer::setMeshHighlight(PresentationId presentation,
                                      std::span<const std::size_t> triangles) {
    Result<Handle(occt::MeshPresentation)> mesh = meshPresentationAt(
        impl_->indexOf(presentation), presentation, impl_->displayed, impl_->presentations);
    if (!mesh.has_value()) {
        return std::unexpected(mesh.error());
    }
    for (const std::size_t triangle : triangles) {
        if (triangle >= (*mesh)->triangleCount()) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("viewer: render triangle {} of {} in presentation {}",
                                         triangle, (*mesh)->triangleCount(),
                                         presentation.value()));
        }
    }
    return geometry::occt::guardKernelCall("viewer mesh highlight", [&]() -> Result<void> {
        (*mesh)->setHighlighted(triangles);
        impl_->context->Redisplay(*mesh, false);
        return Result<void>{};
    });
}

Result<std::vector<std::size_t>> Viewer::meshHighlightOf(PresentationId presentation) const {
    Result<Handle(occt::MeshPresentation)> mesh = meshPresentationAt(
        impl_->indexOf(presentation), presentation, impl_->displayed, impl_->presentations);
    if (!mesh.has_value()) {
        return std::unexpected(mesh.error());
    }
    return (*mesh)->highlighted();
}

Result<std::optional<MeshPick>> Viewer::pickMeshAt(int x, int y) {
    return geometry::occt::guardKernelCall(
        "viewer mesh pick", [&]() -> Result<std::optional<MeshPick>> {
            impl_->context->MoveTo(x, y, impl_->view, false);
            if (!impl_->context->HasDetected()) {
                return std::optional<MeshPick>{};
            }
            const Handle(SelectMgr_EntityOwner) owner = impl_->context->DetectedOwner();
            if (owner.IsNull()) {
                return std::optional<MeshPick>{};
            }
            Handle(occt::MeshPresentation) detected =
                Handle(occt::MeshPresentation)::DownCast(owner->Selectable());
            if (detected.IsNull()) {
                // A CAD body was under the cursor, not a mesh. Nothing, rather
                // than the nearest mesh.
                return std::optional<MeshPick>{};
            }
            const int triangle = detected->lastDetectedTriangle();
            if (triangle < 0) {
                return std::optional<MeshPick>{};
            }
            for (std::size_t i = 0; i < impl_->presentations.size(); ++i) {
                if (impl_->presentations[i] == detected) {
                    return std::optional<MeshPick>{MeshPick{
                        impl_->displayed[i].presentation, static_cast<std::size_t>(triangle)}};
                }
            }
            return std::optional<MeshPick>{};
        });
}

Result<void> Viewer::remove(PresentationId presentation) {
    const std::optional<std::size_t> index = impl_->indexOf(presentation);
    if (!index.has_value()) {
        return makeError(ErrorCode::NotFound,
                         std::format("viewer: presentation {} is not displayed",
                                     presentation.value()));
    }
    return geometry::occt::guardKernelCall("viewer remove", [&]() -> Result<void> {
        impl_->context->Remove(impl_->presentations[*index], false);
        impl_->displayed.erase(impl_->displayed.begin() + static_cast<std::ptrdiff_t>(*index));
        impl_->presentations.erase(impl_->presentations.begin() +
                                   static_cast<std::ptrdiff_t>(*index));
        return Result<void>{};
    });
}

std::span<const DisplayedObject> Viewer::displayed() const noexcept {
    return impl_->displayed;
}

Result<void> Viewer::setVisible(PresentationId presentation, bool visible) {
    const std::optional<std::size_t> index = impl_->indexOf(presentation);
    if (!index.has_value()) {
        return makeError(ErrorCode::NotFound,
                         std::format("viewer: presentation {} is not displayed",
                                     presentation.value()));
    }
    return geometry::occt::guardKernelCall("viewer visibility", [&]() -> Result<void> {
        // PRESENTATION STATE ONLY. Hiding erases nothing: the presentation
        // stays in `displayed`, the body stays in the document, and making it
        // visible again shows the same thing.
        if (visible) {
            impl_->context->Display(impl_->presentations[*index], false);
        } else {
            impl_->context->Erase(impl_->presentations[*index], false);
        }
        impl_->displayed[*index].visible = visible;
        return Result<void>{};
    });
}

Result<void> Viewer::setSelected(PresentationId presentation, bool selected) {
    const std::optional<std::size_t> index = impl_->indexOf(presentation);
    if (!index.has_value()) {
        return makeError(ErrorCode::NotFound,
                         std::format("viewer: presentation {} is not displayed",
                                     presentation.value()));
    }
    return geometry::occt::guardKernelCall("viewer selection", [&]() -> Result<void> {
        impl_->context->AddOrRemoveSelected(impl_->presentations[*index], false);
        impl_->displayed[*index].selected = selected;
        // AddOrRemoveSelected toggles, so ask the context what actually
        // happened rather than trusting the request.
        impl_->displayed[*index].selected =
            impl_->context->IsSelected(impl_->presentations[*index]);
        return Result<void>{};
    });
}

void Viewer::clearSelection() {
    impl_->context->ClearSelected(false);
    for (DisplayedObject& entry : impl_->displayed) {
        entry.selected = false;
    }
}

std::vector<ObjectId> Viewer::selectedObjects() const {
    std::vector<ObjectId> objects;
    for (const DisplayedObject& entry : impl_->displayed) {
        if (entry.selected) {
            objects.push_back(entry.object);
        }
    }
    std::ranges::sort(objects);
    objects.erase(std::ranges::unique(objects).begin(), objects.end());
    return objects;
}

Result<std::optional<ObjectId>> Viewer::pickAt(int x, int y) {
    return geometry::occt::guardKernelCall(
        "viewer pick", [&]() -> Result<std::optional<ObjectId>> {
            impl_->context->MoveTo(x, y, impl_->view, false);
            if (!impl_->context->HasDetected()) {
                return std::optional<ObjectId>{};
            }
            const Handle(SelectMgr_EntityOwner) owner = impl_->context->DetectedOwner();
            if (owner.IsNull()) {
                return std::optional<ObjectId>{};
            }
            // THE TRANSLATION THAT MATTERS. A selection owner is a graphics
            // concept and never leaves this file; what the caller gets is the
            // CAD identity that was displayed.
            const Handle(AIS_InteractiveObject) detected =
                Handle(AIS_InteractiveObject)::DownCast(owner->Selectable());
            if (detected.IsNull()) {
                return std::optional<ObjectId>{};
            }
            for (std::size_t i = 0; i < impl_->presentations.size(); ++i) {
                if (impl_->presentations[i] == detected) {
                    return std::optional<ObjectId>{impl_->displayed[i].object};
                }
            }
            return std::optional<ObjectId>{};
        });
}

void Viewer::setStandardView(StandardView view) {
    impl_->view->SetProj(orientationOf(view));
    impl_->view->FitAll();
}

void Viewer::fitAll() {
    impl_->view->FitAll();
}

void Viewer::orbit(Angle azimuth, Angle elevation) {
    // SI radians in, as every angle in BetterCAD is.
    impl_->view->Rotate(azimuth.si(), elevation.si(), 0.0);
}

void Viewer::pan(double dxPixels, double dyPixels) {
    impl_->view->Pan(static_cast<int>(dxPixels), static_cast<int>(dyPixels));
}

void Viewer::zoom(double factor) {
    if (!(factor > 0.0) || !std::isfinite(factor)) {
        return;
    }
    impl_->view->SetZoom(factor);
}

Result<void> Viewer::redraw() {
    return geometry::occt::guardKernelCall("viewer redraw", [&]() -> Result<void> {
        impl_->view->Redraw();
        return Result<void>{};
    });
}

void Viewer::setBackground(Rgb colour) {
    impl_->background = colour;
    impl_->view->SetBackgroundColor(colourOf(colour));
}

Rgb Viewer::background() const noexcept {
    return impl_->background;
}

Result<RenderedImage> Viewer::render() {
    return geometry::occt::guardKernelCall("viewer render", [&]() -> Result<RenderedImage> {
        impl_->view->Redraw();

        Image_AlienPixMap pixels;
        if (!impl_->view->ToPixMap(pixels, impl_->width, impl_->height)) {
            return makeError(ErrorCode::Internal,
                             "viewer: the view could not be read back into an image");
        }

        RenderedImage image;
        image.width = static_cast<int>(pixels.SizeX());
        image.height = static_cast<int>(pixels.SizeY());
        image.rgb.resize(static_cast<std::size_t>(image.width) *
                         static_cast<std::size_t>(image.height) * 3U);
        for (int y = 0; y < image.height; ++y) {
            for (int x = 0; x < image.width; ++x) {
                const Quantity_Color colour = pixels.PixelColor(x, y).GetRGB();
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                     static_cast<std::size_t>(x)) *
                    3U;
                image.rgb[offset] = static_cast<std::uint8_t>(
                    std::lround(std::clamp(colour.Red(), 0.0, 1.0) * 255.0));
                image.rgb[offset + 1U] = static_cast<std::uint8_t>(
                    std::lround(std::clamp(colour.Green(), 0.0, 1.0) * 255.0));
                image.rgb[offset + 2U] = static_cast<std::uint8_t>(
                    std::lround(std::clamp(colour.Blue(), 0.0, 1.0) * 255.0));
            }
        }
        return image;
    });
}

int Viewer::width() const noexcept {
    return impl_->width;
}

int Viewer::height() const noexcept {
    return impl_->height;
}

Result<void> Viewer::resize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("viewer: a {}x{} view has no area", width, height));
    }
    return geometry::occt::guardKernelCall("viewer resize", [&]() -> Result<void> {
        // A PICK IS NORMALISED AGAINST THE WINDOW, NOT AGAINST THE LAST IMAGE.
        // render() asks ToPixMap for the recorded size and ToPixMap adjusts the
        // camera aspect for the dump, so recording a new size was enough to
        // make the IMAGE come out at the new size -- and every test that
        // compared image dimensions was satisfied. The WINDOW was still the
        // size it was created at, so picks were being taken in a different
        // space from the one the image showed: measured on a 400x200 image of
        // a 256x256 window, the drawn content sat in rows 80-130 while picks
        // landed in rows 120-180. (Finding 6.)
        //
        // The window we created ourselves is ours to resize. A window-backed
        // view's is not: whoever owns it has already resized it -- Qt resizes
        // the widget before delivering resizeEvent -- so there the view only
        // has to re-read it, and calling SetPos would be fighting the owner.
        if (impl_->window->IsVirtual()) {
            impl_->window->SetPos(0, 0, width, height);
            impl_->window->DoResize();
        }
        impl_->view->MustBeResized();
        impl_->width = width;
        impl_->height = height;
        return Result<void>{};
    });
}

} // namespace bettercad::renderer
