#include "MainWindow.hpp"

#include <bettercad/core/BuildInfo.hpp>

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

#include <cstdio>
#include <string_view>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    const std::string_view version = bettercad::buildInfo().version;
    QApplication::setApplicationName(QStringLiteral("BetterCAD"));
    QApplication::setOrganizationName(QStringLiteral("BetterCAD"));
    QApplication::setApplicationVersion(
        QString::fromUtf8(version.data(), static_cast<qsizetype>(version.size())));

    // parse() rather than process(): process() reports errors through a
    // message box on Windows, which would block unattended runs.
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("BetterCAD desktop application"));
    const QCommandLineOption smokeTest(
        QStringLiteral("smoke-test"),
        QStringLiteral("Show the main window, run the event loop once and exit (automated tests)."));
    parser.addOption(smokeTest);
    if (!parser.parse(QApplication::arguments())) {
        std::fprintf(stderr, "bettercad: %s\n", qPrintable(parser.errorText()));
        return 2;
    }

    bettercad::app::MainWindow window;
    window.show();

    if (parser.isSet(smokeTest)) {
        QTimer::singleShot(0, &app, [&window] {
            // Reached only once the event loop is running with the window shown.
            const bettercad::app::ViewportWidget* viewport = window.viewport();
            // REPORTED, not inferred. Under the offscreen platform plugin
            // there is no native window and therefore no 3D view, and a test
            // that could not tell that apart from a broken viewport would be
            // worth very little. So the state is printed either way.
            if (viewport == nullptr) {
                std::fprintf(stdout, "viewport: absent\n");
                QApplication::exit(1);
                return;
            }
            if (viewport->hasView()) {
                std::fprintf(stdout, "viewport: 3D view created\n");
                // MEASURED, not assumed. Qt reports the widget in logical
                // pixels and the view works in device pixels; printing both
                // and the ratio between them is what turns that from a claim
                // into an observation, on whatever display the run happens on.
                std::fprintf(stdout,
                             "viewport: logical %dx%d, device %dx%d, ratio %.3f\n",
                             viewport->width(), viewport->height(),
                             viewport->deviceSize().width(),
                             viewport->deviceSize().height(), viewport->deviceRatio());
            } else {
                std::fprintf(stdout, "viewport: no 3D view (%s)\n",
                             qPrintable(viewport->unavailableReason()));
            }
            std::fflush(stdout);
            QApplication::exit(window.isVisible() ? 0 : 1);
        });
    }
    return QApplication::exec();
}
