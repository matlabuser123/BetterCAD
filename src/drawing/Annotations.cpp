#include <bettercad/drawing/Annotations.hpp>

#include <bettercad/core/document/Document.hpp>
#include <bettercad/drawing/Dimension.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/Datums.hpp>
#include <bettercad/features/FaceReferences.hpp>
#include <bettercad/features/HoleFeature.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace bettercad::drawing {
namespace {

// --- Paper proportions -----------------------------------------------------
//
// ISO 3098 and ISO 1302 give symbol sizes as multiples of the lettering
// height h, so that a drawing lettered at 5 mm gets symbols to match one
// lettered at 3.5 mm. They are multiples HERE too, of the annotation's own
// text height, and they are the only sizes in this file. None of them ever
// meets a view scale.

/// An arrowhead's length, as a multiple of the text height.
constexpr double kArrowLength = 1.0;
/// Its half-width, so the head is long and narrow as ISO 128 draws it.
constexpr double kArrowHalfWidth = 0.25;
/// The horizontal tail a leader's text sits on.
constexpr double kElbow = 2.0;
/// Half the height of a datum's box, and half its width per letter.
constexpr double kDatumBoxHalfHeight = 1.0;
constexpr double kDatumBoxHalfWidth = 1.0;
/// The datum triangle that sits on the surface.
constexpr double kDatumTriangle = 1.0;
/// The surface-finish tick: its short arm, its long arm, and the angle
/// between them (ISO 1302 draws 60 degrees).
constexpr double kFinishShortArm = 1.4;
constexpr double kFinishLongArm = 2.8;

[[nodiscard]] std::unexpected<Error> notFound(AnnotationId id) {
    return makeError(ErrorCode::NotFound,
                     std::format("{} is not an annotation of this document", id));
}

[[nodiscard]] Point2D offsetBy(const Point2D& from, double dx, double dy) {
    return Point2D{from.x + Length::fromSi(dx), from.y + Length::fromSi(dy)};
}

/// Where in the MODEL an annotation points, and which way its axis runs.
struct Anchor {
    Point3D point{};
    /// Set when the target has an axis: a cylinder, a hole, a datum axis.
    std::optional<Direction3D> axis{};
    /// Set for a hole, which knows its own callout.
    std::optional<features::HoleCallout> hole{};
};

[[nodiscard]] Result<Anchor> resolveTarget(const Document& document,
                                           const AnnotationTarget& target,
                                           const features::BodyLookup& bodies) {
    if (target.plane) {
        auto plane = features::resolvePlane(document, *target.plane, bodies);
        if (!plane) {
            return std::unexpected(plane.error());
        }
        return Anchor{.point = plane->origin(), .axis = plane->normal()};
    }
    if (target.axis) {
        auto axis = features::resolveAxis(document, *target.axis, bodies);
        if (!axis) {
            return std::unexpected(axis.error());
        }
        return Anchor{.point = axis->origin, .axis = axis->direction};
    }
    if (target.cylinder) {
        auto cylinder = features::resolveFaceCylinder(document, *target.cylinder, bodies);
        if (!cylinder) {
            return std::unexpected(cylinder.error());
        }
        return Anchor{.point = cylinder->axis.origin, .axis = cylinder->axis.direction};
    }

    // A document object. A hole feature is the one that means something to an
    // annotation: it knows where it is, which way it points, and what it is.
    const auto* hole = document.findObjectAs<features::HoleFeature>(
        FeatureId::fromValue(target.object->value()));
    if (hole == nullptr) {
        if (document.findObject(*target.object) == nullptr) {
            return makeError(ErrorCode::NotFound,
                             std::format("{} does not exist", *target.object));
        }
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} is not a hole; an annotation can point at an object only "
                                     "when the object knows where it is",
                                     *target.object));
    }
    const features::HoleDefinition& definition = hole->definition();
    auto callout = features::holeCallout(definition, document);
    if (!callout) {
        return std::unexpected(callout.error());
    }
    auto frame = geometry::faceFrame(definition.face);
    if (!frame) {
        return std::unexpected(frame.error());
    }
    // The hole goes INTO the material, so its axis runs against the face's
    // outward normal.
    return Anchor{.point = geometry::facePoint(definition.face, definition.center),
                  .axis = frame->normal().reversed(),
                  .hole = *callout};
}

