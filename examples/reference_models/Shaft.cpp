#include "BuildSupport.hpp"
#include "ReferenceModels.hpp"

#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/FilletFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/MirrorFeature.hpp>
#include <bettercad/features/RevolveFeature.hpp>

namespace bettercad::reference {

using namespace bettercad::literals;
using detail::featureId;
using detail::pointMm;
using detail::sketchId;

Result<ShaftModel> buildShaftReferenceModel() {
    ShaftModel m{.document = Document(detail::fixedDocumentId("5eed0000-0000-4000-8000-000000000001"), "Shaft")};
    detail::ModelBuilder b(m.document);

    // Ø30 × 40, Ø40 × 40 and Ø25 × 40 mm: radii and lengths. The overall
    // length is twice half_length, which also places the tail centre hole;
    // the Ø25 segment takes what l1 and l2 leave.
    m.r1 = b.length("r1", 15.0);
    m.l1 = b.length("l1", 40.0);
    m.r2 = b.length("r2", 20.0);
    m.l2 = b.length("l2", 40.0);
    m.r3 = b.length("r3", 12.5);
    m.halfLength = b.length("half_length", 60.0);
    m.drillDiameter = b.length("centre_drill_d", 4.0);
    m.drillDepth = b.length("centre_drill_depth", 10.0);
    m.filletRadius = b.length("shoulder_fillet_r", 1.5);
    m.chamferSize = b.length("end_chamfer", 1.0);

    // Half section in the XZ plane (u = radius along X, v = height along Z),
    // counter-clockwise from the axis at z = 0. The edge back down the axis
    // is part of the profile, and the revolve turns it about that axis.
    detail::SketchBuilder s(b, "Profile", Frame3D::xz());
    const EntityId p0 = s.point(0.0, 0.0);
    const EntityId p1 = s.point(15.0, 0.0);
    const EntityId p2 = s.point(15.0, 40.0);
    const EntityId p3 = s.point(20.0, 40.0);
    const EntityId p4 = s.point(20.0, 80.0);
    const EntityId p5 = s.point(12.5, 80.0);
    const EntityId p6 = s.point(12.5, 120.0);
    const EntityId p7 = s.point(0.0, 120.0);
    const EntityId middle = s.point(0.0, 60.0); // on the axis, half way
    const EntityId end0 = s.line(p0, p1);
    const EntityId journal = s.line(p1, p2);
    const EntityId shoulder1 = s.line(p2, p3);
    const EntityId collar = s.line(p3, p4);
    const EntityId shoulder2 = s.line(p4, p5);
    const EntityId tail = s.line(p5, p6);
    const EntityId end1 = s.line(p6, p7);
    const EntityId axis = s.line(p7, p0);
    s.fixed(p0);
    for (const EntityId line : {end0, shoulder1, shoulder2, end1}) {
        s.horizontal(line);
    }
    for (const EntityId line : {journal, collar, tail, axis}) {
        s.vertical(line);
    }
    s.length(end0, m.r1);
    s.length(journal, m.l1);
    s.distance(p3, axis, m.r2);
    s.length(collar, m.l2);
    s.length(end1, m.r3);
    s.vertical(p0, middle);
    s.distance(p0, middle, m.halfLength);
    s.distance(middle, p7, m.halfLength);
    m.profile = s.finish();

    m.turn = b.feature<features::RevolveFeature>(
        "Turn", {.profile = sketchId(m.profile), .axis = features::RevolveAxis::sketchY()});

    // A DIN 332 style centre hole in the z = 0 end: Ø4 × 10 mm with a 60°
    // countersink of Ø8.5 mm. The tail's hole is its mirror image across the
    // plane half way along the shaft, so it follows the shaft's length.
    m.centreDrill = b.feature<features::HoleFeature>(
        "CentreDrill", {.target = featureId(m.turn),
                        .face = geometry::planeSignature(pointMm(0.0, 0.0, 0.0), Direction3D::unitZ().reversed()),
                        .type = geometry::HoleType::Countersink,
                        .extent = geometry::HoleExtent::Blind,
                        .diameter = 4_mm,
                        .diameterParameter = m.drillDiameter,
                        .depth = 10_mm,
                        .depthParameter = m.drillDepth,
                        .countersinkDiameter = 8.5_mm,
                        .countersinkAngle = 60_deg});
    m.tailCentreDrill = b.feature<features::MirrorFeature>(
        "TailCentreDrill", {.source = featureId(m.centreDrill),
                            .plane = {.origin = pointMm(0.0, 0.0, 0.0),
                                      .normal = {0.0, 0.0, 1.0},
                                      .offset = 60_mm,
                                      .offsetParameter = m.halfLength}});

    // Round the two inside shoulder corners, where the smaller diameters meet
    // the collar, and chamfer both ends. Edges are referred to by their
    // circles (see geometry::EdgeSignature).
    const auto rim = [&](double zMm, double radiusMm, std::string_view what) {
        return b.need(geometry::circleSignature(pointMm(0.0, 0.0, zMm), Direction3D::unitZ(), radiusMm * units::mm),
                      what);
    };
    m.shoulderFillets = b.feature<features::FilletFeature>(
        "ShoulderFillets", {.target = featureId(m.tailCentreDrill),
                            .edges = {rim(40.0, 15.0, "journal shoulder"), rim(80.0, 12.5, "tail shoulder")},
                            .radius = 1.5_mm,
                            .radiusParameter = m.filletRadius});
    m.endChamfers = b.feature<features::ChamferFeature>(
        "EndChamfers", {.target = featureId(m.shoulderFillets),
                        .edges = { features::ChamferEdge{rim(0.0, 15.0, "drive end")}, features::ChamferEdge{rim(120.0, 12.5, "tail end")}},
                        .distance = 1_mm,
                        .distanceParameter = m.chamferSize});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
