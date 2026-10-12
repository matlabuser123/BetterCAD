// The drawable geometry of the structural overlays (P17-VIZ-001, ADR-045).
//
// EVERY POSITION COMES FROM `Mesh::findNode`. There is no spatial search, no
// centroid fit and no inferred location in this file -- the overlay data
// carries no position, and the canonical mesh is the only thing asked for one.
//
// AND NO UNIT CONVERSION. Segments leave in SI metres and reach OCCT model
// space through `toModelLength` in the presentation adapter, the same single
// conversion a mesh buffer passes through. A grep here for `1000` finds
// nothing.

#include <bettercad/renderer/StructuralOverlay.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>

namespace bettercad::renderer {
namespace {

/// A unit vector perpendicular to @p along, chosen deterministically.
///
/// THE SEED AXIS IS THE ONE LEAST ALIGNED WITH @p along, so the cross product
/// is never near-degenerate and the barbs never collapse. Picking a fixed
/// axis instead would produce a zero cross product whenever an arrow happened
/// to point along it -- and an axial load is the most ordinary case there is.
[[nodiscard]] std::optional<Direction3D> perpendicularTo(const Direction3D& along) {
    const std::array<double, 3> magnitude{std::abs(along.x()), std::abs(along.y()),
                                          std::abs(along.z())};
    const auto smallest = static_cast<std::size_t>(
        std::distance(magnitude.begin(), std::ranges::min_element(magnitude)));
    const Direction3D seed = smallest == 0   ? Direction3D::unitX()
                             : smallest == 1 ? Direction3D::unitY()
                                             : Direction3D::unitZ();
    return along.cross(seed);
}

/// Appends the five segments of one arrow: the shaft, then four barbs.
void appendArrow(std::vector<OverlaySegment>& out, const Point3D& head,
                 const Direction3D& along, double length) {
    const auto offset = [](const Point3D& from, const Direction3D& direction,
                           double distance) {
        return Point3D{Length::fromSi(from.x.si() + (distance * direction.x())),
                       Length::fromSi(from.y.si() + (distance * direction.y())),
                       Length::fromSi(from.z.si() + (distance * direction.z()))};
    };

    // THE SHAFT, tail to head. The head is the point of application, exactly.
    const Point3D tail = offset(head, along, -length);
    out.push_back(OverlaySegment{tail, head});

    const std::optional<Direction3D> first = perpendicularTo(along);
    const std::optional<Direction3D> second = first.has_value() ? along.cross(*first)
                                                               : std::nullopt;
    if (!first.has_value() || !second.has_value()) {
        // UNREACHABLE: `perpendicularTo` seeds from the axis least aligned
        // with `along`, so the cross product cannot be degenerate for a unit
        // vector. The shaft is still drawn, because an arrow without barbs is
        // a worse answer than no barbs but still shows where the force acts.
        return;
    }

    const double barb = length * kArrowBarbFraction;
    const double back = barb * std::cos(kArrowBarbRadians);
    const double side = barb * std::sin(kArrowBarbRadians);
    const Point3D base = offset(head, along, -back);
    for (const Direction3D& across : {*first, first->reversed(), *second, second->reversed()}) {
        out.push_back(OverlaySegment{head, offset(base, across, side)});
    }
}

/// The shared body of the two arrow overlays.
///
/// ONE IMPLEMENTATION, so a load arrow and a reaction arrow cannot be drawn by
/// two conventions that drift apart.
template <typename Arrow>
[[nodiscard]] Result<StructuralOverlay> arrowOverlay(const meshing::Mesh& mesh,
                                                     std::span<const Arrow> arrows,
                                                     double scale, double reference,
                                                     OverlayRole role) {
    if (!isUsableArrowScale(scale)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("an arrow display scale must be finite and positive, and {} "
                                     "is not",
                                     scale));
    }
    if (!std::isfinite(reference) || reference <= 0.0) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("an arrow reference magnitude must be finite and positive, "
                                     "and {} is not",
                                     reference));
    }

    StructuralOverlay out;
    out.role = role;
    out.segments.reserve(arrows.size() * kSegmentsPerArrow);
    for (const Arrow& arrow : arrows) {
        const meshing::Node* node = mesh.findNode(arrow.node);
        if (node == nullptr) {
            // REFUSED, NOT DROPPED. An absent node means the arrows and the
            // mesh disagree, and a silently shorter overlay would hide that.
            return makeError(ErrorCode::NotFound,
                             std::format("node {} carries an overlay arrow and is not a node of "
                                         "this mesh",
                                         arrow.node.value()));
        }
        // THE DIRECTION COMES FROM THE FORCE, and `fromComponents` normalises
        // it and answers absent for a zero vector -- which is the skip below
        // rather than a special case written here.
        const std::optional<Direction3D> along = Direction3D::fromComponents(
            arrow.force.x.si(), arrow.force.y.si(), arrow.force.z.si());
        if (!along.has_value()) {
            ++out.skipped;
            continue;
        }
        const double magnitude = std::hypot(arrow.force.x.si(), arrow.force.y.si(),
                                            arrow.force.z.si());
        const Result<double> length = displayArrowLength(magnitude, reference, scale);
        if (!length.has_value()) {
            return std::unexpected(length.error());
        }
        // A NON-POSITIVE DISPLAY LENGTH DRAWS NOTHING, and it is a different
        // cause from a zero force. `displayArrowLength` is a proportion --
        // scale * |magnitude| / reference -- which UNDERFLOWS TO ZERO for a
        // small enough force against a large enough reference. The force has
        // a direction, so it is not skipped above; but a zero-length shaft
        // puts the tail on the head and collapses all four barbs onto the
        // same point, which draws five degenerate segments at a dot.
        if (!(*length > 0.0)) {
            ++out.tooSmall;
            continue;
        }
        appendArrow(out.segments, node->position, *along, *length);
        ++out.glyphs;
    }
    return out;
}

} // namespace

