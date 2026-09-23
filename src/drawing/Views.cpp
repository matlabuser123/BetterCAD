#include <bettercad/drawing/Views.hpp>

#include <bettercad/core/geometry/HiddenLine.hpp>

#include <bettercad/core/document/Document.hpp>
#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Transform.hpp>
#include <bettercad/drawing/Sheets.hpp>

#include <algorithm>
#include <optional>
#include <format>
#include <utility>

namespace bettercad::drawing {
namespace {

/// How deep a parent chain may go before it is called a cycle.
///
/// The dependency graph detects a real cycle, but these walks run outside
/// regeneration too, and a walk that never terminates is worse than one that
/// gives up with a message.
constexpr int kMaxChain = 64;

[[nodiscard]] std::unexpected<Error> notFound(ViewId id) {
    return makeError(ErrorCode::NotFound, std::format("there is no view {}", id));
}

} // namespace

Result<void> checkView(const Document& document, const ViewDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return valid;
    }
    if (findSheet(document, definition.sheet) == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("a view cannot sit on {}, which is not a sheet of this document",
                                     definition.sheet));
    }

    if (isBaseView(definition)) {
        if (!isInternal(definition.source)) {
            // Cannot be checked here: the owning document may not be
            // available, and reaching for it behind the caller's back is the
            // implicit filesystem access ADR-003 forbids.
            return {};
        }
        const DocumentObject* source = document.findObject(definition.source.object);
        if (source == nullptr) {
            return makeError(ErrorCode::NotFound,
                             std::format("a view cannot draw {}, which is not an object of this document",
                                         definition.source.object));
        }
        return {};
    }

    const View* parent = findView(document, *definition.parent);
    if (parent == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("a {} view cannot be derived from {}, which is not a view of "
                                     "this document",
                                     toString(definition.kind), *definition.parent));
    }
    // Aligning against a view on another sheet would align against something
    // that is not on the page.
    if (parent->definition().sheet != definition.sheet) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("a {} view and its parent {} must be on the same sheet",
                                     toString(definition.kind), *definition.parent));
    }
    return {};
}

Result<ViewId> createView(Document& document, std::string name, const ViewDefinition& definition) {
    if (auto valid = checkView(document, definition); !valid) {
        return std::unexpected(valid.error());
    }
    auto view = View::create(std::move(name), definition);
    if (!view) {
        return std::unexpected(view.error());
    }
    auto id = document.addObject(std::move(*view));
    if (!id) {
        return std::unexpected(id.error());
    }
    return ViewId::fromValue(id->value());
}

Result<bool> setViewDefinition(Document& document, ViewId id, const ViewDefinition& definition) {
    if (findView(document, id) == nullptr) {
        return notFound(id);
    }
    // A view may not be projected from itself, nor from anything that is
    // projected from it. The dependency graph would eventually call the
    // second a cycle, but the derivations below walk the parent chain
    // directly and outside regeneration, so an unbroken loop would recurse
    // until the stack ran out rather than produce a diagnostic.
    if (definition.parent && *definition.parent == id) {
        // The commonest mistake gets the clearest message rather than being
        // folded into the general loop case.
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} cannot be projected from itself", id));
    }
    if (definition.parent) {
        ViewId walk = *definition.parent;
        for (int step = 0; step <= kMaxChain; ++step) {
            if (walk == id) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("{} cannot be projected from {}: that would make a "
                                             "loop of views, each derived from the next",
                                             id, *definition.parent));
            }
            const View* view = findView(document, walk);
            if (view == nullptr || isBaseView(view->definition())) {
                break;
            }
            walk = *view->definition().parent;
        }
    }
    if (auto valid = checkView(document, definition); !valid) {
        return std::unexpected(valid.error());
    }
    auto changed = document.modifyObject<View>(
        id, [&](View& view) { return view.setDefinition(definition).value_or(false); });
    if (!changed) {
        return std::unexpected(changed.error());
    }
    return *changed;
}

const View* findView(const Document& document, ViewId id) noexcept {
    return document.findObjectAs<View>(id);
}

