#include <bettercad/drawing/Dimensions.hpp>

#include <bettercad/assembly/Component.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Datums.hpp>
#include <bettercad/features/FaceReferences.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <format>
#include <utility>

namespace bettercad::drawing {
namespace {

/// How near two directions must be to count as parallel.
///
/// 1e-9 on the cosine, which is the figure the geometry module uses for two
/// directions agreeing. A measurement between planes that are only NEARLY
/// parallel has no single answer, and a tolerance loose enough to accept one
/// would be a tolerance loose enough to give the wrong one.
constexpr double kParallelSi = 1e-9;

[[nodiscard]] std::unexpected<Error> notFound(DimensionId id) {
    return makeError(ErrorCode::NotFound, std::format("{} is not a dimension of this document", id));
}

/// What a target resolves to in model space.
struct Anchor {
    enum class Kind : std::uint8_t { Plane, Axis, Cylinder };
    Kind kind = Kind::Plane;
    Frame3D plane = Frame3D::xy();
    Axis3D axis{};
    Length radius{};

    /// The direction the anchor is "along": a plane's normal, or an axis's
    /// own direction.
    [[nodiscard]] Direction3D direction() const noexcept {
        return kind == Kind::Plane ? plane.normal() : axis.direction;
    }
    [[nodiscard]] Point3D origin() const noexcept {
        return kind == Kind::Plane ? plane.origin() : axis.origin;
    }
};

[[nodiscard]] double dot(const Direction3D& a, const Direction3D& b) noexcept {
    return a.x() * b.x() + a.y() * b.y() + a.z() * b.z();
}

/// Vector3D carries plain SI metres, so everything below works in SI and
/// converts once, at the end, when the answer becomes a Length.
[[nodiscard]] Vector3D between(const Point3D& from, const Point3D& to) noexcept {
    return Vector3D{(to.x - from.x).si(), (to.y - from.y).si(), (to.z - from.z).si()};
}

[[nodiscard]] double along(const Vector3D& v, const Direction3D& d) noexcept {
    return v.x * d.x() + v.y * d.y() + v.z * d.z();
}

/// @p v with everything along @p d taken out of it.
[[nodiscard]] Vector3D perpendicularTo(const Vector3D& v, const Direction3D& d) noexcept {
    const double amount = along(v, d);
    return Vector3D{v.x - amount * d.x(), v.y - amount * d.y(), v.z - amount * d.z()};
}

[[nodiscard]] Result<Anchor> resolveTarget(const Document& document, const DimensionTarget& target,
                                           const features::BodyLookup& bodies) {
    if (target.plane) {
        auto plane = features::resolvePlane(document, *target.plane, bodies);
        if (!plane) {
            return std::unexpected(plane.error());
        }
        return Anchor{.kind = Anchor::Kind::Plane, .plane = *plane};
    }
    if (target.axis) {
        auto axis = features::resolveAxis(document, *target.axis, bodies);
        if (!axis) {
            return std::unexpected(axis.error());
        }
        return Anchor{.kind = Anchor::Kind::Axis, .axis = *axis};
    }
    auto cylinder = features::resolveFaceCylinder(document, *target.cylinder, bodies);
    if (!cylinder) {
        return std::unexpected(cylinder.error());
    }
    return Anchor{.kind = Anchor::Kind::Cylinder, .axis = cylinder->axis, .radius = cylinder->radius};
}

/// The anchor moved by @p motion.
[[nodiscard]] Result<Anchor> moved(const Anchor& anchor, const RigidTransform3D& motion) {
    Anchor result = anchor;
    if (anchor.kind == Anchor::Kind::Plane) {
        auto frame = Frame3D::fromAxes(motion.apply(anchor.plane.origin()),
                                       motion.apply(anchor.plane.xAxis()),
                                       motion.apply(anchor.plane.yAxis()),
                                       motion.apply(anchor.plane.normal()));
        if (!frame) {
            return std::unexpected(frame.error());
        }
        result.plane = *frame;
        return result;
    }
    result.axis = Axis3D{motion.apply(anchor.axis.origin), motion.apply(anchor.axis.direction)};
    return result;
}

/// The vector from @p from to @p to, perpendicular to both.
///
/// This is the whole of what a linear-family dimension measures. Every case
/// needs the two anchors to be parallel, because two things that are not
/// parallel have no ONE distance between them -- the answer would depend on
/// where along them it was taken, and picking a place is the silent wrong
/// answer this milestone exists to avoid.
[[nodiscard]] Result<Vector3D> separation(const Anchor& from, const Anchor& to) {
    const Direction3D a = from.direction();
    const Direction3D b = to.direction();

    if (from.kind == Anchor::Kind::Plane && to.kind == Anchor::Kind::Plane) {
        if (std::abs(std::abs(dot(a, b)) - 1.0) > kParallelSi) {
            return makeError(ErrorCode::FailedPrecondition,
                             "the two planes are not parallel, so there is no one distance "
                             "between them; an angular dimension measures what they do have");
        }
        const double distance = from.plane.signedDistance(to.plane.origin()).si();
        return Vector3D{distance * a.x(), distance * a.y(), distance * a.z()};
    }

    if (from.kind != Anchor::Kind::Plane && to.kind != Anchor::Kind::Plane) {
        if (std::abs(std::abs(dot(a, b)) - 1.0) > kParallelSi) {
            return makeError(ErrorCode::FailedPrecondition,
                             "the two axes are not parallel, so there is no one distance between "
                             "them");
        }
        return perpendicularTo(between(from.origin(), to.origin()), a);
    }

    // One plane and one axis. The axis has to lie parallel to the plane, or
    // it crosses it and the distance is zero somewhere and not zero
    // elsewhere.
    const Anchor& plane = from.kind == Anchor::Kind::Plane ? from : to;
    const Anchor& axis = from.kind == Anchor::Kind::Plane ? to : from;
    const Direction3D& normal = plane.plane.normal();
    if (std::abs(dot(normal, axis.axis.direction)) > kParallelSi) {
        return makeError(ErrorCode::FailedPrecondition,
                         "the axis is not parallel to the plane, so it crosses it and there is no "
                         "one distance between them");
    }
    const double distance = plane.plane.signedDistance(axis.axis.origin).si();
    // The vector runs FROM the first target TO the second, so it turns round
    // when the axis was named first. An ordinate reads its sign, and a
    // dimension that reported the same coordinate on both sides of its datum
    // would say nothing.
    const double sign = &plane == &from ? 1.0 : -1.0;
    return Vector3D{sign * distance * normal.x(), sign * distance * normal.y(),
                    sign * distance * normal.z()};
}

/// The angle a protractor would read between two targets.
///
/// TWO PLANES: the angle between the PLANES, not between their normals --
/// 180 degrees minus the angle the normals make. `resolveFacePlane` faces a
/// face's normal out of the material, so this is the angle measured through
/// the material, which is what an angle marked on a drawing means. It comes
/// out right in every case: two faces of a slab (normals opposed) read 0 and
/// are parallel; two coplanar faces (normals together) read 180 and are flat;
/// a wedge whose faces meet at 53.13 reads 53.13.
///
/// TWO AXES: the angle between the two LINES, folded into [0, 90]. An axis's
/// direction may be stored either way round, and an angle that flipped to its
/// supplement because a datum was defined in the other direction would be a
/// number that depended on how the model was typed rather than on its shape.
///
/// atan2 of the cross against the dot, never acos of the dot: acos loses its
/// precision exactly where two faces are nearly parallel or nearly opposed --
/// which is where an angular dimension is most often placed -- and walks out
/// of its domain into NaN as soon as rounding pushes the dot past 1.
[[nodiscard]] Result<Angle> angleBetween(const Anchor& from, const Anchor& to) {
    if (from.kind == Anchor::Kind::Cylinder || to.kind == Anchor::Kind::Cylinder) {
        return makeError(ErrorCode::InvalidArgument,
                         "an angular dimension measures between planes or between axes");
    }
    const bool planes = from.kind == Anchor::Kind::Plane;
    if (planes != (to.kind == Anchor::Kind::Plane)) {
        return makeError(ErrorCode::InvalidArgument,
                         "an angular dimension measures between two planes or between two axes, "
                         "not between a plane and an axis");
    }
    const Direction3D a = from.direction();
    const Direction3D b = to.direction();
    const double cx = a.y() * b.z() - a.z() * b.y();
    const double cy = a.z() * b.x() - a.x() * b.z();
    const double cz = a.x() * b.y() - a.y() * b.x();
    const double betweenDirections = std::atan2(std::hypot(cx, cy, cz), dot(a, b));
    if (planes) {
        return Angle::fromSi(std::numbers::pi - betweenDirections);
    }
    return Angle::fromSi(betweenDirections > 0.5 * std::numbers::pi
                             ? std::numbers::pi - betweenDirections
                             : betweenDirections);
}

} // namespace

Result<void> checkDimension(const Document& document, const DimensionDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return valid;
    }
    if (findView(document, definition.view) == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("a dimension cannot be measured in {}, which is not a view of "
                                     "this document",
                                     definition.view));
    }
    for (const DimensionTarget* target : {&definition.from, &definition.to}) {
        if (isEmpty(*target)) {
            continue;
        }
        if (target->cylinder) {
            if (auto valid = features::checkFaceName(document, *target->cylinder); !valid) {
                return valid;
            }
            continue;
        }
        for (const ObjectId object : referencedObjects(*target)) {
            if (document.findObject(object) == nullptr) {
                return makeError(ErrorCode::NotFound,
                                 std::format("a dimension cannot measure to {}, which is not an "
                                             "object of this document",
                                             object));
            }
        }
    }
    return {};
}

