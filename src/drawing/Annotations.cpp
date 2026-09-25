#include <bettercad/drawing/Annotations.hpp>

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Configurations.hpp>
#include <bettercad/drawing/Bom.hpp>

#include <bettercad/core/document/Document.hpp>
#include <bettercad/drawing/Dimension.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/Datums.hpp>
#include <bettercad/features/FaceReferences.hpp>
#include <bettercad/features/HoleFeature.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <array>
#include <vector>
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
/// A feature-control frame's cell: half its height, and the padding either
/// side of a cell's text. ISO 1101 draws the frame twice the lettering high.
constexpr double kFrameHalfHeight = 1.0;
constexpr double kFramePadding = 0.6;
/// How wide a character is taken to be when a cell is sized. Nothing here
/// knows a font, so this is a stated proportion rather than a measurement,
/// and it is recorded as a limitation.
constexpr double kCharacterWidth = 0.7;
/// A balloon's circle, as a multiple of its text height. ISO 7573 draws the
/// circle about twice the lettering across; this is its RADIUS.
constexpr double kBalloonRadius = 1.1;
/// How many straight pieces a balloon's circle is drawn with. The scene
/// carries polylines, so a circle is sampled -- deterministically, and finely
/// enough that it reads as a circle at drawing sizes.
constexpr int kBalloonSegments = 48;
/// The surface-finish tick: its short arm, its long arm, and the angle
/// between them (ISO 1302 draws 60 degrees).
constexpr double kFinishShortArm = 1.4;
constexpr double kFinishLongArm = 2.8;

[[nodiscard]] std::unexpected<Error> notFound(AnnotationId id) {
    return makeError(ErrorCode::NotFound,
                     std::format("{} is not an annotation of this document", id));
}

