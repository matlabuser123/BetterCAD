#include "MainWindow.hpp"

#include <bettercad/core/BuildInfo.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/renderer/MeshInspection.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

#include <cstdio>
#include <memory>
#include <string_view>
#include <utility>

namespace {

/// Prints a line per step so the automated test can assert on behaviour
/// rather than on a screenshot.
void say(const char* what, const QString& detail) {
    std::fprintf(stdout, "mesh: %s%s%s\n", what, detail.isEmpty() ? "" : " ",
                 detail.isEmpty() ? "" : qPrintable(detail));
}

/// Drives the mesh inspector end to end.
///
/// TEST SCAFFOLDING, AND LABELLED AS SUCH. The part is built here because
/// there is no modelling UI yet to draw one with; it exists only inside
/// --smoke-test and nothing in the application reads it.
///
/// It runs WITHOUT A 3D VIEW under Qt's offscreen platform plugin, which is
/// the point: the inspection layer is viewless, so generation, state,
/// selection, quality navigation and CAD-to-mesh linkage are all assertable on
/// a machine with no GL at all. What a view adds is drawing, and the renderer's
/// own tests cover that.
void exerciseMeshInspection(bettercad::app::MainWindow& window) {
    using namespace bettercad;
    using namespace bettercad::literals;

    Document document{"SmokeTest"};
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    const Result<EntityId> corner =
        sketch->addLine(Point2D{0_mm, 0_mm}, Point2D{30_mm, 0_mm});
    if (!corner.has_value()) {
        say("FAILED to build the test profile", QString{});
        return;
    }
    (void)sketch->addLine(Point2D{30_mm, 0_mm}, Point2D{30_mm, 20_mm});
    (void)sketch->addLine(Point2D{30_mm, 20_mm}, Point2D{0_mm, 20_mm});
    (void)sketch->addLine(Point2D{0_mm, 20_mm}, Point2D{0_mm, 0_mm});
    const Result<ObjectId> profile = document.addObject(std::move(sketch));
    if (!profile.has_value()) {
        say("FAILED to add the test profile", QString{});
        return;
    }
    auto extrude = features::ExtrudeFeature::create(
        "Solid", {.profile = SketchId::fromValue(profile->value()), .depth = 10_mm});
    if (!extrude.has_value() || !document.addObject(std::move(*extrude)).has_value()) {
        say("FAILED to add the test solid", QString{});
        return;
    }
    window.setDocument(std::move(document));

    // 1. no mesh yet
    say("state before generating:", window.meshPanel()->stateText());

    // 2. generate
    const Result<void> generated = window.generateMesh();
    if (!generated.has_value()) {
        say("generation refused:", QString::fromUtf8(generated.error().message.c_str()));
        return;
    }
    const renderer::MeshScene* scene = window.meshScene();
    if (scene == nullptr) {
        say("FAILED: generation reported success and left no scene", QString{});
        return;
    }
    say("generated:", QStringLiteral("%1 nodes, %2 Tet4, %3 boundary facets")
                          .arg(scene->mesh().nodeCount())
                          .arg(scene->mesh().tetrahedra().size())
                          .arg(scene->mesh().triangles().size()));
    say("state when current:", window.meshPanel()->stateText());

    // 3. select an element and read what the panel shows
    const meshing::ElementId facet = scene->mesh().triangles().front().id;
    if (const Result<void> selected = window.selectElement(facet); !selected.has_value()) {
        say("selection refused:", QString::fromUtf8(selected.error().message.c_str()));
    } else {
        say("selected a boundary facet:",
            window.meshPanel()->detailsText().split(QChar::fromLatin1('\n')).first());
    }

    // 4. worst-element navigation, from the report
    const Result<meshing::ElementId> worst =
        window.goToWorstElement(meshing::QualityMetric::TetAspectRatio);
    if (!worst.has_value()) {
        say("worst-element navigation refused:",
            QString::fromUtf8(worst.error().message.c_str()));
    } else {
        say("navigated to the worst aspect ratio:",
            QStringLiteral("element %1").arg(worst->value()));
    }

    // 5. CAD face -> mesh, through P16-MAP-001
    const Result<renderer::MeshHighlight> highlight = window.highlightCadFace(0U);
    if (!highlight.has_value()) {
        say("CAD-to-mesh highlight refused:",
            QString::fromUtf8(highlight.error().message.c_str()));
    } else {
        say("CAD face 0 highlights:", QStringLiteral("%1 facets, %2 render triangles")
                                          .arg(highlight->facets().size())
                                          .arg(highlight->triangles.size()));
    }

    // 6. mesh facet -> CAD, the reverse
    const Result<meshing::FacetSource> source =
        renderer::sourceOfFacet(scene->mesh(), scene->map(), facet);
    if (!source.has_value()) {
        say("mesh-to-CAD refused:", QString::fromUtf8(source.error().message.c_str()));
    } else {
        say("that facet came from:", QStringLiteral("CAD face %1%2")
                                         .arg(source->face)
                                         .arg(source->names.empty() ? " (unnamed)" : ""));
    }

    // 7. change the geometry: the mesh must become stale, visibly
    const Result<bool> edited = window.document().modifyObject<features::ExtrudeFeature>(
        window.document().objects().empty() ? ObjectId{} : scene->feature(),
        [](features::ExtrudeFeature& feature) -> Result<bool> {
            auto definition = feature.definition();
            definition.depth = 14_mm;
            return feature.setDefinition(definition).has_value();
        });
    if (!edited.has_value()) {
        say("could not edit the geometry:", QString::fromUtf8(edited.error().message.c_str()));
        return;
    }
    window.refreshMeshState();
    say("state after editing the geometry:", window.meshPanel()->stateText());

    // 8. regenerate: the selection must NOT survive. ElementId values are
    // reused across generations, so carrying one over would present a
    // different element's numbers under the selected element's identity.
    if (const Result<void> again = window.generateMesh(); !again.has_value()) {
        say("regeneration refused:", QString::fromUtf8(again.error().message.c_str()));
        return;
    }
    say("state after regenerating:", window.meshPanel()->stateText());
    say("selection after regenerating:", window.meshPanel()->detailsText());
}

} // namespace

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
            exerciseMeshInspection(window);
            std::fflush(stdout);
            QApplication::exit(window.isVisible() ? 0 : 1);
        });
    }
    return QApplication::exec();
}
