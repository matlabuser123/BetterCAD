#include "BuildSupport.hpp"
#include "DrawingReferenceModels.hpp"
#include "DrawingSupport.hpp"

#include <bettercad/features/ExtrudeFeature.hpp>

namespace bettercad::reference {

using namespace bettercad::literals;
using detail::featureId;
using detail::sketchId;

// RM-DWG-03 -- a section and a detail of internal geometry.
//
// An 80 x 60 x 30 block with a stepped pocket sunk into its top face:
// 40 x 30 down 12, then 20 x 15 down a further 8. Nothing about it can be
// read from an outside view, which is what a section is for.
//
//   block   80 * 60 * 30 = 144000
//   pocket  40 * 30 * 12 = -14400
//   step    20 * 15 *  8 =  -2400
//                           ------
//                       V = 127200 mm^3 exactly
//
// WHY THE POCKET IS RECTANGULAR AND NOT A BORE. A section through a curved
// face is REFUSED, not drawn: tessellating a curved cut edge is the curve work
// P14-HLR-001 owns, and drawing it wrong would hatch straight across a void
// (Section.cpp). A production drawing suite has to live inside what the stack
// can actually do, so the internal geometry here is prismatic throughout and
// every cut edge is a straight line.
//
// The pocket is placed OFF CENTRE in both directions, so a section that came
// back mirrored would not look right.
[[nodiscard]] Result<DrawnPocketBlockModel> buildDrawnPocketBlockReferenceModel() {
    DrawnPocketBlockModel m{.document = Document(
                                detail::fixedDocumentId("d4a70000-0000-4000-8000-000000000003"),
                                "DrawnPocketBlock")};
    detail::ModelBuilder b(m.document);

    m.blockLength = b.length("block_length", 80.0);
    m.blockWidth = b.length("block_width", 60.0);
    m.blockHeight = b.length("block_height", 30.0);
    m.pocketDepth = b.length("pocket_depth", 12.0);
    m.stepDepth = b.length("step_depth", 8.0);

    {
        detail::SketchBuilder s(b, "BlockProfile", Frame3D::xy());
        const EntityId p0 = s.point(0.0, 0.0);
        const EntityId p1 = s.point(80.0, 0.0);
        const EntityId p2 = s.point(80.0, 60.0);
        const EntityId p3 = s.point(0.0, 60.0);
        m.blockLines = {s.line(p0, p1), s.line(p1, p2), s.line(p2, p3), s.line(p3, p0)};
        s.fixed(p0);
        s.horizontal(m.blockLines[0]);
        s.vertical(m.blockLines[1]);
        s.horizontal(m.blockLines[2]);
        s.vertical(m.blockLines[3]);
        s.distance(p0, p1, m.blockLength);
        s.distance(p0, p3, m.blockWidth);
        m.blockSketch = s.finish();
    }
    m.block = b.feature<features::ExtrudeFeature>(
        "Block",
        {.profile = sketchId(m.blockSketch), .depth = 30_mm, .depthParameter = m.blockHeight});

    // The pocket: 40 x 30, off centre, cut DOWN from the top face.
    {
        detail::SketchBuilder s(b, "PocketProfile", detail::levelPlane(30.0));
        const EntityId p0 = s.point(15.0, 10.0);
        const EntityId p1 = s.point(55.0, 10.0);
        const EntityId p2 = s.point(55.0, 40.0);
        const EntityId p3 = s.point(15.0, 40.0);
        const EntityId bottom = s.line(p0, p1);
        const EntityId right = s.line(p1, p2);
        const EntityId top = s.line(p2, p3);
        const EntityId left = s.line(p3, p0);
        s.fixed(p0);
        s.horizontal(bottom);
        s.vertical(right);
        s.horizontal(top);
        s.vertical(left);
        s.distance(p0, p1, 40.0);
        s.distance(p0, p3, 30.0);
        s.attach(PlaneReference{.object = m.block,
                                .face = FaceSelector{.role = FaceRole::EndCap}});
        m.pocketSketch = s.finish();
    }
    m.pocket = b.feature<features::ExtrudeFeature>(
        "Pocket", {.profile = sketchId(m.pocketSketch),
                   .depth = 12_mm,
                   .depthParameter = m.pocketDepth,
                   .direction = features::ExtrudeDirection::Reversed,
                   .operation = features::FeatureOperation::Cut,
                   .target = featureId(m.block)});

    // The step in the floor of the pocket: 20 x 15, deeper still, and off
    // centre WITHIN the pocket so the section is asymmetric twice over.
    {
        detail::SketchBuilder s(b, "StepProfile", detail::levelPlane(18.0));
        const EntityId p0 = s.point(20.0, 15.0);
        const EntityId p1 = s.point(40.0, 15.0);
        const EntityId p2 = s.point(40.0, 30.0);
        const EntityId p3 = s.point(20.0, 30.0);
        const EntityId bottom = s.line(p0, p1);
        const EntityId right = s.line(p1, p2);
        const EntityId top = s.line(p2, p3);
        const EntityId left = s.line(p3, p0);
        s.fixed(p0);
        s.horizontal(bottom);
        s.vertical(right);
        s.horizontal(top);
        s.vertical(left);
        s.distance(p0, p1, 20.0);
        s.distance(p0, p3, 15.0);
        m.stepSketch = s.finish();
    }
    m.step = b.feature<features::ExtrudeFeature>(
        "Step", {.profile = sketchId(m.stepSketch),
                 .depth = 8_mm,
                 .depthParameter = m.stepDepth,
                 .direction = features::ExtrudeDirection::Reversed,
                 .operation = features::FeatureOperation::Cut,
                 .target = featureId(m.pocket)});

    // --- the drawing ---------------------------------------------------------
    detail::DrawingBuilder d(b);

    m.sheet = d.sheet("Sheet1", drawing::SheetDefinition{.format = drawing::SheetFormat::A3,
                                                         .scale = {1, 1}});

    m.front = d.view("Front", drawing::ViewDefinition{.sheet = m.sheet,
                                                      .source = ObjectReference{m.step},
                                                      .orientation = drawing::StandardView::Front,
                                                      .placement = Point2D{130_mm, 210_mm}});
    // The cut runs across the block at y = 25, which passes through BOTH the
    // pocket and the step in its floor -- so the section shows the full
    // internal profile and not just the outer one.
    m.section = d.view(
        "SectionAA",
        drawing::ViewDefinition{.kind = drawing::ViewKind::Section,
                                .sheet = m.sheet,
                                .parent = m.front,
                                .section = drawing::CuttingPlane{.origin = detail::pointMm(40.0, 25.0, 15.0),
                                                                 .normal = Direction3D::unitY()},
                                .spacing = 90_mm});
    // A detail of the step's corner, where the two depths meet.
    m.detail = d.view("DetailB",
                      drawing::ViewDefinition{.kind = drawing::ViewKind::Detail,
                                              .sheet = m.sheet,
                                              .parent = m.front,
                                              .detail = drawing::DetailRegion{
                                                  .centre = Point2D{130_mm, 210_mm},
                                                  .radius = 25_mm},
                                              .scale = drawing::DrawingScale{2, 1},
                                              .placement = Point2D{320_mm, 210_mm}});

    const auto side = [&](std::size_t line) {
        return drawing::DimensionTarget{
            .plane = PlaneReference{
                .object = m.block,
                .face = FaceSelector{.role = FaceRole::Side, .entity = m.blockLines[line]}}};
    };
    const auto cap = [&](FaceRole role) {
        return drawing::DimensionTarget{
            .plane = PlaneReference{.object = m.block, .face = FaceSelector{.role = role}}};
    };

    // 80.000 mm across the block, measured ON the section.
    m.across = d.dimension("Across",
                           drawing::DimensionDefinition{.view = m.section,
                                                        .from = side(3),
                                                        .to = side(1),
                                                        .placement = Point2D{130_mm, 90_mm}});
    // 30.000 mm tall.
    m.tall = d.dimension("Tall", drawing::DimensionDefinition{.view = m.front,
                                                              .from = cap(FaceRole::StartCap),
                                                              .to = cap(FaceRole::EndCap),
                                                              .placement = Point2D{40_mm, 250_mm}});

    m.note = d.annotation("SectionNote",
                          drawing::AnnotationDefinition{.view = m.section,
                                                        .type = drawing::AnnotationType::Note,
                                                        .text = "SECTION A-A",
                                                        .placement = Point2D{130_mm, 60_mm}});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