std::vector<ViewId> views(const Document& document) {
    std::vector<ViewId> found;
    for (const DocumentObject& object : document.objects()) {
        if (dynamic_cast<const View*>(&object) != nullptr) {
            found.push_back(ViewId::fromValue(object.id().value()));
        }
    }
    return found;
}

std::vector<ViewId> viewsOn(const Document& document, SheetId sheet) {
    std::vector<ViewId> found;
    for (const ViewId id : views(document)) {
        const View* view = findView(document, id);
        if (view != nullptr && view->definition().sheet == sheet) {
            found.push_back(id);
        }
    }
    return found;
}

Result<void> removeView(Document& document, ViewId id) {
    if (findView(document, id) == nullptr) {
        return notFound(id);
    }
    for (const ViewId other : views(document)) {
        const View* view = findView(document, other);
        if (view != nullptr && view->definition().parent == std::optional<ViewId>{id}) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("{} cannot be removed while {} is projected from it", id,
                                         other));
        }
    }
    auto removed = document.removeObject(id);
    if (!removed) {
        return std::unexpected(removed.error());
    }
    return {};
}

Result<ObjectReference> effectiveSource(const Document& document, ViewId id) {
    ViewId current = id;
    for (int step = 0; step < kMaxChain; ++step) {
        const View* view = findView(document, current);
        if (view == nullptr) {
            return notFound(current);
        }
        if (isBaseView(view->definition())) {
            return view->definition().source;
        }
        current = *view->definition().parent;
    }
    return makeError(ErrorCode::FailedPrecondition,
                     std::format("{} is projected through more than {} views; the chain does not "
                                 "reach a base view",
                                 id, kMaxChain));
}

namespace {

/// Depth-limited so a chain that loops gives a diagnostic rather than
/// exhausting the stack. setViewDefinition refuses to build such a loop, and
/// this is the second line: a document loaded from a file has not been
/// through that check.
[[nodiscard]] Result<ViewBasis> basisAt(const Document& document, ViewId id, int depth) {
    if (depth > kMaxChain) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{} is projected through more than {} views; the chain loops or "
                                     "does not reach a base view",
                                     id, kMaxChain));
    }
    const View* view = findView(document, id);
    if (view == nullptr) {
        return notFound(id);
    }
    const ViewDefinition& d = view->definition();
    switch (d.kind) {
    case ViewKind::Base:
        return basisOf(*d.orientation);
    case ViewKind::Section:
        // The cutting plane's own frame, at the model origin like every other
        // view basis. A section view cannot be oriented against its own cut,
        // because its orientation IS the cut (ADR-011, ADR-013).
        return Frame3D::create(Point3D{}, d.section->normal, d.section->reference);
    case ViewKind::Auxiliary:
        return Frame3D::create(Point3D{}, d.auxiliary->normal, d.auxiliary->reference);
    case ViewKind::Projected:
    case ViewKind::Detail:
        break;
    }
    // Derived from the parent's, every time. A projected view has no
    // orientation of its own to contradict its parent with (ADR-018), and a
    // detail view looks exactly the way its parent does -- only closer.
    auto parent = basisAt(document, *d.parent, depth + 1);
    if (!parent) {
        return std::unexpected(parent.error());
    }
    if (d.kind == ViewKind::Detail) {
        return *parent;
    }
    return projectedBasis(*parent, *d.direction);
}

} // namespace

Result<ViewBasis> effectiveBasis(const Document& document, ViewId id) {
    return basisAt(document, id, 0);
}

Result<DrawingScale> effectiveScale(const Document& document, ViewId id) {
    const View* view = findView(document, id);
    if (view == nullptr) {
        return notFound(id);
    }
    if (view->definition().scale) {
        return *view->definition().scale;
    }
    const Sheet* sheet = findSheet(document, view->definition().sheet);
    if (sheet == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("{} sits on {}, which is not a sheet of this document", id,
                                     view->definition().sheet));
    }
    return sheet->definition().scale;
}

