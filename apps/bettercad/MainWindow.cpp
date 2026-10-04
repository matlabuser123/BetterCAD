#include "MainWindow.hpp"

#include <bettercad/core/BuildInfo.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>
#include <bettercad/renderer/MeshInspection.hpp>

#include <QAction>
#include <QKeySequence>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>

#include <string_view>
#include <utility>

namespace bettercad::app {

namespace {

QString toQString(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setObjectName(QStringLiteral("BetterCADMainWindow"));
    setWindowTitle(QStringLiteral("BetterCAD %1").arg(toQString(buildInfo().version)));

    viewport_ = new ViewportWidget(this);
    setCentralWidget(viewport_);
    connect(viewport_, &ViewportWidget::picked, this, [this](std::optional<ObjectId> object) {
        statusBar()->showMessage(object.has_value()
                                     ? tr("Selected %1").arg(object->value())
                                     : tr("Nothing selected"));
    });
    // A MESH PICK IS TRANSLATED HERE, because this is where the MeshView is.
    // The viewport hands over a render triangle; turning it into an ElementId
    // needs the cache that built the presentation, and resolving that element
    // needs the scene.
    connect(viewport_, &ViewportWidget::meshPicked, this,
            [this](std::optional<std::size_t> triangle) {
                if (!triangle.has_value() || !view_.has_value() || !scene_.has_value()) {
                    meshPanel_->clearSelectionDetails();
                    statusBar()->showMessage(tr("Nothing selected"));
                    return;
                }
                const Result<meshing::ElementId> element = view_->elementOfTriangle(*triangle);
                if (!element.has_value()) {
                    meshPanel_->showNotice(toQString(element.error().message));
                    return;
                }
                if (const Result<void> selected = selectElement(*element); !selected.has_value()) {
                    meshPanel_->showNotice(toQString(selected.error().message));
                }
            });

    meshPanel_ = new MeshInspectorPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, meshPanel_);
    connectPanel();

