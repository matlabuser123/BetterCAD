#pragma once

#include "MeshInspectorPanel.hpp"
#include "ViewportWidget.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/renderer/MeshScene.hpp>
#include <bettercad/renderer/MeshView.hpp>

#include <QMainWindow>

#include <memory>
#include <optional>

namespace bettercad::app {

/// Top-level window of the desktop application.
///
/// Menus, a status bar, the 3D viewport (INFRA-VIEWER-001) and the mesh
/// inspector (P16-VIZ-001). The modelling UI -- model tree, property editor,
/// sketch environment, command system -- is still ROADMAP's "Desktop
/// Application" goal and is not here.
///
/// IT HOLDS A DOCUMENT, AND THE DOCUMENT OWNS THE ENGINEERING STATE. That is
/// the same arrangement the CLI has: the window has one Document, through the
/// same public API, and no widget holds a second copy of anything in it. What
/// the window adds is GUI state -- which mesh has been generated, what is
/// selected, what is visible -- and that state is keyed to the mesh's own
/// generation stamp so it cannot outlive its meaning.
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /// Shows @p document, replacing whatever was open.
    ///
    /// Any mesh of the previous document is dropped: a MeshScene belongs to
    /// one feature of one document, and carrying one across would be exactly
    /// the stale-state defect this milestone exists to prevent.
    void setDocument(Document document);

    [[nodiscard]] Document& document() noexcept { return document_; }

    /// Meshes the document's single result feature and shows it.
    ///
    /// Refuses, with the core's own diagnostic, when there is nothing to mesh
    /// or when more than one body would need a model tree to choose between.
    [[nodiscard]] Result<void> generateMesh();

    /// Re-reads the mesh's state from the document and updates the panel and
    /// the viewport. Called after anything that could have changed geometry.
    void refreshMeshState();

    /// Selects @p element, highlights it and shows its details.
    [[nodiscard]] Result<void> selectElement(meshing::ElementId element);
    /// Selects the worst element for @p metric, from the quality report.
    [[nodiscard]] Result<meshing::ElementId> goToWorstElement(meshing::QualityMetric metric);
    /// Highlights the mesh of the CAD face at @p faceIndex in the map.
    [[nodiscard]] Result<renderer::MeshHighlight> highlightCadFace(std::size_t faceIndex);

    // --- for the automated smoke test ---------------------------------------
    [[nodiscard]] ViewportWidget* viewport() noexcept { return viewport_; }
    [[nodiscard]] MeshInspectorPanel* meshPanel() noexcept { return meshPanel_; }
    [[nodiscard]] const renderer::MeshScene* meshScene() const noexcept;
    [[nodiscard]] const renderer::MeshView* meshView() const noexcept;
    [[nodiscard]] renderer::MeshStatus meshStatus() const;

private:
    void createMenus();
    void showAbout();
    void connectPanel();
    /// Pushes the scene's render buffers into the viewport, if it has a view.
    [[nodiscard]] Result<void> displayMesh();
    void dropMesh();

    ViewportWidget* viewport_ = nullptr;
    MeshInspectorPanel* meshPanel_ = nullptr;

    Document document_{"Untitled"};
    features::Regenerator regenerator_;

    /// GUI state about generation attempts. Not canonical: P16-PERSIST-001
    /// owns persistence and comes later, so none of this is saved.
    std::optional<renderer::MeshScene> scene_{};
    std::optional<renderer::MeshView> view_{};
    std::optional<Error> lastFailure_{};
    std::optional<renderer::PresentationId> meshPresentation_{};
    std::optional<renderer::PresentationId> cadPresentation_{};
};

} // namespace bettercad::app
