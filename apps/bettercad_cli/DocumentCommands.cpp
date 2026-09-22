#include "Commands.hpp"

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Mate.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/drawing/Views.hpp>
#include <bettercad/core/document/MateReference.hpp>
#include <bettercad/core/document/ObjectReference.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/CircularPatternFeature.hpp>
#include <bettercad/features/CombineFeature.hpp>
#include <bettercad/features/Datums.hpp>
#include <bettercad/features/DraftFeature.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/FilletFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/LoftFeature.hpp>
#include <bettercad/features/MirrorFeature.hpp>
#include <bettercad/features/RevolveFeature.hpp>
#include <bettercad/features/RibFeature.hpp>
#include <bettercad/features/ShellFeature.hpp>
#include <bettercad/features/SplitFeature.hpp>
#include <bettercad/features/SweepFeature.hpp>
#include <bettercad/features/Validation.hpp>
#include <bettercad/features/VariableFilletFeature.hpp>
#include <bettercad/core/document/ParameterExpressions.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace bettercad::cli {

namespace {

using Row = std::vector<std::string>;

/// Left-aligned columns separated by two spaces, each row indented.
void printTable(std::ostream& out, const std::vector<Row>& rows) {
    std::vector<std::size_t> widths;
    for (const Row& row : rows) {
        widths.resize(std::max(widths.size(), row.size()), 0);
        for (std::size_t i = 0; i < row.size(); ++i) {
            widths[i] = std::max(widths[i], row[i].size());
        }
    }
    for (const Row& row : rows) {
        std::string line = "  ";
        for (std::size_t i = 0; i < row.size(); ++i) {
            line += row[i];
            if (i + 1 < row.size()) {
                line.append(widths[i] - row[i].size() + 2, ' ');
            }
        }
        while (line.ends_with(' ')) {
            line.pop_back();
        }
        out << line << '\n';
    }
}

std::string plural(std::size_t count, std::string_view singular, std::string_view pluralForm) {
    return std::format("{} {}", count, count == 1 ? singular : pluralForm);
}

std::string nameOrId(const Document& document, ObjectId id) {
    const auto name = document.nameOf(id);
    return name ? std::string{*name} : std::format("{} (missing)", id);
}

/// The parameter's value and how it gets it. With a configuration active,
/// the value shown is the one in force: its override, if it has one, marked
/// so that it is clear the base value is something else (P12-PARAM-002).
/// With no configuration active this is what it always printed.
std::string describeParameter(const Document& document, const Parameter& parameter) {
    const auto effective = document.effectiveParameterValue(parameter.id());
    const double value = parameter.displayUnit().scale.fromSi(effective ? effective->siValue
                                                                       : parameter.siValue());
    std::string text = parameter.displayUnit().symbol.empty()
                           ? std::format("{:.10g}", value)
                           : std::format("{:.10g} {}", value, parameter.displayUnit().symbol);
    if (parameter.expression()) {
        text += std::format("  (expression: {})", *parameter.expression());
    } else if (document.activeOverrides().contains(parameter.id())) {
        text += std::format("  (configuration; base {:.10g})", parameter.displayValue());
    }
    return text;
}

/// "new body", or the operation and its target: "cut Pad".
std::string describeOperation(const Document& document, features::FeatureOperation operation,
                              std::optional<FeatureId> target) {
    std::string text{features::toString(operation)};
    if (target) {
        text += " " + nameOrId(document, ObjectId{*target});
    }
    return text;
}

/// "equal distance 5 mm", "two distances 5 mm and 3 mm" or
/// "distance and angle 5 mm and 30 deg"; a driven distance shows its
/// parameter's name.
std::string describeChamferSize(const Document& document, const features::ChamferDefinition& d) {
    const std::string distance = d.distanceParameter ? nameOrId(document, ObjectId{*d.distanceParameter})
                                                     : std::format("{:.10g} mm", d.distance.in(units::mm));
    switch (d.mode) {
    case geometry::ChamferMode::EqualDistance:
        return std::format("equal distance {}", distance);
    case geometry::ChamferMode::TwoDistance:
        return std::format("two distances {} and {:.10g} mm", distance, d.distance2.in(units::mm));
    case geometry::ChamferMode::DistanceAngle:
        return std::format("distance and angle {} and {:.10g} deg", distance, d.angle.in(units::deg));
    }
    return distance;
}

/// A length that may be driven: the parameter's name, or "10 mm".
std::string describeLength(const Document& document, Length value, const std::optional<ParameterId>& parameter) {
    return parameter ? nameOrId(document, ObjectId{*parameter}) : std::format("{:.10g} mm", value.in(units::mm));
}

std::string describeAngle(const Document& document, Angle value, const std::optional<ParameterId>& parameter) {
    return parameter ? nameOrId(document, ObjectId{*parameter}) : std::format("{:.10g} deg", value.in(units::deg));
}

/// Negative zero as 0, for messages.
double tidy(double value) {
    return value == 0.0 ? 0.0 : value;
}

std::string describePoint(const Point3D& p) {
    return std::format("({:.6g}, {:.6g}, {:.6g}) mm", tidy(p.x.in(units::mm)), tidy(p.y.in(units::mm)),
                       tidy(p.z.in(units::mm)));
}

std::string describeDirection(const Direction3D& d) {
    return std::format("({:.6g}, {:.6g}, {:.6g})", tidy(d.x()), tidy(d.y()), tidy(d.z()));
}

/// "the end cap of Base", "the side from entity:4 of Base",
/// "the bottom of Bore, copy 2 of Row".
std::string describeFaceName(const Document& document, const FaceName& name) {
    const FaceSelector& face = name.face;
    std::string text;
    switch (face.role) {
    case FaceRole::StartCap:
        text = "the start cap";
        break;
    case FaceRole::EndCap:
        text = "the end cap";
        break;
    case FaceRole::Side:
        text = !face.entity ? std::string{"a side"}
               : face.along ? std::format("the side from {} along {}", *face.entity, *face.along)
                            : std::format("the side from {}", *face.entity);
        break;
    case FaceRole::HoleBottom:
        text = "the bottom";
        break;
    case FaceRole::CounterboreFloor:
        text = "the counterbore floor";
        break;
    case FaceRole::Chamfer:
        text = face.edge ? std::format("the face of edge reference {}", *face.edge)
                         : std::string{"a chamfer face"};
        break;
    case FaceRole::SpotfaceFloor:
        text = "the spotface floor";
        break;
    }
    text += std::format(" of {}", nameOrId(document, name.feature));
    for (const FaceCopy& copy : face.copies) {
        text += std::format(", copy {} of {}", copy.instance, nameOrId(document, copy.feature));
    }
    return text;
}

/// "the model's xy", "TopPlane", "Station's yz", or a face (describeFaceName()).
std::string describePlaneReference(const Document& document, const PlaneReference& reference) {
    if (!reference.object) {
        return std::format("the model's {}", toString(reference.plane));
    }
    if (reference.face) {
        return describeFaceName(document, FaceName{*reference.object, *reference.face});
    }
    if (document.findObjectAs<features::CoordinateSystem>(*reference.object) != nullptr) {
        return std::format("{}'s {}", nameOrId(document, *reference.object), toString(reference.plane));
    }
    return nameOrId(document, *reference.object);
}

/// "the model's z", "Spindle", "Station's x".
std::string describeAxisReference(const Document& document, const AxisReference& reference) {
    if (!reference.object) {
        return std::format("the model's {}", toString(reference.axis));
    }
    if (document.findObjectAs<features::CoordinateSystem>(*reference.object) != nullptr) {
        return std::format("{}'s {}", nameOrId(document, *reference.object), toString(reference.axis));
    }
    return nameOrId(document, *reference.object);
}

/// "offset from the model's xy by height", "turned from TopPlane about the
/// model's y by 30 deg", "fixed through (0, 0, 5) mm facing (0, 0, 1)".
std::string describeDatumPlane(const Document& document, const features::DatumPlaneDefinition& d) {
    switch (d.kind) {
    case features::DatumPlaneKind::Fixed:
        return std::format("fixed through {} facing {}", describePoint(d.frame.origin()),
                           describeDirection(d.frame.normal()));
    case features::DatumPlaneKind::Offset:
        return std::format("offset from {} by {}", describePlaneReference(document, d.base),
                           describeLength(document, d.offset, d.offsetParameter));
    case features::DatumPlaneKind::Angled:
        return std::format("turned from {} about {} by {}", describePlaneReference(document, d.base),
                           describeAxisReference(document, d.axis), describeAngle(document, d.angle, d.angleParameter));
    }
    return {};
}

/// "where Middle and RearPlane meet", "fixed through (0, 0, 0) mm along (0, 0, 1)".
std::string describeDatumAxis(const Document& document, const features::DatumAxisDefinition& d) {
    if (d.kind == features::DatumAxisKind::Fixed) {
        return std::format("fixed through {} along {}", describePoint(d.axis.origin),
                           describeDirection(d.axis.direction));
    }
    return std::format("where {} and {} meet", describePlaneReference(document, d.first),
                       describePlaneReference(document, d.second));
}

/// "from the model's: moved (150 mm, 0 mm, 0 mm), turned (0 deg, 0 deg, 90 deg)",
/// or "fixed at (0, 0, 0) mm, x along (1, 0, 0), z along (0, 0, 1)".
std::string describeCoordinateSystem(const Document& document, const features::CoordinateSystemDefinition& d) {
    if (d.kind == features::CoordinateSystemKind::Fixed) {
        return std::format("fixed at {}, x along {}, z along {}", describePoint(d.frame.origin()),
                           describeDirection(d.frame.xAxis()), describeDirection(d.frame.normal()));
    }
    const std::string base = d.base ? nameOrId(document, *d.base) : std::string{"the model's"};
    return std::format("from {}: moved ({}, {}, {}), turned ({}, {}, {})", base,
                       describeLength(document, d.translation[0], d.translationParameters[0]),
                       describeLength(document, d.translation[1], d.translationParameters[1]),
                       describeLength(document, d.translation[2], d.translationParameters[2]),
                       describeAngle(document, d.rotation[0], d.rotationParameters[0]),
                       describeAngle(document, d.rotation[1], d.rotationParameters[1]),
                       describeAngle(document, d.rotation[2], d.rotationParameters[2]));
}

/// "target Pad, simple through hole, diameter 10 mm, centre (50 mm, 25 mm)
/// on plane through (0, 0, 20) mm facing (0, 0, 1)", with the depth of a
/// blind hole and the head of a counterbore, countersink or spotface. A
/// threaded hole shows its thread ("thread M8-6H 12 mm long") and a standard
/// clearance hole its size ("clearance for M8 (medium)") in place of the
/// diameter, followed by a tolerance class if there is one.
std::string describeHole(const Document& document, const features::HoleDefinition& d) {
    std::string text = std::format("target {}, {} {} hole", nameOrId(document, ObjectId{d.target}),
                                   geometry::toString(d.type), geometry::toString(d.extent));
    if (d.extent == geometry::HoleExtent::Blind) {
        text += std::format(" {} deep", describeLength(document, d.depth, d.depthParameter));
    }
    if (d.thread) {
        const bool fullLength = d.thread->length == Length{} && !d.thread->lengthParameter;
        text += std::format(", thread {} {}", standards::designation(d.thread->size, d.thread->tolerance),
                            fullLength ? std::string{"full length"}
                                       : describeLength(document, d.thread->length, d.thread->lengthParameter) +
                                             " long");
    } else if (d.clearance) {
        text += std::format(", clearance for {} ({})", d.clearance->bolt.designation(),
                            standards::toString(d.clearance->series));
    } else {
        text += std::format(", diameter {}", describeLength(document, d.diameter, d.diameterParameter));
    }
    if (d.tolerance) {
        text += " " + standards::toString(*d.tolerance);
    }
    if (d.type == geometry::HoleType::Counterbore) {
        text += std::format(", counterbore {:.10g} mm x {:.10g} mm deep", d.counterboreDiameter.in(units::mm),
                            d.counterboreDepth.in(units::mm));
    } else if (d.type == geometry::HoleType::Countersink) {
        text += std::format(", countersink {:.10g} mm at {:.10g} deg", d.countersinkDiameter.in(units::mm),
                            d.countersinkAngle.in(units::deg));
    } else if (d.type == geometry::HoleType::Spotface) {
        text += std::format(", spotface {:.10g} mm x {:.10g} mm deep", d.spotfaceDiameter.in(units::mm),
                            d.spotfaceDepth.in(units::mm));
    }
    text += std::format(", centre ({}, {}) on {}", describeLength(document, d.center.x, d.centerUParameter),
                        describeLength(document, d.center.y, d.centerVParameter), geometry::describe(d.face));
    return text;
}

/// "5 x 20 mm along (1, 0, 0)": the count, the spacing and the direction as
/// given; driven values show their parameter's name.
/// The instances the suppressed ones among them make no geometry:
/// ", suppressed 2, 5", by index, in the order the definition holds them
/// (P12-PATTERN-001). Nothing when none is suppressed.
std::string describeSuppressed(const std::vector<std::uint32_t>& suppressed) {
    if (suppressed.empty()) {
        return {};
    }
    std::string text = ", suppressed ";
    for (std::size_t i = 0; i < suppressed.size(); ++i) {
        text += (i == 0 ? "" : ", ") + std::format("{}", suppressed[i]);
    }
    return text;
}

/// "5 x 20 mm along (1, 0, 0)", or "5 over 80 mm along ..." when the length
/// given is the whole row's rather than one step's, and "symmetric" when the
/// instances sit on both sides of the source (P12-PATTERN-001).
std::string describePatternDirection(const Document& document, const features::PatternDirection& d) {
    const std::string count =
        d.countParameter ? nameOrId(document, ObjectId{*d.countParameter}) : std::format("{}", d.count);
    const bool total = d.distribution == features::PatternDistribution::TotalLength;
    return std::format("{} {} {}{} along ({:.6g}, {:.6g}, {:.6g})", count, total ? "over" : "x",
                       describeLength(document, d.spacing, d.spacingParameter), d.symmetric ? " symmetric" : "",
                       tidy(d.direction.x), tidy(d.direction.y), tidy(d.direction.z));
}

/// "source Bolt, 6 around the axis through (0, 0, 0) mm along (0, 0, 1),
/// full circle", or "..., 90 deg included" / "..., 30 deg apart", and
/// ", negative" for the other direction; driven values show their
/// parameter's name.
std::string describeCircularPattern(const Document& document, const features::CircularPatternDefinition& d) {
    const std::string count =
        d.countParameter ? nameOrId(document, ObjectId{*d.countParameter}) : std::format("{}", d.count);
    const Point3D& o = d.axis.origin;
    std::string text =
        d.axis.reference
            ? std::format("source {}, {} around {}, ", nameOrId(document, ObjectId{d.source}), count,
                          describeAxisReference(document, *d.axis.reference))
            : std::format(
                  "source {}, {} around the axis through ({:.6g}, {:.6g}, {:.6g}) mm along ({:.6g}, {:.6g}, {:.6g}), ",
                  nameOrId(document, ObjectId{d.source}), count, tidy(o.x.in(units::mm)), tidy(o.y.in(units::mm)),
                  tidy(o.z.in(units::mm)), tidy(d.axis.direction.x), tidy(d.axis.direction.y),
                  tidy(d.axis.direction.z));
    const std::string angle = d.angleParameter ? nameOrId(document, ObjectId{*d.angleParameter})
                                               : std::format("{:.10g} deg", d.angle.in(units::deg));
    switch (d.spacing) {
    case features::CircularSpacing::FullCircle:
        text += "full circle";
        break;
    case features::CircularSpacing::IncludedAngle:
        text += std::format("{} included", angle);
        break;
    case features::CircularSpacing::AngleStep:
        text += std::format("{} apart", angle);
        break;
    }
    if (d.direction == features::RotationDirection::Negative) {
        text += ", negative";
    }
    if (d.symmetric) {
        text += ", symmetric";
    }
    text += describeSuppressed(d.suppressed);
    return text;
}

/// "source Drill, feature mirror across the plane through (50, 0, 0) mm
/// facing (1, 0, 0)", with ", offset 10 mm" (or the offset parameter's name)
/// for a moved plane, and "body mirror … , original kept" or "…, mirror
/// image only" for a body mirror.
std::string describeMirror(const Document& document, const features::MirrorDefinition& d) {
    const Point3D& o = d.plane.origin;
    std::string text =
        d.plane.reference
            ? std::format("source {}, {} mirror across {}", nameOrId(document, ObjectId{d.source}),
                          features::toString(d.scope), describePlaneReference(document, *d.plane.reference))
            : std::format("source {}, {} mirror across the plane through ({:.6g}, {:.6g}, {:.6g}) mm facing "
                          "({:.6g}, {:.6g}, {:.6g})",
                          nameOrId(document, ObjectId{d.source}), features::toString(d.scope),
                          tidy(o.x.in(units::mm)), tidy(o.y.in(units::mm)), tidy(o.z.in(units::mm)),
                          tidy(d.plane.normal.x), tidy(d.plane.normal.y), tidy(d.plane.normal.z));
    if (d.plane.offsetParameter || d.plane.offset != Length{}) {
        text += std::format(", offset {}", describeLength(document, d.plane.offset, d.plane.offsetParameter));
    }
    if (d.scope == features::MirrorScope::Body) {
        text += d.keepOriginal ? ", original kept" : ", mirror image only";
    }
    return text;
}

/// A mate target: which component, and what kind of geometry on it.
std::string describeMateTarget(const Document& document, const MateTarget& target) {
    return std::format("{} {}", nameOrId(document, ObjectId{target.component}), toString(target.kind));
}

std::string describeObject(const Document& document, const DocumentObject& object) {
    if (const auto* view = dynamic_cast<const drawing::View*>(&object)) {
        const drawing::ViewDefinition& d = view->definition();
        const auto scale = drawing::effectiveScale(document, view->viewId());
        const std::string scaleText = scale ? scale->label() : std::string{"unknown"};
        if (drawing::isBaseView(d)) {
            return std::format("{} of {}, scale {}, on {}", drawing::toString(*d.orientation),
                               nameOrId(document, d.source.object), scaleText, nameOrId(document, ObjectId{d.sheet}));
        }
        return std::format("{} projected from {}, scale {}, on {}", drawing::toString(*d.direction),
                           nameOrId(document, ObjectId{*d.parent}), scaleText,
                           nameOrId(document, ObjectId{d.sheet}));
    }
    if (const auto* sheet = dynamic_cast<const drawing::Sheet*>(&object)) {
        const drawing::SheetDefinition& d = sheet->definition();
        const auto [width, height] = sheet->size();
        // Sheet N of M is the sheet's POSITION, not its ID (ADR-017): it
        // changes when an earlier sheet is deleted.
        return std::format("{} {}, {} x {} mm, scale {}, sheet {} of {}", drawing::toString(d.format),
                           drawing::toString(d.orientation), width.in(units::mm), height.in(units::mm),
                           d.scale.label(), drawing::sheetNumber(document, sheet->sheetId()),
                           drawing::sheetCount(document));
    }
    if (const auto* mate = dynamic_cast<const assembly::Mate*>(&object)) {
        const assembly::MateDefinition& d = mate->definition();
        std::string text{assembly::toString(d.type)};
        if (d.distance) {
            text += std::format(" {}", describeLength(document, *d.distance, std::nullopt));
        }
        if (d.angle) {
            text += std::format(" {}", describeAngle(document, *d.angle, std::nullopt));
        }
        if (d.component.isValid()) {
            // A fixed mate holds a component rather than relating geometry.
            text += std::format(", holds {}", nameOrId(document, ObjectId{d.component}));
        } else if (d.a && d.b) {
            text += std::format(", {} to {}", describeMateTarget(document, *d.a),
                                describeMateTarget(document, *d.b));
        }
        if (d.suppressed) {
            text += ", suppressed";
        }
        return text;
    }

    if (const auto* component = dynamic_cast<const assembly::Component*>(&object)) {
        const ObjectReference& part = component->definition().part;
        std::string text;
        if (isInternal(part)) {
            const DocumentObject* placed = document.findObject(part.object);
            text = std::format("places {} ({})", part.object, placed != nullptr ? placed->name() : "missing");
        } else {
            // A part in another document. `info` reads one file and supplies
            // no resolver, so it reports the identity rather than pretending
            // to have looked: that is the unresolved state, not an error.
            text = std::format("places {} of document {} (unresolved)", part.object, part.document->value());
            if (!part.hint.empty()) {
                text += std::format(", hint {}", part.hint);
            }
        }
        if (component->definition().suppressed) {
            text += ", suppressed";
        }
        return text;
    }
    if (const auto* sketch = dynamic_cast<const sketch::Sketch*>(&object)) {
        const auto disabled = std::ranges::count_if(sketch->constraints(),
                                                    [](const sketch::Constraint& c) { return !c.enabled; });
        std::string text = std::format("{} entities, {} constraints", sketch->entityCount(), sketch->constraintCount());
        if (disabled > 0) {
            text += std::format(" ({} disabled)", disabled);
        }
        std::vector<ObjectId> drivers = sketch->dependencies();
        if (sketch->attachment()) {
            // The attachment's objects (a face's feature and the features
            // copying it) place the sketch; they do not drive it.
            for (const ObjectId placedBy : referencedObjects(*sketch->attachment())) {
                std::erase(drivers, placedBy);
            }
        }
        if (!drivers.empty()) {
            text += ", driven by ";
            for (std::size_t i = 0; i < drivers.size(); ++i) {
                text += (i == 0 ? "" : ", ") + nameOrId(document, drivers[i]);
            }
        }
        if (sketch->attachment()) {
            text += std::format(", on {}", describePlaneReference(document, *sketch->attachment()));
        }
        return text;
    }
    if (const auto* extrude = dynamic_cast<const features::ExtrudeFeature*>(&object)) {
        const features::ExtrudeDefinition& d = extrude->definition();
        const std::string extent =
            d.termination == features::ExtrudeTermination::ThroughAll ? std::string{"through all"}
            : d.depthParameter ? std::format("depth {}", nameOrId(document, ObjectId{*d.depthParameter}))
                               : std::format("depth {:.10g} mm", d.depth.in(units::mm));
        return std::format("profile {}, {}, {}, {}", nameOrId(document, ObjectId{d.profile}), extent,
                           features::toString(d.direction), describeOperation(document, d.operation, d.target));
    }
    if (const auto* revolve = dynamic_cast<const features::RevolveFeature*>(&object)) {
        const features::RevolveDefinition& d = revolve->definition();
        const std::string axis = d.axis.kind == features::RevolveAxisKind::Line
                                     ? std::format("line {}", d.axis.line)
                                     : std::string{features::toString(d.axis.kind)};
        const std::string angle = d.angleParameter ? nameOrId(document, ObjectId{*d.angleParameter})
                                                   : std::format("{:.10g} deg", d.angle.in(units::deg));
        return std::format("profile {}, axis {}, angle {}, {}, {}", nameOrId(document, ObjectId{d.profile}), axis,
                           angle, features::toString(d.direction), describeOperation(document, d.operation, d.target));
    }
    if (const auto* chamfer = dynamic_cast<const features::ChamferFeature*>(&object)) {
        const features::ChamferDefinition& d = chamfer->definition();
        return std::format("target {}, {}, {}", nameOrId(document, ObjectId{d.target}),
                           plural(d.edges.size(), "edge", "edges"), describeChamferSize(document, d));
    }
    if (const auto* fillet = dynamic_cast<const features::FilletFeature*>(&object)) {
        const features::FilletDefinition& d = fillet->definition();
        const std::string radius = d.radiusParameter ? nameOrId(document, ObjectId{*d.radiusParameter})
                                                     : std::format("{:.10g} mm", d.radius.in(units::mm));
        return std::format("target {}, {}, radius {}", nameOrId(document, ObjectId{d.target}),
                           plural(d.edges.size(), "edge", "edges"), radius);
    }
    if (const auto* variable = dynamic_cast<const features::VariableFilletFeature*>(&object)) {
        // "target Block, 2 edges: low at 0, high at 1; 6 mm at 0, 4 mm at 0.5, 6 mm at 1"
        const features::VariableFilletDefinition& d = variable->definition();
        std::string edges;
        for (const features::VariableFilletEdgeDefinition& entry : d.edges) {
            std::string stations;
            for (const features::VariableFilletStation& station : entry.stations) {
                stations += std::format("{}{} at {:.10g}", stations.empty() ? "" : ", ",
                                        describeLength(document, station.radius, station.radiusParameter),
                                        station.position);
            }
            edges += std::format("{}{}", edges.empty() ? "" : "; ", stations);
        }
        return std::format("target {}, {}: {}", nameOrId(document, ObjectId{d.target}),
                           plural(d.edges.size(), "edge", "edges"), edges);
    }
    if (const auto* rib = dynamic_cast<const features::RibFeature*>(&object)) {
        // "target Bracket, profile RibSketch (2 edges), thickness wall, symmetric, left side"
        const features::RibDefinition& d = rib->definition();
        return std::format("target {}, profile {} ({}), thickness {}, {}, {} side",
                           nameOrId(document, ObjectId{d.target}), nameOrId(document, ObjectId{d.profile}),
                           plural(d.edges.size(), "edge", "edges"),
                           describeLength(document, d.thickness, d.thicknessParameter),
                           geometry::toString(d.placement), d.flipped ? "right" : "left");
    }
    if (const auto* draft = dynamic_cast<const features::DraftFeature*>(&object)) {
        // "target Block, faces the side from entity:4 of Block and ..., neutral
        // plane the model's xy, angle taper"
        const features::DraftDefinition& d = draft->definition();
        std::string faces;
        for (const FaceName& name : d.faces) {
            faces += (faces.empty() ? "" : " and ") + describeFaceName(document, name);
        }
        return std::format("target {}, faces {}, neutral plane {}, angle {}", nameOrId(document, ObjectId{d.target}),
                           faces, describePlaneReference(document, d.neutralPlane),
                           describeAngle(document, d.angle, d.angleParameter));
    }
    if (const auto* shell = dynamic_cast<const features::ShellFeature*>(&object)) {
        // "target Block, open the end cap of Pad, thickness wall, inward"
        const features::ShellDefinition& d = shell->definition();
        std::string faces;
        for (const FaceName& name : d.openFaces) {
            faces += (faces.empty() ? "" : " and ") + describeFaceName(document, name);
        }
        return std::format("target {}, open {}, thickness {}, {}", nameOrId(document, ObjectId{d.target}), faces,
                           describeLength(document, d.thickness, d.thicknessParameter), geometry::toString(d.side));
    }
    if (const auto* hole = dynamic_cast<const features::HoleFeature*>(&object)) {
        return describeHole(document, hole->definition());
    }
    if (const auto* pattern = dynamic_cast<const features::LinearPatternFeature*>(&object)) {
        const features::LinearPatternDefinition& d = pattern->definition();
        std::string text = std::format("source {}, {}", nameOrId(document, ObjectId{d.source}),
                                       describePatternDirection(document, d.first));
        if (d.second) {
            text += std::format(" by {}", describePatternDirection(document, *d.second));
        }
        text += describeSuppressed(d.suppressed);
        return text;
    }
    if (const auto* circular = dynamic_cast<const features::CircularPatternFeature*>(&object)) {
        return describeCircularPattern(document, circular->definition());
    }
    if (const auto* mirror = dynamic_cast<const features::MirrorFeature*>(&object)) {
        return describeMirror(document, mirror->definition());
    }
    if (const auto* sweep = dynamic_cast<const features::SweepFeature*>(&object)) {
        // "profile Ring, path Route (2 edges) then Rise (1 edge), follow
        // path, twisted 90 deg, cut Block": the runs in the order of travel,
        // and the twist or the guide when there is one (P12-SWEEP-001).
        const features::SweepDefinition& d = sweep->definition();
        std::string path = std::format("{} ({})", nameOrId(document, ObjectId{d.path.sketch}),
                                       plural(d.path.edges.size(), "edge", "edges"));
        for (const features::SweepPathRun& run : d.path.runs) {
            path += std::format(" then {} ({})", nameOrId(document, ObjectId{run.sketch}),
                                plural(run.edges.size(), "edge", "edges"));
        }
        std::string carried;
        if (d.guide) {
            carried = std::format(", guided by {} ({})", nameOrId(document, ObjectId{d.guide->sketch}),
                                  plural(d.guide->edges.size() + [&] {
                                      std::size_t more = 0;
                                      for (const features::SweepPathRun& run : d.guide->runs) {
                                          more += run.edges.size();
                                      }
                                      return more;
                                  }(), "edge", "edges"));
        } else if (d.twistParameter || d.twist != Angle{}) {
            carried = std::format(", twisted {}", describeAngle(document, d.twist, d.twistParameter));
        }
        return std::format("profile {}, path {}, {}{}, {}", nameOrId(document, ObjectId{d.profile}), path,
                           features::toString(d.orientation), carried,
                           describeOperation(document, d.operation, d.target));
    }
    if (const auto* split = dynamic_cast<const features::SplitFeature*>(&object)) {
        // "target Block, plane Middle, keep front"
        const features::SplitDefinition& d = split->definition();
        return std::format("target {}, plane {}, keep {}", nameOrId(document, ObjectId{d.target}),
                           describePlaneReference(document, d.plane), geometry::toString(d.keep));
    }
    if (const auto* combine = dynamic_cast<const features::CombineFeature*>(&object)) {
        // "join Block with Boss, Rib"
        const features::CombineDefinition& d = combine->definition();
        std::string tools;
        for (const FeatureId tool : d.tools) {
            tools += (tools.empty() ? "" : ", ") + nameOrId(document, ObjectId{tool});
        }
        return std::format("{} {} with {}", features::toString(d.operation), nameOrId(document, ObjectId{d.target}),
                           tools);
    }
    if (const auto* plane = dynamic_cast<const features::DatumPlane*>(&object)) {
        return describeDatumPlane(document, plane->definition());
    }
    if (const auto* axis = dynamic_cast<const features::DatumAxis*>(&object)) {
        return describeDatumAxis(document, axis->definition());
    }
    if (const auto* system = dynamic_cast<const features::CoordinateSystem*>(&object)) {
        return describeCoordinateSystem(document, system->definition());
    }
    if (const auto* loft = dynamic_cast<const features::LoftFeature*>(&object)) {
        // "sections Bottom to Top (offset height), ruled, new body", in the loft's order.
        const features::LoftDefinition& d = loft->definition();
        std::string text = "sections ";
        for (std::size_t i = 0; i < d.sections.size(); ++i) {
            const features::LoftSection& section = d.sections[i];
            text += (i == 0 ? "" : " to ") + nameOrId(document, ObjectId{section.sketch});
            if (section.offsetParameter || section.offset != Length{}) {
                text += std::format(" (offset {})", describeLength(document, section.offset, section.offsetParameter));
            }
        }
        return std::format("{}, {}, {}", text, features::toString(d.interpolation),
                           describeOperation(document, d.operation, d.target));
    }
    return {};
}

void printInfo(const Document& document, const std::filesystem::path& path, std::ostream& out) {
    out << std::format("Document: {}\n", document.name());
    out << std::format("File: {}\n", displayPath(path));
    out << std::format("ID: {}\n", document.id().value().toString());
    const DocumentMetadata& metadata = document.metadata();
    if (!metadata.description.empty()) {
        out << std::format("Description: {}\n", metadata.description);
    }
    if (!metadata.author.empty()) {
        out << std::format("Author: {}\n", metadata.author);
    }
    for (const auto& [key, value] : metadata.properties) {
        out << std::format("Property {}: {}\n", key, value);
    }

    out << std::format("\nParameters ({}):\n", document.parameters().size());
    std::vector<Row> parameters;
    for (const Parameter& parameter : document.parameters().all()) {
        parameters.push_back({parameter.name(), describeParameter(document, parameter)});
    }
    printTable(out, parameters);

    if (!document.configurations().empty()) {
        out << std::format("\nConfigurations ({}):\n", document.configurations().size());
        std::vector<Row> configurations;
        for (const Configuration& configuration : document.configurations().all()) {
            const std::size_t count = configuration.size();
            configurations.push_back(
                {configuration.name(), std::format("{} {}", count, count == 1 ? "override" : "overrides"),
                 document.activeConfiguration() == configuration.id() ? "active" : ""});
        }
        printTable(out, configurations);
    }

    out << std::format("\nObjects ({}):\n", document.objectCount());
    std::vector<Row> objects;
    for (const DocumentObject& object : document.objects()) {
        objects.push_back({std::format("{}", object.id()), std::string{object.typeName()}, object.name(),
                           describeObject(document, object)});
    }
    printTable(out, objects);
}

std::string checkStatus(const features::ValidationReport& report, features::ValidationCheck check) {
    using features::Severity;
    using features::ValidationCheck;
    const std::size_t errors = report.count(check, Severity::Error);
    const std::size_t warnings = report.count(check, Severity::Warning);
    if (errors > 0) {
        return plural(errors, "error", "errors");
    }
    std::string status = warnings > 0 ? plural(warnings, "warning", "warnings") : "ok";
    if (check == ValidationCheck::FeatureRegeneration) {
        status += std::format(", {} regenerated", plural(report.regenerated, "object", "objects"));
    } else if (check == ValidationCheck::Geometry) {
        status += std::format(", {}", plural(report.bodies.size(), "result body", "result bodies"));
    }
    return status;
}

/// Millimetres to the micrometre, without trailing zeros: "100", "-21.776".
/// Kernel noise such as -1.5e-15 shows as "0".
std::string formatMm(const Length& length) {
    std::string text = std::format("{:.3f}", length.in(units::mm));
    while (text.ends_with('0')) {
        text.pop_back();
    }
    if (text.ends_with('.')) {
        text.pop_back();
    }
    return text == "-0" ? "0" : text;
}

std::string describeBody(const features::BodySummary& body) {
    std::string text = std::format("{} ({}): {}", body.name, body.feature,
                                   plural(body.topology.solids, "solid", "solids"));
    if (body.properties) {
        text += std::format(", volume {:.3f} mm^3, area {:.3f} mm^2", body.properties->volume.in(units::mm3),
                            body.properties->surfaceArea.in(units::mm2));
    }
    if (body.boundingBox) {
        const auto& box = *body.boundingBox;
        text += std::format(", bounds ({}, {}, {}) to ({}, {}, {}) mm", formatMm(box.min.x), formatMm(box.min.y),
                            formatMm(box.min.z), formatMm(box.max.x), formatMm(box.max.y), formatMm(box.max.z));
    }
    if (!body.valid) {
        text += ", INVALID";
    }
    return text;
}

void printValidation(const features::ValidationReport& report, std::ostream& out) {
    for (const features::ValidationCheck check : features::kValidationChecks) {
        out << std::format("  {:<22}{}\n", features::toString(check), checkStatus(report, check));
        for (const features::ValidationIssue& issue : report.issues) {
            if (issue.check == check) {
                out << std::format("    {}: {}\n", features::toString(issue.severity), issue.message);
            }
        }
    }
    if (!report.bodies.empty()) {
        out << std::format("Result bodies ({}):\n", report.bodies.size());
        for (const features::BodySummary& body : report.bodies) {
            out << "  " << describeBody(body) << '\n';
        }
    }
}

std::string resultLine(std::size_t errors, std::size_t warnings) {
    std::string counts;
    if (errors > 0) {
        counts = plural(errors, "error", "errors");
    }
    if (warnings > 0) {
        counts += (counts.empty() ? "" : ", ") + plural(warnings, "warning", "warnings");
    }
    return std::format("Result: {}{}\n", errors == 0 ? "valid" : "invalid",
                       counts.empty() ? "" : std::format(" ({})", counts));
}

} // namespace

