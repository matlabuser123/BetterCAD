#include "MeshInspectorPanel.hpp"

#include <bettercad/core/Units.hpp>

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <array>

namespace bettercad::app {
namespace {

/// The metrics offered for worst-element navigation.
///
/// PER METRIC, because P16-QUALITY-001 reports worst per metric and
/// deliberately has no overall score: a sliver and a stretched element are bad
/// in different ways, and one combined number would hide both.
constexpr std::array<meshing::QualityMetric, 4> kNavigableMetrics{
    meshing::QualityMetric::TetAspectRatio,
    meshing::QualityMetric::TetRadiusRatio,
    meshing::QualityMetric::TetMinDihedralAngle,
    meshing::QualityMetric::TetMaxDihedralAngle,
};

[[nodiscard]] QString millimetres(const Length& length) {
    // NEVER A RAW NUMBER. A coordinate with no unit on it is a different
    // coordinate in a different unit and the reader cannot tell which.
    return QStringLiteral("%1 mm").arg(length.in(units::mm), 0, 'f', 4);
}

[[nodiscard]] QString cubicMillimetres(const Volume& volume) {
    return QStringLiteral("%1 mm³").arg(volume.in(units::mm3), 0, 'f', 5);
}

[[nodiscard]] QString degrees(const Angle& angle) {
    return QStringLiteral("%1°").arg(angle.in(units::deg), 0, 'f', 2);
}

[[nodiscard]] QString text(std::string_view view) {
    return QString::fromUtf8(view.data(), static_cast<qsizetype>(view.size()));
}

} // namespace

MeshInspectorPanel::MeshInspectorPanel(QWidget* parent)
    : QDockWidget(tr("Mesh"), parent) {
    setObjectName(QStringLiteral("MeshInspector"));
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

    auto* body = new QWidget(this);
    auto* layout = new QVBoxLayout(body);

    // ONE STATE INDICATOR. Scattering currency across several widgets is how a
    // UI ends up contradicting itself.
    state_ = new QLabel(tr("No engineering mesh"), body);
    state_->setObjectName(QStringLiteral("MeshState"));
    QFont stateFont = state_->font();
    stateFont.setBold(true);
    state_->setFont(stateFont);
    layout->addWidget(state_);

    notice_ = new QLabel(QString{}, body);
    notice_->setObjectName(QStringLiteral("MeshNotice"));
    notice_->setWordWrap(true);
    notice_->setVisible(false);
    layout->addWidget(notice_);

    auto* summaryBox = new QGroupBox(tr("Mesh"), body);
    auto* summaryLayout = new QVBoxLayout(summaryBox);
    summary_ = new QLabel(tr("—"), summaryBox);
    summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    summaryLayout->addWidget(summary_);
    layout->addWidget(summaryBox);

    auto* qualityBox = new QGroupBox(tr("Quality"), body);
    auto* qualityLayout = new QVBoxLayout(qualityBox);
    quality_ = new QLabel(tr("—"), qualityBox);
    quality_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    qualityLayout->addWidget(quality_);
    auto* navigation = new QFormLayout();
    metric_ = new QComboBox(qualityBox);
    for (const meshing::QualityMetric metric : kNavigableMetrics) {
        metric_->addItem(text(meshing::toString(metric)),
                         QVariant::fromValue(static_cast<int>(metric)));
    }
    navigation->addRow(tr("Metric"), metric_);
    goToWorst_ = new QPushButton(tr("Go to worst element"), qualityBox);
    navigation->addRow(goToWorst_);
    qualityLayout->addLayout(navigation);
    layout->addWidget(qualityBox);

    auto* selectionBox = new QGroupBox(tr("Selection"), body);
    auto* selectionLayout = new QVBoxLayout(selectionBox);
    details_ = new QLabel(tr("Nothing selected"), selectionBox);
    details_->setObjectName(QStringLiteral("MeshSelectionDetails"));
    details_->setWordWrap(true);
    details_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    selectionLayout->addWidget(details_);
    layout->addWidget(selectionBox);

    auto* facesBox = new QGroupBox(tr("CAD faces"), body);
    auto* facesLayout = new QVBoxLayout(facesBox);
    faces_ = new QListWidget(facesBox);
    faces_->setObjectName(QStringLiteral("MeshFaceList"));
    facesLayout->addWidget(faces_);
    layout->addWidget(facesBox);

    auto* displayBox = new QGroupBox(tr("Display"), body);
    auto* displayLayout = new QFormLayout(displayBox);
    styleBox_ = new QComboBox(displayBox);
    styleBox_->addItem(tr("Shaded with edges"),
                       QVariant::fromValue(static_cast<int>(renderer::MeshStyle::ShadedWithEdges)));
    styleBox_->addItem(tr("Shaded"),
                       QVariant::fromValue(static_cast<int>(renderer::MeshStyle::Shaded)));
    styleBox_->addItem(tr("Wireframe"),
                       QVariant::fromValue(static_cast<int>(renderer::MeshStyle::Wireframe)));
    displayLayout->addRow(tr("Style"), styleBox_);
    modeBox_ = new QComboBox(displayBox);
    modeBox_->addItem(tr("Mesh"), QVariant::fromValue(static_cast<int>(SelectionMode::Mesh)));
    modeBox_->addItem(tr("CAD"), QVariant::fromValue(static_cast<int>(SelectionMode::Cad)));
    displayLayout->addRow(tr("Pick"), modeBox_);
    visibility_ = new QPushButton(tr("Hide mesh"), displayBox);
    visibility_->setCheckable(true);
    displayLayout->addRow(visibility_);
    layout->addWidget(displayBox);

    layout->addStretch(1);
    setWidget(body);

    connect(faces_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0) {
            return;
        }
        const QVariant index = faces_->item(row)->data(Qt::UserRole);
        Q_EMIT faceChosen(static_cast<std::size_t>(index.toULongLong()));
    });
    connect(goToWorst_, &QPushButton::clicked, this, [this] {
        const int which = metric_->currentData().toInt();
        Q_EMIT worstElementRequested(static_cast<meshing::QualityMetric>(which));
    });
    connect(styleBox_, &QComboBox::currentIndexChanged, this, [this](int) {
        style_ = static_cast<renderer::MeshStyle>(styleBox_->currentData().toInt());
        Q_EMIT meshStyleChanged(style_);
    });
    connect(modeBox_, &QComboBox::currentIndexChanged, this, [this](int) {
        // SWITCHING MODE CHANGES NOTHING ABOUT THE MODEL, and nothing about
        // what is already selected: it changes what the NEXT click resolves.
        selectionMode_ = static_cast<SelectionMode>(modeBox_->currentData().toInt());
        Q_EMIT selectionModeChanged(selectionMode_);
    });
    connect(visibility_, &QPushButton::toggled, this, [this](bool hidden) {
        meshVisible_ = !hidden;
        visibility_->setText(meshVisible_ ? tr("Hide mesh") : tr("Show mesh"));
        Q_EMIT meshVisibilityChanged(meshVisible_);
    });
}