namespace {

[[nodiscard]] Result<Point2D> placementAt(const Document& document, ViewId id, int depth);
[[nodiscard]] Result<ProjectedGeometry> detailGeometry(const Document& document, ViewId id,
                                                       const BodyLookup& bodies,
                                                       const TransformLookup& transforms,
                                                       const HiddenLineSettings& settings,
                                                       int depth);
[[nodiscard]] Result<ProjectedGeometry> viewGeometry(const Document& document, ViewId id,
                                                     const BodyLookup& bodies,
                                                     const TransformLookup& transforms,
                                                     const HiddenLineSettings& settings,
                                                     int depth);

} // namespace

Result<Point2D> effectivePlacement(const Document& document, ViewId id) {
    return placementAt(document, id, 0);
}

namespace {

/// The body a view draws, in model space.
///
/// A view of a COMPONENT draws that component's part, moved to where the
/// solver put it -- never to where its canonical placement asks for it to go
/// (ADR-005). A view of a feature draws the feature's own body.
[[nodiscard]] Result<geometry::Body> bodyForView(const Document& document, ViewId id,
                                                 const ObjectReference& source,
                                                 const BodyLookup& bodies,
                                                 const TransformLookup& transforms) {
    const auto* component = document.findObjectAs<assembly::Component>(
        ComponentId::fromValue(source.object.value()));
    if (component != nullptr) {
        const ObjectReference& part = component->definition().part;
        if (!isInternal(part)) {
            return makeError(ErrorCode::NotFound,
                             std::format("{} draws {}, which places a part in another document", id,
                                         source.object));
        }
        const geometry::Body* partBody = bodies ? bodies(part.object) : nullptr;
        if (partBody == nullptr || partBody->isEmpty()) {
            return makeError(ErrorCode::NotFound,
                             std::format("{} draws {}, whose part produced no body", id,
                                         source.object));
        }
        const RigidTransform3D* solved = transforms ? transforms(component->componentId()) : nullptr;
        if (solved == nullptr) {
            // No solved transform is not "draw it at the origin": the
            // assembly did not solve, and a view drawn from an unsolved
            // assembly would be a picture of a machine nobody assembled.
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("{} draws {}, which has no solved transform; the assembly "
                                         "did not solve",
                                         id, source.object));
        }
        return geometry::transformed(*partBody, *solved);
    }
    const geometry::Body* body = bodies ? bodies(source.object) : nullptr;
    if (body == nullptr || body->isEmpty()) {
        return makeError(ErrorCode::NotFound,
                         std::format("{} draws {}, which produced no body", id, source.object));
    }
    return *body;
}

/// Where the segment from @p a to @p b enters and leaves a circle, as the two
/// parameters along it, or nothing when it misses.
///
/// Solving |a + t(b - a) - centre|^2 = r^2 rather than sampling, so a segment
/// that crosses the region is clipped exactly at the boundary instead of at
/// whichever sample happened to fall nearest it.
[[nodiscard]] std::optional<std::pair<double, double>> circleSpan(const Point2D& a, const Point2D& b,
                                                                  const Point2D& centre,
                                                                  Length radius) noexcept {
    const double dx = (b.x - a.x).si();
    const double dy = (b.y - a.y).si();
    const double fx = (a.x - centre.x).si();
    const double fy = (a.y - centre.y).si();
    const double r = radius.si();
    const double qa = dx * dx + dy * dy;
    const double qb = 2.0 * (fx * dx + fy * dy);
    const double qc = fx * fx + fy * fy - r * r;
    if (qa <= 0.0) {
        return qc <= 0.0 ? std::optional<std::pair<double, double>>{{0.0, 0.0}} : std::nullopt;
    }
    const double discriminant = qb * qb - 4.0 * qa * qc;
    if (discriminant < 0.0) {
        return std::nullopt;
    }
    const double root = std::sqrt(discriminant);
    const double t0 = std::max(0.0, (-qb - root) / (2.0 * qa));
    const double t1 = std::min(1.0, (-qb + root) / (2.0 * qa));
    if (t1 < t0) {
        return std::nullopt;
    }
    return std::pair{t0, t1};
}

