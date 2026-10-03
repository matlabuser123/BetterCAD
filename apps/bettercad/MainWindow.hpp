#pragma once

#include "ViewportWidget.hpp"

#include <QMainWindow>

namespace bettercad::app {

/// Top-level window of the desktop application.
///
/// Menus, a status bar and the 3D viewport (INFRA-VIEWER-001). The modelling
/// UI -- model tree, property editor, sketch environment, command system --
/// is still ROADMAP's "Desktop Application" goal and is not here.
///
/// IT OWNS NO ENGINEERING STATE. The viewport owns a view; the view owns
/// presentations. Neither owns a Document.
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    /// The viewport, for the automated smoke test.
    [[nodiscard]] ViewportWidget* viewport() noexcept { return viewport_; }

private:
    void createMenus();
    void showAbout();

    ViewportWidget* viewport_ = nullptr;
};

} // namespace bettercad::app
