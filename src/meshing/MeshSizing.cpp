#include <bettercad/meshing/MeshSizing.hpp>

#include <bettercad/core/geometry/Faces.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace bettercad::meshing {
namespace {

void addIssue(std::vector<SizingIssue>& issues, SizingIssueKind kind,
              std::optional<std::size_t> control, std::optional<FaceName> face,
              std::string message) {
    issues.push_back(SizingIssue{kind, control, std::move(face), std::move(message)});
}

/// Orders issues within one kind: by control index, with the global target
/// (no index) first. Total, so the report does not depend on check order.
[[nodiscard]] bool issueLess(const SizingIssue& a, const SizingIssue& b) {
    return a.control.value_or(0) < b.control.value_or(0);
}

/// A position as an exact ordering key, so restrictions enumerate identically
/// in every build. Lexicographic on the SI values: total and deterministic.
struct PositionKey {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    friend auto operator<=>(const PositionKey&, const PositionKey&) = default;
    friend bool operator==(const PositionKey&, const PositionKey&) = default;
};

[[nodiscard]] PositionKey keyOf(const Point3D& point) noexcept {
    return PositionKey{point.x.si(), point.y.si(), point.z.si()};
}

[[nodiscard]] double diagonalOf(const MeshBounds& bounds) noexcept {
    const double dx = bounds.max.x.si() - bounds.min.x.si();
    const double dy = bounds.max.y.si() - bounds.min.y.si();
    const double dz = bounds.max.z.si() - bounds.min.z.si();
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

/// Distance from @p point to the line through @p origin along @p direction.
[[nodiscard]] double distanceToAxis(const Point3D& point, const Axis3D& axis) noexcept {
    const double px = point.x.si() - axis.origin.x.si();
    const double py = point.y.si() - axis.origin.y.si();
    const double pz = point.z.si() - axis.origin.z.si();
    const double dx = axis.direction.x();
    const double dy = axis.direction.y();
    const double dz = axis.direction.z();
    // |p x d| with d a unit direction.
    const double cx = py * dz - pz * dy;
    const double cy = pz * dx - px * dz;
    const double cz = px * dy - py * dx;
    return std::sqrt(cx * cx + cy * cy + cz * cz);
}

/// Whether @p point lies on the plane of @p signature.
///
/// The signed distance along the OUTWARD normal, so two coplanar faces that
/// face opposite ways are still distinguished by their own points: a box's
/// top and bottom share no plane, and the test for one is never satisfied by
/// the other.
[[nodiscard]] bool onPlane(const Point3D& point, const geometry::FaceSignature& signature,
                          double tolerance) noexcept {
    const double dx = point.x.si() - signature.point.x.si();
    const double dy = point.y.si() - signature.point.y.si();
    const double dz = point.z.si() - signature.point.z.si();
    const double signed_ =
        dx * signature.normal.x() + dy * signature.normal.y() + dz * signature.normal.z();
    return std::abs(signed_) <= tolerance;
}

} // namespace

std::string_view toString(SizingIssueKind kind) noexcept {
    switch (kind) {
    case SizingIssueKind::NonPositiveSize:
        return "non_positive_size";
    case SizingIssueKind::NonFiniteSize:
        return "non_finite_size";
    case SizingIssueKind::InvalidFaceSelector:
        return "invalid_face_selector";
    case SizingIssueKind::DuplicateFaceControl:
        return "duplicate_face_control";
    case SizingIssueKind::UnresolvedFace:
        return "unresolved_face";
    case SizingIssueKind::UnsupportedFaceGeometry:
        return "unsupported_face_geometry";
    }
    return "unknown_sizing_issue";
}

std::string_view toString(SizingSelectionState state) noexcept {
    switch (state) {
    case SizingSelectionState::Resolved:
        return "resolved";
    case SizingSelectionState::Unresolved:
        return "unresolved";
    case SizingSelectionState::Unsupported:
        return "unsupported";
    }
    return "unknown_selection_state";
}

std::size_t ResolvedSizing::unresolvedCount() const noexcept {
    return static_cast<std::size_t>(
        std::ranges::count_if(local, [](const LocalSizingResolution& entry) {
            return entry.state != SizingSelectionState::Resolved;
        }));
}

SizingValidationReport validate(const MeshSizingControls& controls) {
    // One bucket per kind, so the report assembles in enumeration order
    // without sorting on an enum value.
    std::vector<SizingIssue> nonPositive;
    std::vector<SizingIssue> nonFinite;
    std::vector<SizingIssue> badSelector;
    std::vector<SizingIssue> duplicate;

    // NON-FINITE IS CHECKED BEFORE NON-POSITIVE, because the comparison that
    // decides "positive" answers false for NaN and would report a NaN as
    // non-positive -- true, but the wrong diagnostic, and it would hide that
    // the value is not a number at all.
    const auto checkSize = [&](Length size, std::optional<std::size_t> index,
                               std::optional<FaceName> face, std::string_view what) {
        if (!std::isfinite(size.si())) {
            addIssue(nonFinite, SizingIssueKind::NonFiniteSize, index, std::move(face),
                     std::format("{}: size is not finite ({})", what, size.si()));
            return;
        }
        if (!(size.si() > 0.0)) {
            addIssue(nonPositive, SizingIssueKind::NonPositiveSize, index, std::move(face),
                     std::format("{}: size is {} m, which is not positive", what, size.si()));
        }
    };

    if (controls.globalTargetSize.has_value()) {
        checkSize(*controls.globalTargetSize, std::nullopt, std::nullopt, "global target size");
    }

    std::set<FaceName> seen;
    for (std::size_t index = 0; index < controls.local.size(); ++index) {
        const LocalMeshSizing& control = controls.local[index];
        checkSize(control.targetSize, index, control.face,
                  std::format("local control {}", index));

        if (const Result<void> selector = validate(control.face.face); !selector.has_value()) {
            addIssue(badSelector, SizingIssueKind::InvalidFaceSelector, index, control.face,
                     std::format("local control {}: {}", index, selector.error().message));
        }
        if (!seen.insert(control.face).second) {
            addIssue(duplicate, SizingIssueKind::DuplicateFaceControl, index, control.face,
                     std::format("local control {}: another control already names this face",
                                 index));
        }
    }

    SizingValidationReport report;
    for (std::vector<SizingIssue>* bucket : {&nonPositive, &nonFinite, &badSelector, &duplicate}) {
        std::ranges::stable_sort(*bucket, issueLess);
        report.issues.insert(report.issues.end(), bucket->begin(), bucket->end());
    }
    return report;
}

Length defaultGlobalTargetSize(const MeshBounds& bounds) {
    const double diagonal = diagonalOf(bounds);
    // A degenerate or non-finite box has no characteristic length to scale by.
    // Returning a positive finite value keeps the caller's contract (a target
    // is always positive and finite) and the mesher then meshes whatever the
    // boundary implies, which is the documented no-restriction behaviour.
    if (!std::isfinite(diagonal) || !(diagonal > 0.0)) {
        return Length::fromSi(1.0);
    }
    return Length::fromSi(diagonal);
}

Result<ResolvedSizing> resolveSizing(const geometry::Body& body, const Mesh& surface,
                                     const MeshSizingControls& controls) {
    // VALIDATION FIRST, and the same validation a caller would run, so there is
    // no path that reaches the backend with an invalid canonical request.
    const SizingValidationReport report = validate(controls);
    if (!report.valid()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("mesh sizing: {} issue(s), first: {} ({})",
                                     report.issues.size(), toString(report.issues.front().kind),
                                     report.issues.front().message));
    }

