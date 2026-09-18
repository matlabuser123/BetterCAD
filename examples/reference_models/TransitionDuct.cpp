#include "BuildSupport.hpp"
#include "ReferenceModels.hpp"

#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/LoftFeature.hpp>

namespace bettercad::reference {

using namespace bettercad::literals;
using detail::featureId;
using detail::sketchId;

Result<TransitionDuctModel> buildTransitionDuctReferenceModel() {
    TransitionDuctModel m{
        .document = Document(detail::fixedDocumentId("5eed0000-0000-4000-8000-000000000104"), "TransitionDuct")};
    detail::ModelBuilder b(m.document);

    // A square-to-round transition carrying a smooth nozzle: the two halves
    // of P12-LOFT-001 in one part. The duct is RULED between sections of
    // different shapes, so its volume is the prismatoid of the mixed area;
    // the nozzle is SMOOTH through three equally spaced circles, which is
    // the one smooth case with a closed form.
    m.inletSide = b.length("inlet_side", 80.0);
    m.flangeSide = b.length("flange_side", 110.0);
    m.flangeThickness = b.length("flange_t", 10.0);
    m.throatRadius = b.length("throat_r", 30.0);
    m.midRadius = b.length("mid_r", 20.0);
    m.outletRadius = b.length("outlet_r", 25.0);
    m.ductHeight = b.length("duct_h", 60.0);
    m.nozzleHeight = b.length("nozzle_h", 60.0);
    m.midOffset = b.length("mid_offset", 90.0);
    m.outletOffset = b.length("outlet_offset", 120.0);
    const auto equation = [&](ParameterId parameter, const char* text) {
        b.need(m.document.setParameterExpression(parameter, text), text);
    };
    equation(m.flangeSide, "inlet_side + 30 mm");
    equation(m.flangeThickness, "inlet_side / 8");
    equation(m.midRadius, "throat_r * 2 / 3");
    equation(m.outletRadius, "throat_r * 5 / 6");
    equation(m.nozzleHeight, "duct_h");
    // The three nozzle sections must stay equally spaced, or the smooth
    // loft's quadratic no longer describes it.
    equation(m.midOffset, "duct_h + nozzle_h / 2");
    equation(m.outletOffset, "duct_h + nozzle_h");
    // Half-sides, so the two square sections stay centred on the axis at any
    // size (see the sketches below).
    m.halfInlet = b.length("half_inlet", 40.0);
    m.halfFlange = b.length("half_flange", 55.0);
    equation(m.halfInlet, "inlet_side / 2");
    equation(m.halfFlange, "flange_side / 2");

    // The inlet: a square centred on the axis, so it is concentric with the
    // circles above it.
    detail::SketchBuilder inlet(b, "InletSketch", Frame3D::xy());
    const EntityId centre = inlet.point(0.0, 0.0);
    const EntityId a = inlet.point(-40.0, -40.0);
    const EntityId c = inlet.point(40.0, -40.0);
    const EntityId d = inlet.point(40.0, 40.0);
    const EntityId e = inlet.point(-40.0, 40.0);
    const EntityId bottom = inlet.line(a, c);
    const EntityId right = inlet.line(c, d);
    const EntityId top = inlet.line(d, e);
    const EntityId left = inlet.line(e, a);
    inlet.fixed(centre);
    inlet.horizontal(bottom);
    inlet.horizontal(top);
    inlet.vertical(right);
    inlet.vertical(left);
    inlet.length(bottom, m.inletSide);
    inlet.equal(bottom, right);
    // Half the side, by equation: a literal would centre the square only at
    // the side it was written for, and any other `inlet_side` would put the
    // square off-axis from the round sections above it -- which is exactly
    // the concentricity the closed form in TransitionDuctTests.cpp assumes.
    inlet.distance(centre, bottom, m.halfInlet);
    inlet.distance(centre, left, m.halfInlet);
    m.inletSketch = inlet.finish();

    // The three round sections, each on the XY plane and lifted by its own
    // offset, so one parameter moves a section without moving a plane.
    const auto circleSketch = [&](const std::string& name, ParameterId radius, double literal) {
        detail::SketchBuilder s(b, name, Frame3D::xy());
        const EntityId middle = s.point(0.0, 0.0);
        const EntityId ring = s.circle(middle, literal);
        s.fixed(middle);
        s.radius(ring, radius);
        return s.finish();
    };
    m.throatSketch = circleSketch("ThroatSketch", m.throatRadius, 30.0);
    m.midSketch = circleSketch("MidSketch", m.midRadius, 20.0);
    m.outletSketch = circleSketch("OutletSketch", m.outletRadius, 25.0);

    // Square to round, ruled: BetterCAD matches the four sides against four
    // quarters of the circle by arc length (P12-LOFT-001).
    m.duct = b.feature<features::LoftFeature>(
        "Duct", {.sections = {{.sketch = sketchId(m.inletSketch)},
                              {.sketch = sketchId(m.throatSketch), .offsetParameter = m.ductHeight}},
                 .interpolation = features::LoftInterpolation::Ruled});

    // Round to round through a waist, smooth: the sides run continuously
    // across the middle section instead of creasing at it.
    m.nozzle = b.feature<features::LoftFeature>(
        "Nozzle", {.sections = {{.sketch = sketchId(m.throatSketch), .offsetParameter = m.ductHeight},
                                {.sketch = sketchId(m.midSketch), .offsetParameter = m.midOffset},
                                {.sketch = sketchId(m.outletSketch), .offsetParameter = m.outletOffset}},
                   .interpolation = features::LoftInterpolation::Smooth,
                   .operation = features::FeatureOperation::Join,
                   .target = featureId(m.duct)});

    // The inlet flange, below the square section: the duct grows upwards
    // from z = 0, so the flange stands under that plane and the two meet on
    // it without sharing any volume.
    detail::SketchBuilder flange(b, "FlangeSketch", Frame3D::xy());
    const EntityId flangeOrigin = flange.point(0.0, 0.0);
    const EntityId fa = flange.point(-55.0, -55.0);
    const EntityId fc = flange.point(55.0, -55.0);
    const EntityId fd = flange.point(55.0, 55.0);
    const EntityId fe = flange.point(-55.0, 55.0);
    const EntityId fbottom = flange.line(fa, fc);
    const EntityId fright = flange.line(fc, fd);
    const EntityId ftop = flange.line(fd, fe);
    const EntityId fleft = flange.line(fe, fa);
    flange.fixed(flangeOrigin);
    flange.horizontal(fbottom);
    flange.horizontal(ftop);
    flange.vertical(fright);
    flange.vertical(fleft);
    flange.length(fbottom, m.flangeSide);
    flange.equal(fbottom, fright);
    flange.distance(flangeOrigin, fbottom, m.halfFlange);
    flange.distance(flangeOrigin, fleft, m.halfFlange);
    m.flangeSketch = flange.finish();
    m.flange = b.feature<features::ExtrudeFeature>("Flange",
                                                   {.profile = sketchId(m.flangeSketch),
                                                    .depth = 10_mm,
                                                    .depthParameter = m.flangeThickness,
                                                    .direction = features::ExtrudeDirection::Reversed,
                                                    .operation = features::FeatureOperation::Join,
                                                    .target = featureId(m.nozzle)});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
