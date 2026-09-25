#include "BuildSupport.hpp"
#include "ReferenceModels.hpp"

#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/SweepFeature.hpp>

namespace bettercad::reference {

using namespace bettercad::literals;
using detail::featureId;
using detail::pointMm;
using detail::sketchId;

Result<UBoltModel> buildUBoltReferenceModel() {
    UBoltModel m{.document = Document(detail::fixedDocumentId("5eed0000-0000-4000-8000-000000000006"), "UBolt")};
    detail::ModelBuilder b(m.document);

    // A Ø10 rod bent into a U: two legs leg long joined by a half turn of
    // radius bend_r, with both ends chamfered.
    m.rodRadius = b.length("rod_r", 5.0);
    m.legLength = b.length("leg", 60.0);
    m.bendRadius = b.length("bend_r", 25.0);
    m.chamferSize = b.length("end_chamfer", 1.0);

    // The rod's section, on the plane where the path starts.
    detail::SketchBuilder rod(b, "RodSection", Frame3D::xy());
    const EntityId centre = rod.point(0.0, 0.0);
    const EntityId section = rod.circle(centre, 5.0);
    rod.fixed(centre);
    rod.radius(section, m.rodRadius);
    m.rodSection = rod.finish();

    // The path, in the XZ plane: down one leg, round the bend and up the
    // other. It starts at the origin, where the section lies, and the legs
    // grow downwards, so the chamfered ends stay put whatever the length.
    detail::SketchBuilder route(b, "Route", Frame3D::xz());
    const EntityId a = route.point(0.0, 0.0);
    const EntityId bottomLeft = route.point(0.0, -60.0);
    const EntityId bendCentre = route.point(25.0, -60.0);
    const EntityId bottomRight = route.point(50.0, -60.0);
    const EntityId e = route.point(50.0, 0.0);
    const EntityId firstLeg = route.line(a, bottomLeft);
    const EntityId bend = route.arc(bendCentre, bottomLeft, bottomRight);
    const EntityId secondLeg = route.line(bottomRight, e);
    route.fixed(a);
    route.vertical(firstLeg);
    route.length(firstLeg, m.legLength);
    route.horizontal(bottomLeft, bendCentre);
    route.radius(bend, m.bendRadius);
    route.horizontal(bendCentre, bottomRight);
    route.vertical(secondLeg);
    route.horizontal(a, e);
    m.route = route.finish();

    m.rod = b.feature<features::SweepFeature>(
        "Rod", {.profile = sketchId(m.rodSection),
                .path = {.sketch = sketchId(m.route), .edges = {firstLeg, bend, secondLeg}}});

    const auto end = [&](double xMm, std::string_view what) {
        return b.need(geometry::circleSignature(pointMm(xMm, 0.0, 0.0), Direction3D::unitZ(),
                                                b.millimetres(m.rodRadius) * units::mm),
                      what);
    };
    m.endChamfers = b.feature<features::ChamferFeature>(
        "EndChamfers", {.target = featureId(m.rod),
                        .edges = { features::ChamferEdge{end(0.0, "first end")}, features::ChamferEdge{end(2.0 * b.millimetres(m.bendRadius), "second end")}},
                        .distance = 1_mm,
                        .distanceParameter = m.chamferSize});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