    const std::optional<MeshBounds> bounds = surface.bounds();
    if (!bounds.has_value()) {
        return makeError(ErrorCode::InvalidArgument,
                         "mesh sizing: the surface has no nodes to size against");
    }

    ResolvedSizing resolved;
    if (controls.globalTargetSize.has_value()) {
        resolved.globalTargetSize = *controls.globalTargetSize;
        resolved.globalIsDefault = false;
    } else {
        resolved.globalTargetSize = defaultGlobalTargetSize(*bounds);
        resolved.globalIsDefault = true;
    }

    // A COMPARISON epsilon, not a modelling tolerance: a planar face's
    // triangulation nodes lie ON its plane, so this only absorbs the last bits
    // of floating-point representation. Scaled by the body's size so it means
    // the same thing for a 1 mm part and a 1 m one.
    const double tolerance = std::max(diagonalOf(*bounds), 1.0) * 1e-9;

    // Keyed on position, holding the SMALLEST size claimed there. The minimum
    // of a set does not depend on insertion order, which is what makes the
    // result order-independent by construction rather than by a rule.
    std::map<PositionKey, std::pair<Point3D, double>> smallest;
    std::vector<BoxSizeRestriction> regions;

    for (const LocalMeshSizing& control : controls.local) {
        LocalSizingResolution entry;
        entry.face = control.face;
        entry.targetSize = control.targetSize;

        const Result<std::vector<geometry::FaceInfo>> faces =
            geometry::findNamedFaces(body, control.face);
        if (!faces.has_value() || faces->empty()) {
            // EXPLICITLY UNRESOLVED. The control is kept and reported; it is
            // never dropped, and it is never moved to a face that happens to
            // be nearby or similarly named.
            entry.state = SizingSelectionState::Unresolved;
            resolved.local.push_back(entry);
            continue;
        }

        bool supported = false;
        std::size_t claimed = 0;
        std::vector<Point3D> claimedPositions;
        // The inward direction of the first planar face carrying this name.
        // Nullopt for a cylindrical face, which has no single one.
        std::optional<std::array<double, 3>> inwardNormal;
        for (const geometry::FaceInfo& face : *faces) {
            if (face.signature.has_value()) {
                supported = true;
                if (!inwardNormal.has_value()) {
                    inwardNormal = std::array<double, 3>{-face.signature->normal.x(),
                                                         -face.signature->normal.y(),
                                                         -face.signature->normal.z()};
                }
                for (const Node& node : surface.nodes()) {
                    if (onPlane(node.position, *face.signature, tolerance)) {
                        ++claimed;
                        claimedPositions.push_back(node.position);
                        const PositionKey key = keyOf(node.position);
                        const auto [it, inserted] =
                            smallest.try_emplace(key, node.position, control.targetSize.si());
                        if (!inserted) {
                            it->second.second =
                                std::min(it->second.second, control.targetSize.si());
                        }
                    }
                }
            } else if (face.cylinder.has_value()) {
                supported = true;
                const double radius = face.cylinder->radius.si();
                for (const Node& node : surface.nodes()) {
                    if (std::abs(distanceToAxis(node.position, face.cylinder->axis) - radius) <=
                        tolerance) {
                        ++claimed;
                        claimedPositions.push_back(node.position);
                        const PositionKey key = keyOf(node.position);
                        const auto [it, inserted] =
                            smallest.try_emplace(key, node.position, control.targetSize.si());
                        if (!inserted) {
                            it->second.second =
                                std::min(it->second.second, control.targetSize.si());
                        }
                    }
                }
            }
        }

        // THE FACE BECOMES A SLAB. The bounding box of the nodes it claimed,
        // extended by one target size in every direction, so the restricted
        // region has VOLUME. A restriction confined to the face's surface is a
        // sheet the mesher almost never samples; see BoxSizeRestriction.
        if (claimed > 0 && !claimedPositions.empty()) {
            double lo[3] = {claimedPositions.front().x.si(), claimedPositions.front().y.si(),
                            claimedPositions.front().z.si()};
            double hi[3] = {lo[0], lo[1], lo[2]};
            for (const Point3D& position : claimedPositions) {
                const double p[3] = {position.x.si(), position.y.si(), position.z.si()};
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    lo[axis] = std::min(lo[axis], p[axis]);
                    hi[axis] = std::max(hi[axis], p[axis]);
                }
            }
            // THREE TARGET SIZES DEEP, and the depth is load-bearing.
            //
            // Ng_RestrictMeshSizeBox walks the box in steps of h -- the target
            // size itself -- so a slab only one target deep gets one or two
            // sample planes, and whether they land usefully depends on where
            // the box corner sits. Measured at one target deep: refining a
            // cylinder's top disc worked (69 nodes in the region against 43)
            // while refining its bottom disc produced FEWER elements than no
            // control at all (490 against 554). At three deep both directions
            // work, symmetrically: 72 against 59 for the top and 71 against 52
            // for the bottom.
            //
            // So the restricted region must be a few element sizes deep for the
            // backend's grid sampling to populate it. That is a property of the
            // mechanism nglib offers, recorded here because the number looks
            // arbitrary and is not.
            const double reach = 3.0 * control.targetSize.si();
            // EXTENDED INWARD, ALONG THE FACE NORMAL -- not symmetrically, and
            // the difference is not cosmetic.
            //
            // Ng_RestrictMeshSizeBox walks the box from its MINIMUM corner in
            // steps of h. A slab centred on the face therefore puts its first
            // sample plane OUTSIDE the material, and whether a second one
            // lands inside depends on where the corner happens to sit. In
            // practice that made a top face refine and the opposite bottom
            // face not: the top slab sampled at 18.5 mm (inside a body running
            // to 20) while the bottom slab sampled at -1.5 and 0 mm, the first
            // outside the body and the second only on its surface.
            //
            // Extending into the material instead puts every sample where it
            // can do work, and says what the control means: refine within one
            // element size INSIDE this face.
            //
            // A cylindrical face has no single inward direction, so its slab
            // stays symmetric; its bounding box already spans the body's
            // cross-section, so the sampling problem does not arise.
            const std::array<double, 3> inward =
                inwardNormal.value_or(std::array<double, 3>{0.0, 0.0, 0.0});
            constexpr double kSkin = 1e-9;
            double boxLo[3];
            double boxHi[3];
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const double in = inward[axis] * reach;
                boxLo[axis] = std::min(lo[axis], lo[axis] + in) - kSkin;
                boxHi[axis] = std::max(hi[axis], hi[axis] + in) + kSkin;
                // A face flat in this axis and with no inward component still
                // needs thickness, or the box is degenerate and the grid walk
                // visits nothing.
                if (boxHi[axis] - boxLo[axis] < reach) {
                    const double centre = 0.5 * (boxLo[axis] + boxHi[axis]);
                    boxLo[axis] = centre - 0.5 * reach;
                    boxHi[axis] = centre + 0.5 * reach;
                }
            }
            regions.push_back(BoxSizeRestriction{
                Point3D{Length::fromSi(boxLo[0]), Length::fromSi(boxLo[1]),
                        Length::fromSi(boxLo[2])},
                Point3D{Length::fromSi(boxHi[0]), Length::fromSi(boxHi[1]),
                        Length::fromSi(boxHi[2])},
                control.targetSize});
        }

        entry.nodeCount = claimed;
        // A face of a kind this milestone cannot turn into a region is
        // reported as Unsupported rather than silently contributing nothing.
        entry.state = supported ? SizingSelectionState::Resolved : SizingSelectionState::Unsupported;
        resolved.local.push_back(entry);
    }

    // std::map, so this enumerates in sorted position order: identical in
    // Debug, Release and Debug-shared, and identical however the controls were
    // ordered.
    resolved.restrictions.reserve(smallest.size());
    for (const auto& [key, value] : smallest) {
        resolved.restrictions.push_back(
            SizeRestriction{value.first, Length::fromSi(value.second)});
    }

    // Sorted by corner then size, so the list is identical however the
    // controls were ordered -- the same determinism the point restrictions get
    // from being held in a std::map.
    std::ranges::sort(regions, [](const BoxSizeRestriction& a, const BoxSizeRestriction& b) {
        return std::tuple{keyOf(a.min), keyOf(a.max), a.maxSize.si()} <
               std::tuple{keyOf(b.min), keyOf(b.max), b.maxSize.si()};
    });
    resolved.regions = std::move(regions);
    return resolved;
}

} // namespace bettercad::meshing
