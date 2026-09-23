#include <bettercad/drawing/Tolerance.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <utility>

namespace bettercad::drawing {
namespace {

[[nodiscard]] std::unexpected<Error> wrong(std::string message) {
    return makeError(ErrorCode::InvalidArgument, std::move(message));
}

} // namespace

std::string_view toString(ToleranceDisplay display) noexcept {
    switch (display) {
    case ToleranceDisplay::PlusMinus:
        return "plus_minus";
    case ToleranceDisplay::Limits:
        return "limits";
    }
    return "unknown";
}

std::optional<ToleranceDisplay> toleranceDisplayFromString(std::string_view text) noexcept {
    for (const ToleranceDisplay display : {ToleranceDisplay::PlusMinus, ToleranceDisplay::Limits}) {
        if (toString(display) == text) {
            return display;
        }
    }
    return std::nullopt;
}

std::string_view toString(FitRole role) noexcept {
    switch (role) {
    case FitRole::Hole:
        return "hole";
    case FitRole::Shaft:
        return "shaft";
    }
    return "unknown";
}

std::optional<FitRole> fitRoleFromString(std::string_view text) noexcept {
    for (const FitRole role : {FitRole::Hole, FitRole::Shaft}) {
        if (toString(role) == text) {
            return role;
        }
    }
    return std::nullopt;
}

Result<void> validate(const FitDesignation& fit) {
    if (toString(fit.role) == "unknown") {
        return wrong("a fit must say whether it is a hole or a shaft");
    }
    if (fit.letter < 'A' || fit.letter > 'Z') {
        return wrong(std::format("a fit's fundamental deviation is a capital letter A to Z; "
                                 "'{}' is not one",
                                 fit.letter));
    }
    // ISO 286-1 Table 1 tabulates grades 1 to 18.
    if (fit.grade < 1 || fit.grade > 18) {
        return wrong(std::format("a standard tolerance grade runs from 1 to 18; {} is outside it",
                                 fit.grade));
    }
    return {};
}

std::string toString(const FitDesignation& fit) {
    // The case is the role's, not a stored fact: ISO 286 writes a hole's
    // letter as a capital and a shaft's in lower case.
    const char letter = fit.role == FitRole::Hole
                            ? fit.letter
                            : static_cast<char>(std::tolower(static_cast<unsigned char>(fit.letter)));
    return std::format("{}{}", letter, fit.grade);
}

Result<Length> fitWidth(Length nominalSize, const FitDesignation& fit) {
    if (auto valid = validate(fit); !valid) {
        return std::unexpected(valid.error());
    }
    // IT is shared by holes and shafts: only the position differs. So a
    // shaft's tolerance WIDTH is known here even where its deviations are
    // not.
    return standards::standardTolerance(nominalSize, fit.grade);
}

Result<standards::LimitDeviations> fitDeviations(Length nominalSize, const FitDesignation& fit) {
    if (auto valid = validate(fit); !valid) {
        return std::unexpected(valid.error());
    }
    if (fit.role != FitRole::Hole) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("the fundamental deviations of shafts are not tabulated in "
                                     "this build, so {} has a known tolerance width but no known "
                                     "limits; the designation is kept and no numbers are invented",
                                     toString(fit)));
    }
    const auto deviation = [&]() -> std::optional<standards::HoleDeviation> {
        switch (fit.letter) {
        case 'D':
            return standards::HoleDeviation::D;
        case 'E':
            return standards::HoleDeviation::E;
        case 'F':
            return standards::HoleDeviation::F;
        case 'G':
            return standards::HoleDeviation::G;
        case 'H':
            return standards::HoleDeviation::H;
        default:
            return std::nullopt;
        }
    }();
    if (!deviation) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("the hole position {} is not tabulated in this build, which "
                                     "carries D, E, F, G and H; the designation is kept and no "
                                     "numbers are invented",
                                     fit.letter));
    }
    return standards::limitDeviations(nominalSize,
                                      standards::HoleToleranceClass{*deviation, fit.grade});
}

bool isSymmetric(const DimensionTolerance& tolerance) noexcept {
    return !tolerance.fit && tolerance.lower.si() == -tolerance.upper.si();
}

