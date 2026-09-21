#include "AssemblyParts.hpp"

#include <bettercad/features/ExtrudeFeature.hpp>

namespace bettercad::reference::detail {

using namespace bettercad::literals;

BlockPart blockPart(ModelBuilder& b, const std::string& name, const std::string& prefix, double wMm, double dMm,
                    double hMm) {
    BlockPart part;
    part.width = b.length(prefix + "_w", wMm);
    part.depth = b.length(prefix + "_d", dMm);
    part.height = b.length(prefix + "_h", hMm);

    SketchBuilder profile(b, name + "Sketch", Frame3D::xy());
    const EntityId origin = profile.point(0.0, 0.0);
    const EntityId alongX = profile.point(wMm, 0.0);
    const EntityId corner = profile.point(wMm, dMm);
    const EntityId alongY = profile.point(0.0, dMm);
    const EntityId bottom = profile.line(origin, alongX);
    const EntityId right = profile.line(alongX, corner);
    const EntityId top = profile.line(corner, alongY);
    const EntityId left = profile.line(alongY, origin);
    profile.fixed(origin);
    profile.horizontal(bottom);
    profile.horizontal(top);
    profile.vertical(right);
    profile.vertical(left);
    profile.length(bottom, part.width);
    profile.length(right, part.depth);
    part.sketch = profile.finish();

    part.solid = b.feature<features::ExtrudeFeature>(
        name, {.profile = sketchId(part.sketch), .depth = hMm * units::mm, .depthParameter = part.height});
    return part;
}

DiscPart discPart(ModelBuilder& b, const std::string& name, const std::string& prefix, double rMm, double hMm) {
    DiscPart part;
    part.radius = b.length(prefix + "_r", rMm);
    part.height = b.length(prefix + "_h", hMm);

    SketchBuilder profile(b, name + "Sketch", Frame3D::xy());
    const EntityId centre = profile.point(0.0, 0.0);
    const EntityId rim = profile.circle(centre, rMm);
    profile.fixed(centre);
    profile.radius(rim, part.radius);
    part.sketch = profile.finish();

    part.solid = b.feature<features::ExtrudeFeature>(
        name, {.profile = sketchId(part.sketch), .depth = hMm * units::mm, .depthParameter = part.height});
    return part;
}

ComponentId place(ModelBuilder& b, const std::string& name, ObjectId part, const ComponentPlacement& placement) {
    return b.need(assembly::createComponent(b.document(), name, {.part = ObjectReference{part},
                                                                 .placement = placement}),
                  name);
}

MateId mate(ModelBuilder& b, const std::string& name, const assembly::MateDefinition& definition) {
    return b.need(assembly::createMate(b.document(), name, definition), name);
}

MateId ground(ModelBuilder& b, const std::string& name, ComponentId component) {
    return mate(b, name, {.type = assembly::MateType::Fixed, .component = component});
}

ComponentPlacement at(double xMm, double yMm, double zMm, double spinDeg) {
    ComponentPlacement placement;
    placement.translation = {xMm * units::mm, yMm * units::mm, zMm * units::mm};
    placement.rotation[2] = spinDeg * units::deg;
    return placement;
}

namespace {

MateTarget planeOf(ComponentId component, PrincipalPlane which) {
    return planeTarget(component, PlaneReference{.plane = which});
}

} // namespace

void locate(ModelBuilder& b, const std::string& prefix, ComponentId held, ComponentId moving, double xMm,
            double yMm, double zMm) {
    using assembly::MateType;
    (void)mate(b, prefix + "Flat",
               {.type = MateType::Parallel,
                .a = planeOf(held, PrincipalPlane::XY),
                .b = planeOf(moving, PrincipalPlane::XY)});
    (void)mate(b, prefix + "Lift",
               {.type = MateType::Distance,
                .a = planeOf(held, PrincipalPlane::XY),
                .b = planeOf(moving, PrincipalPlane::XY),
                .distance = zMm * units::mm});
    (void)mate(b, prefix + "AlongX",
               {.type = MateType::Distance,
                .a = planeOf(held, PrincipalPlane::YZ),
                .b = planeOf(moving, PrincipalPlane::YZ),
                .distance = xMm * units::mm});
    // NEGATED, and not by accident. A principal plane's normal is the cross
    // product of its two axes, and X x Z = -Y, so the XZ plane faces -Y and a
    // positive distance across it moves the component to NEGATIVE y. Passing
    // yMm straight through would put every component this helper places on
    // the wrong side of its deck -- which is exactly what it did until the
    // measurement said so. The helper's contract is a world position, so the
    // convention is absorbed here rather than in five call sites.
    (void)mate(b, prefix + "AlongY",
               {.type = MateType::Distance,
                .a = planeOf(held, PrincipalPlane::XZ),
                .b = planeOf(moving, PrincipalPlane::XZ),
                .distance = -yMm * units::mm});
    (void)mate(b, prefix + "Square",
               {.type = MateType::Perpendicular,
                .a = planeOf(held, PrincipalPlane::YZ),
                .b = planeOf(moving, PrincipalPlane::XZ)});
}

} // namespace bettercad::reference::detail