ExitCode runNew(Args args, std::ostream& out, std::ostream& err) {
    auto parsed = parseArguments(args, {{"--name", true}, {"--force", false}});
    if (!parsed) {
        return usageError("new", kNewUsage, parsed.error().message, err);
    }
    if (parsed->positional().size() != 1) {
        return usageError("new", kNewUsage, "expected one document file", err);
    }
    const std::filesystem::path path = pathFromArgument(parsed->positional().front());
    std::error_code ignored;
    if (std::filesystem::exists(path, ignored) && !parsed->has("--force")) {
        return failure("new", std::format("'{}' already exists (use --force to replace it)", displayPath(path)), err);
    }

    Document document;
    if (const auto name = parsed->value("--name")) {
        if (auto set = document.setName(std::string{*name}); !set) {
            return usageError("new", kNewUsage, set.error().message, err);
        }
    } else {
        // Named after the file; a file name that is not a valid document name
        // keeps the default name.
        [[maybe_unused]] const auto named = document.setName(displayPath(path.stem()));
    }
    if (auto saved = io::saveDocument(document, path); !saved) {
        return failure("new", saved.error().message, err);
    }
    out << std::format("Created {} (document '{}', ID {})\n", displayPath(path), document.name(),
                       document.id().value().toString());
    return ExitCode::Success;
}