/// How many CHARACTERS a cell's text is, not how many bytes it takes.
///
/// The GD&T symbols are outside ASCII -- U+2316 POSITION INDICATOR is three
/// bytes of UTF-8 and the diameter sign is two -- so sizing a cell by its
/// byte count would draw a one-character cell three characters wide, and
/// would draw two cells of the same length differently depending on which
/// symbols they held. A UTF-8 continuation byte is 10xxxxxx and starts no
/// character.
[[nodiscard]] std::size_t characterCount(std::string_view text) noexcept {
    std::size_t count = 0;
    for (const char byte : text) {
        if ((static_cast<unsigned char>(byte) & 0xC0U) != 0x80U) {
            ++count;
        }
    }
    return count;
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
    /// Set when the target IS an occurrence, so the caller can move the point
    /// into assembly space with that occurrence's solved transform.
    std::optional<ComponentId> occurrence{};
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

    // A document object. Two kinds know where they are: a hole feature, and
    // a component occurrence, which is what a balloon labels.
    //
    // An EMPTY target reaches here only from a caller that should have
    // handled its own kind first -- a note and a BOM table point at nothing.
    // Saying so is a diagnostic; dereferencing the optional to find out was
    // undefined behaviour, and in a release build it read whatever was there
    // (P14-REFMOD-001).
    if (!target.object) {
        return makeError(ErrorCode::InvalidArgument,
                         "this annotation points at nothing, so there is no place to resolve");
    }
    const ComponentId asComponent = ComponentId::fromValue(target.object->value());
    if (const auto* component = document.findObjectAs<assembly::Component>(asComponent);
        component != nullptr) {
        const ObjectReference& part = component->definition().part;
        const geometry::Body* body = (bodies && isInternal(part)) ? bodies(part.object) : nullptr;
        if (body == nullptr || body->isEmpty()) {
            return makeError(ErrorCode::NotFound,
                             std::format("{} places a part that produced no body", asComponent));
        }
        auto box = body->boundingBox();
        if (!box) {
            return std::unexpected(box.error());
        }
        // The MIDDLE OF THE PART, in the part's own space. The caller turns it
        // into assembly space with the occurrence's solved transform, which is
        // what makes a leader land on the instance it labels rather than on
        // whichever instance was drawn first.
        const Point3D middle{Length::fromSi(0.5 * (box->min.x.si() + box->max.x.si())),
                             Length::fromSi(0.5 * (box->min.y.si() + box->max.y.si())),
                             Length::fromSi(0.5 * (box->min.z.si() + box->max.z.si()))};
        return Anchor{.point = middle, .axis = Direction3D::unitZ(), .occurrence = asComponent};
    }

    const auto* hole = document.findObjectAs<features::HoleFeature>(
        FeatureId::fromValue(target.object->value()));
    if (hole == nullptr) {
        if (document.findObject(*target.object) == nullptr) {
            return makeError(ErrorCode::NotFound,
                             std::format("{} does not exist", *target.object));
        }
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} is not a hole or a component; an annotation can point at "
                                     "an object only when the object knows where it is",
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

/// The table a bill of materials draws, in SHEET millimetres.
///
/// `placement` is the table's TOP-LEFT corner and the rows grow downward, so
/// adding a part lengthens the table away from where it was put rather than
/// moving it. Every length here is a paper length: a BOM table is a sheet
/// annotation and meets no DrawingScale, exactly as a note does not
/// (P14-ANNO-001's invariant).
[[nodiscard]] SceneItems tableItems(const BillOfMaterials& bom, const AnnotationDefinition& d) {
    const BomTableStyle& style = d.table;
    const double left = d.placement.x.si();
    const double top = d.placement.y.si();
    const double rowHeight = style.rowHeight.si();
    const std::array<double, 3> widths{style.itemWidth.si(), style.partWidth.si(),
                                       style.quantityWidth.si()};
    const double width = widths[0] + widths[1] + widths[2];
    const std::size_t lines = bom.rows.size() + (style.header ? 1U : 0U);
    const double height = rowHeight * static_cast<double>(lines);

    SceneItems items;
    if (lines == 0) {
        // An empty assembly draws no table rather than an empty box. There is
        // nothing to list, and a box with nothing in it says there is nothing
        // to buy, which is a different claim.
        return items;
    }

    const auto at = [&](double x, double y) {
        return Point2D{Length::fromSi(x), Length::fromSi(y)};
    };
    // The outer boundary.
    items.lines.push_back(SceneLine{.points = {at(left, top), at(left + width, top),
                                               at(left + width, top - height),
                                               at(left, top - height), at(left, top)},
                                    .style = LineStyle::Continuous});
    // One rule between each pair of rows.
    for (std::size_t i = 1; i < lines; ++i) {
        const double y = top - rowHeight * static_cast<double>(i);
        items.lines.push_back(SceneLine{.points = {at(left, y), at(left + width, y)},
                                        .style = LineStyle::Continuous});
    }
    // One rule between each pair of columns.
    double x = left;
    for (std::size_t i = 0; i + 1 < widths.size(); ++i) {
        x += widths[i];
        items.lines.push_back(SceneLine{.points = {at(x, top), at(x, top - height)},
                                        .style = LineStyle::Continuous});
    }

    // The cells, row by row and column by column, so the order a reader gets
    // them in is the order they are read in.
    const double pad = 0.25 * rowHeight;
    std::size_t line = 0;
    const auto writeRow = [&](const std::array<std::string, 3>& cells) {
        const double middle = top - rowHeight * (static_cast<double>(line) + 0.5);
        double cellLeft = left;
        for (std::size_t column = 0; column < cells.size(); ++column) {
            // The part name reads from the left, as a name does; the numbers
            // sit in the middle of their column, as numbers do.
            const bool centred = column != 1;
            items.texts.push_back(SceneText{
                .at = centred ? at(cellLeft + 0.5 * widths[column], middle)
                              : at(cellLeft + pad, middle),
                .text = cells[column],
                .height = d.style.height,
                .anchor = centred ? TextAnchor::MiddleCentre : TextAnchor::MiddleLeft});
            cellLeft += widths[column];
        }
        ++line;
    };

    if (style.header) {
        writeRow({std::string{"ITEM"}, std::string{"PART"}, std::string{"QTY"}});
    }
    for (const BomRow& row : bom.rows) {
        writeRow({std::to_string(row.item), row.name, std::to_string(row.quantity())});
    }
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

    // The three kinds whose words are derived, each answered by the path that
    // DRAWS it, so the two can never disagree about what an annotation says.
    //
    // This dispatch is the fix P14-REFMOD-001 found the need for. Until then
    // every model-driven kind was sent down the hole-callout path, which
    // `isModelDriven()` had meant when it was written and had not meant since
    // P14-BOM-001 added balloons and tables to it. A balloon was reported as
    // "a hole callout, and what it points at is not a hole" -- a false
    // statement about the document -- and a table, which points at nothing,
    // reached resolveTarget() with an empty target.
    if (d.type == AnnotationType::Balloon) {
        // The item number of the OCCURRENCE, through the bill of materials
        // now. itemNumberOf() is the one path, shared with draw() (ADR-022).
        if (!d.target.object) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} ({}) is a balloon that points at no occurrence",
                                         annotation->name(), id));
        }
        auto item = itemNumberOf(document, d.view,
                                 ComponentId::fromValue(d.target.object->value()));
        if (!item) {
            return makeError(item.error().code,
                             std::format("{} ({}) has no item number: {}", annotation->name(), id,
                                         item.error().message));
        }
        return std::to_string(*item);
    }
    if (d.type == AnnotationType::BomTable) {
        // A table's words are its ROWS, and there is no one string that is
        // what it says. Refusing is right; billOfMaterials() is what a caller
        // wanting the contents should ask.
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} ({}) is a bill-of-materials table: its words are its "
                                     "rows, so ask billOfMaterials() for them rather than for one "
                                     "run of text",
                                     annotation->name(), id));
    }

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

    // A BOM table reaches no geometry either: it is a table on the paper,
    // and what it says comes from the assembly rather than from a point in
    // it. Its rows are computed here, on every call, and none is stored.
    if (d.type == AnnotationType::BomTable) {
        auto bom = billOfMaterials(document, d.view);
        if (!bom) {
            return makeError(bom.error().code,
                             std::format("{} ({}) cannot be drawn: {}", annotation->name(), id,
                                         bom.error().message));
        }
        SceneItems items = tableItems(*bom, d);
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
    // An occurrence's anchor comes back in its PART's space, because that is
    // where its body is. Moving it with the occurrence's own solved transform
    // is what makes a balloon land on the instance it names rather than on
    // whichever instance shares the part (ADR-005).
    Point3D anchorPoint = anchor->point;
    if (anchor->occurrence) {
        const RigidTransform3D* solved = transforms ? transforms(*anchor->occurrence) : nullptr;
        if (solved == nullptr) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("{} ({}) cannot be drawn: {} has no solved transform; the "
                                         "assembly did not solve",
                                         annotation->name(), id, *anchor->occurrence));
        }
        anchorPoint = solved->apply(anchorPoint);
    }
    auto at = toSheet(document, d.view, anchorPoint, bodies, transforms);
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

    case AnnotationType::FeatureControlFrame: {
        // The cells, in the order ISO 1101 reads them: the characteristic's
        // symbol, then the zone and its size, then the datums in the order
        // they were cited. The order is the meaning -- A|B|C is a different
        // requirement from B|A|C -- so it comes from the stored vector and
        // never from a set.
        const FeatureControlFrame& frame = *d.frame;
        std::vector<std::string> cells;
        cells.emplace_back(symbolOf(frame.characteristic));
        // Enough decimals that no zone is rounded into the frame: two would
        // draw a 0.005 zone as "0.01", a figure twice what the model holds,
        // on the face of the drawing. The same helper the dimension side
        // uses, so the two cannot come to round differently.
        auto magnitude = formatLength(
            frame.tolerance,
            DimensionFormat{.decimals = decimalsWithoutRounding(frame.tolerance, 1),
                            .trailingZeros = false});
        if (!magnitude) {
            return std::unexpected(magnitude.error());
        }
        // A cylindrical zone is written with the diameter sign, which is what
        // tells a reader the zone is a cylinder and not a width.
        cells.push_back((frame.zone == ToleranceZone::Cylindrical ? "Ø" : "") + *magnitude);
        for (const DatumReference& datum : frame.datums) {
            cells.emplace_back(1, datum.letter);
        }

        const double halfHeight = kFrameHalfHeight * height;
        const double padding = kFramePadding * height;
        double x = d.placement.x.si();
        const double y = d.placement.y.si();
        std::vector<double> edges{x};
        for (const std::string& cell : cells) {
            const double width =
                2.0 * padding +
                static_cast<double>(characterCount(cell)) * kCharacterWidth * height;
            x += width;
            edges.push_back(x);
        }
        // The outer frame, then one divider per cell boundary, then the text
        // centred in each cell.
        items.lines.push_back(SceneLine{
            .points = {Point2D{Length::fromSi(edges.front()), Length::fromSi(y - halfHeight)},
                       Point2D{Length::fromSi(edges.back()), Length::fromSi(y - halfHeight)},
                       Point2D{Length::fromSi(edges.back()), Length::fromSi(y + halfHeight)},
                       Point2D{Length::fromSi(edges.front()), Length::fromSi(y + halfHeight)},
                       Point2D{Length::fromSi(edges.front()), Length::fromSi(y - halfHeight)}},
            .style = LineStyle::Continuous});
        for (std::size_t i = 1; i + 1 < edges.size(); ++i) {
            items.lines.push_back(SceneLine{
                .points = {Point2D{Length::fromSi(edges[i]), Length::fromSi(y - halfHeight)},
                           Point2D{Length::fromSi(edges[i]), Length::fromSi(y + halfHeight)}},
                .style = LineStyle::Continuous});
        }
        for (std::size_t i = 0; i < cells.size(); ++i) {
            items.texts.push_back(
                SceneText{.at = Point2D{Length::fromSi(0.5 * (edges[i] + edges[i + 1])),
                                        Length::fromSi(y)},
                          .text = cells[i],
                          .height = d.style.height,
                          .anchor = TextAnchor::MiddleCentre});
        }
        items.append(leader(d.placement, target, height));
        break;
    }

    case AnnotationType::Balloon: {
        // The number is the OCCURRENCE's, resolved through the bill of
        // materials now. Nothing about it is stored, so a balloon cannot show
        // a figure the assembly has moved on from (ADR-022).
        auto item = itemNumberOf(document, d.view, ComponentId::fromValue(d.target.object->value()));
        if (!item) {
            return makeError(item.error().code,
                             std::format("{} ({}) cannot be drawn: {}", annotation->name(), id,
                                         item.error().message));
        }
        const double radius = kBalloonRadius * height;
        std::vector<Point2D> circle;
        circle.reserve(static_cast<std::size_t>(kBalloonSegments) + 1);
        for (int i = 0; i <= kBalloonSegments; ++i) {
            const double angle = 2.0 * std::numbers::pi * static_cast<double>(i) /
                                 static_cast<double>(kBalloonSegments);
            circle.push_back(offsetBy(d.placement, radius * std::cos(angle),
                                      radius * std::sin(angle)));
        }
        items.lines.push_back(SceneLine{.points = std::move(circle),
                                        .style = LineStyle::Continuous});
        items.texts.push_back(SceneText{.at = d.placement,
                                        .text = std::to_string(*item),
                                        .height = d.style.height,
                                        .anchor = TextAnchor::MiddleCentre});
        items.append(leader(d.placement, target, height));
        break;
    }

    case AnnotationType::BomTable:
        // Drawn above, before any target was resolved.
        break;

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

