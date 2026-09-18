#include "BuildSupport.hpp"
#include "ReferenceModels.hpp"

#include <bettercad/core/standards/MetricThreads.hpp>
#include <bettercad/features/Datums.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/RibFeature.hpp>
#include <bettercad/features/VariableFilletFeature.hpp>

namespace bettercad::reference {

using namespace bettercad::literals;
using detail::featureId;
using detail::pointMm;
using detail::sketchId;

Result<RibbedBracketModel> buildRibbedBracketReferenceModel() {
    RibbedBracketModel m{
        .document = Document(detail::fixedDocumentId("5eed0000-0000-4000-8000-000000000106"), "RibbedBracket")};
    detail::ModelBuilder b(m.document);

    // An angle bracket: a base and an upright joined, stiffened by a rib,
    // bolted through a standard clearance hole, with one corner rounded by a
    // fillet whose radius varies up the edge.
    m.width = b.length("width", 90.0);
    m.baseDepth = b.length("base_depth", 70.0);
    m.thickness = b.length("thickness", 10.0);
    m.wallHeight = b.length("wall_h", 65.0);
    m.ribThickness = b.length("rib_t", 6.0);
    m.ribReach = b.length("rib_reach", 40.0);
    m.ribToe = b.length("rib_toe", 50.0);
    m.cornerBottom = b.length("corner_bottom_r", 2.0);
    m.cornerTop = b.length("corner_top_r", 4.0);
    const auto equation = [&](ParameterId parameter, const char* text) {
        b.need(m.document.setParameterExpression(parameter, text), text);
    };
    equation(m.ribThickness, "thickness * 3 / 5");
    equation(m.ribReach, "wall_h - 25 mm");
    equation(m.ribToe, "base_depth - 20 mm");
    equation(m.cornerTop, "thickness * 2 / 5");
    m.halfWidth = b.length("half_width", 45.0);
    equation(m.halfWidth, "width / 2");

    // Everything is measured from a coordinate system of the model's own,
    // so the whole bracket can be moved by moving one datum
    // (P12-DATUM-001).
    m.frame = b.feature<features::CoordinateSystem>(
        "MountFrame", {.kind = features::CoordinateSystemKind::Offset, .translation = {0_mm, 0_mm, 0_mm}});

    // The base, on the mount frame's XY plane -- ATTACHED to it, not merely
    // drawn on a frame that happens to coincide with it. The sketches and
    // the rib's datum all hang off the frame, so the solid geometry follows
    // it; the bolt hole and the fillet are placed by GEOMETRIC signatures
    // and do not, which
    // ReferenceModel_RibbedBracketIsBuiltOnItsFrame records exactly.
    detail::SketchBuilder base(b, "BaseSketch", Frame3D::xy());
    base.attach(PlaneReference{.object = m.frame, .plane = PrincipalPlane::XY});
    const EntityId origin = base.point(0.0, 0.0);
    const EntityId alongX = base.point(90.0, 0.0);
    const EntityId corner = base.point(90.0, 70.0);
    const EntityId alongY = base.point(0.0, 70.0);
    const EntityId front = base.line(origin, alongX);
    const EntityId right = base.line(alongX, corner);
    const EntityId back = base.line(corner, alongY);
    const EntityId left = base.line(alongY, origin);
    base.fixed(origin);
    base.horizontal(front);
    base.horizontal(back);
    base.vertical(right);
    base.vertical(left);
    base.length(front, m.width);
    base.length(right, m.baseDepth);
    m.baseSketch = base.finish();
    m.base = b.feature<features::ExtrudeFeature>(
        "Base", {.profile = sketchId(m.baseSketch), .depth = 10_mm, .depthParameter = m.thickness});

    // The upright, standing on the base's back edge and joined to it.
    detail::SketchBuilder wall(b, "WallSketch", Frame3D::xz());
    wall.attach(PlaneReference{.object = m.frame, .plane = PrincipalPlane::XZ});
    const EntityId wallOrigin = wall.point(0.0, 0.0);
    const EntityId wallRight = wall.point(90.0, 0.0);
    const EntityId wallTop = wall.point(90.0, 65.0);
    const EntityId wallLeft = wall.point(0.0, 65.0);
    const EntityId wallBottom = wall.line(wallOrigin, wallRight);
    const EntityId wallSide = wall.line(wallRight, wallTop);
    const EntityId wallCap = wall.line(wallTop, wallLeft);
    const EntityId wallBack = wall.line(wallLeft, wallOrigin);
    wall.fixed(wallOrigin);
    wall.horizontal(wallBottom);
    wall.horizontal(wallCap);
    wall.vertical(wallSide);
    wall.vertical(wallBack);
    wall.length(wallBottom, m.width);
    wall.length(wallSide, m.wallHeight);
    m.wallSketch = wall.finish();
    m.wall = b.feature<features::ExtrudeFeature>("Wall", {.profile = sketchId(m.wallSketch),
                                                          .depth = 10_mm,
                                                          .depthParameter = m.thickness,
                                                          .operation = features::FeatureOperation::Join,
                                                          .target = featureId(m.base)});

    // The rib: one straight line across the inside corner, in a plane
    // ACROSS the wall (its local u is Y, its v is Z) halfway along the
    // bracket. A straight profile makes the stiffener a triangular prism, so
    // its volume is exact (P12-FEAT-005). Its ends sit on the wall's inner
    // face and the base's top face, both an equation away, so the gusset
    // follows the bracket rather than floating at fixed coordinates.
    auto acrossPlane = Frame3D::create(pointMm(45.0, 0.0, 0.0), Direction3D::unitX(), Direction3D::unitY());
    if (!acrossPlane) {
        return std::unexpected(acrossPlane.error());
    }
    const Frame3D& across = *acrossPlane;
    // Halfway along the bracket BY EQUATION: a datum at width/2, not a frame
    // frozen at x = 45, so the rib stays central at any width.
    m.ribPlane = b.feature<features::DatumPlane>("RibPlane",
                                                 {.kind = features::DatumPlaneKind::Offset,
                                                  .base = PlaneReference{.object = m.frame,
                                                                         .plane = PrincipalPlane::YZ},
                                                  .offset = 45_mm,
                                                  .offsetParameter = m.halfWidth});
    detail::SketchBuilder gusset(b, "GussetSketch", across);
    gusset.attach(PlaneReference{.object = m.ribPlane});
    const EntityId gussetOrigin = gusset.point(0.0, 0.0);
    const EntityId onBase = gusset.point(0.0, 10.0);
    const EntityId ribTop = gusset.point(0.0, 40.0);
    const EntityId ribToe = gusset.point(50.0, 10.0);
    const EntityId slope = gusset.line(ribTop, ribToe);
    gusset.fixed(gussetOrigin);
    // The gusset starts ON the wall's face (the plane's origin lies in it)
    // and ends ON the base's top face, so the triangle it fills is bounded
    // by the two of them and nothing has to be extended.
    gusset.vertical(gussetOrigin, ribTop);
    gusset.distance(gussetOrigin, ribTop, m.ribReach);
    gusset.vertical(gussetOrigin, onBase);
    gusset.distance(gussetOrigin, onBase, m.thickness);
    gusset.horizontal(onBase, ribToe);
    gusset.distance(onBase, ribToe, m.ribToe);
    m.gussetSketch = gusset.finish();
    m.rib = b.feature<features::RibFeature>("Rib", {.target = featureId(m.wall),
                                                    .profile = sketchId(m.gussetSketch),
                                                    .edges = {slope},
                                                    .thickness = 6_mm,
                                                    .thicknessParameter = m.ribThickness,
                                                    .placement = geometry::RibPlacement::Symmetric,
                                                    .flipped = true});

    // A clearance hole for an M8 bolt, of the medium series: the diameter
    // comes from ISO 273, not from a number here (P12-HOLE-001).
    auto bolt = standards::parseMetricThread("M8");
    if (!bolt) {
        return std::unexpected(bolt.error());
    }
    m.boltHole = b.feature<features::HoleFeature>(
        "BoltHole",
        {.target = featureId(m.rib),
         .face = geometry::planeSignature(pointMm(0.0, 0.0, 0.0), Direction3D::unitZ().reversed()),
         .center = Point2D{20_mm, 20_mm},
         .clearance = features::HoleClearance{.bolt = *bolt,
                                              .series = standards::ClearanceSeries::Medium}});

    // One vertical corner of the base rounded by a fillet that grows up the
    // edge: 2 mm at the bottom to two fifths of the thickness at the top
    // (P12-FEAT-006).
    m.cornerRound = b.feature<features::VariableFilletFeature>(
        "CornerRound",
        {.target = featureId(m.boltHole),
         .edges = {{.edge = geometry::lineSignature(pointMm(0.0, 0.0, 0.0), Direction3D::unitZ()),
                    .stations = {{.position = 0.0, .radius = 2_mm, .radiusParameter = m.cornerBottom},
                                 {.position = 1.0, .radius = 4_mm, .radiusParameter = m.cornerTop}}}}});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
