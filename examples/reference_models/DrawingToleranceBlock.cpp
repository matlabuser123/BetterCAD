#include "BuildSupport.hpp"
#include "DrawingReferenceModels.hpp"
#include "DrawingSupport.hpp"

#include <bettercad/features/ExtrudeFeature.hpp>

namespace bettercad::reference {

using namespace bettercad::literals;
using detail::featureId;
using detail::sketchId;

// RM-DWG-05 -- what a part is MADE TO, rather than what it measures.
//
//   V = 80*50*25 - pi*10^2*25
//     = 100000 - 2500 pi
//     = 92146.018366025517 mm^3
//
// Three kinds of tolerance, because they are written differently and resolve
// differently, and a drawing that got one of them right could get the others
// wrong:
//
//   Length    80 +/-0.10         a symmetric deviation pair, stored
//   Width     50 +0.15 -0.05     an asymmetric pair, written as LIMITS
//   BoreSize  Ø20 H7             a fit, resolved from ISO 286 every time
//
// The third is the one worth having. `H7` is not two numbers in the file: the
// file says H7 and the deviations come from the standard when they are asked
// for, so the drawing follows ISO 286 rather than a copy of it that somebody
// transcribed once.
//
// THE DATUM ORDER IS THE POINT of the feature-control frame. The bore is held
// to Ø0.05 against A then B, and A|B is a different requirement from B|A: the
// primary datum takes three of the six degrees of freedom and the secondary
// takes what is left. A frame that stored its datums as a set could not say
// which was which.
//
// The bore is an extrude CUT of a circle so that its cylindrical face has a
// semantic name -- a feature-control frame has to point at the feature it
// controls, and a HoleFeature's wall is not nameable.
[[nodiscard]] Result<DrawnToleranceBlockModel> buildDrawnToleranceBlockReferenceModel() {
    DrawnToleranceBlockModel m{.document = Document(
                                   detail::fixedDocumentId("d4a70000-0000-4000-8000-000000000005"),
                                   "DrawnToleranceBlock")};
    detail::ModelBuilder b(m.document);

    // --- the part -----------------------------------------------------------
    m.blockLength = b.length("block_length", 80.0);
    m.blockWidth = b.length("block_width", 50.0);
    m.blockHeight = b.length("block_height", 25.0);
    m.boreRadius = b.length("bore_r", 10.0);

    {
        detail::SketchBuilder s(b, "BlockProfile", Frame3D::xy());
        const EntityId a = s.point(0.0, 0.0);
        const EntityId c = s.point(80.0, 0.0);
        const EntityId d = s.point(80.0, 50.0);
        const EntityId e = s.point(0.0, 50.0);
        m.blockLines = {s.line(a, c), s.line(c, d), s.line(d, e), s.line(e, a)};
        s.fixed(a);
        s.horizontal(m.blockLines[0]);
        s.vertical(m.blockLines[1]);
        s.horizontal(m.blockLines[2]);
        s.vertical(m.blockLines[3]);
        s.distance(a, c, m.blockLength);
        s.distance(a, e, m.blockWidth);
        m.blockSketch = s.finish();
    }
    m.block = b.feature<features::ExtrudeFeature>(
        "Block", {.profile = sketchId(m.blockSketch),
                  .depth = 25_mm,
                  .depthParameter = m.blockHeight});

    {
        detail::SketchBuilder s(b, "BoreProfile", detail::levelPlane(25.0));
        const EntityId centre = s.point(40.0, 25.0);
        m.boreCircle = s.circle(centre, 10.0);
        s.fixed(centre);
        s.radius(m.boreCircle, m.boreRadius);
        s.attach(PlaneReference{.object = m.block,
                                .face = FaceSelector{.role = FaceRole::EndCap}});
        m.boreSketch = s.finish();
    }
    m.bore = b.feature<features::ExtrudeFeature>(
        "Bore", {.profile = sketchId(m.boreSketch),
                 .direction = features::ExtrudeDirection::Reversed,
                 .operation = features::FeatureOperation::Cut,
                 .target = featureId(m.block),
                 .termination = features::ExtrudeTermination::ThroughAll});

    // --- the drawing ---------------------------------------------------------
    detail::DrawingBuilder d(b);

    m.sheet = d.sheet("Sheet1", drawing::SheetDefinition{.format = drawing::SheetFormat::A3,
                                                         .scale = {1, 1}});

    // Front is 80 wide (X) by 25 tall (Z); the top view below it is 80 by 50.
    m.front = d.view("Front", drawing::ViewDefinition{.sheet = m.sheet,
                                                      .source = ObjectReference{m.bore},
                                                      .orientation = drawing::StandardView::Front,
                                                      .placement = Point2D{140_mm, 190_mm}});
    // Projecting TOP from a Front parent gives right = +X, up = +Y, normal =
    // +Z -- the top view -- and first angle puts it on the far side, below.
    m.top = d.view("Top", drawing::ViewDefinition{.kind = drawing::ViewKind::Projected,
                                                  .sheet = m.sheet,
                                                  .parent = m.front,
                                                  .direction = drawing::ProjectedDirection::Top,
                                                  .spacing = 90_mm});

    const auto side = [&](std::size_t line) {
        return drawing::DimensionTarget{
            .plane = PlaneReference{
                .object = m.block,
                .face = FaceSelector{.role = FaceRole::Side, .entity = m.blockLines[line]}}};
    };
    const FaceName boreFace{.feature = m.bore,
                            .face = FaceSelector{.role = FaceRole::Side, .entity = m.boreCircle}};

    // 80.000 +/-0.10, written as a symmetric pair.
    m.length = d.dimension(
        "Length",
        drawing::DimensionDefinition{
            .view = m.front,
            .from = side(3),
            .to = side(1),
            .format = {.decimals = 2},
            .tolerance = drawing::DimensionTolerance{.lower = -0.10_mm, .upper = 0.10_mm},
            .placement = Point2D{140_mm, 160_mm}});
    // 50.000 +0.15 -0.05, written as the two LIMITS. Asymmetric on purpose:
    // a symmetric pair reads the same whichever way round the deviations are
    // stored, and would not catch a sign that had been lost.
    m.width = d.dimension(
        "Width",
        drawing::DimensionDefinition{
            .view = m.top,
            .from = side(0),
            .to = side(2),
            .format = {.decimals = 2},
            .tolerance = drawing::DimensionTolerance{.lower = -0.05_mm,
                                                      .upper = 0.15_mm,
                                                      .display = drawing::ToleranceDisplay::Limits},
            .placement = Point2D{50_mm, 100_mm}});
    // Ø20 H7. The deviations are NOT here: they are resolved from ISO 286
    // when the dimension is measured, which is what makes the drawing follow
    // the standard rather than a transcription of it.
    m.boreSize = d.dimension(
        "BoreSize",
        drawing::DimensionDefinition{
            .view = m.top,
            .type = drawing::DimensionType::Diameter,
            .from = {.cylinder = boreFace},
            .format = {.decimals = 3},
            .tolerance = drawing::DimensionTolerance{
                .fit = drawing::FitDesignation{.role = drawing::FitRole::Hole,
                                                .letter = 'H',
                                                .grade = 7}},
            .placement = Point2D{265_mm, 100_mm}});

    // Datum A is the face the part sits on; datum B is the end it is located
    // against. Both are NAMED faces, so they follow the block.
    m.datumA = d.annotation(
        "DatumA",
        drawing::AnnotationDefinition{
            .view = m.front,
            .type = drawing::AnnotationType::Datum,
            .target = drawing::AnnotationTarget{
                .plane = PlaneReference{.object = m.block,
                                        .face = FaceSelector{.role = FaceRole::StartCap}}},
            .text = "A",
            .placement = Point2D{215_mm, 205_mm}});
    m.datumB = d.annotation(
        "DatumB",
        drawing::AnnotationDefinition{
            .view = m.front,
            .type = drawing::AnnotationType::Datum,
            .target = drawing::AnnotationTarget{
                .plane = PlaneReference{
                    .object = m.block,
                    .face = FaceSelector{.role = FaceRole::Side, .entity = m.blockLines[3]}}},
            .text = "B",
            .placement = Point2D{75_mm, 205_mm}});

    // The bore's axis held inside a Ø0.05 cylinder, against A then B.
    m.borePosition = d.annotation(
        "BorePosition",
        drawing::AnnotationDefinition{
            .view = m.top,
            .type = drawing::AnnotationType::FeatureControlFrame,
            .target = drawing::AnnotationTarget{.cylinder = boreFace},
            .placement = Point2D{215_mm, 55_mm},
            .frame = drawing::FeatureControlFrame{
                .characteristic = drawing::GeometricCharacteristic::Position,
                .zone = drawing::ToleranceZone::Cylindrical,
                .tolerance = 0.05_mm,
                .datums = {drawing::DatumReference{'A'}, drawing::DatumReference{'B'}}}});

    // Ra 1.6 um on the top face, material removal required.
    m.topFinish = d.annotation(
        "TopFinish",
        drawing::AnnotationDefinition{
            .view = m.front,
            .type = drawing::AnnotationType::SurfaceFinish,
            .target = drawing::AnnotationTarget{
                .plane = PlaneReference{.object = m.block,
                                        .face = FaceSelector{.role = FaceRole::EndCap}}},
            .placement = Point2D{140_mm, 225_mm},
            .finish = drawing::SurfaceFinish{Length::fromSi(1.6e-6),
                                              drawing::MaterialRemoval::Required}});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