Result<Point2D> placementAt(const Document& document, ViewId id, int depth) {
    if (depth > kMaxChain) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{} is projected through more than {} views; the chain loops or "
                                     "does not reach a base view",
                                     id, kMaxChain));
    }
    const View* view = findView(document, id);
    if (view == nullptr) {
        return notFound(id);
    }
    const ViewDefinition& d = view->definition();
    // Base and detail views are placed directly; every other kind aligns to
    // its parent.
    if (d.kind == ViewKind::Base || d.kind == ViewKind::Detail) {
        return d.placement;
    }
    const Sheet* sheet = findSheet(document, d.sheet);
    if (sheet == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("{} sits on {}, which is not a sheet of this document", id,
                                     d.sheet));
    }
    auto parent = placementAt(document, *d.parent, depth + 1);
    if (!parent) {
        return std::unexpected(parent.error());
    }
    // The alignment is exact because it is the same number, not because
    // something keeps two numbers equal: a Top view's x IS its parent's x.
    const ProjectionConvention convention = sheet->definition().convention;
    if (d.kind == ViewKind::Projected) {
        const auto [stepX, stepY] = placementStep(*d.direction, convention);
        return Point2D{parent->x + stepX * d.spacing, parent->y + stepY * d.spacing};
    }
    // A section or auxiliary view looks a way no ProjectedDirection names, so
    // its side of the sheet is worked out from its own normal expressed in
    // its parent's axes. For the four orthogonal directions this gives back
    // exactly what placementStep says, which is what a test asserts.
    auto own = basisAt(document, id, depth + 1);
    if (!own) {
        return std::unexpected(own.error());
    }
    auto parentBasis = basisAt(document, *d.parent, depth + 1);
    if (!parentBasis) {
        return std::unexpected(parentBasis.error());
    }
    const Direction3D& n = own->normal();
    const auto [stepX, stepY] = sheetDisplacement(
        {parentBasis->xAxis().dot(n), parentBasis->yAxis().dot(n)}, convention);
    return Point2D{parent->x + stepX * d.spacing, parent->y + stepY * d.spacing};
}

} // namespace

Result<ProjectedGeometry> projectedGeometry(const Document& document, ViewId id,
                                            const BodyLookup& bodies,
                                            const TransformLookup& transforms) {
    const View* view = findView(document, id);
    if (view == nullptr) {
        return notFound(id);
    }
    return viewGeometry(document, id, bodies, transforms, view->definition().hiddenLine, 0);
}

