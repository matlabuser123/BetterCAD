#include "features/SolidSupport.hpp"
#include "features/pattern/PatternSupport.hpp"

#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Hole.hpp>
#include <bettercad/core/geometry/Transform.hpp>
#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/CircularPatternFeature.hpp>
#include <bettercad/features/FilletFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/MirrorFeature.hpp>
#include <bettercad/features/Regeneration.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::features {

namespace {

/// Negative zero as 0, for messages.
double tidy(double value) {
    return value == 0.0 ? 0.0 : value;
}

std::string describe(const MirrorReflection& reflection) {
    const Point3D& p = reflection.point;
    const Direction3D& n = reflection.normal;
    return std::format("the plane through ({:.6g}, {:.6g}, {:.6g}) mm facing ({:.6g}, {:.6g}, {:.6g})",
                       tidy(p.x.in(units::mm)), tidy(p.y.in(units::mm)), tidy(p.z.in(units::mm)), tidy(n.x()),
                       tidy(n.y()), tidy(n.z()));
}

double dot(const Direction3D& a, double x, double y, double z) {
    return a.x() * x + a.y() * y + a.z() * z;
}

/// A hole the plane maps onto itself would be drilled a second time where it
/// already is. It is, when the mirrored hole has the same axis line (a through
/// hole) or the same entry point and direction (a blind one), to the
/// tolerances of face matching (1e-7 mm, 1e-9 rad).
Result<void> checkHoleImage(const HoleFeature& hole, const Document& document, const MirrorReflection& reflection) {
    auto request = resolveHoleRequest(hole.definition(), document);
    if (!request) {
        return {}; // reported when the hole's operation is resolved
    }
    const geometry::HoleRequest image = geometry::transformed(*request, reflection.motion);
    const Point3D a = geometry::facePoint(request->face, request->center);
    const Point3D b = geometry::facePoint(image.face, image.center);
    const Direction3D& da = request->face.normal;
    const Direction3D& db = image.face.normal;
    const double cx = da.y() * db.z() - da.z() * db.y();
    const double cy = da.z() * db.x() - da.x() * db.z();
    const double cz = da.x() * db.y() - da.y() * db.x();
    const bool parallel = std::sqrt(cx * cx + cy * cy + cz * cz) < 1e-9;
    const double dx = (b.x - a.x).in(units::mm);
    const double dy = (b.y - a.y).in(units::mm);
    const double dz = (b.z - a.z).in(units::mm);
    const double along = dot(da, dx, dy, dz);
    const double offAxis = std::sqrt(std::max(0.0, dx * dx + dy * dy + dz * dz - along * along));
    const bool sameHole = request->extent == geometry::HoleExtent::Through
                              ? parallel && offAxis <= 1e-7
                              : parallel && dot(da, db.x(), db.y(), db.z()) > 0.0 && std::hypot(dx, dy, dz) <= 1e-7;
    if (sameHole) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("the mirror image across {} is the hole '{}' itself: the plane maps the hole "
                                     "centred at ({:.6g}, {:.6g}, {:.6g}) mm onto itself, so there is nothing to "
                                     "mirror",
                                     describe(reflection), hole.name(), tidy(a.x.in(units::mm)),
                                     tidy(a.y.in(units::mm)), tidy(a.z.in(units::mm))));
    }
    return {};
}

/// A chamfer or fillet (@p kind, done to its edges: @p done) whose edge the
/// plane maps onto one of the feature's own edges would be applied again to
/// an edge it has already consumed. The references are compared as
/// findEdges() compares them (1e-7 mm, 1e-9 rad).
Result<void> checkEdgeImages(std::string_view kind, std::string_view done, std::string_view name,
                             const std::vector<geometry::EdgeSignature>& edges, const MirrorReflection& reflection) {
    for (std::size_t i = 0; i < edges.size(); ++i) {
        const geometry::EdgeSignature image = geometry::transformed(edges[i], reflection.motion);
        for (std::size_t j = 0; j < edges.size(); ++j) {
            if (!geometry::sameCurve(image, edges[j])) {
                continue;
            }
            const std::string which = i == j ? std::string{"that edge itself"}
                                             : std::format("its edge reference {} ({})", j + 1,
                                                           geometry::describe(edges[j]));
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("the mirror image across {} of edge reference {} ({}) of the {} '{}' is {}, "
                                         "which is already {}, so there is nothing to mirror onto",
                                         describe(reflection), i + 1, geometry::describe(edges[i]), kind, name,
                                         which, done));
        }
    }
    return {};
}

} // namespace

