#include "BuildSupport.hpp"
#include "ReferenceModels.hpp"

#include <bettercad/features/Datums.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/SweepFeature.hpp>

namespace bettercad::reference {

using namespace bettercad::literals;
using detail::featureId;
using detail::sketchId;

Result<ManifoldTubeModel> buildManifoldTubeReferenceModel() {
    ManifoldTubeModel m{
        .document = Document(detail::fixedDocumentId("5eed0000-0000-4000-8000-000000000103"), "ManifoldTube")};
    detail::ModelBuilder b(m.document);

    // A square-section manifold leg that drops, turns to run along X, then
    // turns again to run along Y: two runs on different planes, so the path
    // leaves any one plane (P12-SWEEP-001). The section turns with it.
    m.side = b.length("side", 12.0);
    m.flangeSide = b.length("flange_side", 40.0);
    m.flangeThickness = b.length("flange_t", 12.0);
    m.drop = b.length("drop", 50.0);
    m.firstBend = b.length("bend1_r", 30.0);
    m.secondBend = b.length("bend2_r", 25.0);
    m.twist = b.need(m.document.createParameter("twist", 90_deg, units::deg), "twist");
    // The second bend is five sixths of the first: one free radius, not two.
    b.need(m.document.setParameterExpression(m.secondBend, "bend1_r * 5 / 6"), "bend2_r = bend1_r * 5 / 6");
    b.need(m.document.setParameterExpression(m.flangeSide, "side * 10 / 3"), "flange_side = side * 10 / 3");
    b.need(m.document.setParameterExpression(m.flangeThickness, "side"), "flange_t = side");
    // Half-sides, so the two square sections stay centred on their origins
    // at ANY side (see the sections below).
    m.halfSide = b.length("half_side", 6.0);
    m.halfFlange = b.length("half_flange", 20.0);
    b.need(m.document.setParameterExpression(m.halfSide, "side / 2"), "half_side = side / 2");
    b.need(m.document.setParameterExpression(m.halfFlange, "flange_side / 2"), "half_flange = flange_side / 2");

    // The section: a square centred on the path's start, on the plane the
    // path leaves at right angles.
    detail::SketchBuilder bore(b, "BoreSection", Frame3D::xy());
    const EntityId a = bore.point(-6.0, -6.0);
    const EntityId c = bore.point(6.0, -6.0);
    const EntityId d = bore.point(6.0, 6.0);
    const EntityId e = bore.point(-6.0, 6.0);
    const EntityId bottom = bore.line(a, c);
    const EntityId right = bore.line(c, d);
    const EntityId top = bore.line(d, e);
    const EntityId left = bore.line(e, a);
    bore.horizontal(bottom);
    bore.horizontal(top);
    bore.vertical(right);
    bore.vertical(left);
    bore.length(bottom, m.side);
    bore.equal(bottom, right);
    // Centred on the origin, so the section's centroid rides the path and
    // Pappus gives the volume exactly. The centring is HALF THE SIDE, driven
    // by an equation rather than written out: a literal 6 would centre the
    // section only at the side it was written for, and any other `side`
    // would slide the centroid off the path and quietly cost the model the
    // very property this comment claims.
    const EntityId origin = bore.point(0.0, 0.0);
    bore.fixed(origin);
    bore.distance(origin, bottom, m.halfSide);
    bore.distance(origin, left, m.halfSide);
    m.boreSection = bore.finish();

    // Run one, in the XZ plane: straight down, then a quarter turn into +X.
    detail::SketchBuilder first(b, "DropRun", Frame3D::xz());
    const EntityId start = first.point(0.0, 0.0);
    const EntityId kneeTop = first.point(0.0, -50.0);
    const EntityId kneeCentre = first.point(30.0, -50.0);
    const EntityId kneeEnd = first.point(30.0, -80.0);
    const EntityId leg = first.line(start, kneeTop);
    const EntityId knee = first.arc(kneeCentre, kneeTop, kneeEnd);
    first.fixed(start);
    first.vertical(leg);
    first.length(leg, m.drop);
    first.horizontal(kneeTop, kneeCentre);
    first.radius(knee, m.firstBend);
    first.vertical(kneeCentre, kneeEnd);
    m.dropRun = first.finish();

    // Run two lies on the level the first run ends at, which is
    // drop + bend1_r below the start -- so it is a DATUM plane placed by
    // that equation, not a fixed height. A fixed plane would come adrift the
    // moment either dimension changed, and the path would break.
    m.elbowOffset = b.length("elbow_offset", -80.0);
    b.need(m.document.setParameterExpression(m.elbowOffset, "0 mm - drop - bend1_r"),
           "elbow_offset = -(drop + bend1_r)");
    m.elbowPlane = b.feature<features::DatumPlane>("ElbowPlane",
                                                   {.kind = features::DatumPlaneKind::Offset,
                                                    .base = PlaneReference{.plane = PrincipalPlane::XY},
                                                    .offset = -80_mm,
                                                    .offsetParameter = m.elbowOffset});

    // A quarter turn from +X into +Y, which takes the path out of the XZ
    // plane for good. Its corner is bend1_r along X, where run one ends.
    detail::SketchBuilder second(b, "SweepRun", detail::levelPlane(-80.0));
    second.attach(PlaneReference{.object = m.elbowPlane});
    const EntityId sketchOrigin = second.point(0.0, 0.0);
    const EntityId elbowStart = second.point(30.0, 0.0);
    const EntityId elbowCentre = second.point(30.0, 25.0);
    const EntityId elbowEnd = second.point(55.0, 25.0);
    const EntityId elbow = second.arc(elbowCentre, elbowStart, elbowEnd);
    second.fixed(sketchOrigin);
    second.horizontal(sketchOrigin, elbowStart);
    second.distance(sketchOrigin, elbowStart, m.firstBend);
    second.vertical(elbowStart, elbowCentre);
    second.radius(elbow, m.secondBend);
    second.horizontal(elbowCentre, elbowEnd);
    m.sweepRun = second.finish();

    m.tube = b.feature<features::SweepFeature>(
        "Tube", {.profile = sketchId(m.boreSection),
                 .path = {.sketch = sketchId(m.dropRun),
                          .edges = {leg, knee},
                          .runs = {{.sketch = sketchId(m.sweepRun), .edges = {elbow}}}},
                 .twist = 90_deg,
                 .twistParameter = m.twist});

    // The mounting flange, above the inlet: the path runs down from z = 0,
    // so the flange stands on the other side of that plane and the two meet
    // on it without sharing any volume.
    detail::SketchBuilder flange(b, "FlangeSketch", Frame3D::xy());
    const EntityId flangeOrigin = flange.point(0.0, 0.0);
    const EntityId fa = flange.point(-20.0, -20.0);
    const EntityId fc = flange.point(20.0, -20.0);
    const EntityId fd = flange.point(20.0, 20.0);
    const EntityId fe = flange.point(-20.0, 20.0);
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
    m.flange = b.feature<features::ExtrudeFeature>("Flange", {.profile = sketchId(m.flangeSketch),
                                                              .depth = 12_mm,
                                                              .depthParameter = m.flangeThickness,
                                                              .operation = features::FeatureOperation::Join,
                                                              .target = featureId(m.tube)});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