namespace {

/// What a view draws, with @p settings standing in for its own.
///
/// The override exists for one caller: a detail view has to ask its parent
/// for everything the parent COULD draw, so that its own hidden-line and
/// tangent settings decide what the detail shows. Asking for the parent's
/// already-filtered lines would make a detail unable to show anything its
/// parent hid, which is a per-view toggle that is not per-view.
Result<ProjectedGeometry> viewGeometry(const Document& document, ViewId id,
                                       const BodyLookup& bodies,
                                       const TransformLookup& transforms,
                                       const HiddenLineSettings& settings, int depth) {
    if (const View* view = findView(document, id);
        view != nullptr && view->definition().kind == ViewKind::Detail) {
        return detailGeometry(document, id, bodies, transforms, settings, depth);
    }
    auto source = effectiveSource(document, id);
    if (!source) {
        return std::unexpected(source.error());
    }
    if (!isInternal(*source)) {
        return makeError(ErrorCode::NotFound,
                         std::format("{} draws an object of another document, which cannot be "
                                     "projected",
                                     id));
    }
    auto basis = effectiveBasis(document, id);
    if (!basis) {
        return std::unexpected(basis.error());
    }
    auto scale = effectiveScale(document, id);
    if (!scale) {
        return std::unexpected(scale.error());
    }
    auto placement = effectivePlacement(document, id);
    if (!placement) {
        return std::unexpected(placement.error());
    }

    auto resolved = bodyForView(document, id, *source, bodies, transforms);
    if (!resolved) {
        return std::unexpected(resolved.error());
    }
    // A section view draws the CUT solid. Doing it here, rather than in a
    // second routine beside this one, is what keeps a section's outline and
    // its cut faces from ever being drawn from different solids.
    if (const View* view = findView(document, id);
        view != nullptr && view->definition().kind == ViewKind::Section) {
        auto cut = cutBody(*resolved, *view->definition().section, *basis);
        if (!cut) {
            return std::unexpected(cut.error());
        }
        resolved = std::move(*cut);
    }
    // Hidden-line removal, on the solid this view actually draws -- which for
    // a section view is the CUT solid, cut just above. Classifying the uncut
    // solid would hide the very faces a section exists to show (ADR-019).
    auto drawing = geometry::hiddenLineDrawing(*resolved, *basis);
    if (!drawing) {
        return std::unexpected(drawing.error());
    }
    if (drawing->edges.empty()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{} draws a body with no edges to project", id));
    }

    HiddenLinePolicyResult classified = applyHiddenLinePolicy(*drawing, settings);

    // The view is centred on what it COULD draw, not on what its settings
    // leave: turning hidden lines off must not shift the drawing on the
    // sheet. So the extent is taken from the merged set before anything is
    // suppressed.
    double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;
    bool first = true;
    const auto see = [&](const Point2D& p) {
        const double x = p.x.si();
        const double y = p.y.si();
        if (first) {
            minX = maxX = x;
            minY = maxY = y;
            first = false;
            return;
        }
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
    };
    for (const geometry::ProjectedEdge& edge : drawing->edges) {
        for (const Point2D& p : edge.polyline) {
            see(p);
        }
    }

    // Placement anchors the projected bounding-box CENTRE, so a view sits
    // where it was put regardless of where the model happens to be in space.
    const double centreX = 0.5 * (minX + maxX);
    const double centreY = 0.5 * (minY + maxY);
    const double factor = scale->factor();
    const auto toSheet = [&](const Point2D& p) {
        return Point2D{placement->x + Length::fromSi((p.x.si() - centreX) * factor),
                       placement->y + Length::fromSi((p.y.si() - centreY) * factor)};
    };

    ProjectedGeometry result;
    result.merged = classified.merged;
    result.suppressed = classified.suppressed;
    result.edges.reserve(classified.edges.size());
    for (const geometry::ProjectedEdge& edge : classified.edges) {
        DrawnEdge drawn;
        drawn.curve = edge.curve;
        drawn.start = toSheet(edge.start);
        drawn.end = toSheet(edge.end);
        drawn.midpoint = toSheet(edge.midpoint);
        drawn.visibility = edge.visibility;
        drawn.kind = edge.kind;
        drawn.polyline.reserve(edge.polyline.size());
        for (const Point2D& p : edge.polyline) {
            drawn.polyline.push_back(toSheet(p));
            result.points.push_back(drawn.polyline.back());
        }
        result.edges.push_back(std::move(drawn));
    }

    // Bounds come from the whole extent, not from the points that survived,
    // for the same reason the centring does.
    result.bounds = BoundingBox2D::around(toSheet(Point2D{Length::fromSi(minX), Length::fromSi(minY)}));
    result.bounds.include(toSheet(Point2D{Length::fromSi(maxX), Length::fromSi(maxY)}));
    return result;
}

Result<ProjectedGeometry> detailGeometry(const Document& document, ViewId id,
                                         const BodyLookup& bodies,
                                         const TransformLookup& transforms,
                                         const HiddenLineSettings& settings, int depth) {
    if (depth > kMaxChain) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{} is detailed through more than {} views; the chain loops",
                                     id, kMaxChain));
    }
    const View* view = findView(document, id);
    if (view == nullptr) {
        return notFound(id);
    }
    const ViewDefinition& d = view->definition();
    const DetailRegion& region = *d.detail;

    // A detail draws what its PARENT draws. Going back to the model instead
    // would mean projecting twice and hoping the two agreed; this way the
    // detail cannot show anything its parent does not.
    //
    // The parent is asked for EVERYTHING -- hidden lines and tangent edges
    // included -- so that this view's own settings decide what it shows. A
    // detail that could only narrow its parent's choices would not have a
    // toggle of its own.
    const HiddenLineSettings everything{.showHidden = true,
                                        .tangentEdges = TangentEdgePolicy::Show};
    auto parent = viewGeometry(document, *d.parent, bodies, transforms, everything, depth + 1);
    if (!parent) {
        return std::unexpected(parent.error());
    }
    auto ownScale = effectiveScale(document, id);
    if (!ownScale) {
        return std::unexpected(ownScale.error());
    }
    auto parentScale = effectiveScale(document, *d.parent);
    if (!parentScale) {
        return std::unexpected(parentScale.error());
    }

    // The region is in the parent's sheet coordinates, so the enlargement is
    // the RATIO of the two scales: a 2:1 detail of a 1:1 parent doubles.
    const double factor = ownScale->factor() / parentScale->factor();
    const auto toSheet = [&](const Point2D& p) {
        return Point2D{d.placement.x + Length::fromSi((p.x - region.centre.x).si() * factor),
                       d.placement.y + Length::fromSi((p.y - region.centre.y).si() * factor)};
    };

    // Crop each line's polyline piece by piece. A curve crosses the region's
    // boundary wherever it likes, so the pieces that survive are kept as
    // polylines; the curve KIND is carried over, but a cropped curve's
    // midpoint is a sampled point on it rather than an exact one, which is
    // recorded as a limitation rather than presented as exact.
    ProjectedGeometry result;
    for (const DrawnEdge& edge : parent->edges) {
        std::vector<std::vector<Point2D>> pieces;
        std::vector<Point2D> run;
        for (std::size_t i = 0; i + 1 < edge.polyline.size(); ++i) {
            const Point2D& a = edge.polyline[i];
            const Point2D& b = edge.polyline[i + 1];
            if (!region.cropped) {
                // An uncropped detail keeps whole any line the circle
                // touches, which is what a partial view is: enlarged, but not
                // cut off.
                if (circleSpan(a, b, region.centre, region.radius)) {
                    run = edge.polyline;
                    break;
                }
                continue;
            }
            const auto span = circleSpan(a, b, region.centre, region.radius);
            if (!span) {
                if (!run.empty()) {
                    pieces.push_back(std::exchange(run, {}));
                }
                continue;
            }
            const auto at = [&](double t) {
                return Point2D{a.x + Length::fromSi(t * (b.x - a.x).si()),
                               a.y + Length::fromSi(t * (b.y - a.y).si())};
            };
            // A piece that was not clipped keeps the ENDPOINT it already had.
            // Recomputing it as a + 1.0 * (b - a) is the same number in
            // arithmetic and not always the same double, and the difference
            // would stop consecutive pieces joining -- so a curve lying wholly
            // inside the region would come back as a fan of two-point
            // fragments rather than one line.
            const Point2D from = span->first <= 0.0 ? a : at(span->first);
            const Point2D to = span->second >= 1.0 ? b : at(span->second);
            if (from == to) {
                continue; // a segment that only grazes the boundary
            }
            if (run.empty() || run.back() != from) {
                if (!run.empty()) {
                    pieces.push_back(std::exchange(run, {}));
                }
                run.push_back(from);
            }
            run.push_back(to);
        }
        if (!run.empty()) {
            pieces.push_back(std::move(run));
        }
        for (std::vector<Point2D>& piece : pieces) {
            if (piece.size() < 2) {
                continue;
            }
            DrawnEdge drawn;
            drawn.curve = edge.curve;
            drawn.visibility = edge.visibility;
            drawn.kind = edge.kind;
            drawn.polyline.reserve(piece.size());
            for (const Point2D& p : piece) {
                drawn.polyline.push_back(toSheet(p));
            }
            drawn.start = drawn.polyline.front();
            drawn.end = drawn.polyline.back();
            drawn.midpoint = drawn.polyline[drawn.polyline.size() / 2];
            result.edges.push_back(std::move(drawn));
        }
    }

    // This view's own settings, applied to what survived the crop. Merging
    // has already happened in the parent, so only suppression is left.
    std::vector<DrawnEdge> shown;
    shown.reserve(result.edges.size());
    for (DrawnEdge& edge : result.edges) {
        const bool hiddenAndNotShown =
            edge.visibility == geometry::EdgeVisibility::Hidden && !settings.showHidden;
        const bool tangentAndNotShown = edge.kind == geometry::ProjectedEdgeKind::Smooth &&
                                        settings.tangentEdges == TangentEdgePolicy::Hide;
        if (hiddenAndNotShown || tangentAndNotShown) {
            ++result.suppressed;
            continue;
        }
        shown.push_back(std::move(edge));
    }
    result.edges = std::move(shown);
    result.merged = parent->merged;

    for (const DrawnEdge& edge : result.edges) {
        for (const Point2D& p : edge.polyline) {
            result.points.push_back(p);
        }
    }
    if (result.points.empty()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{} details a region of its parent that nothing reaches", id));
    }
    result.bounds = BoundingBox2D::around(result.points.front());
    for (const Point2D& p : result.points) {
        result.bounds.include(p);
    }
    return result;
}

} // namespace

