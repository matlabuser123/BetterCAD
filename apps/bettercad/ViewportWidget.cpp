#include "ViewportWidget.hpp"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>

#include <cmath>
#include <numbers>
#include <utility>

namespace bettercad::app {
namespace {

/// Radians per LOGICAL pixel of drag. A quarter turn across a 400-pixel
/// widget, which is the feel every CAD viewport has; it is interaction tuning
/// and belongs in the GUI, not in the renderer.
///
/// Logical and not device, deliberately: a logical pixel is the same physical
/// size at every scale factor, so the drag that gives a quarter turn is the
/// same movement of the hand on a 100% and a 150% display. Converting this one
/// to device pixels would make the viewport spin half again as fast here.
constexpr double kOrbitRadiansPerPixel = std::numbers::pi / 800.0;
/// One wheel notch is 120 eighths of a degree in Qt.
constexpr double kZoomPerNotch = 1.15;

} // namespace

ViewportWidget::ViewportWidget(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("Viewport"));
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
    // OCCT draws into the window itself, so Qt must neither paint the
    // background nor double-buffer it away.
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

ViewportWidget::~ViewportWidget() = default;

// THE LOGICAL-TO-DEVICE CONVERSION, AND WHAT IS AND IS NOT CLAIMED FOR IT.
//
// Qt 6 reports a widget's width(), a mouse event's pos() and a resize event's
// size() in LOGICAL pixels. The window OCCT renders into is a Win32 HWND, and
// WNT_Window takes its extent from GetClientRect in DEVICE pixels;
// AIS_InteractiveContext::MoveTo normalises a pick against that extent. So
// whenever the ratio is not 1, a logical coordinate handed straight to the
// viewer is read in the wrong space, and a pick is displaced by more the
// further it is from the origin.
//
// MEASURED, through --smoke-test, which prints both extents and the ratio:
//
//     ratio 1.000   logical 1280x746   device 1280x746
//     ratio 1.500   logical 1280x746   device 1920x1119
//     ratio 3.000   logical  853x469   device 2559x1407
//
// NOT CLAIMED: that this was observed going wrong. On this machine, in its
// default configuration, the ratio is 1.000 and the conversion is a no-op --
// the two rows above it came from QT_SCALE_FACTOR, which is what made the
// divergence observable at all. The defect this guards against is therefore
// LATENT here and real elsewhere: another display, another machine, or a user
// whose scaling Qt honours. It is fixed rather than carried because the
// correct conversion costs one multiplication and the failure it prevents --
// picks that land near the cursor but not on it -- is one of the hardest
// things to attribute from a bug report. (Finding 7.)
int ViewportWidget::toDevice(int logical) const {
    return static_cast<int>(std::lround(static_cast<double>(logical) * devicePixelRatio()));
}

QSize ViewportWidget::deviceSize() const {
    return {toDevice(width()), toDevice(height())};
}

double ViewportWidget::deviceRatio() const {
    return devicePixelRatio();
}

void ViewportWidget::ensureView() {
    if (viewer_ || !unavailable_.isEmpty()) {
        return;
    }

    // A VIEW NEEDS A NATIVE WINDOW. Qt's offscreen platform plugin provides
    // none, which is how the GUI smoke test and a display-less machine run, so
    // this is a reported state rather than an error to crash on.
    const auto handle = static_cast<std::uintptr_t>(winId());
    if (handle == 0) {
        unavailable_ = tr("No native window is available on the %1 platform, so no 3D view "
                          "was created.")
                           .arg(QGuiApplication::platformName());
        return;
    }

    // Device pixels: the view measures itself against the native window.
    const QSize device = deviceSize();
    Result<renderer::Viewer> created =
        renderer::Viewer::createForWindow(handle, device.width(), device.height());
    if (!created.has_value()) {
        unavailable_ = tr("No 3D view: %1")
                           .arg(QString::fromUtf8(created.error().message.c_str()));
        return;
    }
    viewer_ = std::make_unique<renderer::Viewer>(std::move(*created));
    viewer_->setStandardView(renderer::StandardView::Isometric);
}

bool ViewportWidget::hasView() const noexcept {
    return viewer_ != nullptr;
}

QString ViewportWidget::unavailableReason() const {
    return unavailable_;
}

Result<renderer::PresentationId> ViewportWidget::display(ObjectId object,
                                                         const geometry::Body& body) {
    if (viewer_ == nullptr) {
        // REFUSED, not ignored. A caller that displays into a widget with no
        // view has to know nothing was shown.
        return makeError(ErrorCode::FailedPrecondition,
                         unavailable_.isEmpty()
                             ? "viewport: the view has not been created yet"
                             : unavailable_.toStdString());
    }
    Result<renderer::PresentationId> presentation = viewer_->display(object, body);
    if (presentation.has_value()) {
        viewer_->fitAll();
        update();
    }
    return presentation;
}

std::optional<ObjectId> ViewportWidget::lastPicked() const noexcept {
    return lastPicked_;
}

renderer::Viewer* ViewportWidget::viewer() noexcept {
    return viewer_.get();
}

QSize ViewportWidget::sizeHint() const {
    return {960, 640};
}

void ViewportWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    ensureView();
}

