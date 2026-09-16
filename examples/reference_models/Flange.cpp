#include "BuildSupport.hpp"
#include "ReferenceModels.hpp"

#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/CircularPatternFeature.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/FilletFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>

namespace bettercad::reference {

using namespace bettercad::literals;
using detail::featureId;
using detail::pointMm;
using detail::sketchId;

Result<FlangeModel> buildFlangeReferenceModel() {
    FlangeModel m{.document = Document(detail::fixedDocumentId("5eed0000-0000-4000-8000-000000000002"), "Flange")};
    detail::ModelBuilder b(m.document);

    // Ø100 × 12 mm with a Ø30 bore and six Ø8 bolt holes on a Ø70 circle.
    m.outerRadius = b.length("outer_r", 50.0);
    m.thickness = b.length("thickness", 12.0);
    m.boreDiameter = b.length("bore_d", 30.0);
    m.boltCircleRadius = b.length("bolt_circle_r", 35.0);
    m.boltDiameter = b.length("bolt_d", 8.0);
    m.boltCount = b.count("bolt_count", 6.0);
    m.chamferSize = b.length("edge_chamfer", 1.0);
    m.filletRadius = b.length("rim_fillet_r", 2.0);

    // The disc, on the mating face z = 0, extruded up by the thickness.
    detail::SketchBuilder s(b, "DiscSketch", Frame3D::xy());
    const EntityId centre = s.point(0.0, 0.0);
    const EntityId rim = s.circle(centre, 50.0);
    s.fixed(centre);
    s.radius(rim, m.outerRadius);
    m.discSketch = s.finish();
    m.disc = b.feature<features::ExtrudeFeature>(
        "Disc", {.profile = sketchId(m.discSketch), .depth = 12_mm, .depthParameter = m.thickness});

    // The bore and one bolt hole, drilled from the mating face (the extrude's
    // start plane, so any thickness keeps them through), then the bolt hole
    // repeated around the axis. The bolt circle radius places the first hole.
    const geometry::FaceSignature matingFace =
        geometry::planeSignature(pointMm(0.0, 0.0, 0.0), Direction3D::unitZ().reversed());
    m.bore = b.feature<features::HoleFeature>(
        "Bore", {.target = featureId(m.disc), .face = matingFace, .diameter = 30_mm, .diameterParameter = m.boreDiameter});
    m.boltHole = b.feature<features::HoleFeature>("BoltHole", {.target = featureId(m.bore),
                                                               .face = matingFace,
                                                               .center = Point2D{35_mm, 0_mm},
                                                               .centerUParameter = m.boltCircleRadius,
                                                               .diameter = 8_mm,
                                                               .diameterParameter = m.boltDiameter});
    m.boltCircle = b.feature<features::CircularPatternFeature>(
        "BoltCircle", {.source = featureId(m.boltHole),
                       .axis = {.origin = pointMm(0.0, 0.0, 0.0), .direction = {0.0, 0.0, 1.0}},
                       .count = 6,
                       .countParameter = m.boltCount});

    // Break the top edges of the rim and the bore; round the rim's bottom
    // edge.
    const auto circle = [&](double zMm, double radiusMm, std::string_view what) {
        return b.need(geometry::circleSignature(pointMm(0.0, 0.0, zMm), Direction3D::unitZ(), radiusMm * units::mm),
                      what);
    };
    m.edgeChamfers = b.feature<features::ChamferFeature>(
        "EdgeChamfers", {.target = featureId(m.boltCircle),
                         .edges = {circle(12.0, 50.0, "top rim"), circle(12.0, 15.0, "bore edge")},
                         .distance = 1_mm,
                         .distanceParameter = m.chamferSize});
    m.rimFillet = b.feature<features::FilletFeature>("RimFillet", {.target = featureId(m.edgeChamfers),
                                                                   .edges = {circle(0.0, 50.0, "bottom rim")},
                                                                   .radius = 2_mm,
                                                                   .radiusParameter = m.filletRadius});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
