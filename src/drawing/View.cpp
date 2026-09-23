#include <bettercad/drawing/View.hpp>

#include <bettercad/core/math/Vector.hpp>

#include <array>
#include <cmath>
#include <format>
#include <utility>

namespace bettercad::drawing {
namespace {

[[nodiscard]] std::unexpected<Error> wrong(std::string message) {
    return makeError(ErrorCode::InvalidArgument, std::move(message));
}

/// Cross product of two directions, as a direction.
///
/// Both arguments are unit and orthogonal wherever this is called -- they
/// come from a Frame3D, which validated them -- so the result is unit and
/// the normalisation cannot divide by zero.
[[nodiscard]] Direction3D cross(const Direction3D& a, const Direction3D& b) {
    return Direction3D::fromComponents(a.y() * b.z() - a.z() * b.y(), a.z() * b.x() - a.x() * b.z(),
                                       a.x() * b.y() - a.y() * b.x())
        .value();
}

/// A frame from an explicit right, up and normal.
///
/// Built through Frame3D::fromAxes so orthonormality and handedness are
/// checked by the qualified code rather than asserted here.
[[nodiscard]] Result<ViewBasis> frameFrom(const Direction3D& right, const Direction3D& up,
                                          const Direction3D& normal) {
    return Frame3D::fromAxes(Point3D{}, right, up, normal);
}

} // namespace

std::string_view toString(StandardView view) noexcept {
    switch (view) {
    case StandardView::Front:
        return "front";
    case StandardView::Rear:
        return "rear";
    case StandardView::Left:
        return "left";
    case StandardView::Right:
        return "right";
    case StandardView::Top:
        return "top";
    case StandardView::Bottom:
        return "bottom";
    case StandardView::Isometric:
        return "isometric";
    }
    return "unknown";
}

std::optional<StandardView> standardViewFromString(std::string_view text) noexcept {
    for (const StandardView view : {StandardView::Front, StandardView::Rear, StandardView::Left,
                                    StandardView::Right, StandardView::Top, StandardView::Bottom,
                                    StandardView::Isometric}) {
        if (toString(view) == text) {
            return view;
        }
    }
    return std::nullopt;
}

std::string_view toString(ProjectedDirection direction) noexcept {
    switch (direction) {
    case ProjectedDirection::Top:
        return "top";
    case ProjectedDirection::Bottom:
        return "bottom";
    case ProjectedDirection::Left:
        return "left";
    case ProjectedDirection::Right:
        return "right";
    }
    return "unknown";
}

std::optional<ProjectedDirection> projectedDirectionFromString(std::string_view text) noexcept {
    for (const ProjectedDirection d : {ProjectedDirection::Top, ProjectedDirection::Bottom,
                                       ProjectedDirection::Left, ProjectedDirection::Right}) {
        if (toString(d) == text) {
            return d;
        }
    }
    return std::nullopt;
}

Result<ViewBasis> basisOf(StandardView view) {
    // ADR-013: the normal points from the model toward the viewer, which makes
    // Front, Right and Top exactly Frame3D::xz(), yz() and xy(). The other
    // three are those reversed -- the normal flips, and one in-plane axis
    // flips with it to keep the frame right-handed.
    const Direction3D x = Direction3D::unitX();
    const Direction3D y = Direction3D::unitY();
    const Direction3D z = Direction3D::unitZ();
    switch (view) {
    case StandardView::Front:
        return frameFrom(x, z, y.reversed());
    case StandardView::Rear:
        return frameFrom(x.reversed(), z, y);
    case StandardView::Right:
        return frameFrom(y, z, x);
    case StandardView::Left:
        return frameFrom(y.reversed(), z, x.reversed());
    case StandardView::Top:
        return frameFrom(x, y, z);
    case StandardView::Bottom:
        return frameFrom(x, y.reversed(), z.reversed());
    case StandardView::Isometric: {
        // The standard isometric: looking along (-1,-1,-1), so the normal --
        // which points at the viewer -- is (1,1,1) normalised. Up is the
        // model's +Z projected into the view plane, which is what puts Z
        // upright on the sheet. Each model axis then foreshortens by the same
        // sqrt(2/3), and that equality is what the tests check.
        const double r3 = 1.0 / std::sqrt(3.0);
        const double r6 = 1.0 / std::sqrt(6.0);
        // Both are unit by construction, so fromComponents cannot fail here;
        // it is still checked rather than dereferenced blind.
        const auto normal = Direction3D::fromComponents(r3, r3, r3);
        const auto up = Direction3D::fromComponents(-r6, -r6, 2.0 * r6);
        if (!normal || !up) {
            return wrong("the isometric basis could not be built");
        }
        return frameFrom(cross(*up, *normal), *up, *normal);
    }
    }
    return wrong("unknown standard view");
}

Result<ViewBasis> projectedBasis(const ViewBasis& parent, ProjectedDirection direction) {
    // Turning the parent 90 degrees about one of its own in-plane axes. Each
    // case names the new normal and one axis it keeps; the third follows from
    // the right-handed rule, so a case cannot produce a left-handed frame.
    switch (direction) {
    case ProjectedDirection::Top: {
        // Look down on what the parent shows as up.
        const Direction3D normal = parent.yAxis();
        const Direction3D right = parent.xAxis();
        return frameFrom(right, cross(normal, right), normal);
    }
    case ProjectedDirection::Bottom: {
        const Direction3D normal = parent.yAxis().reversed();
        const Direction3D right = parent.xAxis();
        return frameFrom(right, cross(normal, right), normal);
    }
    case ProjectedDirection::Right: {
        // Look at what the parent shows on its right.
        const Direction3D normal = parent.xAxis();
        const Direction3D up = parent.yAxis();
        return frameFrom(cross(up, normal), up, normal);
    }
    case ProjectedDirection::Left: {
        const Direction3D normal = parent.xAxis().reversed();
        const Direction3D up = parent.yAxis();
        return frameFrom(cross(up, normal), up, normal);
    }
    }
    return wrong("unknown projected direction");
}

std::pair<double, double> placementStep(ProjectedDirection direction,
                                        ProjectionConvention convention) noexcept {
    // The whole of the first-angle/third-angle difference, in one table
    // (ADR-018). First angle projects through the object onto a plane behind
    // it, so every view lands on the side opposite the one it looks at.
    const bool first = convention == ProjectionConvention::FirstAngle;
    switch (direction) {
    case ProjectedDirection::Top:
        return {0.0, first ? -1.0 : 1.0};
    case ProjectedDirection::Bottom:
        return {0.0, first ? 1.0 : -1.0};
    case ProjectedDirection::Right:
        return {first ? -1.0 : 1.0, 0.0};
    case ProjectedDirection::Left:
        return {first ? 1.0 : -1.0, 0.0};
    }
    return {0.0, 0.0};
}

std::string_view toString(ViewKind kind) noexcept {
    switch (kind) {
    case ViewKind::Base:
        return "base";
    case ViewKind::Projected:
        return "projected";
    case ViewKind::Section:
        return "section";
    case ViewKind::Detail:
        return "detail";
    case ViewKind::Auxiliary:
        return "auxiliary";
    }
    return "unknown";
}

std::optional<ViewKind> viewKindFromString(std::string_view text) noexcept {
    for (const ViewKind kind : {ViewKind::Base, ViewKind::Projected, ViewKind::Section,
                                ViewKind::Detail, ViewKind::Auxiliary}) {
        if (toString(kind) == text) {
            return kind;
        }
    }
    return std::nullopt;
}

Result<void> validate(const DetailRegion& region) {
    if (!std::isfinite(region.centre.x.si()) || !std::isfinite(region.centre.y.si())) {
        return wrong("a detail region's centre must be finite");
    }
    if (!std::isfinite(region.radius.si()) || region.radius.si() <= 0.0) {
        return wrong("a detail region's radius must be finite and greater than zero");
    }
    return {};
}

Result<void> validate(const ViewDirection& direction) {
    const Direction3D& n = direction.normal;
    const Direction3D& r = direction.reference;
    for (const auto& [name, d] : std::array<std::pair<std::string_view, const Direction3D*>, 2>{
             {{"normal", &n}, {"reference", &r}}}) {
        if (!std::isfinite(d->x()) || !std::isfinite(d->y()) || !std::isfinite(d->z())) {
            return wrong(std::format("an auxiliary view's {} must be finite", name));
        }
    }
    // Frame3D::create projects the reference into the plane, which needs it
    // not to be (nearly) parallel to the normal. Refused here so the message
    // names the view's own field rather than a frame the caller never saw.
    const double alignment = std::abs(n.dot(r));
    if (alignment > 1.0 - 1e-9) {
        return wrong("an auxiliary view's reference direction must not be parallel to its normal");
    }
    return {};
}

std::pair<double, double> sheetDisplacement(const std::pair<double, double>& normal,
                                            ProjectionConvention convention) noexcept {
    // The same rule placementStep states for the four orthogonal directions,
    // written for an arbitrary direction: the view goes on the side of the
    // sheet its own normal points to, reversed in first angle because first
    // angle projects THROUGH the object onto a plane behind it (ADR-018).
    const double length = std::hypot(normal.first, normal.second);
    if (!(length > 0.0)) {
        return {0.0, 0.0}; // looking the parent's way, or straight against it
    }
    const double sign = convention == ProjectionConvention::FirstAngle ? -1.0 : 1.0;
    return {sign * normal.first / length, sign * normal.second / length};
}

bool isBaseView(const ViewDefinition& definition) noexcept {
    return definition.kind == ViewKind::Base;
}

std::string_view toString(ViewSubject subject) noexcept {
    switch (subject) {
    case ViewSubject::Object:
        return "object";
    case ViewSubject::Assembly:
        return "assembly";
    }
    return "unknown";
}

std::optional<ViewSubject> viewSubjectFromString(std::string_view text) noexcept {
    for (const ViewSubject subject : {ViewSubject::Object, ViewSubject::Assembly}) {
        if (toString(subject) == text) {
            return subject;
        }
    }
    return std::nullopt;
}

Result<void> validate(const ViewDefinition& definition) {
    if (!definition.sheet.isValid()) {
        return wrong("a view must name the sheet it sits on");
    }
    if (toString(definition.kind) == "unknown") {
        return wrong("a view must have a known kind");
    }

    // Which fields belong to which kind, in one place. Everything below either
    // requires one of these or refuses it, so a definition can never carry two
    // kinds' worth of intent and leave the reader to guess which it meant.
    const bool base = definition.kind == ViewKind::Base;
    if (base != definition.orientation.has_value()) {
        return wrong(base ? "a base view must have an orientation"
                          : std::format("a {} view takes its orientation from its parent, not an "
                                        "orientation of its own",
                                        toString(definition.kind)));
    }
    if (base == definition.parent.has_value()) {
        return wrong(base ? "a base view has no parent: it is the root of a chain"
                          : std::format("a {} view must name its parent", toString(definition.kind)));
    }
    if ((definition.kind == ViewKind::Projected) != definition.direction.has_value()) {
        return wrong(definition.kind == ViewKind::Projected
                         ? "a projected view must say which direction it is from its parent"
                         : std::format("a {} view takes no projected direction",
                                       toString(definition.kind)));
    }
    if ((definition.kind == ViewKind::Section) != definition.section.has_value()) {
        return wrong(definition.kind == ViewKind::Section
                         ? "a section view must name the plane it cuts on"
                         : std::format("a {} view takes no cutting plane", toString(definition.kind)));
    }
    if ((definition.kind == ViewKind::Detail) != definition.detail.has_value()) {
        return wrong(definition.kind == ViewKind::Detail
                         ? "a detail view must name the region of its parent it enlarges"
                         : std::format("a {} view takes no detail region", toString(definition.kind)));
    }
    if ((definition.kind == ViewKind::Auxiliary) != definition.auxiliary.has_value()) {
        return wrong(definition.kind == ViewKind::Auxiliary
                         ? "an auxiliary view must name the direction it looks from"
                         : std::format("a {} view takes no auxiliary direction",
                                       toString(definition.kind)));
    }

    // A base view is the one that says what is being drawn; every other kind
    // inherits it, so two views of one thing cannot disagree.
    if (toString(definition.subject) == "unknown") {
        return wrong("a view must say whether it draws an object or the assembly");
    }
    const bool assembly = definition.subject == ViewSubject::Assembly;
    if (base) {
        if (toString(*definition.orientation) == "unknown") {
            return wrong("a base view must have a known orientation");
        }
        // An assembly view draws every ACTIVE occurrence, which is the
        // document's answer and not a stored list, so it names no object. One
        // that also named an object would be saying two things about what it
        // draws (ADR-021).
        if (assembly) {
            if (definition.source.object.isValid()) {
                return wrong("an assembly view draws every active component and names no single "
                             "object; use an object view to draw one of them");
            }
        } else if (auto valid = validate(definition.source); !valid) {
            return std::unexpected(valid.error());
        }
    } else if (definition.source.object.isValid()) {
        return wrong(std::format("a {} view takes its source from its parent and names none of its own",
                                 toString(definition.kind)));
    } else if (assembly) {
        // Every other kind inherits the subject with the source. Storing one
        // of its own would let a section of an assembly view claim to be a
        // section of something else.
        return wrong(std::format("a {} view takes its subject from its parent, not one of its own",
                                 toString(definition.kind)));
    }

    if (definition.direction && toString(*definition.direction) == "unknown") {
        return wrong("a projected view must have a known direction");
    }
    if (definition.section) {
        if (auto valid = validate(*definition.section); !valid) {
            return std::unexpected(valid.error());
        }
        if (auto valid = validate(definition.hatch); !valid) {
            return std::unexpected(valid.error());
        }
    }
    if (definition.detail) {
        if (auto valid = validate(*definition.detail); !valid) {
            return std::unexpected(valid.error());
        }
    }
    if (definition.auxiliary) {
        if (auto valid = validate(*definition.auxiliary); !valid) {
            return std::unexpected(valid.error());
        }
    }

    // Placement is intent for the kinds that are placed directly, and derived
    // for the kinds that align to a parent. Spacing is the other way round.
    const bool placedDirectly = base || definition.kind == ViewKind::Detail;
    if (placedDirectly) {
        if (definition.spacing.si() != 0.0) {
            return wrong(std::format("a {} view takes no spacing: it is placed directly",
                                     toString(definition.kind)));
        }
    } else {
        if (!std::isfinite(definition.spacing.si()) || definition.spacing.si() <= 0.0) {
            return wrong(std::format("a {} view's spacing must be finite and greater than zero",
                                     toString(definition.kind)));
        }
        if (definition.placement != Point2D{}) {
            return wrong(std::format("a {} view's placement is derived from its parent's; it stores none",
                                     toString(definition.kind)));
        }
    }

    if (definition.scale) {
        if (auto valid = validate(*definition.scale); !valid) {
            return std::unexpected(valid.error());
        }
    }
    // Every kind of view has hidden-line settings, so this is checked once
    // for all of them rather than per kind.
    if (auto valid = validate(definition.hiddenLine); !valid) {
        return std::unexpected(valid.error());
    }
    for (const auto& [name, value] : std::array<std::pair<std::string_view, Length>, 2>{
             {{"x", definition.placement.x}, {"y", definition.placement.y}}}) {
        if (!std::isfinite(value.si())) {
            return wrong(std::format("a view's {} placement must be finite", name));
        }
    }
    return {};
}

View::View(std::string name, const ViewDefinition& definition)
    : DocumentObject(std::move(name)), definition_(definition) {}

Result<std::unique_ptr<View>> View::create(std::string name, const ViewDefinition& definition) {
    if (auto valid = validateObjectName(name); !valid) {
        return std::unexpected(valid.error());
    }
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    return std::unique_ptr<View>(new View(std::move(name), definition));
}

std::unique_ptr<DocumentObject> View::clone() const {
    return std::unique_ptr<View>(new View(*this));
}

bool View::contentEquals(const DocumentObject& other) const {
    const auto* view = dynamic_cast<const View*>(&other);
    return view != nullptr && view->definition_ == definition_;
}

std::vector<ObjectId> View::dependencies() const {
    std::vector<ObjectId> result;
    result.push_back(ObjectId{definition_.sheet});
    // An external source contributes no edge: an ObjectId means nothing
    // outside its document, and a graph that pretended otherwise would be
    // wrong (ADR-003). Such a view is failed by its handler instead.
    //
    // Nor does an ABSENT one. An assembly view names no source at all
    // (ADR-021), and localTarget() hands back the reference's object either
    // way -- so without the validity check this pushed ObjectId{0} and the
    // graph failed every assembly view with "references object:0, which does
    // not exist". Dimension::dependencies() and Annotation::dependencies()
    // have always made this check; this one had not (P14-REGEN-001).
    if (const auto local = localTarget(definition_.source); local && local->isValid()) {
        result.push_back(*local);
    }
    if (definition_.parent) {
        result.push_back(ObjectId{*definition_.parent});
    }
    return result;
}

Result<bool> View::setDefinition(const ViewDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    if (definition == definition_) {
        return false;
    }
    definition_ = definition;
    return true;
}

Point2D projectToViewPlane(const ViewBasis& basis, const Point3D& point) noexcept {
    // Frame3D::toLocal is (P - O) . right and (P - O) . up -- orthographic
    // projection onto the plane, qualified since P0. Not reimplemented.
    return basis.toLocal(point);
}

Length depthInView(const ViewBasis& basis, const Point3D& point) noexcept {
    return basis.signedDistance(point);
}

} // namespace bettercad::drawing
