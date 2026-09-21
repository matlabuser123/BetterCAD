#include "AssemblyParts.hpp"
#include "AssemblyReferenceModels.hpp"

#include <bettercad/core/document/MateReference.hpp>

namespace bettercad::reference {

using namespace bettercad::literals;
using assembly::MateType;
using detail::at;
using detail::blockPart;
using detail::discPart;
using detail::ground;
using detail::mate;
using detail::place;

namespace {

MateTarget axisOf(ComponentId component, PrincipalAxis which = PrincipalAxis::Z) {
    return axisTarget(component, AxisReference{.axis = which});
}

MateTarget faceOf(ComponentId component) { return planeTarget(component, PlaneReference{}); }

/// A placement displaced BOTH in the freedoms the joint keeps and in one it
/// must remove. `kStrayMm` across the joint axis is the part that has to go;
/// whatever else is passed is the part that has to survive.
ComponentPlacement displaced(double alongMm, double spinDeg) {
    ComponentPlacement placement = detail::at(JointSetModel::kStrayMm, 0.0, alongMm, spinDeg);
    return placement;
}

} // namespace

// --- RM-D: JointSet -------------------------------------------------------------

Result<JointSetModel> buildJointSetReferenceModel() {
    JointSetModel m{
        .document = Document(detail::fixedDocumentId("a55e0000-0000-4000-8000-000000000004"), "JointSet")};
    detail::ModelBuilder b(m.document);

    m.framePart = blockPart(b, "Frame", "frame", 120.0, 100.0, 12.0).solid;
    m.linkPart = blockPart(b, "Link", "link", 50.0, 12.0, 8.0).solid;
    m.shoePart = blockPart(b, "Shoe", "shoe", 30.0, 20.0, 10.0).solid;
    m.sleevePart = discPart(b, "Sleeve", "sleeve", 8.0, 40.0).solid;
    m.padPart = blockPart(b, "Pad", "pad", 40.0, 40.0, 6.0).solid;

    m.frame = place(b, "FrameBody", m.framePart);
    // Each joint's component starts displaced across its axis by kStrayMm --
    // which every one of the four must remove -- and in the freedom it is
    // meant to keep.
    m.hinge = place(b, "HingeArm", m.linkPart, displaced(0.0, JointSetModel::kHingeSpinDeg));
    m.shoe = place(b, "SlideShoe", m.shoePart, displaced(JointSetModel::kShoeAlongMm, 25.0));
    m.sleeve = place(b, "SleeveRing", m.sleevePart,
                     displaced(JointSetModel::kSleeveAlongMm, JointSetModel::kSleeveSpinDeg));
    m.pad = place(b, "FacePad", m.padPart,
                  detail::at(JointSetModel::kPadAcrossXMm, JointSetModel::kPadAcrossYMm, 45.0,
                             JointSetModel::kPadSpinDeg));

    m.groundFrame = ground(b, "HoldFrame", m.frame);

    // A hinge: on the axis, along it and turning freely about it. 5 equations.
    m.revolute = mate(b, "Hinge",
                      {.type = MateType::Revolute, .a = axisOf(m.frame), .b = axisOf(m.hinge)});
    // A slide: on the axis, free along it, held against turning. 5 equations,
    // and the roll pair is why it is not a cylindrical joint.
    m.slider = mate(b, "Slide",
                    {.type = MateType::Slider,
                     .a = axisOf(m.frame),
                     .b = axisOf(m.shoe),
                     .a2 = axisOf(m.frame, PrincipalAxis::X),
                     .b2 = axisOf(m.shoe, PrincipalAxis::X)});
    // A sleeve: free to slide AND to turn. 4 equations.
    m.cylindrical = mate(b, "SleeveJoint",
                         {.type = MateType::Cylindrical, .a = axisOf(m.frame), .b = axisOf(m.sleeve)});
    // A face: flat on the plane, free to slide in it and spin about its
    // normal. 3 equations.
    m.planar = mate(b, "PadFace", {.type = MateType::Planar, .a = faceOf(m.frame), .b = faceOf(m.pad)});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