/// The text a hole callout shows.
///
/// The diameter sign is the one ISO 129 uses, and the words are the ones a
/// drawing uses: THRU for a hole that goes through, DEEP for one that stops.
[[nodiscard]] std::string calloutText(const features::HoleCallout& callout) {
    // The formatter P14-DIM-001 qualified, not a second one: it rounds in
    // integers, half away from zero, and does not go through the locale.
    const DimensionFormat format{.decimals = 2, .trailingZeros = false};
    auto diameter = formatLength(callout.diameter, format);
    std::string text = "Ø";
    text += diameter ? *diameter : std::string{"?"};
    if (callout.extent == geometry::HoleExtent::Through) {
        text += " THRU";
        return text;
    }
    auto depth = formatLength(callout.depth, format);
    text += " DEEP ";
    text += depth ? *depth : std::string{"?"};
    return text;
}

/// An arrowhead at @p tip pointing back along (dx, dy), as a closed triangle.
[[nodiscard]] SceneLine arrowhead(const Point2D& tip, double dx, double dy, double height) {
    const double length = std::hypot(dx, dy);
    const double ux = length > 0.0 ? dx / length : 1.0;
    const double uy = length > 0.0 ? dy / length : 0.0;
    const double back = kArrowLength * height;
    const double side = kArrowHalfWidth * height;
    const Point2D root = offsetBy(tip, -ux * back, -uy * back);
    // Perpendicular, for the two barbs.
    const Point2D left = offsetBy(root, -uy * side, ux * side);
    const Point2D right = offsetBy(root, uy * side, -ux * side);
    return SceneLine{.points = {tip, left, right, tip}, .style = LineStyle::Continuous};
}

/// The leader from an annotation's text to what it points at: an elbow under
/// the text, a slanted line to the target, and an arrowhead.
[[nodiscard]] SceneItems leader(const Point2D& text, const Point2D& target, double height) {
    SceneItems items;
    // The elbow runs toward the target, so the text never sits on top of its
    // own leader.
    const double direction = target.x.si() >= text.x.si() ? 1.0 : -1.0;
    const Point2D knee = offsetBy(text, direction * kElbow * height, 0.0);
    items.lines.push_back(
        SceneLine{.points = {text, knee, target}, .style = LineStyle::Continuous});
    items.lines.push_back(
        arrowhead(target, (target.x - knee.x).si(), (target.y - knee.y).si(), height));
    return items;
}

} // namespace

Result<void> checkAnnotation(const Document& document, const AnnotationDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return valid;
    }
    if (findView(document, definition.view) == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("an annotation cannot belong to {}, which is not a view of "
                                     "this document",
                                     definition.view));
    }
    if (definition.target.cylinder) {
        return features::checkFaceName(document, *definition.target.cylinder);
    }
    for (const ObjectId object : referencedObjects(definition.target)) {
        if (document.findObject(object) == nullptr) {
            return makeError(ErrorCode::NotFound,
                             std::format("an annotation cannot point at {}, which is not an object "
                                         "of this document",
                                         object));
        }
    }
    return {};
}

Result<AnnotationId> createAnnotation(Document& document, std::string name,
                                      const AnnotationDefinition& definition) {
    if (auto valid = checkAnnotation(document, definition); !valid) {
        return std::unexpected(valid.error());
    }
    auto annotation = Annotation::create(std::move(name), definition);
    if (!annotation) {
        return std::unexpected(annotation.error());
    }
    auto id = document.addObject(std::move(*annotation));
    if (!id) {
        return std::unexpected(id.error());
    }
    return AnnotationId::fromValue(id->value());
}

