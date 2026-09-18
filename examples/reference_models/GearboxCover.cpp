#include "BuildSupport.hpp"
#include "ReferenceModels.hpp"

#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/standards/MetricThreads.hpp>
#include <bettercad/features/Datums.hpp>
#include <bettercad/features/DraftFeature.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/ShellFeature.hpp>

namespace bettercad::reference {

using namespace bettercad::literals;
using detail::featureId;
using detail::pointMm;
using detail::sketchId;

Result<GearboxCoverModel> buildGearboxCoverReferenceModel() {
    GearboxCoverModel m{
        .document = Document(detail::fixedDocumentId("5eed0000-0000-4000-8000-000000000102"), "GearboxCover")};
    detail::ModelBuilder b(m.document);

    // A cast cover: a drafted box, hollowed to a wall, with a spotfaced
    // inspection port and a row of tapped fixing holes.
    m.length = b.length("length", 120.0);
    m.width = b.length("width", 80.0);
    m.height = b.length("height", 40.0);
    m.wall = b.length("wall", 4.0);
    m.draftAngle = b.need(m.document.createParameter("draft_angle", 3_deg, units::deg), "draft_angle");
    m.portDiameter = b.length("port_d", 24.0);
    m.fixingPitch = b.length("fixing_pitch", 40.0);
    m.partingOffset = b.length("parting_z", -40.0);
    const auto equation = [&](ParameterId parameter, const char* text) {
        b.need(m.document.setParameterExpression(parameter, text), text);
    };
    // The wall is a twentieth of the width, and the port a fifth of it.
    equation(m.wall, "width / 20");
    equation(m.portDiameter, "width * 0.3");
    equation(m.fixingPitch, "length / 3");
    // The cover hangs below its outside face, so the parting face is a
    // height below it. Driving the datum this way is what lets `height`
    // move at all: see the block below.
    equation(m.partingOffset, "0 mm - height");

    // The block, drawn DOWN from the outside face at z = 0.
    //
    // The direction matters, and is not a matter of taste. A hole is placed
    // on a geometric FaceSignature (a plane and a side), not on a persistent
    // topological name, so a hole's face must be a plane that does not move
    // when the part is parameterised -- otherwise the reference matches no
    // face and regeneration fails. Drawn upward from z = 0, the outside face
    // would sit at z = height and both holes would break the moment `height`
    // changed. Drawn downward, the outside face IS z = 0 for every height,
    // and the faces that move (the parting face and the four sides) are
    // reached by a datum and by named faces, which do follow.
    detail::SketchBuilder box(b, "BoxSketch", Frame3D::xy());
    const EntityId origin = box.point(0.0, 0.0);
    const EntityId alongX = box.point(120.0, 0.0);
    const EntityId corner = box.point(120.0, 80.0);
    const EntityId alongY = box.point(0.0, 80.0);
    const EntityId bottom = box.line(origin, alongX);
    const EntityId right = box.line(alongX, corner);
    const EntityId top = box.line(corner, alongY);
    const EntityId left = box.line(alongY, origin);
    box.fixed(origin);
    box.horizontal(bottom);
    box.horizontal(top);
    box.vertical(right);
    box.vertical(left);
    box.length(bottom, m.length);
    box.length(right, m.width);
    m.boxSketch = box.finish();
    m.block = b.feature<features::ExtrudeFeature>(
        "Block", {.profile = sketchId(m.boxSketch),
                  .depth = 40_mm,
                  .depthParameter = m.height,
                  .direction = features::ExtrudeDirection::Reversed});

    // A datum plane on the parting face -- the open rim, a height below the
    // outside face -- which the draft pulls about: the mould opens along its
    // normal (P12-DATUM-001). Driven by `parting_z`, so it follows `height`.
    m.partingPlane = b.feature<features::DatumPlane>(
        "PartingPlane", {.kind = features::DatumPlaneKind::Offset,
                         .base = PlaneReference{.plane = PrincipalPlane::XY},
                         .offset = -40_mm,
                         .offsetParameter = m.partingOffset});

    // Draft the four sides about the parting plane, so the cover leaves the
    // mould. The sides are named faces of the extrude (P12-STREF-001).
    std::vector<FaceName> sides;
    for (const EntityId side : {bottom, right, top, left}) {
        sides.push_back(FaceName{m.block, {.role = FaceRole::Side, .entity = side}});
    }
    m.draft = b.feature<features::DraftFeature>(
        "SideDraft", {.target = featureId(m.block),
                      .faces = std::move(sides),
                      .neutralPlane = PlaneReference{.object = m.partingPlane},
                      .angle = 3_deg,
                      .angleParameter = m.draftAngle});

    // Hollow it from the parting face, leaving a wall. The open face is a
    // NAMED face of the extrude (the far cap, at the parting face), so it
    // follows the block however tall it becomes.
    m.shell = b.feature<features::ShellFeature>(
        "Hollow", {.target = featureId(m.draft),
                   .openFaces = {FaceName{m.block, {.role = FaceRole::EndCap}}},
                   .thickness = 4_mm,
                   .thicknessParameter = m.wall});

    // A spotfaced inspection port through the top, and a row of M6 holes
    // tapped through the wall (P12-HOLE-001). Both are drilled from the
    // cover's outside face, z = 0 for every height, which the draft and the
    // shell both leave in place.
    const geometry::FaceSignature outside =
        geometry::planeSignature(pointMm(0.0, 0.0, 0.0), Direction3D::unitZ());
    m.port = b.feature<features::HoleFeature>("InspectionPort", {.target = featureId(m.shell),
                                                                 .face = outside,
                                                                 .center = Point2D{60_mm, 40_mm},
                                                                 .type = geometry::HoleType::Spotface,
                                                                 .diameter = 24_mm,
                                                                 .diameterParameter = m.portDiameter,
                                                                 .spotfaceDiameter = 36_mm,
                                                                 .spotfaceDepth = 1.5_mm});
    // The thread is stored as its designation, not as dimensions
    // (P12-HOLE-001); the bore is cut at its basic minor diameter.
    auto thread = standards::parseMetricThread("M6");
    if (!thread) {
        return std::unexpected(thread.error());
    }
    m.fixingHole = b.feature<features::HoleFeature>(
        "FixingHole", {.target = featureId(m.port),
                       .face = outside,
                       .center = Point2D{20_mm, 10_mm},
                       .extent = geometry::HoleExtent::Through,
                       .thread = features::HoleThread{.size = *thread}});
    m.fixingHoles = b.feature<features::LinearPatternFeature>(
        "FixingHoles", {.source = featureId(m.fixingHole),
                        .first = {.direction = {1.0, 0.0, 0.0}, .count = 3, .spacingParameter = m.fixingPitch}});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