void MeshInspectorPanel::setScene(const renderer::MeshScene* scene,
                                  const renderer::MeshStatus& status) {
    notice_->setVisible(false);
    notice_->clear();

    if (scene == nullptr) {
        state_->setText(tr("No engineering mesh"));
        summary_->setText(tr("—"));
        quality_->setText(tr("—"));
        faces_->clear();
        shownGeneration_ = meshing::MeshStamp{};
        clearSelectionDetails();
        return;
    }

    // A NEW MESH GENERATION CLEARS THE SELECTION. ElementId values are reused
    // across generations (ADR-031), so an element's details left on screen
    // after a remesh would describe the OLD element under the new one's
    // identity -- and look perfectly plausible doing it. There is deliberately
    // no attempt to carry the selection across: nothing tracks a tetrahedron
    // between meshes, and a CAD-face selection re-highlights through
    // P16-MAP-001 instead, which is the kind of selection that SHOULD survive.
    if (!(scene->stamp() == shownGeneration_)) {
        shownGeneration_ = scene->stamp();
        clearSelectionDetails();
        faces_->setCurrentRow(-1);
    }

    // THE STATE COMES IN, it is not worked out here. A stale mesh says so, and
    // a mesh whose latest regeneration failed says that instead -- never
    // "current".
    switch (status.state) {
    case renderer::MeshVisualState::Current:
        state_->setText(tr("Mesh: current"));
        break;
    case renderer::MeshVisualState::Stale:
        state_->setText(tr("Mesh: STALE — does not correspond to the current geometry"));
        break;
    case renderer::MeshVisualState::GenerationFailed:
        state_->setText(status.inspectable
                            ? tr("Mesh: GENERATION FAILED — showing the previous, stale mesh")
                            : tr("Mesh: GENERATION FAILED"));
        break;
    case renderer::MeshVisualState::NoMesh:
        state_->setText(tr("No engineering mesh"));
        break;
    }
    if (status.failure.has_value()) {
        showNotice(tr("Generation failed: %1")
                       .arg(QString::fromUtf8(status.failure->message.c_str())));
    }

    const meshing::Mesh& mesh = scene->mesh();
    summary_->setText(tr("%1 nodes\n%2 Tet4 elements\n%3 boundary facets\nmesh %4 generation %5")
                          .arg(mesh.nodeCount())
                          .arg(mesh.tetrahedra().size())
                          .arg(mesh.triangles().size())
                          .arg(mesh.stamp().mesh.value())
                          .arg(mesh.stamp().generation));

    rebuildQuality(scene);
    rebuildFaceList(scene);
}

