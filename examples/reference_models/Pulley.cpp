#include "BuildSupport.hpp"
#include "ReferenceModels.hpp"

#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/FilletFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/RevolveFeature.hpp>

namespace bettercad::reference {

using namespace bettercad::literals;
using detail::featureId;
using detail::pointMm;
using detail::sketchId;

Result<PulleyModel> buildPulleyReferenceModel() {
    PulleyModel m{.document = Document(detail::fixedDocumentId("5eed0000-0000-4000-8000-000000000003"), "Pulley")};
    detail::ModelBuilder b(m.document);

    // A Ø120 × 30 mm rim on a 10 mm web, a Ø50 hub 40 mm long that projects
    // past the rim on one side, a Ø20 bore and a V-groove 6 mm deep around
    // the rim.
    m.outerRadius = b.length("outer_r", 60.0);
    m.rimInnerRadius = b.length("rim_inner_r", 50.0);
    m.rimWidth = b.length("rim_width", 30.0);
    m.hubRadius = b.length("hub_r", 25.0);
    m.hubLength = b.length("hub_length", 40.0);
    m.webStart = b.length("web_z0", 10.0);
    m.webThickness = b.length("web_thickness", 10.0);
    m.boreDiameter = b.length("bore_d", 20.0);
    m.grooveCentre = b.length("groove_z", 15.0);
    m.grooveDepth = b.length("groove_depth", 6.0);
    m.grooveWidth = b.length("groove_width", 12.0);
    m.grooveBottomWidth = b.length("groove_bottom_width", 4.0);
    m.filletRadius = b.length("web_fillet_r", 3.0);
    m.chamferSize = b.length("hub_chamfer", 1.0);

    // The half section in the XZ plane (u = radius, v = height), counter-
    // clockwise from the axis: hub, web, rim, and back along the axis.
    detail::SketchBuilder s(b, "Section", Frame3D::xz());
    const EntityId a0 = s.point(0.0, 0.0);
    const EntityId a1 = s.point(25.0, 0.0);
    const EntityId a2 = s.point(25.0, 10.0);
    const EntityId a3 = s.point(50.0, 10.0);
    const EntityId a4 = s.point(50.0, 0.0);
    const EntityId a5 = s.point(60.0, 0.0);
    const EntityId a6 = s.point(60.0, 30.0);
    const EntityId a7 = s.point(50.0, 30.0);
    const EntityId a8 = s.point(50.0, 20.0);
    const EntityId a9 = s.point(25.0, 20.0);
    const EntityId a10 = s.point(25.0, 40.0);
    const EntityId a11 = s.point(0.0, 40.0);
    const EntityId hubEnd = s.line(a0, a1);
    const EntityId hubLower = s.line(a1, a2);
    const EntityId webLower = s.line(a2, a3);
    const EntityId rimInnerLower = s.line(a3, a4);
    const EntityId rimEndLower = s.line(a4, a5);
    const EntityId rimFace = s.line(a5, a6);
    const EntityId rimEndUpper = s.line(a6, a7);
    const EntityId rimInnerUpper = s.line(a7, a8);
    const EntityId webUpper = s.line(a8, a9);
    const EntityId hubUpper = s.line(a9, a10);
    const EntityId hubTop = s.line(a10, a11);
    const EntityId axis = s.line(a11, a0);
    s.fixed(a0);
    for (const EntityId line : {hubEnd, webLower, rimEndLower, rimEndUpper, webUpper, hubTop}) {
        s.horizontal(line);
    }
    for (const EntityId line : {hubLower, rimInnerLower, rimFace, rimInnerUpper, hubUpper, axis}) {
        s.vertical(line);
    }
    s.length(hubEnd, m.hubRadius);
    s.length(hubLower, m.webStart);
    s.distance(a3, axis, m.rimInnerRadius);
    s.horizontal(a0, a4); // the rim starts at the hub's end face
    s.distance(a5, axis, m.outerRadius);
    s.length(rimFace, m.rimWidth);
    s.distance(a7, axis, m.rimInnerRadius);
    s.distance(a8, webLower, m.webThickness);
    s.distance(a9, axis, m.hubRadius);
    s.length(axis, m.hubLength);
    m.section = s.finish();
    m.blank = b.feature<features::RevolveFeature>(
        "Blank", {.profile = sketchId(m.section), .axis = features::RevolveAxis::sketchY()});

    // The V-groove, turned out of the rim. Its sketch measures the depth from
    // a construction line at the outer radius, so the groove follows the
    // diameter; its mouth lies on the rim and the cut runs 3 mm past it.
    detail::SketchBuilder g(b, "GrooveSection", Frame3D::xz());
    const EntityId origin = g.point(0.0, 0.0);
    const EntityId up = g.point(0.0, 10.0);
    const EntityId across = g.point(10.0, 0.0);
    const EntityId turnAxis = g.line(origin, up);
    const EntityId datum = g.line(origin, across);
    const EntityId o1 = g.point(60.0, 0.0);
    const EntityId o2 = g.point(60.0, 10.0);
    const EntityId rimLine = g.line(o1, o2);
    g.construction(turnAxis);
    g.construction(datum);
    g.construction(rimLine);
    g.fixed(origin);
    g.fixed(up);
    g.fixed(across);
    g.horizontal(origin, o1);
    g.distance(o1, turnAxis, m.outerRadius);
    g.vertical(rimLine);
    g.distance(o1, o2, 10.0);

    const EntityId q0 = g.point(54.0, 13.0);
    const EntityId q1 = g.point(54.0, 17.0);
    const EntityId q2 = g.point(60.0, 21.0);
    const EntityId q2b = g.point(63.0, 21.0);
    const EntityId q3b = g.point(63.0, 9.0);
    const EntityId q3 = g.point(60.0, 9.0);
    const EntityId centre = g.point(54.0, 15.0);
    const EntityId grooveBottom = g.line(q0, q1);
    const EntityId upperFlank = g.line(q1, q2);
    const EntityId upperLip = g.line(q2, q2b);
    const EntityId outside = g.line(q2b, q3b);
    const EntityId lowerLip = g.line(q3b, q3);
    const EntityId lowerFlank = g.line(q3, q0);
    const EntityId lowerHalf = g.line(q0, centre);
    const EntityId upperHalf = g.line(centre, q1);
    const EntityId mouth = g.line(q3, q2);
    g.construction(lowerHalf);
    g.construction(upperHalf);
    g.construction(mouth);
    g.distance(q0, rimLine, m.grooveDepth);
    g.vertical(grooveBottom);
    g.length(grooveBottom, m.grooveBottomWidth);
    g.vertical(centre, q0);
    g.equal(lowerHalf, upperHalf);
    g.distance(centre, datum, m.grooveCentre);
    g.distance(q2, turnAxis, m.outerRadius);
    g.distance(q3, turnAxis, m.outerRadius);
    g.length(mouth, m.grooveWidth);
    g.equal(upperFlank, lowerFlank);
    g.horizontal(upperLip);
    g.horizontal(lowerLip);
    g.vertical(outside);
    g.distance(q2b, rimLine, 3.0);
    m.grooveSection = g.finish();
    m.groove = b.feature<features::RevolveFeature>("Groove", {.profile = sketchId(m.grooveSection),
                                                              .axis = features::RevolveAxis::alongLine(turnAxis),
                                                              .operation = features::FeatureOperation::Cut,
                                                              .target = featureId(m.blank)});

    // The bore, drilled through from the hub's end face, then the web
    // corners rounded and the hub's end edges chamfered.
    m.bore = b.feature<features::HoleFeature>(
        "Bore", {.target = featureId(m.groove),
                 .face = geometry::planeSignature(pointMm(0.0, 0.0, 0.0), Direction3D::unitZ().reversed()),
                 .diameter = 20_mm,
                 .diameterParameter = m.boreDiameter});
    const auto circle = [&](double zMm, double radiusMm, std::string_view what) {
        return b.need(geometry::circleSignature(pointMm(0.0, 0.0, zMm), Direction3D::unitZ(), radiusMm * units::mm),
                      what);
    };
    m.webFillets = b.feature<features::FilletFeature>(
        "WebFillets", {.target = featureId(m.bore),
                       .edges = {circle(10.0, 25.0, "hub, lower"), circle(10.0, 50.0, "rim, lower"),
                                 circle(20.0, 25.0, "hub, upper"), circle(20.0, 50.0, "rim, upper")},
                       .radius = 3_mm,
                       .radiusParameter = m.filletRadius});
    m.hubChamfers = b.feature<features::ChamferFeature>(
        "HubChamfers", {.target = featureId(m.webFillets),
                        .edges = { features::ChamferEdge{circle(0.0, 25.0, "hub end")}, features::ChamferEdge{circle(40.0, 25.0, "hub top")}},
                        .distance = 1_mm,
                        .distanceParameter = m.chamferSize});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