Result<bool> setAnnotationDefinition(Document& document, AnnotationId id,
                                     const AnnotationDefinition& definition) {
    if (findAnnotation(document, id) == nullptr) {
        return notFound(id);
    }
    if (auto valid = checkAnnotation(document, definition); !valid) {
        return std::unexpected(valid.error());
    }
    return document.modifyObject<Annotation>(
        ObjectId{id}, [&](Annotation& a) { return a.setDefinition(definition); });
}

const Annotation* findAnnotation(const Document& document, AnnotationId id) noexcept {
    return document.findObjectAs<Annotation>(id);
}

std::vector<AnnotationId> annotations(const Document& document) {
    std::vector<AnnotationId> found;
    for (const DocumentObject& object : document.objects()) {
        if (dynamic_cast<const Annotation*>(&object) != nullptr) {
            found.push_back(AnnotationId::fromValue(object.id().value()));
        }
    }
    return found;
}

std::vector<AnnotationId> annotationsOn(const Document& document, ViewId view) {
    std::vector<AnnotationId> found;
    for (const AnnotationId id : annotations(document)) {
        if (findAnnotation(document, id)->definition().view == view) {
            found.push_back(id);
        }
    }
    return found;
}

Result<void> removeAnnotation(Document& document, AnnotationId id) {
    if (findAnnotation(document, id) == nullptr) {
        return notFound(id);
    }
    auto removed = document.removeObject(ObjectId{id});
    if (!removed) {
        return std::unexpected(removed.error());
    }
    return {};
}

Result<std::string> annotationText(const Document& document, AnnotationId id,
                                   const BodyLookup& bodies, const TransformLookup& transforms) {
    const Annotation* annotation = findAnnotation(document, id);
    if (annotation == nullptr) {
        return notFound(id);
    }
    const AnnotationDefinition& d = annotation->definition();
    if (!isModelDriven(d.type)) {
        return d.text;
    }
    (void)transforms;
    auto anchor = resolveTarget(document, d.target, bodies);
    if (!anchor) {
        return makeError(anchor.error().code,
                         std::format("{} ({}) cannot be resolved: {}", annotation->name(), id,
                                     anchor.error().message));
    }
    if (!anchor->hole) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} ({}) is a hole callout, and what it points at is not a hole",
                                     annotation->name(), id));
    }
    return calloutText(*anchor->hole);
}