Result<Point2D> toSheet(const Document& document, ViewId id, const Point3D& point,
                        const BodyLookup& bodies, const TransformLookup& transforms) {
    auto source = effectiveSource(document, id);
    if (!source) {
        return std::unexpected(source.error());
    }
    if (!isInternal(*source)) {
        return makeError(ErrorCode::NotFound,
                         std::format("{} draws an object of another document", id));
    }
    auto basis = effectiveBasis(document, id);
    if (!basis) {
        return std::unexpected(basis.error());
    }
    auto scale = effectiveScale(document, id);
    if (!scale) {
        return std::unexpected(scale.error());
    }
    auto placement = effectivePlacement(document, id);
    if (!placement) {
        return std::unexpected(placement.error());
    }
    auto body = bodyForView(document, id, *source, bodies, transforms);
    if (!body) {
        return std::unexpected(body.error());
    }
    // A section view draws the cut solid, and its extent is what it is
    // centred on, so a point is placed against that and not against the whole
    // one it was cut from.
    if (const View* view = findView(document, id);
        view != nullptr && view->definition().kind == ViewKind::Section) {
        auto cut = cutBody(*body, *view->definition().section, *basis);
        if (!cut) {
            return std::unexpected(cut.error());
        }
        body = std::move(*cut);
    }
    auto drawing = geometry::hiddenLineDrawing(*body, *basis);
    if (!drawing) {
        return std::unexpected(drawing.error());
    }
    if (drawing->edges.empty()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{} draws a body with no edges to project", id));
    }

    double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;
    bool first = true;
    for (const geometry::ProjectedEdge& edge : drawing->edges) {
        for (const Point2D& p : edge.polyline) {
            if (first) {
                minX = maxX = p.x.si();
                minY = maxY = p.y.si();
                first = false;
                continue;
            }
            minX = std::min(minX, p.x.si());
            maxX = std::max(maxX, p.x.si());
            minY = std::min(minY, p.y.si());
            maxY = std::max(maxY, p.y.si());
        }
    }
    const Point2D flat = projectToViewPlane(*basis, point);
    const double factor = scale->factor();
    return Point2D{
        placement->x + Length::fromSi((flat.x.si() - 0.5 * (minX + maxX)) * factor),
        placement->y + Length::fromSi((flat.y.si() - 0.5 * (minY + maxY)) * factor)};
}

