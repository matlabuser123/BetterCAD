#include <bettercad/drawing/HiddenLine.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace bettercad::drawing {
namespace {

/// How near two drawn curves must be to be the same curve.
///
/// In VIEW-PLANE units, so it does not depend on the scale the view is drawn
/// at. 1e-7 mm is the figure the geometry module uses to decide that two
/// points coincide, and a hidden-line result's coincident pairs come from the
/// SAME model edges projected the same way -- a box's far face lands on its
/// near one exactly, not approximately -- so there is nothing here that needs
/// a looser figure than the rest of the geometry.
constexpr double kCoincidentSi = 1e-10;

[[nodiscard]] bool samePoint(const Point2D& a, const Point2D& b) noexcept {
    return std::abs((a.x - b.x).si()) <= kCoincidentSi &&
           std::abs((a.y - b.y).si()) <= kCoincidentSi;
}

/// Whether two projected curves draw the same line.
///
/// The ends, the middle and the length, in either direction. The ends alone
/// are not enough: an arc and its chord share them, and so do the two arcs of
/// a circle cut in half. The middle separates those, and the length is a
/// cheap third opinion.
[[nodiscard]] bool sameCurve(const geometry::ProjectedEdge& a,
                             const geometry::ProjectedEdge& b) noexcept {
    if (std::abs((a.length - b.length).si()) > kCoincidentSi) {
        return false;
    }
    if (!samePoint(a.midpoint, b.midpoint)) {
        return false;
    }
    return (samePoint(a.start, b.start) && samePoint(a.end, b.end)) ||
           (samePoint(a.start, b.end) && samePoint(a.end, b.start));
}

/// Where a kind sits in the precedence order; lower wins.
[[nodiscard]] int rank(geometry::ProjectedEdgeKind kind) noexcept {
    switch (kind) {
    case geometry::ProjectedEdgeKind::Sharp:
        return 0;
    case geometry::ProjectedEdgeKind::Outline:
        return 1;
    case geometry::ProjectedEdgeKind::Smooth:
        return 2;
    case geometry::ProjectedEdgeKind::Sewn:
        return 3;
    }
    return 4;
}

/// Which of two edges drawing the same line survives.
[[nodiscard]] bool wins(const geometry::ProjectedEdge& candidate,
                        const geometry::ProjectedEdge& incumbent) noexcept {
    // ISO 128 line precedence: a visible line takes precedence over a hidden
    // one. That comes first, because it is the one a reader would notice.
    if (candidate.visibility != incumbent.visibility) {
        return candidate.visibility == geometry::EdgeVisibility::Visible;
    }
    return rank(candidate.kind) < rank(incumbent.kind);
}

} // namespace

std::string_view toString(TangentEdgePolicy policy) noexcept {
    switch (policy) {
    case TangentEdgePolicy::Show:
        return "show";
    case TangentEdgePolicy::Hide:
        return "hide";
    }
    return "unknown";
}

std::optional<TangentEdgePolicy> tangentEdgePolicyFromString(std::string_view text) noexcept {
    for (const TangentEdgePolicy policy : {TangentEdgePolicy::Show, TangentEdgePolicy::Hide}) {
        if (toString(policy) == text) {
            return policy;
        }
    }
    return std::nullopt;
}

Result<void> validate(const HiddenLineSettings& settings) {
    if (toString(settings.tangentEdges) == "unknown") {
        return makeError(ErrorCode::InvalidArgument,
                         "a view must have a known tangent-edge policy");
    }
    return {};
}

HiddenLinePolicyResult applyHiddenLinePolicy(const geometry::HiddenLineDrawing& drawing,
                                             const HiddenLineSettings& settings) {
    HiddenLinePolicyResult result;

    // Merge first, and unconditionally. Two model edges that project onto
    // each other are one line however the view is set; merging after
    // suppression would let a hidden duplicate be dropped by policy and its
    // visible twin then look like a line with no duplicate at all, so the
    // merged count would depend on the toggle.
    //
    // The input is in the geometry module's canonical order, so which of a
    // coincident group is met first does not vary between runs -- and the
    // winner is chosen by precedence rather than by arrival, so it would not
    // matter if it did.
    std::vector<geometry::ProjectedEdge> kept;
    kept.reserve(drawing.edges.size());
    for (const geometry::ProjectedEdge& edge : drawing.edges) {
        const auto same = std::ranges::find_if(
            kept, [&](const geometry::ProjectedEdge& e) { return sameCurve(e, edge); });
        if (same == kept.end()) {
            kept.push_back(edge);
            continue;
        }
        ++result.merged;
        if (wins(edge, *same)) {
            // Keep the survivor's class, but not its polyline: both describe
            // the same curve, and taking the winner's whole record keeps the
            // two consistent.
            *same = edge;
        }
    }

    // Then policy. An edge that both settings would drop is counted once,
    // because it is one line that is not drawn.
    for (geometry::ProjectedEdge& edge : kept) {
        const bool hiddenAndNotShown =
            edge.visibility == geometry::EdgeVisibility::Hidden && !settings.showHidden;
        const bool tangentAndNotShown = edge.kind == geometry::ProjectedEdgeKind::Smooth &&
                                        settings.tangentEdges == TangentEdgePolicy::Hide;
        if (hiddenAndNotShown || tangentAndNotShown) {
            ++result.suppressed;
            continue;
        }
        result.edges.push_back(std::move(edge));
    }
    return result;
}

} // namespace bettercad::drawing
