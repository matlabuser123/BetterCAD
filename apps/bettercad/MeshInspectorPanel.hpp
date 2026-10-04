#pragma once

// The mesh inspection panel (P16-VIZ-001).
//
// A VIEW OVER renderer::MeshScene AND NOTHING MORE. It holds no mesh, no
// quality threshold and no mapping: every number it shows comes from the scene,
// which gets them from `meshing`. If this panel and the engine ever disagreed
// about whether an element is acceptable, it would be because someone put a
// constant in here.
//
// It also holds no CAD state. Selecting a face in its list emits the face's
// INDEX IN THE MAP, which the window turns into a highlight through
// P16-MAP-001; the panel never searches geometry.

#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/renderer/MeshScene.hpp>
#include <bettercad/renderer/Viewer.hpp>

#include <QDockWidget>
#include <QString>

#include <cstddef>
#include <optional>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace bettercad::app {

/// What the user is picking in the viewport.
///
/// GUI STATE, not model state. A CAD body and its mesh occupy the same space,
/// so without a mode the result of a click would depend on drawing order --
/// which is the one thing a selection must never depend on.
enum class SelectionMode : std::uint8_t {
    Cad,
    Mesh,
};

class MeshInspectorPanel final : public QDockWidget {
    Q_OBJECT

public:
    explicit MeshInspectorPanel(QWidget* parent = nullptr);

    /// Shows @p scene, or "no engineering mesh" when it is null.
    ///
    /// The status is passed in rather than derived here: `renderer::statusOf`
    /// decides it from the document's own geometry revision, and a panel that
    /// worked it out for itself would be a second opinion about currency.
    void setScene(const renderer::MeshScene* scene, const renderer::MeshStatus& status);

    /// Shows what a pick resolved to.
    void showNode(const renderer::NodeInspection& node);
    void showElement(const renderer::ElementInspection& element);
    void clearSelectionDetails();

    /// Reports a refusal from the core -- an unresolved reference, a stale
    /// mapping -- rather than leaving the user with an empty highlight and no
    /// explanation.
    void showNotice(const QString& notice);

    [[nodiscard]] SelectionMode selectionMode() const noexcept { return selectionMode_; }
    [[nodiscard]] renderer::MeshStyle style() const noexcept { return style_; }
    [[nodiscard]] bool meshVisible() const noexcept { return meshVisible_; }

    /// The text of the single mesh-state indicator, for the smoke test.
    [[nodiscard]] QString stateText() const;
    /// The selection details currently shown, for the smoke test.
    [[nodiscard]] QString detailsText() const;

Q_SIGNALS:
    /// A CAD face was chosen from the list, by its index in the map.
    void faceChosen(std::size_t faceIndex);
    /// Go to the worst element for this metric.
    void worstElementRequested(bettercad::meshing::QualityMetric metric);
    void meshVisibilityChanged(bool visible);
    void meshStyleChanged(bettercad::renderer::MeshStyle style);
    void selectionModeChanged(bettercad::app::SelectionMode mode);

private:
    void rebuildFaceList(const renderer::MeshScene* scene);
    void rebuildQuality(const renderer::MeshScene* scene);

    QLabel* state_ = nullptr;
    QLabel* notice_ = nullptr;
    QLabel* summary_ = nullptr;
    QLabel* quality_ = nullptr;
    QLabel* details_ = nullptr;
    QListWidget* faces_ = nullptr;
    QComboBox* metric_ = nullptr;
    QComboBox* styleBox_ = nullptr;
    QComboBox* modeBox_ = nullptr;
    QPushButton* visibility_ = nullptr;
    QPushButton* goToWorst_ = nullptr;

    /// The mesh generation whose selection is currently on screen.
    ///
    /// A SELECTION DOES NOT SURVIVE A REMESH, and this is what notices. An
    /// ElementId is reused across generations, so element 13 of the new mesh
    /// is a different element from element 13 of the old one -- and leaving the
    /// old details on screen would present one element's numbers under
    /// another's identity. Keeping the stamp is how the panel can tell.
    meshing::MeshStamp shownGeneration_{};

    SelectionMode selectionMode_ = SelectionMode::Mesh;
    renderer::MeshStyle style_ = renderer::MeshStyle::ShadedWithEdges;
    bool meshVisible_ = true;
};

} // namespace bettercad::app