namespace {

/// Activates the configuration @p name names, so that what follows is
/// reported for it. An unknown name is a failure rather than a silent
/// fallback to the base configuration.
Result<void> selectConfiguration(Document& document, std::string_view name) {
    const Configuration* found = document.configurations().findByName(name);
    if (found == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("no configuration named '{}' in this document", name));
    }
    if (auto set = document.setActiveConfiguration(found->id()); !set) {
        return std::unexpected(set.error());
    }
    return {};
}

} // namespace

ExitCode runInfo(Args args, std::ostream& out, std::ostream& err) {
    auto parsed = parseArguments(args, {{"--configuration", true}});
    if (!parsed) {
        return usageError("info", kInfoUsage, parsed.error().message, err);
    }
    if (parsed->positional().size() != 1) {
        return usageError("info", kInfoUsage, "expected one document file", err);
    }
    const std::filesystem::path path = pathFromArgument(parsed->positional().front());
    auto document = io::loadDocument(path);
    if (!document) {
        return failure("info", document.error().message, err);
    }
    if (const auto name = parsed->value("--configuration")) {
        if (auto selected = selectConfiguration(*document, *name); !selected) {
            return failure("info", selected.error().message, err);
        }
        // Report the values the chosen configuration actually gives, which
        // means evaluating the equations under it.
        (void)evaluateParameterExpressions(*document);
    }
    printInfo(*document, path, out);
    return ExitCode::Success;
}