Result<void> validate(const DimensionTolerance& tolerance) {
    if (toString(tolerance.display) == "unknown") {
        return wrong("a tolerance must say how it is written");
    }
    if (tolerance.fit) {
        // A fit IS a deviation pair. Carrying both would let the two
        // disagree, and nothing could say which the part was made to.
        if (tolerance.lower.si() != 0.0 || tolerance.upper.si() != 0.0) {
            return wrong("a tolerance gives a fit designation or a pair of deviations, not both: "
                         "a fit already says what the deviations are");
        }
        return validate(*tolerance.fit);
    }
    if (!std::isfinite(tolerance.lower.si()) || !std::isfinite(tolerance.upper.si())) {
        return wrong("a tolerance's deviations must be finite");
    }
    // The only invariant. The nominal need NOT be inside the interval: ISO
    // 286 routinely puts a whole class above it, and H7 has both deviations
    // at or above zero.
    if (tolerance.lower.si() > tolerance.upper.si()) {
        return wrong("a tolerance's lower deviation must not be above its upper one; the "
                     "deviations are signed from the nominal, so a symmetric tolerance is "
                     "-x and +x");
    }
    // An interval of no width admits exactly one size, which nothing can be
    // made to -- the same reason a geometric zone of no width is refused. A
    // default-constructed tolerance lands here, so a dimension cannot come to
    // read "100 +/-0" because somebody meant to leave the tolerance off.
    if (tolerance.lower.si() == tolerance.upper.si()) {
        return wrong("a tolerance of no width admits only the nominal exactly, which nothing can "
                     "be made to; leave the tolerance off instead");
    }
    return {};
}

Result<standards::LimitDeviations> deviationsOf(Length nominal,
                                                const DimensionTolerance& tolerance) {
    if (auto valid = validate(tolerance); !valid) {
        return std::unexpected(valid.error());
    }
    if (!std::isfinite(nominal.si())) {
        return makeError(ErrorCode::InvalidArgument,
                         "a tolerance is about a nominal size, and this one is not finite");
    }
    if (tolerance.fit) {
        return fitDeviations(nominal, *tolerance.fit);
    }
    return standards::LimitDeviations{tolerance.lower, tolerance.upper};
}

Result<ToleranceInterval> intervalOf(Length nominal, const DimensionTolerance& tolerance) {
    auto deviations = deviationsOf(nominal, tolerance);
    if (!deviations) {
        return std::unexpected(deviations.error());
    }
    return ToleranceInterval{nominal + deviations->lower, nominal + deviations->upper};
}

std::string_view toString(GeometricCharacteristic characteristic) noexcept {
    switch (characteristic) {
    case GeometricCharacteristic::Straightness:
        return "straightness";
    case GeometricCharacteristic::Flatness:
        return "flatness";
    case GeometricCharacteristic::Circularity:
        return "circularity";
    case GeometricCharacteristic::Cylindricity:
        return "cylindricity";
    case GeometricCharacteristic::Parallelism:
        return "parallelism";
    case GeometricCharacteristic::Perpendicularity:
        return "perpendicularity";
    case GeometricCharacteristic::Angularity:
        return "angularity";
    case GeometricCharacteristic::Position:
        return "position";
    case GeometricCharacteristic::CircularRunout:
        return "circular_runout";
    case GeometricCharacteristic::TotalRunout:
        return "total_runout";
    }
    return "unknown";
}

std::optional<GeometricCharacteristic> geometricCharacteristicFromString(
    std::string_view text) noexcept {
    for (const GeometricCharacteristic characteristic :
         {GeometricCharacteristic::Straightness, GeometricCharacteristic::Flatness,
          GeometricCharacteristic::Circularity, GeometricCharacteristic::Cylindricity,
          GeometricCharacteristic::Parallelism, GeometricCharacteristic::Perpendicularity,
          GeometricCharacteristic::Angularity, GeometricCharacteristic::Position,
          GeometricCharacteristic::CircularRunout, GeometricCharacteristic::TotalRunout}) {
        if (toString(characteristic) == text) {
            return characteristic;
        }
    }
    return std::nullopt;
}

std::string_view symbolOf(GeometricCharacteristic characteristic) noexcept {
    // Unicode's Miscellaneous Technical block names most of these outright.
    // The codepoint is given beside each so the choice can be checked rather
    // than trusted.
    switch (characteristic) {
    case GeometricCharacteristic::Straightness:
        return "⏤"; // U+23E4 STRAIGHTNESS
    case GeometricCharacteristic::Flatness:
        return "⏥"; // U+23E5 FLATNESS
    case GeometricCharacteristic::Circularity:
        return "○"; // U+25CB WHITE CIRCLE
    case GeometricCharacteristic::Cylindricity:
        return "⌭"; // U+232D CYLINDRICITY
    case GeometricCharacteristic::Parallelism:
        return "∥"; // U+2225 PARALLEL TO
    case GeometricCharacteristic::Perpendicularity:
        return "⟂"; // U+27C2 PERPENDICULAR
    case GeometricCharacteristic::Angularity:
        return "∠"; // U+2220 ANGLE
    case GeometricCharacteristic::Position:
        return "⌖"; // U+2316 POSITION INDICATOR
    case GeometricCharacteristic::CircularRunout:
        // Unicode has no codepoint for it; ISO 1101 draws a single arrow,
        // and this is the conventional substitute rather than the symbol.
        return "↗"; // U+2197 NORTH EAST ARROW
    case GeometricCharacteristic::TotalRunout:
        return "⌰"; // U+2330 TOTAL RUNOUT
    }
    return "?";
}

