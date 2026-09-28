#include <bettercad/core/materials/Provenance.hpp>

#include <array>
#include <format>

namespace bettercad::materials {

namespace {

[[nodiscard]] bool isLeapYear(int year) noexcept {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

[[nodiscard]] int daysInMonth(int year, int month) noexcept {
    constexpr std::array<int, 12> kDays{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const int days = kDays[static_cast<std::size_t>(month - 1)];
    return (month == 2 && isLeapYear(year)) ? 29 : days;
}

constexpr std::array kSourceKinds{
    SourceKind::Unspecified, SourceKind::LibraryReference, SourceKind::ManufacturerData,
    SourceKind::Standard,    SourceKind::Handbook,         SourceKind::Measured,
    SourceKind::Calculated,  SourceKind::UserEntered,
};

} // namespace

std::optional<Date> Date::of(int year, int month, int day) noexcept {
    // A range wide enough for any datasheet and narrow enough that a garbled
    // number is caught rather than stored. Year 0 does not exist in the proleptic
    // Gregorian calendar as ISO 8601 uses it for this purpose, and a negative year
    // is not something a material citation has.
    if (year < 1 || year > 9999) {
        return std::nullopt;
    }
    if (month < 1 || month > 12) {
        return std::nullopt;
    }
    if (day < 1 || day > daysInMonth(year, month)) {
        return std::nullopt;
    }
    return Date{year, month, day};
}

std::string toString(const Date& date) {
    // Assembled from the three integers with fixed widths, so there is no locale,
    // no time zone and no clock anywhere in the result.
    return std::format("{:04}-{:02}-{:02}", date.year(), date.month(), date.day());
}

std::optional<Date> parseDate(std::string_view text) {
    // Hand-parsed digit by digit rather than through a stream or std::from_chars
    // on a substring, because the shape must be EXACTLY what toString writes:
    // "1999-1-1", "1999/01/01" and " 1999-01-01" are all rejected, and no locale
    // or stream state can influence the result.
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        return std::nullopt;
    }
    const auto digits = [&](std::size_t from, std::size_t count) -> std::optional<int> {
        int value = 0;
        for (std::size_t i = from; i < from + count; ++i) {
            if (text[i] < '0' || text[i] > '9') {
                return std::nullopt;
            }
            value = value * 10 + (text[i] - '0');
        }
        return value;
    };
    const std::optional<int> year = digits(0, 4);
    const std::optional<int> month = digits(5, 2);
    const std::optional<int> day = digits(8, 2);
    if (!year || !month || !day) {
        return std::nullopt;
    }
    // Through Date::of, so the calendar check -- including leap years -- is the
    // same one a caller constructing a date goes through.
    return Date::of(*year, *month, *day);
}

std::span<const SourceKind> sourceKinds() noexcept {
    return std::span<const SourceKind>{kSourceKinds};
}

std::string_view toString(SourceKind kind) noexcept {
    switch (kind) {
        case SourceKind::Unspecified:
            return "unspecified";
        case SourceKind::LibraryReference:
            return "library reference";
        case SourceKind::ManufacturerData:
            return "manufacturer data";
        case SourceKind::Standard:
            return "standard";
        case SourceKind::Handbook:
            return "handbook";
        case SourceKind::Measured:
            return "measured";
        case SourceKind::Calculated:
            return "calculated";
        case SourceKind::UserEntered:
            return "user entered";
    }
    return "unspecified";
}

bool isMeasured(SourceKind kind) noexcept { return kind == SourceKind::Measured; }

bool isReferenceData(SourceKind kind) noexcept {
    switch (kind) {
        case SourceKind::LibraryReference:
        case SourceKind::ManufacturerData:
        case SourceKind::Standard:
        case SourceKind::Handbook:
            return true;
        case SourceKind::Unspecified:
        case SourceKind::Measured:
        case SourceKind::Calculated:
        case SourceKind::UserEntered:
            return false;
    }
    return false;
}

PropertyProvenance effectiveProvenance(const MaterialProvenance& provenance,
                                       MechanicalPropertyKind kind) {
    if (isDerivedKind(kind)) {
        // A derived value has no source of its own (ADR-028). Saying `Calculated`
        // and nothing else is the truthful answer; the inputs it came from are
        // derivedInputProvenance()'s business, because there may be two of them
        // with different sources and one PropertyProvenance cannot hold both.
        PropertyProvenance derived;
        derived.kind = SourceKind::Calculated;
        return derived;
    }
    const auto found = provenance.mechanical.find(kind);
    if (found != provenance.mechanical.end() && !found->second.empty()) {
        return found->second;
    }
    return provenance.material;
}

PropertyProvenance effectiveProvenance(const MaterialProvenance& provenance,
                                       ThermalPropertyKind kind) {
    const auto found = provenance.thermal.find(kind);
    if (found != provenance.thermal.end() && !found->second.empty()) {
        return found->second;
    }
    return provenance.material;
}

bool hasOwnProvenance(const MaterialProvenance& provenance, MechanicalPropertyKind kind) {
    const auto found = provenance.mechanical.find(kind);
    return found != provenance.mechanical.end() && !found->second.empty();
}

bool hasOwnProvenance(const MaterialProvenance& provenance, ThermalPropertyKind kind) {
    const auto found = provenance.thermal.find(kind);
    return found != provenance.thermal.end() && !found->second.empty();
}

std::vector<std::pair<MechanicalPropertyKind, PropertyProvenance>>
derivedInputProvenance(const MaterialProvenance& provenance, MechanicalPropertyKind kind) {
    if (!isDerivedKind(kind)) {
        return {};
    }
    // Both G and K come from exactly E and nu (ADR-027). In enumeration order, so
    // the result is deterministic.
    std::vector<std::pair<MechanicalPropertyKind, PropertyProvenance>> inputs;
    for (const MechanicalPropertyKind input :
         {MechanicalPropertyKind::YoungsModulus, MechanicalPropertyKind::PoissonRatio}) {
        inputs.emplace_back(input, effectiveProvenance(provenance, input));
    }
    return inputs;
}

std::vector<std::string> provenanceProblems(const PropertyProvenance& provenance) {
    std::vector<std::string> problems;
    // Only claims that contradict themselves. Sparse metadata is legitimate, so
    // "no source given" is never a problem here -- that would make every ordinary
    // material report an issue and train users to ignore the report.
    if (provenance.kind == SourceKind::Standard && provenance.standard.empty()) {
        problems.emplace_back("the source is a standard, but no standard is named");
    }
    if (provenance.kind == SourceKind::ManufacturerData && provenance.source.empty()) {
        problems.emplace_back("the source is manufacturer data, but no source is named");
    }
    if (provenance.kind == SourceKind::Unspecified && !provenance.empty()) {
        // Citation details with no kind: the details are not wrong, but the claim
        // they support has not been made, so a reader cannot tell whether this is a
        // measurement or a handbook nominal.
        problems.emplace_back(
            "source details are given but the kind of source is unspecified");
    }
    return problems;
}

} // namespace bettercad::materials