ExitCode runValidate(Args args, std::ostream& out, std::ostream& err) {
    auto parsed = parseArguments(args, {{"--configuration", true}});
    if (!parsed) {
        return usageError("validate", kValidateUsage, parsed.error().message, err);
    }
    if (parsed->positional().size() != 1) {
        return usageError("validate", kValidateUsage, "expected one document file", err);
    }
    const std::filesystem::path path = pathFromArgument(parsed->positional().front());
    out << std::format("Validating {}\n", displayPath(path));
    auto document = io::loadDocument(path);
    if (!document) {
        // A file that does not load is the most basic consistency failure.
        out << std::format("  {:<22}{}\n    error: {}\n",
                           features::toString(features::ValidationCheck::DocumentConsistency), "1 error",
                           document.error().message);
        out << resultLine(1, 0);
        return ExitCode::Failure;
    }
    if (const auto name = parsed->value("--configuration")) {
        if (auto selected = selectConfiguration(*document, *name); !selected) {
            return failure("validate", selected.error().message, err);
        }
    }
    const features::ValidationReport report = features::validateDocument(*document);
    printValidation(report, out);
    out << resultLine(report.count(features::Severity::Error), report.count(features::Severity::Warning));
    return report.valid() ? ExitCode::Success : ExitCode::Failure;
}

} // namespace bettercad::cli