Result<DimensionId> createDimension(Document& document, std::string name,
                                    const DimensionDefinition& definition) {
    if (auto valid = checkDimension(document, definition); !valid) {
        return std::unexpected(valid.error());
    }
    auto dimension = Dimension::create(std::move(name), definition);
    if (!dimension) {
        return std::unexpected(dimension.error());
    }
    auto id = document.addObject(std::move(*dimension));
    if (!id) {
        return std::unexpected(id.error());
    }
    return DimensionId::fromValue(id->value());
}

Result<bool> setDimensionDefinition(Document& document, DimensionId id,
                                    const DimensionDefinition& definition) {
    if (findDimension(document, id) == nullptr) {
        return notFound(id);
    }
    if (auto valid = checkDimension(document, definition); !valid) {
        return std::unexpected(valid.error());
    }
    return document.modifyObject<Dimension>(
        ObjectId{id}, [&](Dimension& d) { return d.setDefinition(definition); });
}

const Dimension* findDimension(const Document& document, DimensionId id) noexcept {
    return document.findObjectAs<Dimension>(id);
}

std::vector<DimensionId> dimensions(const Document& document) {
    std::vector<DimensionId> found;
    for (const DocumentObject& object : document.objects()) {
        if (dynamic_cast<const Dimension*>(&object) != nullptr) {
            found.push_back(DimensionId::fromValue(object.id().value()));
        }
    }
    return found;
}

