#include "BuildSupport.hpp"
#include "ReferenceModels.hpp"

#include <bettercad/features/CircularPatternFeature.hpp>
#include <bettercad/features/CombineFeature.hpp>
#include <bettercad/features/Datums.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <variant>

namespace bettercad::reference {

using namespace bettercad::literals;
using detail::featureId;
using detail::pointMm;
using detail::sketchId;

Result<IndexPlateModel> buildIndexPlateReferenceModel() {
    IndexPlateModel m{
        .document = Document(detail::fixedDocumentId("5eed0000-0000-4000-8000-000000000105"), "IndexPlate")};
    detail::ModelBuilder b(m.document);

    // An index plate lightened by a ring of elliptical pockets, one of them
    // left solid for a keyway, carrying a hub joined to it as a body.
    m.plateRadius = b.length("plate_r", 90.0);
    m.thickness = b.length("thickness", 12.0);
    m.pocketSemiMajor = b.length("pocket_a", 26.0);
    m.pocketSemiMinor = b.length("pocket_b", 14.0);
    m.pocketCircle = b.length("pocket_circle_r", 55.0);
    m.hubRadius = b.length("hub_r", 26.0);
    m.hubHeight = b.length("hub_h", 24.0);
    m.pocketCount = b.count("pocket_count", 6.0);
    const auto equation = [&](ParameterId parameter, const char* text) {
        b.need(m.document.setParameterExpression(parameter, text), text);
    };
    // The pockets and the hub are all proportions of the plate.
    equation(m.pocketSemiMinor, "pocket_a * 7 / 13");
    equation(m.pocketCircle, "plate_r - pocket_a - 9 mm");
    equation(m.hubRadius, "plate_r * 13 / 45");
    equation(m.hubHeight, "thickness * 2");

    // The plate.
    detail::SketchBuilder plate(b, "PlateSketch", Frame3D::xy());
    const EntityId centre = plate.point(0.0, 0.0);
    const EntityId rim = plate.circle(centre, 90.0);
    plate.fixed(centre);
    plate.radius(rim, m.plateRadius);
    m.plateSketch = plate.finish();
    m.plate = b.feature<features::ExtrudeFeature>(
        "Plate", {.profile = sketchId(m.plateSketch), .depth = 12_mm, .depthParameter = m.thickness});

    // The axis the pockets are spread about: a datum, not a bare direction,
    // so the pattern follows the model rather than a hard-coded line
    // (P12-DATUM-001).
    m.axis = b.feature<features::DatumAxis>(
        "PlateAxis", {.kind = features::DatumAxisKind::Fixed,
                      .axis = Axis3D{.origin = pointMm(0.0, 0.0, 0.0), .direction = Direction3D::unitZ()}});

    // One elliptical pocket, cut through all the material however thick the
    // plate becomes (P12-FEAT-001), from an ellipse (P12-SKETCH-002). Its
    // centre sits on the pocket circle and its semi-axes are driven, so the
    // whole pocket follows the plate.
    auto pocket = std::make_unique<sketch::Sketch>("PocketSketch");
    const EntityId sketchOrigin = b.need(pocket->addPoint(Point2D{}), "the pocket sketch origin");
    const EntityId ellipse =
        b.need(pocket->addEllipse(Point2D{55_mm, 0_mm}, 26_mm, 14_mm, Angle{}), "the pocket ellipse");
    b.need(pocket->addFixed(sketchOrigin), "fixing the pocket sketch origin");
    const auto& shape = std::get<sketch::EllipseEntity>(pocket->findEntity(ellipse)->geometry);
    b.need(pocket->addHorizontal(sketchOrigin, shape.center), "the pocket centre on the X axis");
    const auto drive = [&](Result<ConstraintId> constraint, ParameterId parameter, std::string_view what) {
        const ConstraintId id = b.need(std::move(constraint), what);
        b.need(pocket->setConstraintParameter(id, parameter), what);
    };
    drive(pocket->addDistance(sketchOrigin, shape.center, 55_mm), m.pocketCircle, "the pocket circle");
    // The major axis lies along the radius, which also fixes the ellipse's
    // rotation: without it the pocket would be under-constrained.
    b.need(pocket->addHorizontal(shape.center, shape.xVertex), "the pocket's major axis along the radius");
    drive(pocket->addDistance(shape.center, shape.xVertex, 26_mm), m.pocketSemiMajor, "the pocket's semi-major");
    drive(pocket->addDistance(shape.center, shape.yVertex, 14_mm), m.pocketSemiMinor, "the pocket's semi-minor");
    m.pocketSketch = b.add(std::move(pocket), "PocketSketch");
    m.pocket = b.feature<features::ExtrudeFeature>(
        "Pocket", {.profile = sketchId(m.pocketSketch),
                   .direction = features::ExtrudeDirection::Symmetric,
                   .operation = features::FeatureOperation::Cut,
                   .target = featureId(m.plate),
                   .termination = features::ExtrudeTermination::ThroughAll});

    // Six pockets round the axis, with one suppressed: the plate keeps solid
    // metal there for a keyway, and the instance that is gone keeps its
    // index, so the others do not renumber (P12-PATTERN-001).
    m.pockets = b.feature<features::CircularPatternFeature>(
        "Pockets", {.source = featureId(m.pocket),
                    .axis = {.reference = AxisReference{.object = m.axis}},
                    .count = 6,
                    .countParameter = m.pocketCount,
                    .suppressed = {3}});

    // The hub, made as its own body and then combined with the plate
    // (P12-FEAT-002), which is how a separately machined boss is modelled.
    detail::SketchBuilder hub(b, "HubSketch", Frame3D::xy());
    const EntityId hubCentre = hub.point(0.0, 0.0);
    const EntityId hubRim = hub.circle(hubCentre, 26.0);
    hub.fixed(hubCentre);
    hub.radius(hubRim, m.hubRadius);
    m.hubSketch = hub.finish();
    m.hub = b.feature<features::ExtrudeFeature>(
        "Hub", {.profile = sketchId(m.hubSketch), .depth = 24_mm, .depthParameter = m.hubHeight});
    m.assembly = b.feature<features::CombineFeature>(
        "Assembly", {.target = featureId(m.pockets),
                     .tools = {featureId(m.hub)},
                     .operation = features::FeatureOperation::Join});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