void MeshInspectorPanel::rebuildQuality(const renderer::MeshScene* scene) {
    const meshing::MeshQualityReport& report = scene->quality().report();
    // THE REPORT'S OWN CATEGORIES, in the report's own words. No other scale.
    QString summary = tr("%1 valid, %2 warning, %3 failure, %4 invalid")
                          .arg(report.validElements)
                          .arg(report.warningElements)
                          .arg(report.failureElements)
                          .arg(report.invalidElements);
    for (const meshing::QualityMetric metric : kNavigableMetrics) {
        const auto found = report.summaries.find(metric);
        if (found == report.summaries.end() || !found->second.worst.isValid()) {
            continue;
        }
        // WORSE MEANS DIFFERENT THINGS FOR DIFFERENT METRICS, and which end of
        // the summary is the bad end is the report's to say: for an aspect
        // ratio a larger value is worse, for a radius ratio a smaller one is.
        // Reading the wrong end would quietly present the BEST element as the
        // worst, with the right element id beside it.
        const double worstValue =
            (meshing::direction(metric) == meshing::QualityDirection::LowerIsBetter)
                ? found->second.maximum
                : found->second.minimum;
        summary += tr("\n%1: worst %2 at element %3")
                       .arg(text(meshing::toString(metric)))
                       .arg(worstValue, 0, 'g', 5)
                       .arg(found->second.worst.value());
    }
    quality_->setText(summary);
}

void MeshInspectorPanel::rebuildFaceList(const renderer::MeshScene* scene) {
    faces_->clear();
    for (const renderer::MeshScene::FaceEntry& face : scene->faces()) {
        auto* item = new QListWidgetItem(
            tr("%1 — %2 facets")
                .arg(QString::fromUtf8(face.label.c_str()))
                .arg(face.facetCount),
            faces_);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(face.index));
    }
}

void MeshInspectorPanel::showNode(const renderer::NodeInspection& node) {
    QString shown = tr("Node %1\nx %2\ny %3\nz %4\n%5")
                        .arg(node.node.value())
                        .arg(millimetres(node.position.x))
                        .arg(millimetres(node.position.y))
                        .arg(millimetres(node.position.z))
                        .arg(node.onBoundary ? tr("on the boundary") : tr("interior"));
    if (node.source.has_value()) {
        shown += tr("\nCAD face %1").arg(node.source->face);
        if (node.source->names.empty()) {
            // Said plainly rather than left blank: P16-MAP-001 does not name
            // every face, and a bored hole's wall is the usual case.
            shown += tr(" (unnamed)");
        }
    }
    details_->setText(shown);
}

void MeshInspectorPanel::showElement(const renderer::ElementInspection& element) {
    QString shown = tr("Element %1 (%2)")
                        .arg(element.element.value())
                        .arg(text(meshing::toString(element.type)));
    shown += tr("\nnodes");
    for (const meshing::NodeId node : element.nodes) {
        shown += QStringLiteral(" %1").arg(node.value());
    }
    if (element.signedVolume.has_value()) {
        // SIGNED. An inverted element shows a negative volume, which is the
        // only way the user can see that it is inverted.
        shown += tr("\nsigned volume %1").arg(cubicMillimetres(*element.signedVolume));
    }
    shown += tr("\n%1").arg(text(meshing::toString(element.classification)));
    if (element.tetQuality.has_value()) {
        shown += tr("\naspect ratio %1\nradius ratio %2\nmin dihedral %3\nmax dihedral %4")
                     .arg(element.tetQuality->aspectRatio, 0, 'g', 5)
                     .arg(element.tetQuality->radiusRatio, 0, 'g', 5)
                     .arg(degrees(element.tetQuality->minDihedral))
                     .arg(degrees(element.tetQuality->maxDihedral));
    }
    if (element.source.has_value()) {
        shown += tr("\nCAD face %1").arg(element.source->face);
        if (element.source->names.empty()) {
            shown += tr(" (unnamed)");
        }
    }
    details_->setText(shown);
}

void MeshInspectorPanel::clearSelectionDetails() {
    details_->setText(tr("Nothing selected"));
}

void MeshInspectorPanel::showNotice(const QString& notice) {
    notice_->setText(notice);
    notice_->setVisible(!notice.isEmpty());
}

QString MeshInspectorPanel::stateText() const {
    return state_->text();
}

QString MeshInspectorPanel::detailsText() const {
    return details_->text();
}

} // namespace bettercad::app