std::vector<DimensionId> dimensionsOn(const Document& document, ViewId view) {
    std::vector<DimensionId> found;
    for (const DimensionId id : dimensions(document)) {
        if (findDimension(document, id)->definition().view == view) {
            found.push_back(id);
        }
    }
    return found;
}

Result<void> removeDimension(Document& document, DimensionId id) {
    if (findDimension(document, id) == nullptr) {
        return notFound(id);
    }
    auto removed = document.removeObject(ObjectId{id});
    if (!removed) {
        return std::unexpected(removed.error());
    }
    return {};
}

Result<MeasuredDimension> measure(const Document& document, DimensionId id,
                                  const BodyLookup& bodies, const TransformLookup& transforms) {
    const Dimension* dimension = findDimension(document, id);
    if (dimension == nullptr) {
        return notFound(id);
    }
    const DimensionDefinition& d = dimension->definition();

    auto from = resolveTarget(document, d.from, bodies);
    if (!from) {
        return makeError(from.error().code,
                         std::format("{} ({}) cannot be measured: {}", dimension->name(), id,
                                     from.error().message));
    }
    std::optional<Anchor> to;
    if (!isSingleTarget(d.type)) {
        auto resolved = resolveTarget(document, d.to, bodies);
        if (!resolved) {
            return makeError(resolved.error().code,
                             std::format("{} ({}) cannot be measured: {}", dimension->name(), id,
                                         resolved.error().message));
        }
        to = *resolved;
    }

    // A view of a component measures the part where the SOLVER put it. That
    // changes nothing about a length or an angle, which a rigid motion leaves
    // alone, and everything about which view axis a distance runs along.
    auto source = effectiveSource(document, d.view);
    if (!source) {
        return std::unexpected(source.error());
    }
    if (isInternal(*source)) {
        const auto* component = document.findObjectAs<assembly::Component>(
            ComponentId::fromValue(source->object.value()));
        if (component != nullptr) {
            const RigidTransform3D* solved =
                transforms ? transforms(component->componentId()) : nullptr;
            if (solved == nullptr) {
                return makeError(ErrorCode::FailedPrecondition,
                                 std::format("{} measures {}, which has no solved transform; the "
                                             "assembly did not solve",
                                             id, source->object));
            }
            auto movedFrom = moved(*from, *solved);
            if (!movedFrom) {
                return std::unexpected(movedFrom.error());
            }
            from = *movedFrom;
            if (to) {
                auto movedTo = moved(*to, *solved);
                if (!movedTo) {
                    return std::unexpected(movedTo.error());
                }
                to = *movedTo;
            }
        }
    }

    MeasuredDimension measured;

    if (d.type == DimensionType::Radius || d.type == DimensionType::Diameter) {
        // Diameter is twice the radius by the ONE path, so the two can never
        // disagree about the same face.
        const double factor = d.type == DimensionType::Diameter ? 2.0 : 1.0;
        measured.length = Length::fromSi(from->radius.si() * factor);
    } else if (d.type == DimensionType::Angular) {
        auto angle = angleBetween(*from, *to);
        if (!angle) {
            return makeError(angle.error().code,
                             std::format("{} ({}) cannot be measured: {}", dimension->name(), id,
                                         angle.error().message));
        }
        measured.angle = *angle;
    } else {
        auto vector = separation(*from, *to);
        if (!vector) {
            return makeError(vector.error().code,
                             std::format("{} ({}) cannot be measured: {}", dimension->name(), id,
                                         vector.error().message));
        }
        if (d.type == DimensionType::Linear) {
            measured.length = Length::fromSi(std::hypot(vector->x, vector->y, vector->z));
        } else {
            // Everything else is read off the VIEW's own axes, so the view
            // has to say what they are. Its scale plays no part: a scale
            // changes how big the drawing is, never how big the part is.
            auto basis = effectiveBasis(document, d.view);
            if (!basis) {
                return std::unexpected(basis.error());
            }
            const double x = along(*vector, basis->xAxis());
            const double y = along(*vector, basis->yAxis());
            switch (d.type) {
            case DimensionType::Horizontal:
                measured.length = Length::fromSi(std::abs(x));
                break;
            case DimensionType::Vertical:
                measured.length = Length::fromSi(std::abs(y));
                break;
            case DimensionType::Aligned:
                measured.length = Length::fromSi(std::hypot(x, y));
                break;
            case DimensionType::Ordinate:
                // Signed: an ordinate says which side of the datum it is on,
                // and a negative one is information rather than an error.
                measured.length = Length::fromSi(d.ordinate == OrdinateAxis::X ? x : y);
                break;
            default:
                return makeError(ErrorCode::Internal,
                                 std::format("{} has a type this build does not measure", id));
            }
        }
    }

    auto text = measured.angle ? formatAngle(*measured.angle, d.format)
                               : formatLength(*measured.length, d.format);
    if (!text) {
        return makeError(text.error().code,
                         std::format("{} ({}) cannot be written: {}", dimension->name(), id,
                                     text.error().message));
    }
    measured.text = std::move(*text);

    // The tolerance, from the ONE interval. Both presentations are written
    // from the same numbers, so "20 +/-0.05" and the pair "20.05 / 19.95"
    // cannot come to describe different parts.
    if (d.tolerance) {
        // An angular dimension cannot carry one -- validate() refuses it
        // whether the definition was built in memory or read from a file --
        // so this holds. It is checked rather than assumed because the cost
        // of being wrong is reading an empty optional.
        if (!measured.length) {
            return makeError(ErrorCode::Internal,
                             std::format("{} ({}) carries a tolerance but measures no length",
                                         dimension->name(), id));
        }
        auto interval = intervalOf(*measured.length, *d.tolerance);
        if (!interval) {
            return makeError(interval.error().code,
                             std::format("{} ({}) cannot be written: {}", dimension->name(), id,
                                         interval.error().message));
        }
        measured.interval = *interval;
        if (d.tolerance->display == ToleranceDisplay::Limits) {
            // The precision comes from the DEVIATIONS, not from the limits:
            // the limits carry the measured length, which need not be a round
            // number, and asking how many decimals 111.803399 needs would put
            // six of them on the drawing. What must survive being written is
            // the tolerance.
            auto deviations = deviationsOf(*measured.length, *d.tolerance);
            if (!deviations) {
                return std::unexpected(deviations.error());
            }
            DimensionFormat precise = d.format;
            precise.decimals =
                std::max(decimalsWithoutRounding(deviations->lower, d.format.decimals),
                         decimalsWithoutRounding(deviations->upper, d.format.decimals));
            auto upper = formatLength(interval->upper, precise);
            if (!upper) {
                return std::unexpected(upper.error());
            }
            auto lower = formatLength(interval->lower, precise);
            if (!lower) {
                return std::unexpected(lower.error());
            }
            // Larger over smaller, as a drawing stacks them.
            measured.text = std::move(*upper);
            measured.lowerText = std::move(*lower);
        } else {
            auto suffix = formatTolerance(*d.tolerance, d.format);
            if (!suffix) {
                return std::unexpected(suffix.error());
            }
            measured.text += ' ';
            measured.text += *suffix;
        }
    }
    return measured;
}

