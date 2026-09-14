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
            QApplication::exit(window.isVisible() ? 0 : 1);
        });
    }
    return QApplication::exec();
}
