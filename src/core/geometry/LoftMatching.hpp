#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/core/units/Units.hpp>

#include <cstddef>
#include <span>
#include <vector>

// Matching loft sections of *different* shapes (P12-LOFT-001), and the
// areas the prismatoid check needs for them. P11-FEAT-009 matches sections
// that already have the same lines and arcs in the same order; this is what
// happens when they do not.
//
// The scheme: both loops are measured by normalized arc length, and each is
// split wherever the other has a corner, so that they end with the same
// number of segments and corresponding segments span the same stretch of
// each loop. The kernel is then told the correspondence, as it already is
// for equal shapes.
//
// BetterCAD decides all of this; the kernel only builds.
namespace bettercad::geometry::detail {

/// Where a loop's corners lie, by normalized arc length from its first
/// segment's start: breaks[i] is the start of segment i, so breaks[0] is 0
/// and the values increase to (but never reach) 1.
[[nodiscard]] std::vector<double> cornerParameters(const ProfileLoop& loop);

/// The length of @p loop, summed over its segments.
[[nodiscard]] Length loopLength(const ProfileLoop& loop);

/// @p loop split at every normalized arc length in @p parameters (each in
/// [0, 1)), so that the result has one segment between consecutive
/// parameters, in order. A parameter that falls on an existing corner
/// splits nothing. Lines split into lines and arcs into arcs, so the curve
/// is unchanged; a circle becomes arcs.
///
/// Fails with InvalidArgument if @p parameters is empty or a split would
/// make a segment shorter than the sketch tolerance.
[[nodiscard]] Result<ProfileLoop> splitAt(const ProfileLoop& loop, const std::vector<double>& parameters);

/// Two loops of different shapes, matched: both split at the union of their
/// corner parameters, with @p offset added to @p second's parameters first,
/// so that the two loops' starts are aligned where the caller chose.
struct MatchedPair {
    ProfileLoop first;
    ProfileLoop second;
};

[[nodiscard]] Result<MatchedPair> matchByArcLength(const ProfileLoop& first, const ProfileLoop& second,
                                                   double offset);

/// A whole chain of loops matched at once: loop i is split at the union of
/// every loop's corners, each mapped into loop i's own parameter through
/// @p shifts[i] -- the parameter of loop i that corresponds to the chain's
/// parameter 0. Segment k of every result then spans the same stretch of the
/// chain.
///
/// A chain must be matched in one go rather than pair by pair. A section in
/// the middle belongs to two pairs, and splitting it once for each would
/// leave the two pairs disagreeing about how many segments it has: the
/// kernel would be handed sections with different numbers of edges, and the
/// first pair's correspondence would be lost.
///
/// Fails with InvalidArgument if any section has no length, or if a split
/// would leave a segment shorter than the sketch tolerance.
[[nodiscard]] Result<std::vector<ProfileLoop>> matchChainByArcLength(std::span<const ProfileLoop> loops,
                                                                     const std::vector<double>& shifts);

/// The offsets worth trying when aligning @p second to @p first: each
/// corner of either loop, as a normalized arc length of @p second. The
/// caller scores them and keeps the least twist, as P11-FEAT-009 does for
/// loops of the same shape.
[[nodiscard]] std::vector<double> alignmentCandidates(const ProfileLoop& first, const ProfileLoop& second);

/// The mixed area of two matched loops: M = the closed integral of
/// q x dp/ds over the loops' shared parameter, which is what the area of
/// the cross-section between them needs.
///
/// Between matched loops p and q whose points move in straight lines, the
/// cross-section at t has area
///
///     A(t) = (1 - t)^2 A_p + t (1 - t) M + t^2 A_q,
///
/// a quadratic, so the prismatoid formula h/6 (A_p + 4 A_m + A_q) with
/// A_m = A(1/2) = (A_p + M + A_q) / 4 is exact. This computes M in closed
/// form for lines and arcs, without sampling and without the kernel.
///
/// The loops must already be matched: the same number of segments, taken
/// pairwise in order.
[[nodiscard]] Result<Area> mixedArea(const ProfileLoop& first, const ProfileLoop& second);

} // namespace bettercad::geometry::detail
