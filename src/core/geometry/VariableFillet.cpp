// Validation and the radius law of variable-radius fillets: everything that
// can be computed without a body.
#include "core/geometry/EdgeMatching.hpp"
#include "core/geometry/RadiusLaw.hpp"

#include <bettercad/core/geometry/VariableFillet.hpp>
#include <bettercad/core/units/Format.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace bettercad::geometry {

namespace {

constexpr std::string_view kNoun = "variable-radius fillet";
// The law may pass a station's radius by rounding, never by more.
constexpr double kLawRoundingRelative = 1e-12;

double mm(Length value) {
    return value.in(units::mm);
}

/// @p where names the stations in messages ("edge reference 2").
Result<void> checkPositions(const std::string& where, const std::vector<RadiusStation>& stations) {
    if (stations.size() < 2) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{}: a {} needs two or more radius stations on each edge, got {}", where, kNoun,
                                     stations.size()));
    }
    for (std::size_t i = 0; i < stations.size(); ++i) {
        if (!std::isfinite(stations[i].position)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{}, station {}: the position must be finite, got {}", where, i + 1,
                                         stations[i].position));
        }
    }
    if (stations.front().position != 0.0) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{}: the first station must be at position 0, got {}", where,
                                     stations.front().position));
    }
    if (stations.back().position != 1.0) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{}: the last station must be at position 1, got {}", where,
                                     stations.back().position));
    }
    for (std::size_t i = 1; i < stations.size(); ++i) {
        if (!(stations[i].position - stations[i - 1].position >= kMinimumStationGap)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{}: station {} (at {}) does not follow station {} (at {}); stations are "
                                         "listed by increasing position, at least {} apart",
                                         where, i + 1, stations[i].position, i, stations[i - 1].position,
                                         kMinimumStationGap));
        }
    }
    return {};
}

Result<void> checkRadii(const std::string& where, const std::vector<RadiusStation>& stations) {
    for (std::size_t i = 0; i < stations.size(); ++i) {
        const Length radius = stations[i].radius;
        if (!isFinite(radius) || !(radius > Length{})) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{}, station {}: the radius must be positive and finite, got {}", where,
                                         i + 1, toString(radius, units::mm)));
        }
    }
    return {};
}

Result<void> checkLaw(const std::string& where, const std::vector<RadiusStation>& stations) {
    const auto [low, high] = std::ranges::minmax(stations, {}, &RadiusStation::radius);
    const double spreadMm = mm(high.radius) - mm(low.radius);
    if (spreadMm > 0.0 && spreadMm < kMinimumRadiusSpreadMm) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{}: the station radii differ by only {:.6g} mm; give them one radius, or "
                                     "radii at least {} mm apart",
                                     where, spreadMm, kMinimumRadiusSpreadMm));
    }
    const detail::RadiusLaw law(stations);
    for (std::size_t span = 0; span < law.stationSpans(); ++span) {
        const RadiusStation& from = stations[span];
        const RadiusStation& to = stations[span + 1];
        const double lower = std::min(from.radius.si(), to.radius.si());
        const double upper = std::max(from.radius.si(), to.radius.si());
        const double slack = kLawRoundingRelative * upper;
        const detail::LawExtreme lowest = law.lowest(span);
        const detail::LawExtreme highest = law.highest(span);
        const bool rises = highest.radius > upper + slack;
        const bool falls = lowest.radius < lower - slack;
        if (!rises && !falls) {
            continue;
        }
        const detail::LawExtreme& worst = rises ? highest : lowest;
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{}: between stations {} and {} ({:.6g} mm at {}, {:.6g} mm at {}) the "
                                     "radius would {} to {:.6g} mm at {:.6g}; a variable radius must stay between "
                                     "the radii of the stations on either side",
                                     where, span + 1, span + 2, mm(from.radius), from.position, mm(to.radius),
                                     to.position, rises ? "rise" : "fall", worst.radius * 1e3, worst.position));
    }
    return {};
}

Result<void> checkStations(const std::string& where, const std::vector<RadiusStation>& stations,
                           StationCheck check) {
    if (auto positions = checkPositions(where, stations); !positions) {
        return positions;
    }
    if (auto radii = checkRadii(where, stations); !radii) {
        return radii;
    }
    if (check == StationCheck::Complete) {
        return checkLaw(where, stations);
    }
    return {};
}

} // namespace

Result<void> validate(const VariableFilletRequest& request, StationCheck check) {
    std::vector<EdgeSignature> signatures;
    signatures.reserve(request.edges.size());
    for (const VariableFilletEdge& edge : request.edges) {
        signatures.push_back(edge.edge);
    }
    if (auto edges = detail::validateEdgeSelection(kNoun, signatures); !edges) {
        return edges;
    }
    for (std::size_t i = 0; i < request.edges.size(); ++i) {
        const VariableFilletEdge& edge = request.edges[i];
        if (edge.edge.curve != EdgeCurve::Line) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("edge reference {} ({}) is not straight; a {} rounds straight edges only",
                                         i + 1, describe(edge.edge), kNoun));
        }
        if (auto stations = checkStations(std::format("edge reference {}", i + 1), edge.stations, check);
            !stations) {
            return stations;
        }
    }
    return {};
}

Result<Length> radiusAt(const std::vector<RadiusStation>& stations, double position) {
    if (auto valid = checkStations("the stations", stations, StationCheck::Complete); !valid) {
        return std::unexpected(valid.error());
    }
    if (!(position >= 0.0 && position <= 1.0)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the position must be between 0 and 1, got {}", position));
    }
    return Length::fromSi(detail::RadiusLaw(stations)(position));
}

} // namespace bettercad::geometry