Result<SceneItems> draw(const Document& document, AnnotationId id, const BodyLookup& bodies,
                        const TransformLookup& transforms) {
    const Annotation* annotation = findAnnotation(document, id);
    if (annotation == nullptr) {
        return notFound(id);
    }
    const AnnotationDefinition& d = annotation->definition();
    const double height = d.style.height.si();

    // A note is the one kind that reaches no geometry at all: it is words on
    // paper, and it draws in one step.
    if (d.type == AnnotationType::Note) {
        SceneItems items;
        items.texts.push_back(SceneText{.at = d.placement,
                                        .text = d.text,
                                        .height = d.style.height,
                                        .anchor = TextAnchor::BaselineLeft});
        if (auto valid = validate(items); !valid) {
            return std::unexpected(valid.error());
        }
        return items;
    }

    auto anchor = resolveTarget(document, d.target, bodies);
    if (!anchor) {
        return makeError(anchor.error().code,
                         std::format("{} ({}) cannot be drawn: {}", annotation->name(), id,
                                     anchor.error().message));
    }
    auto at = toSheet(document, d.view, anchor->point, bodies, transforms);
    if (!at) {
        return makeError(at.error().code,
                         std::format("{} ({}) cannot be drawn: {}", annotation->name(), id,
                                     at.error().message));
    }
    const Point2D target = *at;

    SceneItems items;
    switch (d.type) {
    case AnnotationType::Note:
        break; // handled above

    case AnnotationType::Leader:
    case AnnotationType::HoleCallout: {
        auto text = annotationText(document, id, bodies, transforms);
        if (!text) {
            return std::unexpected(text.error());
        }
        items.append(leader(d.placement, target, height));
        items.texts.push_back(SceneText{.at = d.placement,
                                        .text = *text,
                                        .height = d.style.height,
                                        .anchor = TextAnchor::BaselineLeft});
        break;
    }

    case AnnotationType::Centreline: {
        if (!anchor->axis) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} ({}) is a centreline, and what it points at has no axis",
                                         annotation->name(), id));
        }
        // Two points on the model axis, projected, give the line's direction
        // on the sheet. Its LENGTH is the projected extent of what the view
        // draws along that direction, plus a paper extension at each end --
        // so the line covers the feature and the extension does not grow with
        // the scale.
        const Point3D along{anchor->point.x + Length::fromSi(anchor->axis->x()),
                            anchor->point.y + Length::fromSi(anchor->axis->y()),
                            anchor->point.z + Length::fromSi(anchor->axis->z())};
        auto second = toSheet(document, d.view, along, bodies, transforms);
        if (!second) {
            return std::unexpected(second.error());
        }
        const double dx = (second->x - target.x).si();
        const double dy = (second->y - target.y).si();
        const double length = std::hypot(dx, dy);
        if (!(length > 1e-12)) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("{} ({}) is a centreline whose axis points at the viewer, "
                                         "so it draws as a point rather than a line; a centre mark "
                                         "is what that view wants",
                                         annotation->name(), id));
        }
        const double ux = dx / length;
        const double uy = dy / length;

        auto drawn = projectedGeometry(document, d.view, bodies, transforms);
        if (!drawn) {
            return std::unexpected(drawn.error());
        }
        // How far the drawing reaches along the axis, from the view's BOUNDS
        // rather than from the lines it happens to be showing. The bounds are
        // taken before the view's settings drop anything (P14-HLR-001), so a
        // centreline does not get shorter when hidden detail is switched off
        // -- which it would if the span were measured from the drawn edges.
        double lowest = 0.0;
        double highest = 0.0;
        for (const Point2D& corner :
             {drawn->bounds.min, Point2D{drawn->bounds.max.x, drawn->bounds.min.y},
              drawn->bounds.max, Point2D{drawn->bounds.min.x, drawn->bounds.max.y}}) {
            const double t = (corner.x - target.x).si() * ux + (corner.y - target.y).si() * uy;
            lowest = std::min(lowest, t);
            highest = std::max(highest, t);
        }
        const double extension = d.extension.si();
        items.lines.push_back(SceneLine{
            .points = {offsetBy(target, ux * (lowest - extension), uy * (lowest - extension)),
                       offsetBy(target, ux * (highest + extension), uy * (highest + extension))},
            .style = LineStyle::Centre});
        break;
    }

    case AnnotationType::Centremark: {
        // Two arms crossing at the projected centre, along the SHEET's axes,
        // each of a length given in paper millimetres. Nothing here consults
        // the scale, which is exactly what a centre mark must not do.
        const double arm = d.armLength.si();
        items.lines.push_back(SceneLine{.points = {offsetBy(target, -arm, 0.0),
                                                   offsetBy(target, arm, 0.0)},
                                        .style = LineStyle::Centre});
        items.lines.push_back(SceneLine{.points = {offsetBy(target, 0.0, -arm),
                                                   offsetBy(target, 0.0, arm)},
                                        .style = LineStyle::Centre});
        break;
    }

    case AnnotationType::SurfaceFinish: {
        // ISO 1302's tick: a short arm and a long one meeting on the surface,
        // with the roughness written beside them. A bar across the two arms
        // says material must be removed; a circle in the vee says it must not
        // -- the circle is drawn as a small square here, and that is recorded
        // as a foundation rather than the symbol in full.
        const double shortArm = kFinishShortArm * height;
        const double longArm = kFinishLongArm * height;
        const Point2D root = d.placement;
        // 60 degrees between the arms, opening upward.
        const Point2D left = offsetBy(root, -shortArm * 0.5, shortArm * 0.866);
        const Point2D right = offsetBy(root, longArm * 0.5, longArm * 0.866);
        items.lines.push_back(
            SceneLine{.points = {left, root, right}, .style = LineStyle::Continuous});
        if (d.finish->removal == MaterialRemoval::Required) {
            // The bar across the vee: material must be removed.
            items.lines.push_back(
                SceneLine{.points = {left, right}, .style = LineStyle::Continuous});
        } else if (d.finish->removal == MaterialRemoval::Prohibited) {
            const double r = shortArm * 0.35;
            const Point2D centre = offsetBy(root, 0.0, shortArm * 0.7);
            items.lines.push_back(SceneLine{.points = {offsetBy(centre, -r, -r),
                                                       offsetBy(centre, r, -r),
                                                       offsetBy(centre, r, r),
                                                       offsetBy(centre, -r, r),
                                                       offsetBy(centre, -r, -r)},
                                            .style = LineStyle::Continuous});
        }
        auto roughness = formatLength(
            d.finish->roughness,
            DimensionFormat{.decimals = 2, .trailingZeros = false, .unit = "um"});
        if (!roughness) {
            return std::unexpected(roughness.error());
        }
        items.texts.push_back(SceneText{.at = offsetBy(root, longArm * 0.6, longArm * 0.9),
                                        .text = "Ra " + *roughness,
                                        .height = d.style.height,
                                        .anchor = TextAnchor::BaselineLeft});
        items.append(leader(d.placement, target, height));
        break;
    }

    case AnnotationType::Datum: {
        // ISO 5459: the letter in a box, a line down to the surface, and a
        // filled triangle sitting on it. The triangle is drawn as an outline;
        // filling is a writer's business.
        const double halfWidth = kDatumBoxHalfWidth * height;
        const double halfHeight = kDatumBoxHalfHeight * height;
        const Point2D centre = d.placement;
        items.lines.push_back(SceneLine{.points = {offsetBy(centre, -halfWidth, -halfHeight),
                                                   offsetBy(centre, halfWidth, -halfHeight),
                                                   offsetBy(centre, halfWidth, halfHeight),
                                                   offsetBy(centre, -halfWidth, halfHeight),
                                                   offsetBy(centre, -halfWidth, -halfHeight)},
                                        .style = LineStyle::Continuous});
        items.texts.push_back(SceneText{.at = centre,
                                        .text = d.text,
                                        .height = d.style.height,
                                        .anchor = TextAnchor::MiddleCentre});
        items.lines.push_back(
            SceneLine{.points = {centre, target}, .style = LineStyle::Continuous});
        const double side = kDatumTriangle * height;
        items.lines.push_back(SceneLine{.points = {target,
                                                   offsetBy(target, -side * 0.5, -side * 0.866),
                                                   offsetBy(target, side * 0.5, -side * 0.866),
                                                   target},
                                        .style = LineStyle::Continuous});
        break;
    }
    }

    if (auto valid = validate(items); !valid) {
        return std::unexpected(valid.error());
    }
    return items;
}

Result<SceneItems> drawAnnotations(const Document& document, ViewId view, const BodyLookup& bodies,
                                   const TransformLookup& transforms) {
    SceneItems items;
    for (const AnnotationId id : annotationsOn(document, view)) {
        auto drawn = draw(document, id, bodies, transforms);
        if (!drawn) {
            return std::unexpected(drawn.error());
        }
        items.append(*drawn);
    }
    return items;
}

} // namespace bettercad::drawing
