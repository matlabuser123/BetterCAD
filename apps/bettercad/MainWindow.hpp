#pragma once

#include <QMainWindow>

namespace bettercad::app {

/// Top-level window of the desktop application. P0 placeholder: menus, status
/// bar and an empty viewport area; modelling UI arrives with later milestones.
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void createMenus();
    void showAbout();
};

} // namespace bettercad::app