Result<void> resolveDimensionTargets(const Document& document, DimensionId id,
                                     const BodyLookup& bodies) {
    const Dimension* dimension = findDimension(document, id);
    if (dimension == nullptr) {
        return notFound(id);
    }
    const DimensionDefinition& d = dimension->definition();

    // measure()'s own opening, to the line -- the same resolver on the same
    // targets, with the same wording, stopping before the geometry (ADR-023).
    auto from = resolveTarget(document, d.from, bodies);
    if (!from) {
        return makeError(from.error().code,
                         std::format("{} ({}) cannot be measured: {}", dimension->name(), id,
                                     from.error().message));
    }
    if (!isSingleTarget(d.type)) {
        auto to = resolveTarget(document, d.to, bodies);
        if (!to) {
            return makeError(to.error().code,
                             std::format("{} ({}) cannot be measured: {}", dimension->name(), id,
                                         to.error().message));
        }
    }
    return {};
}

// --- What a dimension DRAWS (P14-EXPORT-001) ---------------------------------------------------
//
// ISO 129's shape, in sheet millimetres, built once here so that three writers
// transcribe it instead of three writers each constructing it (ADR-016).
//
// Every size below is PAPER size and none of them meets a view's scale. Only
// the measured points are scaled, because only they are geometry.

