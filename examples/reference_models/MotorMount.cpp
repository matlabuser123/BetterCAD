#include "BuildSupport.hpp"
#include "ReferenceModels.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>

namespace bettercad::reference {

using namespace bettercad::literals;
using detail::featureId;
using detail::pointMm;
using detail::sketchId;

Result<MotorMountModel> buildMotorMountReferenceModel() {
    MotorMountModel m{
        .document = Document(detail::fixedDocumentId("5eed0000-0000-4000-8000-000000000101"), "MotorMount")};
    detail::ModelBuilder b(m.document);

    // One free dimension; everything else is an equation on it. The
    // configurations set `width` alone, so the plate, the bolt pattern and
    // the hole sizes all follow (P12-PARAM-001, P12-PARAM-002).
    m.width = b.length("width", 120.0);
    m.height = b.length("height", 60.0);
    m.thickness = b.length("thickness", 8.0);
    m.boltDiameter = b.length("bolt_d", 8.0);
    m.edge = b.length("edge", 16.0);
    m.boltSpan = b.length("bolt_span", 88.0);
    m.halfWidth = b.length("half_width", 60.0);
    m.halfHeight = b.length("half_height", 30.0);
    const auto equation = [&](ParameterId parameter, const char* text) {
        b.need(m.document.setParameterExpression(parameter, text), text);
    };
    equation(m.height, "width / 2");
    equation(m.thickness, "width / 15");
    equation(m.boltDiameter, "thickness");
    equation(m.edge, "2 * thickness");
    equation(m.boltSpan, "width - 2 * edge");
    // The plate's centre lines, so the features that should stay centred on
    // the plate follow it into every configuration.
    equation(m.halfWidth, "width / 2");
    equation(m.halfHeight, "height / 2");

    // The plate: width x height, extruded by thickness from z = 0.
    detail::SketchBuilder plate(b, "PlateSketch", Frame3D::xy());
    const EntityId origin = plate.point(0.0, 0.0);
    const EntityId alongX = plate.point(120.0, 0.0);
    const EntityId corner = plate.point(120.0, 60.0);
    const EntityId alongY = plate.point(0.0, 60.0);
    const EntityId bottom = plate.line(origin, alongX);
    const EntityId right = plate.line(alongX, corner);
    const EntityId top = plate.line(corner, alongY);
    const EntityId left = plate.line(alongY, origin);
    plate.fixed(origin);
    plate.horizontal(bottom);
    plate.horizontal(top);
    plate.vertical(right);
    plate.vertical(left);
    plate.length(bottom, m.width);
    plate.length(right, m.height);
    m.plateSketch = plate.finish();
    m.plate = b.feature<features::ExtrudeFeature>(
        "Plate", {.profile = sketchId(m.plateSketch), .depth = 8_mm, .depthParameter = m.thickness});

    // Two bolt holes, drilled from the plate's START plane (z = 0), which no
    // configuration moves: drilling from the top would name a plane whose
    // height is itself an equation. Their size and spacing are equations, so
    // the bolt pattern grows with the bracket.
    const geometry::FaceSignature startPlane =
        geometry::planeSignature(pointMm(0.0, 0.0, 0.0), Direction3D::unitZ().reversed());
    m.boltHole = b.feature<features::HoleFeature>("BoltHole", {.target = featureId(m.plate),
                                                               .face = startPlane,
                                                               .center = Point2D{16_mm, 30_mm},
                                                               .centerUParameter = m.edge,
                                                               .centerVParameter = m.halfHeight,
                                                               .diameter = 8_mm,
                                                               .diameterParameter = m.boltDiameter});
    m.boltHoles = b.feature<features::LinearPatternFeature>(
        "BoltHoles", {.source = featureId(m.boltHole),
                      .first = {.direction = {1.0, 0.0, 0.0}, .count = 2, .spacingParameter = m.boltSpan}});

    // The motor pilot boss, on a sketch attached to the plate's END CAP by
    // name (P12-STREF-001, P12-SKETCH-003). The cap's height is `thickness`,
    // an equation, so every configuration moves it and the sketch must
    // follow. The boss itself does not scale: it mates to a motor, whose
    // spigot is what it is.
    //
    // Its CENTRE is driven to the middle of the plate. Written as the
    // literal (45, 22.5) it was the middle of the Small plate only, and sat
    // off-centre in Medium and Large -- which no volume check would ever
    // catch, since moving a boss does not change how much material it adds.
    detail::SketchBuilder pilot(b, "PilotSketch", Frame3D::xy());
    pilot.attach(PlaneReference{.object = m.plate, .face = FaceSelector{.role = FaceRole::EndCap}});
    const EntityId pilotOrigin = pilot.point(0.0, 0.0);
    const EntityId alongU = pilot.point(10.0, 0.0);
    const EntityId alongV = pilot.point(0.0, 10.0);
    const EntityId uAxis = pilot.line(pilotOrigin, alongU);
    const EntityId vAxis = pilot.line(pilotOrigin, alongV);
    pilot.construction(uAxis);
    pilot.construction(vAxis);
    // Fixed reference axes: pinning both ends is what makes them axes. A
    // horizontal/vertical constraint alone would leave each far end free to
    // slide along its own line, and the sketch under-constrained by 2 DOF.
    pilot.fixed(pilotOrigin);
    pilot.fixed(alongU);
    pilot.fixed(alongV);
    const EntityId pilotCentre = pilot.point(45.0, 22.5);
    const EntityId pilotCircle = pilot.circle(pilotCentre, 16.0);
    // The boss does not scale: it mates to a motor, whose spigot is what it
    // is. So the radius is a literal, deliberately, while the centre is not.
    pilot.radius(pilotCircle, 16.0);
    pilot.distance(pilotCentre, uAxis, m.halfHeight);
    pilot.distance(pilotCentre, vAxis, m.halfWidth);
    m.pilotSketch = pilot.finish();
    m.pilot = b.feature<features::ExtrudeFeature>("Pilot", {.profile = sketchId(m.pilotSketch),
                                                            .depth = 10_mm,
                                                            .operation = features::FeatureOperation::Join,
                                                            .target = featureId(m.boltHoles)});

    // Three sizes of the same bracket, each setting `width` and nothing else.
    const auto configure = [&](const char* name, double widthMm) {
        const ConfigurationId id = b.need(m.document.createConfiguration(name), name);
        b.need(m.document.setConfigurationOverride(id, m.width, widthMm * units::mm), name);
        return id;
    };
    m.small = configure("Small", 90.0);
    m.medium = configure("Medium", 120.0);
    m.large = configure("Large", 180.0);

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