std::string_view toString(OverlayRole role) noexcept {
    switch (role) {
    case OverlayRole::LoadArrows:
        return "load arrows";
    case OverlayRole::ReactionArrows:
        return "reaction arrows";
    case OverlayRole::RestraintSymbols:
        return "restraint symbols";
    }
    return "an unknown overlay";
}

Result<StructuralOverlay> loadArrowOverlay(const meshing::Mesh& mesh,
                                           std::span<const NodalLoadArrow> arrows, double scale,
                                           double reference) {
    return arrowOverlay(mesh, arrows, scale, reference, OverlayRole::LoadArrows);
}

Result<StructuralOverlay> reactionArrowOverlay(const meshing::Mesh& mesh,
                                               std::span<const ReactionArrow> arrows,
                                               double scale, double reference) {
    // THE SIGN IS THE QUALIFIED ONE: the glyph's Force3D is drawn as it
    // stands. P17-REACTION settled that a reaction opposes its load, and a
    // viewer negating it here would be a second opinion about the physics.
    return arrowOverlay(mesh, arrows, scale, reference, OverlayRole::ReactionArrows);
}

Result<StructuralOverlay> restraintSymbolOverlay(const meshing::Mesh& mesh,
                                                 const structural::PreparedRestraints& restraints,
                                                 const structural::MeshDofMap& numbering,
                                                 double barLength) {
    if (!std::isfinite(barLength) || barLength <= 0.0) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("a restraint bar length must be finite and positive, and {} "
                                     "is not",
                                     barLength));
    }
    if (!restraints.describes(mesh)) {
        return makeError(ErrorCode::FailedPrecondition,
                         "these prepared restraints were resolved against a different mesh");
    }
    if (!numbering.describes(mesh)) {
        return makeError(ErrorCode::FailedPrecondition,
                         "this degree-of-freedom numbering was built for a different mesh "
                         "generation");
    }

    StructuralOverlay out;
    out.role = OverlayRole::RestraintSymbols;
    const double half = barLength / 2.0;
    constexpr std::array<Direction3D, structural::kDofsPerNode> kAxes{
        Direction3D::unitX(), Direction3D::unitY(), Direction3D::unitZ()};

    for (const meshing::NodeId node : restraints.nodes()) {
        const meshing::Node* found = mesh.findNode(node);
        if (found == nullptr) {
            return makeError(ErrorCode::NotFound,
                             std::format("node {} is restrained and is not a node of this mesh",
                                         node.value()));
        }
        const Result<std::array<structural::DofIndex, structural::kDofsPerNode>> indices =
            numbering.indicesOf(node);
        if (!indices.has_value()) {
            return makeError(indices.error().code, indices.error().message);
        }

        // ONE BAR PER CONSTRAINED COMPONENT, asked of the owners one degree of
        // freedom at a time. Not derived from the resolutions' masks: see
        // DEFECT_RESTRAINT_COMPONENTS.md for what that cost.
        std::size_t bars = 0;
        for (std::size_t which = 0; which < structural::kDofsPerNode; ++which) {
            if (!restraints.constraints().contains((*indices)[which])) {
                continue;
            }
            const Direction3D& axis = kAxes[which];
            const Point3D from{
                Length::fromSi(found->position.x.si() - (half * axis.x())),
                Length::fromSi(found->position.y.si() - (half * axis.y())),
                Length::fromSi(found->position.z.si() - (half * axis.z()))};
            const Point3D to{Length::fromSi(found->position.x.si() + (half * axis.x())),
                             Length::fromSi(found->position.y.si() + (half * axis.y())),
                             Length::fromSi(found->position.z.si() + (half * axis.z()))};
            out.segments.push_back(OverlaySegment{from, to});
            ++bars;
        }
        if (bars == 0) {
            // THE SAME CROSS-CHECK `inspectNode` MAKES: a node the prepared
            // restraints name must hold at least one constrained degree of
            // freedom. The two answers come from different objects, so
            // disagreement means they were built from different inputs.
            return makeError(
                ErrorCode::Internal,
                std::format("node {} is named by the prepared restraints and yet none of its "
                            "degrees of freedom is in the constraint set",
                            node.value()));
        }
        ++out.glyphs;
    }
    return out;
}

} // namespace bettercad::renderer