Result<MirrorReflection> resolveMirrorReflection(const MirrorDefinition& definition, const Document& document) {
    const MirrorPlane& plane = definition.plane;
    const auto normal = Direction3D::fromComponents(plane.normal.x, plane.normal.y, plane.normal.z);
    if (!normal || !isFinite(plane.origin.x) || !isFinite(plane.origin.y) || !isFinite(plane.origin.z)) {
        return makeError(ErrorCode::InvalidArgument, "the plane needs a finite origin and a finite, non-zero normal");
    }
    Length offset = plane.offset;
    if (plane.offsetParameter) {
        auto value = detail::drivingValue<Length>(document, *plane.offsetParameter, "offset parameter");
        if (!value) {
            return std::unexpected(value.error());
        }
        offset = *value;
    }
    if (!isFinite(offset)) {
        return makeError(ErrorCode::InvalidArgument, "the plane's offset must be finite");
    }
    return mirrorReflection(plane.origin + Translation3D::along(*normal, offset), *normal);
}

Result<geometry::Body> regenerateMirror(const MirrorFeature& feature, const Document& document,
                                        const geometry::Body* target) {
    const MirrorDefinition& definition = feature.definition();
    const auto build = [&](const geometry::Body& sourceBody) -> Result<geometry::Body> {
        auto reflection = resolveMirrorReflection(definition, document);
        if (!reflection) {
            return std::unexpected(reflection.error());
        }
        const DocumentObject* source = document.findObject(ObjectId{definition.source});
        if (source == nullptr) {
            return makeError(ErrorCode::NotFound, std::format("the source {} does not exist", ObjectId{definition.source}));
        }
        // Instance 0 is the source's body; instance 1 its mirror image.
        const std::vector<detail::PatternPlacement> image{
            {.motion = reflection->motion, .label = std::format("the mirror image across {}", describe(*reflection))}};

        if (definition.scope == MirrorScope::Body) {
            const bool keep = definition.keepOriginal;
            const detail::InstanceOperation mirrorBody =
                [&sourceBody, keep](const geometry::Body& body, const RigidTransform3D& motion) -> Result<geometry::Body> {
                auto mirrored = geometry::transformed(sourceBody, motion);
                if (!mirrored || !keep) {
                    return mirrored;
                }
                return geometry::booleanUnion(body, *mirrored);
            };
            return detail::buildPattern(sourceBody, mirrorBody, image);
        }

        // The feature scope repeats the source's own operation, as a pattern
        // does, so nesting is refused as in patterns; the whole body of a
        // pattern or mirror can be mirrored instead.
        if (dynamic_cast<const LinearPatternFeature*>(source) != nullptr ||
            dynamic_cast<const CircularPatternFeature*>(source) != nullptr ||
            dynamic_cast<const MirrorFeature*>(source) != nullptr) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("a feature mirror cannot repeat a {}; mirror its body instead",
                                         source->typeName()));
        }
        Result<void> distinct{};
        if (const auto* hole = dynamic_cast<const HoleFeature*>(source)) {
            distinct = checkHoleImage(*hole, document, *reflection);
        } else if (const auto* chamfer = dynamic_cast<const ChamferFeature*>(source)) {
            distinct = checkEdgeImages("chamfer", "chamfered", chamfer->name(), chamfer->definition().edges,
                                       *reflection);
        } else if (const auto* fillet = dynamic_cast<const FilletFeature*>(source)) {
            distinct = checkEdgeImages("fillet", "filleted", fillet->name(), fillet->definition().edges, *reflection);
        }
        if (!distinct) {
            return std::unexpected(distinct.error());
        }
        auto apply = detail::instanceOperation(*source, document, "mirror", "");
        if (!apply) {
            return std::unexpected(apply.error());
        }
        return detail::buildPattern(sourceBody, *apply, image);
    };
    const auto mirror = [&](const geometry::Body& sourceBody) -> Result<geometry::Body> {
        auto body = build(sourceBody);
        if (!body) {
            return makeError(body.error().code, std::format("mirror: {}", body.error().message));
        }
        return body;
    };
    return detail::applyToTargetBody(feature.name(), "mirror", target, mirror, "source");
}

} // namespace bettercad::features
