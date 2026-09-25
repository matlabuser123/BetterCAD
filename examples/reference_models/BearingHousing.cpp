#include "BuildSupport.hpp"
#include "ReferenceModels.hpp"

#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/FilletFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/MirrorFeature.hpp>

namespace bettercad::reference {

using namespace bettercad::literals;
using detail::featureId;
using detail::pointMm;
using detail::sketchId;

Result<BearingHousingModel> buildBearingHousingReferenceModel() {
    BearingHousingModel m{
        .document = Document(detail::fixedDocumentId("5eed0000-0000-4000-8000-000000000004"), "BearingHousing")};
    detail::ModelBuilder b(m.document);

    // A pillow block: a 120 × 60 × 12 mm base carrying a Ø70 boss whose Ø40
    // bearing bore is 45 mm above the base, with four Ø10 mounting holes.
    // Half of it is drawn and mirrored, so the two halves cannot drift apart.
    m.baseHalfLength = b.length("base_half_length", 60.0);
    m.baseWidth = b.length("base_width", 60.0);
    m.baseThickness = b.length("base_t", 12.0);
    m.bossRadius = b.length("boss_r", 35.0);
    m.axisHeight = b.length("axis_height", 45.0);
    m.boreRadius = b.length("bore_r", 20.0);
    m.mountDiameter = b.length("mount_d", 10.0);
    m.mountX = b.length("mount_x", 48.0);
    m.mountY = b.length("mount_y", 15.0);
    m.mountPitchX = b.length("mount_pitch_x", 96.0);
    m.mountPitchY = b.length("mount_pitch_y", 30.0);
    m.filletRadius = b.length("boss_fillet_r", 3.0);
    m.chamferSize = b.length("base_chamfer", 1.5);

    // The base's half section in the XZ plane, extruded both ways along Y so
    // that the part stays symmetric about y = 0 whatever its width.
    detail::SketchBuilder base(b, "BaseSection", Frame3D::xz());
    const EntityId b0 = base.point(0.0, 0.0);
    const EntityId b1 = base.point(60.0, 0.0);
    const EntityId b2 = base.point(60.0, 12.0);
    const EntityId b3 = base.point(0.0, 12.0);
    const EntityId bottom = base.line(b0, b1);
    const EntityId end = base.line(b1, b2);
    const EntityId top = base.line(b2, b3);
    const EntityId middle = base.line(b3, b0);
    base.fixed(b0);
    base.horizontal(bottom);
    base.vertical(end);
    base.horizontal(top);
    base.vertical(middle);
    base.length(bottom, m.baseHalfLength);
    base.length(end, m.baseThickness);
    m.baseSection = base.finish();
    m.base = b.feature<features::ExtrudeFeature>("Base",
                                                 {.profile = sketchId(m.baseSection),
                                                  .depth = 60_mm,
                                                  .depthParameter = m.baseWidth,
                                                  .direction = features::ExtrudeDirection::Symmetric});

    // The boss: half an arched housing over the bearing axis. The bore is
    // cut after the halves are joined, so that no half cylinder lies on the
    // mirror plane: uniting two halves that meet there is refused (see
    // docs/verification/P11-REF-001).
    detail::SketchBuilder boss(b, "BossSection", Frame3D::xz());
    const EntityId q0 = boss.point(0.0, 0.0);
    const EntityId q1 = boss.point(35.0, 0.0);
    const EntityId q2 = boss.point(35.0, 45.0);
    const EntityId q3 = boss.point(0.0, 80.0);
    const EntityId centre = boss.point(0.0, 45.0);
    const EntityId bossBottom = boss.line(q0, q1);
    const EntityId bossSide = boss.line(q1, q2);
    const EntityId crown = boss.arc(centre, q2, q3);
    const EntityId bossMiddle = boss.line(q3, q0);
    boss.fixed(q0);
    boss.horizontal(bossBottom);
    boss.vertical(bossSide);
    boss.vertical(bossMiddle);
    boss.vertical(q0, centre);
    boss.distance(q0, centre, m.axisHeight);
    boss.horizontal(centre, q2);
    boss.radius(crown, m.bossRadius);
    m.bossSection = boss.finish();
    m.boss = b.feature<features::ExtrudeFeature>("Boss",
                                                 {.profile = sketchId(m.bossSection),
                                                  .depth = 60_mm,
                                                  .depthParameter = m.baseWidth,
                                                  .direction = features::ExtrudeDirection::Symmetric,
                                                  .operation = features::FeatureOperation::Join,
                                                  .target = featureId(m.base)});
    m.housing = b.feature<features::MirrorFeature>("Housing",
                                                   {.source = featureId(m.boss),
                                                    .plane = {.origin = pointMm(0.0, 0.0, 0.0),
                                                              .normal = {1.0, 0.0, 0.0}},
                                                    .scope = features::MirrorScope::Body});

    // The bearing bore, cut right through the joined housing. An extrude has
    // no "through all" mode, so the cutter is as deep as the housing is wide
    // and driven by the same parameter.
    detail::SketchBuilder bore(b, "BoreSection", Frame3D::xz());
    const EntityId boreOrigin = bore.point(0.0, 0.0);
    const EntityId boreCentre = bore.point(0.0, 45.0);
    const EntityId boreCircle = bore.circle(boreCentre, 20.0);
    bore.fixed(boreOrigin);
    bore.vertical(boreOrigin, boreCentre);
    bore.distance(boreOrigin, boreCentre, m.axisHeight);
    bore.radius(boreCircle, m.boreRadius);
    m.boreSection = bore.finish();
    m.bore = b.feature<features::ExtrudeFeature>("Bore", {.profile = sketchId(m.boreSection),
                                                          .depth = 60_mm,
                                                          .depthParameter = m.baseWidth,
                                                          .direction = features::ExtrudeDirection::Symmetric,
                                                          .operation = features::FeatureOperation::Cut,
                                                          .target = featureId(m.housing)});

    // Four mounting holes through the base: one drilled, then a 2 × 2 grid.
    m.mountHole = b.feature<features::HoleFeature>(
        "MountHole", {.target = featureId(m.bore),
                      .face = geometry::planeSignature(pointMm(0.0, 0.0, 12.0), Direction3D::unitZ()),
                      .center = Point2D{48_mm, 15_mm},
                      .centerUParameter = m.mountX,
                      .centerVParameter = m.mountY,
                      .diameter = 10_mm,
                      .diameterParameter = m.mountDiameter});
    m.mountHoles = b.feature<features::LinearPatternFeature>(
        "MountHoles", {.source = featureId(m.mountHole),
                       .first = {.direction = {-1.0, 0.0, 0.0},
                                 .count = 2,
                                 .spacing = 96_mm,
                                 .spacingParameter = m.mountPitchX},
                       .second = features::PatternDirection{.direction = {0.0, -1.0, 0.0},
                                                            .count = 2,
                                                            .spacing = 30_mm,
                                                            .spacingParameter = m.mountPitchY}});

    // Round where the boss meets the base and break the base's top edges.
    const auto alongWidth = [&](double xMm) {
        return geometry::lineSignature(pointMm(xMm, 0.0, 12.0), Direction3D::unitY());
    };
    m.bossFillets = b.feature<features::FilletFeature>("BossFillets", {.target = featureId(m.mountHoles),
                                                                       .edges = {alongWidth(35.0), alongWidth(-35.0)},
                                                                       .radius = 3_mm,
                                                                       .radiusParameter = m.filletRadius});
    m.baseChamfers = b.feature<features::ChamferFeature>("BaseChamfers",
                                                         {.target = featureId(m.bossFillets),
                                                          .edges = { features::ChamferEdge{alongWidth(60.0)}, features::ChamferEdge{alongWidth(-60.0)}},
                                                          .distance = 1.5_mm,
                                                          .distanceParameter = m.chamferSize});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