void ViewportWidget::paintEvent(QPaintEvent* event) {
    if (viewer_ != nullptr) {
        // The viewer draws into the native window; Qt contributes nothing.
        (void)viewer_->redraw();
        return;
    }
    // No view: say so legibly rather than leaving an empty hole.
    QPainter painter(this);
    painter.fillRect(event->rect(), palette().window());
    painter.setPen(palette().windowText().color());
    painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap,
                     unavailable_.isEmpty() ? tr("3D view not initialised") : unavailable_);
}

void ViewportWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (viewer_ != nullptr) {
        (void)viewer_->resize(toDevice(event->size().width()),
                              toDevice(event->size().height()));
    }
}

void ViewportWidget::mousePressEvent(QMouseEvent* event) {
    lastMouse_ = event->pos();
    if (viewer_ == nullptr) {
        QWidget::mousePressEvent(event);
        return;
    }
    if (event->button() == Qt::LeftButton) {
        const int x = toDevice(event->pos().x());
        const int y = toDevice(event->pos().y());
        if (pickMesh_) {
            // A MESH PICK ANSWERS A RENDER TRIANGLE, and the widget passes it
            // on without interpreting it: translating a buffer position into
            // an ElementId needs the MeshView that built the presentation, and
            // a Qt widget has no business holding one.
            const Result<std::optional<renderer::MeshPick>> hit = viewer_->pickMeshAt(x, y);
            if (hit.has_value()) {
                Q_EMIT meshPicked(hit->has_value() ? std::optional<std::size_t>{(*hit)->triangle}
                                                   : std::nullopt);
                update();
            }
            return;
        }
        // A CAD pick resolves to a CAD identity. The widget never sees a
        // graphics index.
        const Result<std::optional<ObjectId>> object = viewer_->pickAt(x, y);
        if (object.has_value()) {
            lastPicked_ = *object;
            Q_EMIT picked(*object);
            update();
        }
    }
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* event) {
    if (viewer_ == nullptr) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    const QPoint delta = event->pos() - lastMouse_;
    lastMouse_ = event->pos();
    if ((event->buttons() & Qt::LeftButton) != 0) {
        viewer_->orbit(Angle::fromSi(static_cast<double>(delta.x()) * kOrbitRadiansPerPixel),
                       Angle::fromSi(static_cast<double>(delta.y()) * kOrbitRadiansPerPixel));
        update();
    } else if ((event->buttons() & Qt::MiddleButton) != 0) {
        // Qt's y grows downward and the view's upward. V3d_View::Pan moves by
        // view pixels, so the drag is converted to device pixels and the model
        // tracks the cursor one to one at any scale factor.
        viewer_->pan(static_cast<double>(toDevice(delta.x())),
                     static_cast<double>(-toDevice(delta.y())));
        update();
    }
}

void ViewportWidget::wheelEvent(QWheelEvent* event) {
    if (viewer_ == nullptr) {
        QWidget::wheelEvent(event);
        return;
    }
    const double notches = static_cast<double>(event->angleDelta().y()) / 120.0;
    if (notches != 0.0) {
        viewer_->zoom(notches > 0.0 ? kZoomPerNotch : 1.0 / kZoomPerNotch);
        update();
    }
}

} // namespace bettercad::app