std::string_view toString(ToleranceZone zone) noexcept {
    switch (zone) {
    case ToleranceZone::Width:
        return "width";
    case ToleranceZone::Cylindrical:
        return "cylindrical";
    }
    return "unknown";
}

std::optional<ToleranceZone> toleranceZoneFromString(std::string_view text) noexcept {
    for (const ToleranceZone zone : {ToleranceZone::Width, ToleranceZone::Cylindrical}) {
        if (toString(zone) == text) {
            return zone;
        }
    }
    return std::nullopt;
}

bool isForm(GeometricCharacteristic characteristic) noexcept {
    switch (characteristic) {
    case GeometricCharacteristic::Straightness:
    case GeometricCharacteristic::Flatness:
    case GeometricCharacteristic::Circularity:
    case GeometricCharacteristic::Cylindricity:
        return true;
    default:
        return false;
    }
}

bool allowsCylindricalZone(GeometricCharacteristic characteristic) noexcept {
    // A cylindrical zone holds a LINE -- an axis or a median line. So the
    // characteristics that can control one may have it, and the ones that
    // control a surface or a circle cannot: a flatness zone is two planes, a
    // circularity zone two concentric circles, and neither is ever a
    // cylinder.
    switch (characteristic) {
    case GeometricCharacteristic::Straightness:
    case GeometricCharacteristic::Parallelism:
    case GeometricCharacteristic::Perpendicularity:
    case GeometricCharacteristic::Angularity:
    case GeometricCharacteristic::Position:
        return true;
    default:
        return false;
    }
}

Result<void> validateDatumLetter(char letter) {
    if (letter < 'A' || letter > 'Z') {
        return wrong(std::format("a datum's letter must be a capital A to Z; '{}' is not one",
                                 letter));
    }
    if (letter == 'I' || letter == 'O' || letter == 'Q') {
        return wrong("ISO 5459 does not use I, O or Q as datum letters: they read as 1 and 0");
    }
    return {};
}

Result<void> validate(const FeatureControlFrame& frame) {
    if (toString(frame.characteristic) == "unknown") {
        return wrong("a feature-control frame must name a known characteristic");
    }
    if (toString(frame.zone) == "unknown") {
        return wrong("a feature-control frame must name a known tolerance zone");
    }
    if (!std::isfinite(frame.tolerance.si()) || frame.tolerance.si() <= 0.0) {
        return wrong("a geometric tolerance must be finite and greater than zero: a zone of no "
                     "width admits nothing");
    }

    if (!allowsCylindricalZone(frame.characteristic) && frame.zone == ToleranceZone::Cylindrical) {
        return wrong(std::format("a {} tolerance holds a surface or a circle, not a line, so its "
                                 "zone cannot be a cylinder",
                                 toString(frame.characteristic)));
    }

    // Form is a property of a feature by itself; orientation, position and
    // runout are relations, and a relation needs something to be related to.
    if (isForm(frame.characteristic)) {
        if (!frame.datums.empty()) {
            return wrong(std::format("a {} tolerance is a property of the feature by itself and "
                                     "cites no datum; citing one says an orientation tolerance "
                                     "was meant",
                                     toString(frame.characteristic)));
        }
    } else if (frame.datums.empty()) {
        return wrong(std::format("a {} tolerance holds a feature against a datum, and this frame "
                                 "cites none: parallel to what?",
                                 toString(frame.characteristic)));
    }

    if (frame.datums.size() > 3) {
        return wrong(std::format("a feature-control frame cites at most three datums -- primary, "
                                 "secondary and tertiary -- and this one cites {}",
                                 frame.datums.size()));
    }
    for (std::size_t i = 0; i < frame.datums.size(); ++i) {
        if (auto valid = validateDatumLetter(frame.datums[i].letter); !valid) {
            return std::unexpected(valid.error());
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (frame.datums[j].letter == frame.datums[i].letter) {
                return wrong(std::format("datum {} is cited twice in one frame; a datum constrains "
                                         "what it constrains once",
                                         frame.datums[i].letter));
            }
        }
    }
    return {};
}

} // namespace bettercad::drawing
