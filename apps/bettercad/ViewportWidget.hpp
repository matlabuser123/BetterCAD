#pragma once

#include <bettercad/core/Id.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/renderer/Viewer.hpp>

#include <QString>
#include <QWidget>

#include <memory>
#include <optional>

namespace bettercad::app {

/// The 3D viewport: a Qt widget that hosts a BetterCAD renderer view.
///
/// THE WIDGET OWNS NO ENGINEERING STATE AND NO RENDERING LOGIC. It owns a
/// native window, forwards mouse and resize events to a `renderer::Viewer`,
/// and reports what the viewer says. Every OCCT call lives in the renderer's
/// adapter; this file sees Qt and `renderer::Viewer`, and nothing else.
///
/// IT CONVERTS QT'S LOGICAL PIXELS INTO THE VIEW'S DEVICE PIXELS, and that is
/// the one piece of arithmetic it is allowed to do. Qt 6 reports widget sizes
/// and mouse positions in logical (device-independent) pixels; the native
/// window OCCT draws into is measured in device pixels. This boundary is the
/// only place that knows the ratio, so it is the only place it may be applied.
///
/// IT MAY HAVE NO VIEW, AND THAT IS A SUPPORTED STATE RATHER THAN A FAILURE.
/// A view needs a native window: under Qt's `offscreen` platform plugin --
/// which is how the automated GUI smoke test runs, and how a machine with no
/// display runs -- there is no native window to give it. The widget then says
/// so through `unavailableReason()` and paints that message instead of
/// pretending to be a viewport. The renderer's own offscreen tests are what
/// prove rendering works; this widget's job is hosting.
class ViewportWidget final : public QWidget {
    Q_OBJECT

public:
    explicit ViewportWidget(QWidget* parent = nullptr);
    ~ViewportWidget() override;

    /// Whether a 3D view exists. False under a platform with no native window.
    [[nodiscard]] bool hasView() const noexcept;
    /// Why there is no view, empty when there is one. Shown in the widget and
    /// available to tests, so "no viewport" is never silent.
    [[nodiscard]] QString unavailableReason() const;

    /// Displays @p body for @p object. Fails, rather than silently doing
    /// nothing, when there is no view.
    [[nodiscard]] Result<renderer::PresentationId> display(ObjectId object,
                                                           const geometry::Body& body);

    /// The object last picked, for the status bar and for tests.
    [[nodiscard]] std::optional<ObjectId> lastPicked() const noexcept;

    /// What a click resolves to.
    ///
    /// GUI STATE. A CAD body and its mesh occupy the same space, so without a
    /// mode the answer would depend on drawing order -- the one thing a
    /// selection must never depend on. Switching it changes what the NEXT
    /// click means and nothing about the model.
    void setPickMesh(bool pickMesh) noexcept { pickMesh_ = pickMesh; }
    [[nodiscard]] bool picksMesh() const noexcept { return pickMesh_; }

    [[nodiscard]] renderer::Viewer* viewer() noexcept;

    /// The widget's size in the DEVICE pixels the view works in, and the ratio
    /// used to get there. Exposed so the conversion can be observed rather
    /// than assumed: the GUI smoke test prints both.
    [[nodiscard]] QSize deviceSize() const;
    [[nodiscard]] double deviceRatio() const;

    [[nodiscard]] QSize sizeHint() const override;

Q_SIGNALS:
    /// A pick resolved to a CAD identity, or to nothing.
    void picked(std::optional<ObjectId> object);
    /// A pick resolved to a RENDER TRIANGLE of a mesh presentation, or to
    /// nothing. A render index, which the window translates through the
    /// MeshView that built the presentation -- the widget has neither.
    void meshPicked(std::optional<std::size_t> triangle);

protected:
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    /// Creates the view once a native window exists. Called from showEvent,
    /// because `winId()` is only meaningful by then.
    void ensureView();

    /// Logical pixels to device pixels. Qt's units in, the view's units out.
    [[nodiscard]] int toDevice(int logical) const;

    std::unique_ptr<renderer::Viewer> viewer_;
    QString unavailable_;
    QPoint lastMouse_{};
    std::optional<ObjectId> lastPicked_{};
    bool pickMesh_ = true;
};

} // namespace bettercad::app