namespace {

/// An arrowhead's length on paper, and its half-width. ISO 128 draws the head
/// long and narrow; these are the proportions Annotations.cpp already uses, so
/// a dimension's arrow and a leader's arrow are the same arrow.
constexpr double kArrowLength = 0.0035;    // 3.5 mm
constexpr double kArrowHalfWidth = 0.0009; // 0.9 mm

/// The gap an extension line leaves at the geometry, and how far it runs past
/// the dimension line.
constexpr double kExtensionGap = 0.001;      // 1 mm
constexpr double kExtensionBeyond = 0.002;   // 2 mm
/// How far a leader's elbow runs before the text.
constexpr double kElbow = 0.005;             // 5 mm
/// Default lettering, when the dimension does not say.
constexpr double kTextHeight = 0.0035;       // 3.5 mm

struct Vector2 {
    double x = 0.0;
    double y = 0.0;
};

[[nodiscard]] Vector2 minus(const Point2D& a, const Point2D& b) {
    return Vector2{a.x.si() - b.x.si(), a.y.si() - b.y.si()};
}
[[nodiscard]] double length(const Vector2& v) { return std::hypot(v.x, v.y); }
[[nodiscard]] Point2D offsetBy(const Point2D& from, double dx, double dy) {
    return Point2D{from.x + Length::fromSi(dx), from.y + Length::fromSi(dy)};
}

/// A solid arrowhead at @p tip pointing along (@p ux, @p uy), as a closed
/// triangle. A polyline rather than a filled region, because the scene has no
/// fill and a drawn triangle reads identically at drawing line widths.
[[nodiscard]] SceneLine arrowhead(const Point2D& tip, double ux, double uy) {
    const Point2D back = offsetBy(tip, -ux * kArrowLength, -uy * kArrowLength);
    // The perpendicular, for the two barbs.
    const Point2D left = offsetBy(back, -uy * kArrowHalfWidth, ux * kArrowHalfWidth);
    const Point2D right = offsetBy(back, uy * kArrowHalfWidth, -ux * kArrowHalfWidth);
    return SceneLine{.points = {tip, left, right, tip}, .style = LineStyle::Continuous};
}

/// A leader from @p at to @p to, with an elbow, an arrowhead at the geometry,
/// and the text sitting on the elbow.
[[nodiscard]] SceneItems leaderTo(const Point2D& at, const Point2D& to, const std::string& text,
                                  Length height) {
    SceneItems items;
    const Vector2 along = minus(to, at);
    const double span = length(along);
    if (span > 1e-12) {
        items.lines.push_back(arrowhead(at, along.x / span, along.y / span));
    }
    const bool leftward = to.x.si() < at.x.si();
    const Point2D elbow = offsetBy(to, leftward ? -kElbow : kElbow, 0.0);
    items.lines.push_back(SceneLine{.points = {at, to, elbow}, .style = LineStyle::Thin});
    items.texts.push_back(SceneText{.at = offsetBy(elbow, leftward ? -0.0005 : 0.0005, 0.0005),
                                    .text = text,
                                    .height = height,
                                    .anchor = leftward ? TextAnchor::BaselineRight
                                                       : TextAnchor::BaselineLeft});
    return items;
}

} // namespace

