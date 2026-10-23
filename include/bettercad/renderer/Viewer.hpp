#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/renderer/MeshView.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/renderer/Export.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// The 3D viewer (INFRA-VIEWER-001).
//
// WHAT THIS IS FOR. BetterCAD had no viewport at all, which is what blocked
// P16-VIZ-001: there was nothing to display a mesh into and no CAD display to
// select alongside it. This is the viewport, and nothing more -- it shows CAD
// bodies, moves a camera, and resolves a pick to a CAD identity.
//
// THE VIEWER OWNS NO ENGINEERING STATE, and that is the invariant everything
// else here serves. It holds presentations, visibility, selection and a
// camera. It does not hold a Document, it cannot regenerate a feature, and it
// never becomes the authority for anything:
//
//     canonical            the Document, its features, their Bodies
//     viewer-derived       presentations, visibility, selection, camera
//
// A `Body` handed to `display` is COPIED BY VALUE, and a Body copy shares the
// underlying kernel shape rather than duplicating it -- so the viewer holds a
// handle to geometry it does not own and cannot change.
//
// NO QT AND NO OCCT APPEAR IN THIS HEADER, which is not politeness: the
// architecture check (tests/architecture/CheckLayering.cmake) confines Qt to
// `apps/bettercad/` and `src/renderer/`, and OCCT headers to an `occt/`
// adapter directory. Everything OCCT lives in `src/renderer/occt/`, so a
// consumer of this header -- including a test -- needs neither.
//
// OFFSCREEN ON WINDOWS MEANS HIDDEN, NOT WINDOW-LESS. An
// `Aspect_NeutralWindow` is not enough: OCCT asks the window for a native
// handle and calls `SetPixelFormat` on its device context, so a window-less
// view fails at creation with "SetPixelFormat failed". `createOffscreen`
// therefore makes a real native window and never maps it. Measured, not
// assumed -- see docs/verification/INFRA-VIEWER-001/.
namespace bettercad::renderer {

/// Forward-declared rather than included: `StructuralOverlay.hpp` reaches the
/// structural module, and `displayOverlay` only takes a reference to one.
struct StructuralOverlay;
struct ThermalOverlay;

/// Where a standard view looks from.
enum class StandardView : std::uint8_t {
    Front,
    Back,
    Left,
    Right,
    Top,
    Bottom,
    Isometric,
};

[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::string_view toString(StandardView view) noexcept;

/// One presentation in one viewer.
///
/// VIEWER-LOCAL AND NOT AN IDENTITY, for the same reasons `NodeId` is not a
/// CAD identity (ADR-031): it is handed out by one viewer, it is invalidated
/// when the presentation is removed, it is never persisted, and it says
/// nothing about the model. The CAD identity of what is displayed is the
/// `ObjectId` the caller supplied, and that is what a pick resolves to.
class PresentationId {
public:
    using ValueType = std::uint32_t;

    constexpr PresentationId() noexcept = default;

    [[nodiscard]] static constexpr PresentationId fromValue(ValueType value) noexcept {
        PresentationId id;
        id.value_ = value;
        return id;
    }

    [[nodiscard]] constexpr ValueType value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool isValid() const noexcept { return value_ != 0; }
    constexpr explicit operator bool() const noexcept { return isValid(); }

    friend constexpr bool operator==(const PresentationId&, const PresentationId&) = default;
    friend constexpr auto operator<=>(const PresentationId&, const PresentationId&) = default;

private:
    ValueType value_{};
};

/// What the viewer is showing for one document object.
/// What kind of thing a presentation shows.
///
/// ADDED BY P16-VIZ-001, because "hide the CAD" and "hide the mesh" have to be
/// separable: a mesh presentation is not a CAD object, and `object` on it names
/// the feature the mesh was built FROM rather than something being drawn.
enum class PresentationKind : std::uint8_t {
    /// A `geometry::Body`, drawn through AIS_Shape.
    CadBody,
    /// An engineering mesh, drawn through batched primitive arrays.
    EngineeringMesh,
    /// A structural overlay: load arrows, reaction arrows or restraint
    /// symbols, drawn as line segments over whatever else is displayed.
    ///
    /// Named `Overlay` rather than `StructuralOverlay` so the KIND and the
    /// struct it depicts do not share a name. The enumerator is scoped, so
    /// both spellings would compile; one of them would just read badly.
    Overlay,
    /// A thermal overlay (P18-VIZ-001): recovered heat-flux vectors, applied heat-flux arrows, prescribed-temperature
    /// symbols or convection symbols, drawn as line segments over whatever else is displayed.
    ThermalOverlay,
};

[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::string_view toString(PresentationKind kind) noexcept;

struct DisplayedObject {
    PresentationId presentation{};
    /// The CAD identity. The viewer's only link back to the model, and it is a
    /// reference rather than ownership.
    ObjectId object{};
    /// What this presentation shows. For an engineering mesh, `object` names
    /// the feature the mesh was built FROM, which is why the kind has to be
    /// carried: hiding the CAD and hiding its mesh are different acts.
    PresentationKind kind = PresentationKind::CadBody;
    bool visible = true;
    bool selected = false;

    friend bool operator==(const DisplayedObject&, const DisplayedObject&) = default;
};

/// An 8-bit colour, for backgrounds and for comparing rendered images.
struct Rgb {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;

    friend bool operator==(const Rgb&, const Rgb&) = default;
};

/// A rendered image, read back from the view.
///
/// This is how the viewer is TESTED. Pixel-exact comparison between machines
/// is not a goal and is not asserted anywhere: drivers differ. What an image
/// supports is the question that actually matters -- did anything get drawn,
/// and did it change when it should have -- which `coverage` answers.
struct RenderedImage {
    int width = 0;
    int height = 0;
    /// Three bytes per pixel, row-major from the top left.
    std::vector<std::uint8_t> rgb{};

    [[nodiscard]] BETTERCAD_RENDERER_EXPORT Rgb at(int x, int y) const;

    /// How many pixels differ from @p background by more than @p tolerance in
    /// any channel.
    ///
    /// The viewer's primary assertion. An empty scene must cover 0 pixels and
    /// a displayed body must cover more than 0 -- a render that produces the
    /// same image either way has drawn nothing, however successfully it
    /// returned.
    [[nodiscard]] BETTERCAD_RENDERER_EXPORT std::size_t coverage(Rgb background,
                                                                 int tolerance = 6) const;

    friend bool operator==(const RenderedImage&, const RenderedImage&) = default;
};

/// A 3D view of CAD bodies.
///
/// Move-only: it owns a graphic driver, a window and a view, which are not
/// copyable in any meaningful sense.
/// How a displayed engineering mesh is drawn.
enum class MeshStyle : std::uint8_t {
    /// Flat-shaded facets only.
    Shaded,
    /// Flat-shaded facets with their edges over them. The default, because a
    /// shaded mesh with no edges is indistinguishable from a solid.
    ShadedWithEdges,
    /// Edges only.
    Wireframe,
};

[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::string_view toString(MeshStyle style) noexcept;

/// What a pick on a mesh presentation hit.
struct MeshPick {
    PresentationId presentation{};
    /// THE RENDER TRIANGLE, not an element. Translate it through the MeshView
    /// that built the presentation; a position in a GPU buffer is not identity.
    std::size_t triangle = 0;

    friend bool operator==(const MeshPick&, const MeshPick&) = default;
};

class BETTERCAD_RENDERER_EXPORT Viewer {
public:
    /// A view that renders offscreen, into a native window that is never
    /// mapped, so nothing appears on screen.
    ///
    /// Fails with FailedPrecondition when no graphic driver can be created or
    /// no view can be made -- a machine or session with no usable OpenGL
    /// implementation, for instance. The diagnostic names the call that
    /// failed; it is never reported as an empty scene.
    [[nodiscard]] static Result<Viewer> createOffscreen(int width, int height);

    /// A view that draws into an existing native window, for the Qt widget
    /// that hosts it.
    ///
    /// @p nativeWindow is a platform window handle (an `HWND` on Windows).
    /// Deliberately an integer rather than a typed handle: a typed one would
    /// put a platform header in this file, and the viewer has no use for the
    /// window beyond handing it to the driver.
    [[nodiscard]] static Result<Viewer> createForWindow(std::uintptr_t nativeWindow, int width,
                                                        int height);

    ~Viewer();
    Viewer(Viewer&&) noexcept;
    Viewer& operator=(Viewer&&) noexcept;
    Viewer(const Viewer&) = delete;
    Viewer& operator=(const Viewer&) = delete;

    // --- display ------------------------------------------------------------

    /// Displays @p body as @p object's presentation.
    ///
    /// The body is a handle to geometry the viewer does not own. Displaying
    /// the same object twice gives two presentations: the viewer does not
    /// decide that an object may appear once, because a caller showing the
    /// same body in two states has a reason to.
    [[nodiscard]] Result<PresentationId> display(ObjectId object, const geometry::Body& body);

    /// Removes a presentation. The object stays in the document; only the
    /// presentation goes.
    /// Displays @p view as the engineering mesh of @p source.
    ///
    /// ONE BATCHED PRESENTATION FOR THE WHOLE MESH, never one per element.
    /// @p source names the feature the mesh was built from, so the viewer can
    /// tell a mesh presentation from the CAD body beside it; the viewer holds
    /// no mesh and no mesh identity.
    ///
    /// The view's buffers are copied, and its `MeshStamp` with them: a caller
    /// that remeshes displays a NEW view, and `meshStampOf` is what says which
    /// generation is on screen.
    [[nodiscard]] Result<PresentationId> displayMesh(ObjectId source, const MeshView& view,
                                                     MeshStyle style = MeshStyle::ShadedWithEdges);

    /// Displays @p view with its vertices moved to @p positions.
    ///
    /// A VISUAL TRANSFORM OF A DERIVED RESULT, and nothing else. @p positions
    /// is what `StructuralResultView::deformedPositions` produced -- `x + s*u`
    /// in **SI metres**, in exactly the layout of `MeshView::positions()`. No
    /// mesh, no CAD geometry, no document parameter and no result is touched
    /// by displaying it.
    ///
    /// A NAMED SIBLING RATHER THAN A DEFAULTED ARGUMENT on `displayMesh`
    /// (ADR-043): a span that silently meant "undeformed" would hide which
    /// path a caller took, and this layer's rule is the opposite one.
    ///
    /// REFUSES A LENGTH MISMATCH. @p positions must be exactly as long as
    /// `view.positions()`: a short buffer would be read past its end, and a
    /// longer one means the caller is holding a different mesh.
    ///
    /// A NEW SCALE MEANS A NEW PRESENTATION. There is no API to re-scale one
    /// in place, for the reason ADR-031 gives for `MeshView` itself -- a
    /// presentation whose buffers were swapped under it would pick against one
    /// scale while drawing another.
    [[nodiscard]] Result<PresentationId> displayDeformedMesh(
        ObjectId source, const MeshView& view, std::span<const double> positions,
        MeshStyle style = MeshStyle::ShadedWithEdges);

    /// Displays  overlay: load arrows, reaction arrows or restraint symbols.
    ///
    /// AN ANNOTATION, NOT A RESULT. The overlay is a list of line segments in
    /// SI metres that `renderer::loadArrowOverlay`, `reactionArrowOverlay` or
    /// `restraintSymbolOverlay` resolved from the glyph data and the canonical
    /// mesh. Displaying it touches no mesh, no result and no document.
    ///
    /// NOT SELECTABLE, deliberately: an arrow drawn over a mesh must not come
    /// between the user and the element they were trying to pick.
    ///
    /// Refuses an overlay with no segments -- nothing to draw is a caller
    /// error rather than an empty presentation to manage, and the overlay
    /// functions already report `skipped` for the glyphs they left out.
    [[nodiscard]] Result<PresentationId> displayOverlay(ObjectId source,
                                                        const StructuralOverlay& overlay);

    /// Displays a thermal overlay (P18-VIZ-001).
    ///
    /// AN ANNOTATION, NOT A RESULT, exactly as the structural one: a list of line segments in SI metres that
    /// `renderer::heatFluxVectorOverlay`, `appliedHeatFluxOverlay`, `prescribedTemperatureOverlay` or
    /// `convectionOverlay` resolved from canonical intent and the qualified result. They pass through the SAME
    /// single metres-to-millimetres conversion the structural overlay and the mesh use. NOT SELECTABLE. Refuses an
    /// overlay with no segments.
    [[nodiscard]] Result<PresentationId> displayOverlay(ObjectId source, const ThermalOverlay& overlay);

    /// The mesh generation a mesh presentation is showing.
    [[nodiscard]] Result<meshing::MeshStamp> meshStampOf(PresentationId presentation) const;

    /// Changes how a mesh presentation is drawn. A VISUAL OPERATION: it
    /// touches no mesh and triggers no regeneration.
    [[nodiscard]] Result<void> setMeshStyle(PresentationId presentation, MeshStyle style);
    [[nodiscard]] Result<MeshStyle> meshStyleOf(PresentationId presentation) const;

    /// Draws @p triangles as highlighted. Render indices, from the MeshView
    /// that built this presentation; highlight state lives here and nowhere
    /// near the mesh.
    /// Draws a mesh presentation as stale, or as current.
    ///
    /// The viewer does not DECIDE staleness -- `renderer::statusOf` does, from
    /// the document's own geometry revision. This only renders the decision,
    /// which is why it is a setter and not a query of the document.
    /// Colours @p presentation's facet corners from @p rgb: nine doubles per
    /// render triangle, three sRGB components for each of three corners, each
    /// in `[0, 1]`.
    ///
    /// WHAT `renderer::contourCornerColours` RETURNS, and the intended way to
    /// obtain it — that function bands a `ScalarField` through a
    /// `ContourLegend` and decides, from the channel's own domain, whether a
    /// facet is flat or interpolated. Assembling this buffer by hand is
    /// possible and is checked rather than trusted.
    ///
    /// A VISUAL OPERATION. It touches no mesh, no result and no document, and
    /// it triggers no regeneration. The CONTOUR IS NOT THE RESULT: it is a
    /// colour per corner, and a caller that wants the number asks the field.
    ///
    /// Refuses a buffer of the wrong length, and refuses a component that is
    /// not a finite value within `[0, 1]` rather than clamping it.
    ///
    /// A SELECTED FACET KEEPS THE HIGHLIGHT COLOUR, not its band, so
    /// selecting something while a contour is displayed still shows.
    [[nodiscard]] Result<void> setMeshContour(PresentationId presentation,
                                              std::span<const double> rgb);

    /// Returns @p presentation to its uniform facet colour.
    [[nodiscard]] Result<void> clearMeshContour(PresentationId presentation);

    /// Whether @p presentation is currently drawing a contour.
    [[nodiscard]] Result<bool> meshHasContour(PresentationId presentation) const;

    [[nodiscard]] Result<void> setMeshStale(PresentationId presentation, bool stale);
    [[nodiscard]] Result<bool> meshIsStale(PresentationId presentation) const;

    [[nodiscard]] Result<void> setMeshHighlight(PresentationId presentation,
                                                std::span<const std::size_t> triangles);
    [[nodiscard]] Result<std::vector<std::size_t>> meshHighlightOf(
        PresentationId presentation) const;

    /// Picks a mesh at a pixel.
    ///
    /// Answers a RENDER TRIANGLE INDEX, which the caller translates through the
    /// MeshView that built the presentation. The viewer deliberately does not
    /// do that translation: it has no MeshView and no mesh, and an AIS object
    /// has no business producing engineering identity.
    [[nodiscard]] Result<std::optional<MeshPick>> pickMeshAt(int x, int y);

    [[nodiscard]] Result<void> remove(PresentationId presentation);

    [[nodiscard]] std::span<const DisplayedObject> displayed() const noexcept;

    /// Presentation state, not model state. Hiding is not deleting.
    [[nodiscard]] Result<void> setVisible(PresentationId presentation, bool visible);

    // --- selection ----------------------------------------------------------

    [[nodiscard]] Result<void> setSelected(PresentationId presentation, bool selected);
    void clearSelection();

    /// The CAD identities currently selected, ascending and without repeats.
    ///
    /// ObjectIds, never presentation handles and never graphics indices: what
    /// a caller does with a selection is a question about the MODEL.
    [[nodiscard]] std::vector<ObjectId> selectedObjects() const;

    /// What is under the pixel at (@p x, @p y), or nullopt for nothing.
    ///
    /// Resolved through the viewer's own selection, then translated to the CAD
    /// identity that was displayed. A graphics primitive index never leaves
    /// this class.
    [[nodiscard]] Result<std::optional<ObjectId>> pickAt(int x, int y);

    // --- camera -------------------------------------------------------------

    void setStandardView(StandardView view);
    void fitAll();
    /// Turns the camera about the scene. Angles, because they are angles.
    void orbit(Angle azimuth, Angle elevation);
    void pan(double dxPixels, double dyPixels);
    /// Multiplies the view scale. A factor of 1 changes nothing.
    void zoom(double factor);

    // --- rendering ----------------------------------------------------------

    /// Redraws the view.
    [[nodiscard]] Result<void> redraw();

    void setBackground(Rgb colour);
    [[nodiscard]] Rgb background() const noexcept;

    /// Renders and reads the image back.
    ///
    /// Works for an offscreen view and for a windowed one; OCCT renders
    /// through a framebuffer object either way.
    [[nodiscard]] Result<RenderedImage> render();

    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;
    /// Tells the view its window changed size.
    [[nodiscard]] Result<void> resize(int width, int height);

private:
    class Impl;
    explicit Viewer(std::unique_ptr<Impl> impl) noexcept;

    /// The one body behind `displayMesh` and `displayDeformedMesh`, so the two
    /// cannot drift apart. @p deformed empty means the view's own positions.
    ///
    /// The PRECONDITIONS of a deformed display are checked by its public entry
    /// point and not here, deliberately: this function cannot tell an empty
    /// span that means "undeformed" from one that means "the caller had
    /// nothing", and only the caller's chosen entry point carries that
    /// distinction.
    [[nodiscard]] Result<PresentationId> displayMeshInternal(ObjectId source,
                                                             const MeshView& view,
                                                             std::span<const double> deformed,
                                                             MeshStyle style);

    std::unique_ptr<Impl> impl_;
};

} // namespace bettercad::renderer