Result<SectionGeometry> sectionOf(const Document& document, ViewId id, const BodyLookup& bodies,
                                  const TransformLookup& transforms) {
    const View* view = findView(document, id);
    if (view == nullptr) {
        return notFound(id);
    }
    const ViewDefinition& d = view->definition();
    if (d.kind != ViewKind::Section) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} is a {} view and cuts nothing, so it has no cut faces", id,
                                     toString(d.kind)));
    }
    auto source = effectiveSource(document, id);
    if (!source) {
        return std::unexpected(source.error());
    }
    if (!isInternal(*source)) {
        return makeError(ErrorCode::NotFound,
                         std::format("{} draws an object of another document, which cannot be cut",
                                     id));
    }
    auto basis = effectiveBasis(document, id);
    if (!basis) {
        return std::unexpected(basis.error());
    }
    auto scale = effectiveScale(document, id);
    if (!scale) {
        return std::unexpected(scale.error());
    }
    auto placement = effectivePlacement(document, id);
    if (!placement) {
        return std::unexpected(placement.error());
    }
    auto body = bodyForView(document, id, *source, bodies, transforms);
    if (!body) {
        return std::unexpected(body.error());
    }
    return sectionGeometry(*body, *d.section, *basis, scale->factor(), *placement, d.hatch);
}

} // namespace bettercad::drawing