Result<SceneItems> drawDimension(const Document& document, DimensionId id, const BodyLookup& bodies,
                                 const TransformLookup& transforms) {
    const Dimension* dimension = findDimension(document, id);
    if (dimension == nullptr) {
        return notFound(id);
    }
    const DimensionDefinition& d = dimension->definition();

    // The VALUE is measure()'s. This lays it out and never works it out, so a
    // dimension drawn after the model moved shows the new number for exactly
    // the reason it always did.
    auto measured = measure(document, id, bodies, transforms);
    if (!measured) {
        return std::unexpected(measured.error());
    }
    // A dimension carries no lettering style of its own -- ISO 3098 makes it
    // a drawing-wide choice rather than a per-dimension one -- so the sheet's
    // figure is used.
    const Length height = Length::fromSi(kTextHeight);

    // Where the measured geometry lands on the page.
    auto from = resolveTarget(document, d.from, bodies);
    if (!from) {
        return makeError(from.error().code,
                         std::format("{} ({}) cannot be drawn: {}", dimension->name(), id,
                                     from.error().message));
    }
    auto fromSheet = toSheet(document, d.view, from->origin(), bodies, transforms);
    if (!fromSheet) {
        return std::unexpected(fromSheet.error());
    }

    const bool linearFamily = d.type == DimensionType::Linear || d.type == DimensionType::Horizontal ||
                              d.type == DimensionType::Vertical || d.type == DimensionType::Aligned;
    if (!linearFamily) {
        // A radius, a diameter, an ordinate or an angle: a leader to the text.
        // That is what those need, and it is all this layer can honestly draw
        // for an angle -- see the header.
        SceneItems items = leaderTo(*fromSheet, d.placement, measured->text, height);
        if (auto valid = validate(items); !valid) {
            return std::unexpected(valid.error());
        }
        return items;
    }

    auto to = resolveTarget(document, d.to, bodies);
    if (!to) {
        return makeError(to.error().code,
                         std::format("{} ({}) cannot be drawn: {}", dimension->name(), id,
                                     to.error().message));
    }
    auto toSheetPoint = toSheet(document, d.view, to->origin(), bodies, transforms);
    if (!toSheetPoint) {
        return std::unexpected(toSheetPoint.error());
    }
    const Point2D a = *fromSheet;
    const Point2D b = *toSheetPoint;

    // Which way the dimension line runs. Horizontal and Vertical are the
    // sheet's own axes; Linear and Aligned run along what is measured.
    Vector2 along{1.0, 0.0};
    if (d.type == DimensionType::Vertical) {
        along = Vector2{0.0, 1.0};
    } else if (d.type != DimensionType::Horizontal) {
        const Vector2 span = minus(b, a);
        const double size = length(span);
        if (size < 1e-12) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("{} ({}) measures between two points that draw at the same "
                                         "place, so it has no direction to run along",
                                         dimension->name(), id));
        }
        along = Vector2{span.x / size, span.y / size};
    }
    // The normal the dimension line is offset along.
    const Vector2 normal{-along.y, along.x};
    const Vector2 toPlacement = minus(d.placement, a);
    const double offset = toPlacement.x * normal.x + toPlacement.y * normal.y;

    // The two ends of the dimension line: each measured point, moved onto the
    // line the placement chose.
    const auto onLine = [&](const Point2D& p) {
        const Vector2 fromA = minus(p, a);
        const double runs = fromA.x * along.x + fromA.y * along.y;
        return offsetBy(a, along.x * runs + normal.x * offset, along.y * runs + normal.y * offset);
    };
    const Point2D a1 = onLine(a);
    const Point2D b1 = onLine(b);

    SceneItems items;
    // Extension lines: from just off the geometry to just past the dimension
    // line, so the drawing does not touch the part.
    const auto extension = [&](const Point2D& at, const Point2D& end) {
        const Vector2 out = minus(end, at);
        const double span = length(out);
        if (span < kExtensionGap) {
            return; // the dimension line sits on the geometry; no extension to draw
        }
        const double ux = out.x / span;
        const double uy = out.y / span;
        items.lines.push_back(
            SceneLine{.points = {offsetBy(at, ux * kExtensionGap, uy * kExtensionGap),
                                 offsetBy(end, ux * kExtensionBeyond, uy * kExtensionBeyond)},
                      .style = LineStyle::Thin});
    };
    extension(a, a1);
    extension(b, b1);

    // The dimension line, and an arrowhead at each end pointing OUTWARD.
    items.lines.push_back(SceneLine{.points = {a1, b1}, .style = LineStyle::Thin});
    const Vector2 run = minus(b1, a1);
    const double runLength = length(run);
    if (runLength > 1e-12) {
        const double ux = run.x / runLength;
        const double uy = run.y / runLength;
        items.lines.push_back(arrowhead(a1, -ux, -uy));
        items.lines.push_back(arrowhead(b1, ux, uy));
    }

    // The value, on the dimension line, reading along it.
    const double radians = std::atan2(along.y, along.x);
    // ISO 129 reads a dimension from the bottom or the right, never upside
    // down, so a line running leftward is lettered the other way up.
    const double quarter = std::numbers::pi / 2.0;
    const double readable = radians > quarter ? radians - std::numbers::pi
                                              : (radians <= -quarter ? radians + std::numbers::pi : radians);
    items.texts.push_back(SceneText{.at = d.placement,
                                    .text = measured->text,
                                    .height = height,
                                    .rotation = Angle::fromSi(readable),
                                    .anchor = TextAnchor::MiddleCentre});
    if (!measured->lowerText.empty()) {
        // Limits: the upper is the value, the lower goes under it.
        items.texts.push_back(
            SceneText{.at = offsetBy(d.placement, 0.0, -height.si() * 1.2),
                      .text = measured->lowerText,
                      .height = height,
                      .rotation = Angle::fromSi(readable),
                      .anchor = TextAnchor::MiddleCentre});
    }

    if (auto valid = validate(items); !valid) {
        return std::unexpected(valid.error());
    }
    return items;
}

Result<SceneItems> drawDimensions(const Document& document, ViewId view, const BodyLookup& bodies,
                                  const TransformLookup& transforms) {
    SceneItems items;
    for (const DimensionId id : dimensionsOn(document, view)) {
        auto drawn = drawDimension(document, id, bodies, transforms);
        if (!drawn) {
            return std::unexpected(drawn.error());
        }
        items.append(*drawn);
    }
    return items;
}

} // namespace bettercad::drawing