Result<std::vector<char>> undefinedDatums(const Document& document, AnnotationId id) {
    const Annotation* annotation = findAnnotation(document, id);
    if (annotation == nullptr) {
        return notFound(id);
    }
    const AnnotationDefinition& d = annotation->definition();
    if (d.type != AnnotationType::FeatureControlFrame || !d.frame) {
        return std::vector<char>{};
    }
    // Every letter a datum feature symbol gives, anywhere in the document: a
    // datum is a property of the PART, and a frame on one view may cite a
    // datum lettered on another.
    std::string defined;
    for (const AnnotationId other : annotations(document)) {
        const Annotation* candidate = findAnnotation(document, other);
        if (candidate != nullptr && candidate->definition().type == AnnotationType::Datum &&
            candidate->definition().text.size() == 1) {
            defined.push_back(candidate->definition().text.front());
        }
    }
    std::vector<char> missing;
    // In the order the frame cites them, because that order is the
    // requirement and a report about it should read the same way.
    for (const DatumReference& datum : d.frame->datums) {
        if (defined.find(datum.letter) == std::string::npos) {
            missing.push_back(datum.letter);
        }
    }
    return missing;
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

Result<void> resolveAnnotationTarget(const Document& document, AnnotationId id,
                                     const BodyLookup& bodies) {
    const Annotation* annotation = findAnnotation(document, id);
    if (annotation == nullptr) {
        return notFound(id);
    }
    const AnnotationDefinition& d = annotation->definition();

    // draw()'s own branches, in its order, stopping at the point where each
    // one stops naming and starts building (ADR-023).
    if (d.type == AnnotationType::Note) {
        // Words on paper: it reaches no geometry, so there is nothing that
        // could have stopped resolving.
        return {};
    }
    if (d.type == AnnotationType::BomTable) {
        // A table names no point either, but it does name the assembly its
        // view draws -- and that can stop resolving.
        auto bom = billOfMaterials(document, d.view);
        if (!bom) {
            return makeError(bom.error().code,
                             std::format("{} ({}) cannot be drawn: {}", annotation->name(), id,
                                         bom.error().message));
        }
        return {};
    }
    auto anchor = resolveTarget(document, d.target, bodies);
    if (!anchor) {
        return makeError(anchor.error().code,
                         std::format("{} ({}) cannot be drawn: {}", annotation->name(), id,
                                     anchor.error().message));
    }
    if (anchor->occurrence) {
        // An occurrence resolves through its PART, which a configuration does
        // not touch -- so resolveTarget() happily anchors a balloon on a
        // component this configuration suppresses. draw() catches that one
        // step later, by finding no solved transform for it, and that step is
        // out of reach here (ADR-023).
        //
        // Asking whether the occurrence is IN FORCE gets the same answer
        // without the solve, because activeComponents() reads the
        // configuration rather than the solver -- and it is the same call
        // drawnOccurrences() makes.
        const std::vector<ComponentId> active = assembly::activeComponents(document);
        if (std::ranges::find(active, *anchor->occurrence) == active.end()) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("{} ({}) points at {}, which the active configuration "
                                         "does not place",
                                         annotation->name(), id, *anchor->occurrence));
        }
    }
    return {};
}

} // namespace bettercad::drawing