    createMenus();
    statusBar()->showMessage(tr("Ready"));
    resize(1280, 800);
}

MainWindow::~MainWindow() = default;

void MainWindow::connectPanel() {
    connect(meshPanel_, &MeshInspectorPanel::faceChosen, this, [this](std::size_t faceIndex) {
        const Result<renderer::MeshHighlight> highlight = highlightCadFace(faceIndex);
        if (!highlight.has_value()) {
            // SURFACED, not swallowed. A stale mapping or an unresolved
            // reference is something the user has to be told.
            meshPanel_->showNotice(toQString(highlight.error().message));
            return;
        }
        if (!highlight->fullyResolved()) {
            meshPanel_->showNotice(tr("That face no longer resolves against this mesh."));
            return;
        }
        statusBar()->showMessage(tr("%1 boundary facets highlighted")
                                     .arg(highlight->facets().size()));
    });
    connect(meshPanel_, &MeshInspectorPanel::worstElementRequested, this,
            [this](meshing::QualityMetric metric) {
                const Result<meshing::ElementId> worst = goToWorstElement(metric);
                if (!worst.has_value()) {
                    meshPanel_->showNotice(toQString(worst.error().message));
                    return;
                }
                statusBar()->showMessage(tr("Worst %1: element %2")
                                             .arg(toQString(meshing::toString(metric)))
                                             .arg(worst->value()));
            });
    connect(meshPanel_, &MeshInspectorPanel::meshVisibilityChanged, this, [this](bool visible) {
        if (!meshPresentation_.has_value() || viewport_->viewer() == nullptr) {
            return;
        }
        // VISIBILITY IS RENDERING STATE. Hiding deletes nothing: the scene,
        // the mesh and the document are untouched.
        if (const Result<void> changed =
                viewport_->viewer()->setVisible(*meshPresentation_, visible);
            !changed.has_value()) {
            meshPanel_->showNotice(toQString(changed.error().message));
        }
        viewport_->update();
    });
    connect(meshPanel_, &MeshInspectorPanel::meshStyleChanged, this,
            [this](renderer::MeshStyle style) {
                if (!meshPresentation_.has_value() || viewport_->viewer() == nullptr) {
                    return;
                }
                // A VISUAL OPERATION: no regeneration, no change to the mesh.
                if (const Result<void> changed =
                        viewport_->viewer()->setMeshStyle(*meshPresentation_, style);
                    !changed.has_value()) {
                    meshPanel_->showNotice(toQString(changed.error().message));
                }
                viewport_->update();
            });
    connect(meshPanel_, &MeshInspectorPanel::selectionModeChanged, this, [this](SelectionMode mode) {
        viewport_->setPickMesh(mode == SelectionMode::Mesh);
    });
    viewport_->setPickMesh(meshPanel_->selectionMode() == SelectionMode::Mesh);
}

void MainWindow::setDocument(Document document) {
    dropMesh();
    document_ = std::move(document);
    regenerator_ = features::Regenerator{};
    (void)regenerator_.regenerate(document_);
    cadPresentation_.reset();
    refreshMeshState();
}

void MainWindow::dropMesh() {
    // A SCENE BELONGS TO ONE FEATURE OF ONE DOCUMENT AND ONE GENERATION.
    // Dropping it wholesale is the only safe thing to do when any of those
    // change: keeping a render cache or a selection across would be the stale
    // state this milestone exists to make impossible.
    if (meshPresentation_.has_value() && viewport_->viewer() != nullptr) {
        (void)viewport_->viewer()->remove(*meshPresentation_);
    }
    meshPresentation_.reset();
    view_.reset();
    scene_.reset();
    lastFailure_.reset();
}

Result<void> MainWindow::generateMesh() {
    // THE MODEL IS BROUGHT UP TO DATE FIRST, AND NOT SILENTLY. P16-GEOM-001
    // refuses to mesh stale geometry -- "a stale or failed model must never
    // produce a nominally valid current mesh" -- so a mesh of an edited
    // document is only possible after a regeneration. Doing it here is what
    // "mesh this" means in a CAD application; what would be wrong is meshing
    // the OLD body and calling the result current, which the core will not
    // allow in any case.
    //
    // A regeneration that fails is reported as the mesh's failure, because
    // from the user's point of view that is what happened: they asked for a
    // mesh and did not get one, and the reason is the model's.
    if (const Result<features::RegenerationReport> report = regenerator_.regenerate(document_);
        !report.has_value()) {
        lastFailure_ = report.error();
        refreshMeshState();
        return std::unexpected(report.error());
    }

    const std::vector<ObjectId> results = features::resultFeatures(document_);
    if (results.empty()) {
        lastFailure_ = Error{ErrorCode::FailedPrecondition,
                            "there is no solid body in this document to mesh"};
        refreshMeshState();
        return std::unexpected(*lastFailure_);
    }
    if (results.size() > 1U) {
        // Honest about the missing UI rather than silently meshing the first.
        lastFailure_ = Error{ErrorCode::FailedPrecondition,
                            "this document has more than one body, and choosing between them "
                            "needs a model tree, which this version does not have"};
        refreshMeshState();
        return std::unexpected(*lastFailure_);
    }

    const ObjectId feature = results.front();
    dropMesh();

    Result<meshing::VolumeMesh> volume =
        meshing::volumeMeshFor(document_, regenerator_, feature, {});
    if (!volume.has_value()) {
        lastFailure_ = volume.error();
        refreshMeshState();
        return std::unexpected(volume.error());
    }
    Result<meshing::GeometryMeshMap> map =
        meshing::geometryMeshMapFor(document_, regenerator_, feature, *volume);
    if (!map.has_value()) {
        lastFailure_ = map.error();
        refreshMeshState();
        return std::unexpected(map.error());
    }
    Result<renderer::MeshQualityView> quality =
        renderer::MeshQualityView::evaluate(volume->mesh(), meshing::reportOnlyThresholds());
    if (!quality.has_value()) {
        lastFailure_ = quality.error();
        refreshMeshState();
        return std::unexpected(quality.error());
    }

    Result<renderer::MeshScene> scene = renderer::MeshScene::adopt(
        feature, std::move(*volume), std::move(*map), std::move(*quality));
    if (!scene.has_value()) {
        lastFailure_ = scene.error();
        refreshMeshState();
        return std::unexpected(scene.error());
    }
    scene_ = std::move(*scene);
    lastFailure_.reset();

    Result<renderer::MeshView> view = renderer::MeshView::volumeBoundaryOf(scene_->mesh());
    if (!view.has_value()) {
        lastFailure_ = view.error();
        refreshMeshState();
        return std::unexpected(view.error());
    }
    view_ = std::move(*view);

    if (const Result<void> shown = displayMesh(); !shown.has_value()) {
        // A viewport with no 3D view is a supported state: the inspector still
        // works, which is why this is not a failure of generation.
        statusBar()->showMessage(toQString(shown.error().message));
    }
    refreshMeshState();
    return Result<void>{};
}

Result<void> MainWindow::displayMesh() {
    if (!scene_.has_value() || !view_.has_value()) {
        return makeError(ErrorCode::FailedPrecondition, "there is no mesh to display");
    }
    renderer::Viewer* viewer = viewport_->viewer();
    if (viewer == nullptr) {
        return makeError(ErrorCode::FailedPrecondition,
                         "there is no 3D view, so the mesh cannot be drawn; the inspector still "
                         "reports it");
    }
    Result<renderer::PresentationId> presentation =
        viewer->displayMesh(scene_->feature(), *view_, meshPanel_->style());
    if (!presentation.has_value()) {
        return std::unexpected(presentation.error());
    }
    meshPresentation_ = *presentation;
    viewer->fitAll();
    viewport_->update();
    return Result<void>{};
}

void MainWindow::refreshMeshState() {
    const renderer::MeshStatus status = meshStatus();
    meshPanel_->setScene(scene_.has_value() ? &*scene_ : nullptr, status);

    // THE VIEW SHOWS THE DECISION THE CORE MADE. A stale mesh is drawn
    // differently so that it cannot be mistaken for a current one.
    if (meshPresentation_.has_value() && viewport_->viewer() != nullptr) {
        const bool stale = status.state == renderer::MeshVisualState::Stale ||
                           status.state == renderer::MeshVisualState::GenerationFailed;
        (void)viewport_->viewer()->setMeshStale(*meshPresentation_, stale);
        viewport_->update();
    }
}

renderer::MeshStatus MainWindow::meshStatus() const {
    if (scene_.has_value()) {
        return scene_->status(document_, lastFailure_);
    }
    renderer::MeshHolding holding;
    holding.lastFailure = lastFailure_;
    return renderer::statusOf(document_, holding);
}

const renderer::MeshScene* MainWindow::meshScene() const noexcept {
    return scene_.has_value() ? &*scene_ : nullptr;
}

const renderer::MeshView* MainWindow::meshView() const noexcept {
    return view_.has_value() ? &*view_ : nullptr;
}

Result<void> MainWindow::selectElement(meshing::ElementId element) {
    if (!scene_.has_value() || !view_.has_value()) {
        return makeError(ErrorCode::FailedPrecondition, "there is no mesh to select in");
    }
    Result<renderer::ElementInspection> inspection = scene_->inspectElement(element);
    if (!inspection.has_value()) {
        return std::unexpected(inspection.error());
    }
    meshPanel_->showElement(*inspection);

    if (meshPresentation_.has_value() && viewport_->viewer() != nullptr) {
        // Highlighting resolves the element's own render triangles -- one for a
        // boundary facet, none for an interior tetrahedron in a boundary view.
        const std::vector<std::size_t> triangles = view_->trianglesOfElement(element);
        if (const Result<void> lit =
                viewport_->viewer()->setMeshHighlight(*meshPresentation_, triangles);
            !lit.has_value()) {
            return std::unexpected(lit.error());
        }
        viewport_->update();
    }
    statusBar()->showMessage(tr("Element %1").arg(element.value()));
    return Result<void>{};
}

Result<meshing::ElementId> MainWindow::goToWorstElement(meshing::QualityMetric metric) {
    if (!scene_.has_value()) {
        return makeError(ErrorCode::FailedPrecondition, "there is no mesh to inspect");
    }
    // THE REPORT'S ANSWER, AND IT IS REFUSED IF THE REPORT IS NOT THIS MESH'S.
    // The scene could only be adopted with a report that describes its mesh,
    // so this cannot navigate into a different generation.
    Result<meshing::ElementId> worst = scene_->worstElementFor(metric);
    if (!worst.has_value()) {
        return std::unexpected(worst.error());
    }
    if (const Result<void> selected = selectElement(*worst); !selected.has_value()) {
        return std::unexpected(selected.error());
    }
    return *worst;
}

Result<renderer::MeshHighlight> MainWindow::highlightCadFace(std::size_t faceIndex) {
    if (!scene_.has_value() || !view_.has_value()) {
        return makeError(ErrorCode::FailedPrecondition, "there is no mesh to highlight");
    }
    Result<renderer::MeshHighlight> highlight =
        renderer::highlightFor(*view_, scene_->mesh(), scene_->map(), faceIndex);
    if (!highlight.has_value()) {
        return std::unexpected(highlight.error());
    }
    if (meshPresentation_.has_value() && viewport_->viewer() != nullptr) {
        if (const Result<void> lit = viewport_->viewer()->setMeshHighlight(*meshPresentation_,
                                                                          highlight->triangles);
            !lit.has_value()) {
            return std::unexpected(lit.error());
        }
        viewport_->update();
    }
    return highlight;
}

void MainWindow::createMenus() {
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    QAction* quitAction = fileMenu->addAction(tr("&Quit"), this, &QWidget::close);
    quitAction->setShortcut(QKeySequence::Quit);

    QMenu* meshMenu = menuBar()->addMenu(tr("&Mesh"));
    meshMenu->addAction(tr("&Generate"), this, [this] {
        if (const Result<void> generated = generateMesh(); !generated.has_value()) {
            statusBar()->showMessage(toQString(generated.error().message));
        } else {
            statusBar()->showMessage(tr("Mesh generated"));
        }
    });
    meshMenu->addAction(tr("&Refresh state"), this, &MainWindow::refreshMeshState);

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
