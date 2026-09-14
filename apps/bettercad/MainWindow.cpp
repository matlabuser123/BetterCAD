#include "MainWindow.hpp"

#include <bettercad/core/BuildInfo.hpp>

#include <QAction>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>

#include <string_view>

namespace bettercad::app {

namespace {

QString toQString(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setObjectName(QStringLiteral("BetterCADMainWindow"));
    setWindowTitle(QStringLiteral("BetterCAD %1").arg(toQString(buildInfo().version)));

    auto* viewport = new QLabel(tr("Viewport — not implemented yet"), this);
    viewport->setObjectName(QStringLiteral("ViewportPlaceholder"));
    viewport->setAlignment(Qt::AlignCenter);
    setCentralWidget(viewport);

    createMenus();
    statusBar()->showMessage(tr("Ready"));
    resize(1280, 800);
}

void MainWindow::createMenus() {
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    QAction* quitAction = fileMenu->addAction(tr("&Quit"), this, &QWidget::close);
    quitAction->setShortcut(QKeySequence::Quit);

    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About BetterCAD"), this, &MainWindow::showAbout);
}

void MainWindow::showAbout() {
    const QString details = toQString(formatBuildInfo(buildInfo()));
    QMessageBox::about(this, tr("About BetterCAD"),
                       QStringLiteral("<pre>%1\n  Qt         : %2 (built against %3)</pre>")
                           .arg(details.toHtmlEscaped().trimmed(), QString::fromLatin1(qVersion()),
                                QStringLiteral(QT_VERSION_STR)));
}

} // namespace bettercad::app
