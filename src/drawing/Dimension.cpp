#include <bettercad/drawing/Dimension.hpp>

#include <bettercad/core/units/UnitCatalog.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>
#include <utility>

namespace bettercad::drawing {
namespace {

[[nodiscard]] std::unexpected<Error> wrong(std::string message) {
    return makeError(ErrorCode::InvalidArgument, std::move(message));
}

/// How many decimals a double can honestly carry here.
constexpr std::uint8_t kMaxDecimals = 6;

} // namespace

std::string_view toString(DimensionType type) noexcept {
    switch (type) {
    case DimensionType::Linear:
        return "linear";
    case DimensionType::Horizontal:
        return "horizontal";
    case DimensionType::Vertical:
        return "vertical";
    case DimensionType::Aligned:
        return "aligned";
    case DimensionType::Angular:
        return "angular";
    case DimensionType::Radius:
        return "radius";
    case DimensionType::Diameter:
        return "diameter";
    case DimensionType::Ordinate:
        return "ordinate";
    }
    return "unknown";
}

std::optional<DimensionType> dimensionTypeFromString(std::string_view text) noexcept {
    for (const DimensionType type :
         {DimensionType::Linear, DimensionType::Horizontal, DimensionType::Vertical,
          DimensionType::Aligned, DimensionType::Angular, DimensionType::Radius,
          DimensionType::Diameter, DimensionType::Ordinate}) {
        if (toString(type) == text) {
            return type;
        }
    }
    return std::nullopt;
}

std::string_view toString(OrdinateAxis axis) noexcept {
    switch (axis) {
    case OrdinateAxis::X:
        return "x";
    case OrdinateAxis::Y:
        return "y";
    }
    return "unknown";
}

std::optional<OrdinateAxis> ordinateAxisFromString(std::string_view text) noexcept {
    for (const OrdinateAxis axis : {OrdinateAxis::X, OrdinateAxis::Y}) {
        if (toString(axis) == text) {
            return axis;
        }
    }
    return std::nullopt;
}

bool isAngular(DimensionType type) noexcept { return type == DimensionType::Angular; }

bool isSingleTarget(DimensionType type) noexcept {
    return type == DimensionType::Radius || type == DimensionType::Diameter;
}

bool isEmpty(const DimensionTarget& target) noexcept {
    return !target.plane && !target.axis && !target.cylinder;
}

Result<void> validate(const DimensionTarget& target) {
    const int named = static_cast<int>(target.plane.has_value()) +
                      static_cast<int>(target.axis.has_value()) +
                      static_cast<int>(target.cylinder.has_value());
    if (named == 0) {
        return wrong("a dimension target must name a plane, an axis or a cylindrical face");
    }
    if (named > 1) {
        return wrong("a dimension target names one thing: a plane, an axis or a cylindrical "
                     "face, not several");
    }
    if (target.plane) {
        return validate(*target.plane);
    }
    if (target.cylinder) {
        // The selector's own rules; whether the feature exists and generates
        // the face is checkDimension()'s to say, against the document.
        return validate(target.cylinder->face);
    }
    return {};
}

std::vector<ObjectId> referencedObjects(const DimensionTarget& target) {
    if (target.plane) {
        return referencedObjects(*target.plane);
    }
    if (target.axis) {
        return target.axis->object ? std::vector<ObjectId>{*target.axis->object}
                                   : std::vector<ObjectId>{};
    }
    if (target.cylinder) {
        std::vector<ObjectId> objects{target.cylinder->feature};
        for (const FaceCopy& copy : target.cylinder->face.copies) {
            if (std::ranges::find(objects, copy.feature) == objects.end()) {
                objects.push_back(copy.feature);
            }
        }
        return objects;
    }
    return {};
}

Result<void> validate(const DimensionFormat& format) {
    if (format.decimals > kMaxDecimals) {
        return wrong(std::format("a dimension may be written to at most {} decimals; {} says more "
                                 "than a model built in double precision can support",
                                 kMaxDecimals, format.decimals));
    }
    if (!findUnit(format.unit)) {
        return wrong(std::format("'{}' is not a unit this build knows", format.unit));
    }
    return {};
}

Result<void> validate(const DimensionDefinition& definition) {
    if (!definition.view.isValid()) {
        return wrong("a dimension must name the view it is measured in");
    }
    if (toString(definition.type) == "unknown") {
        return wrong("a dimension must have a known type");
    }
    if (auto valid = validate(definition.from); !valid) {
        return std::unexpected(valid.error());
    }

    // A radius and a diameter measure ONE thing. A second target would be a
    // second fact with nothing to do, and the reader could not tell which of
    // the two the number came from.
    if (isSingleTarget(definition.type)) {
        if (!isEmpty(definition.to)) {
            return wrong(std::format("a {} dimension measures one face and takes no second target",
                                     toString(definition.type)));
        }
        if (!definition.from.cylinder) {
            return wrong(std::format("a {} dimension measures a cylindrical face; a plane or an "
                                     "axis has no radius",
                                     toString(definition.type)));
        }
    } else {
        if (isEmpty(definition.to)) {
            return wrong(std::format("a {} dimension measures between two things and must name "
                                     "both",
                                     toString(definition.type)));
        }
        if (auto valid = validate(definition.to); !valid) {
            return std::unexpected(valid.error());
        }
        if (definition.from.cylinder || definition.to.cylinder) {
            return wrong(std::format("a {} dimension measures between planes and axes; a "
                                     "cylindrical face is named only by a radius or a diameter",
                                     toString(definition.type)));
        }
    }

    if (auto valid = validate(definition.format); !valid) {
        return std::unexpected(valid.error());
    }
    if (definition.tolerance) {
        if (isAngular(definition.type)) {
            return wrong("an angular tolerance is not part of this foundation; the deviations "
                         "here are lengths, and a length on an angle means nothing");
        }
        if (auto valid = validate(*definition.tolerance); !valid) {
            return std::unexpected(valid.error());
        }
    }
    // The unit has to suit what is being measured. Writing an angle in
    // millimetres is not a formatting choice, it is a category error, and a
    // drawing that did it would read as a length.
    const auto unit = findUnit(definition.format.unit);
    const bettercad::Dimension wanted =
        isAngular(definition.type) ? dimensions::angle : dimensions::length;
    if (unit->dimension != wanted) {
        return wrong(std::format("a {} dimension cannot be written in {}: it measures {}",
                                 toString(definition.type), definition.format.unit,
                                 isAngular(definition.type) ? "an angle" : "a length"));
    }

    for (const auto& [name, value] : std::array<std::pair<std::string_view, Length>, 2>{
             {{"x", definition.placement.x}, {"y", definition.placement.y}}}) {
        if (!std::isfinite(value.si())) {
            return wrong(std::format("a dimension's {} placement must be finite", name));
        }
    }
    return {};
}

Result<std::unique_ptr<Dimension>> Dimension::create(std::string name,
                                                     const DimensionDefinition& definition) {
    if (auto valid = validateObjectName(name); !valid) {
        return std::unexpected(valid.error());
    }
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    return std::unique_ptr<Dimension>(new Dimension(std::move(name), definition));
}

Dimension::Dimension(std::string name, const DimensionDefinition& definition)
    : DocumentObject(std::move(name)), definition_(definition) {}

std::unique_ptr<DocumentObject> Dimension::clone() const {
    return std::unique_ptr<Dimension>(new Dimension(*this));
}

bool Dimension::contentEquals(const DocumentObject& other) const {
    const auto* dimension = dynamic_cast<const Dimension*>(&other);
    return dimension != nullptr && dimension->definition_ == definition_;
}

std::vector<ObjectId> Dimension::dependencies() const {
    std::vector<ObjectId> result;
    result.push_back(ObjectId{definition_.view});
    for (const DimensionTarget* target : {&definition_.from, &definition_.to}) {
        for (const ObjectId object : referencedObjects(*target)) {
            if (object.isValid() && std::ranges::find(result, object) == result.end()) {
                result.push_back(object);
            }
        }
    }
    return result;
}

Result<bool> Dimension::setDefinition(const DimensionDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    if (definition == definition_) {
        return false;
    }
    definition_ = definition;
    return true;
}

namespace {

/// Rounds half AWAY FROM ZERO and writes the result with exactly @p decimals
/// digits, without going through the locale.
///
/// std::format rounds half to EVEN, so 0.125 at two decimals comes out 0.12
/// because the digit before it is even, while 0.135 comes out 0.14. A drawing
/// office rounds both up. That is not a rounding detail: it is one dimension
/// disagreeing with another produced from the same number.
///
/// The digits are taken from an integer count of the last decimal place, so
/// the text cannot pick up a locale's decimal separator or grouping.
[[nodiscard]] Result<std::string> writeFixed(double value, std::uint8_t decimals) {
    const double magnitude = std::abs(value);

    // The rounding is done in INTEGERS, on a count of a decimal place finer
    // than the one that will be shown. Two things make that necessary.
    //
    // A value typed in millimetres is held in METRES, and the trip back out
    // does not land on the decimal that was typed: 0.145 mm comes back a
    // fraction low. And the double nearest 0.145 times 100 is
    // 14.499999999999998, so even a value that IS 0.145 would round down if
    // the last step were another multiply. Counting at six decimals finer and
    // then dividing settles both, in arithmetic that is exact, and the guard
    // it absorbs is at most 1e-12 of the display unit -- far below anything a
    // drawing distinguishes.
    int guardDigits = static_cast<int>(decimals) + 6;
    guardDigits = guardDigits > 12 ? 12 : guardDigits;
    double guardScale = 1.0;
    const auto scaleFor = [](int digits) {
        double result = 1.0;
        for (int i = 0; i < digits; ++i) {
            result *= 10.0;
        }
        return result;
    };
    // Back the guard off until the count lands inside the range where a
    // double holds integers exactly.
    while (guardDigits > static_cast<int>(decimals) &&
           magnitude * scaleFor(guardDigits) >= 9.0e15) {
        --guardDigits;
    }
    guardScale = scaleFor(guardDigits);
    if (magnitude * guardScale >= 9.0e15) {
        return makeError(ErrorCode::FailedPrecondition,
                         "a dimension this large cannot be written to this many decimals");
    }

    const long long guardTicks = std::llround(magnitude * guardScale);
    long long divisor = 1;
    for (int i = 0; i < guardDigits - static_cast<int>(decimals); ++i) {
        divisor *= 10;
    }
    // Half away from zero, as integer arithmetic on a non-negative count.
    const long long ticks = (guardTicks + divisor / 2) / divisor;

    long long unitScale = 1;
    for (std::uint8_t i = 0; i < decimals; ++i) {
        unitScale *= 10;
    }
    const long long whole = ticks / unitScale;
    const long long fraction = ticks % unitScale;

    std::string text;
    if (value < 0.0 && ticks != 0) {
        text += '-'; // never "-0.00"
    }
    text += std::to_string(whole);
    if (decimals > 0) {
        std::string digits = std::to_string(fraction);
        digits.insert(digits.begin(), decimals - digits.size(), '0');
        text += '.';
        text += digits;
    }
    return text;
}

/// Drops the trailing zeros of a fixed-point number, and the point with them.
[[nodiscard]] std::string withoutTrailingZeros(std::string text) {
    if (text.find('.') == std::string::npos) {
        return text;
    }
    while (!text.empty() && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    return text.empty() || text == "-" ? "0" : text;
}

[[nodiscard]] Result<std::string> write(double si, const DimensionFormat& format,
                                        bettercad::Dimension expected) {
    if (auto valid = validate(format); !valid) {
        return std::unexpected(valid.error());
    }
    const auto unit = findUnit(format.unit);
    if (unit->dimension != expected) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("'{}' does not measure what this dimension measures",
                                     format.unit));
    }
    if (!std::isfinite(si)) {
        return makeError(ErrorCode::FailedPrecondition,
                         "a dimension whose value is not finite cannot be written");
    }
    auto text = writeFixed(unit->scale.fromSi(si), format.decimals);
    if (!text) {
        return std::unexpected(text.error());
    }
    std::string written = format.trailingZeros ? std::move(*text)
                                               : withoutTrailingZeros(std::move(*text));
    if (format.showUnit) {
        written += ' ';
        written += format.unit;
    }
    return written;
}

} // namespace

Result<std::string> formatLength(Length value, const DimensionFormat& format) {
    return write(value.si(), format, dimensions::length);
}

Result<std::string> formatAngle(Angle value, const DimensionFormat& format) {
    return write(value.si(), format, dimensions::angle);
}

std::uint8_t decimalsWithoutRounding(Length value, std::uint8_t atLeast) noexcept {
    constexpr std::uint8_t kMost = 6;
    const double millimetres = std::abs(value.si()) * 1000.0;
    if (!std::isfinite(millimetres)) {
        return atLeast;
    }
    std::uint8_t decimals = std::min(atLeast, kMost);
    double scale = 1.0;
    for (std::uint8_t i = 0; i < decimals; ++i) {
        scale *= 10.0;
    }
    // Enough decimals that the value survives being written. The slack is
    // RELATIVE: it absorbs the representation error of a number that came
    // through metres, and nothing else. An absolute slack would call every
    // value below it zero -- and answer "no decimals" for the very values
    // that most need them.
    const auto rounds = [&]() {
        const double ticks = millimetres * scale;
        return std::abs(ticks - std::round(ticks)) > std::abs(ticks) * 1e-9;
    };
    while (decimals < kMost && rounds()) {
        ++decimals;
        scale *= 10.0;
    }
    return decimals;
}

Result<std::string> formatDeviation(Length value, const DimensionFormat& format) {
    auto text = formatLength(value, format);
    if (!text) {
        return std::unexpected(text.error());
    }
    // A deviation is read as a signed offset from the nominal, so the sign is
    // always written: "+0.10 -0.02" rather than "0.10 -0.02", which reads as
    // a range.
    if (!text->empty() && text->front() == '-') {
        return *text;
    }
    return "+" + *text;
}

Result<std::string> formatTolerance(const DimensionTolerance& tolerance,
                                    const DimensionFormat& format) {
    if (auto valid = validate(tolerance); !valid) {
        return std::unexpected(valid.error());
    }
    if (tolerance.fit) {
        // The designation, not its numbers: a drawing that cites H7 means the
        // standard, and the standard is read when the limits are wanted.
        return toString(*tolerance.fit);
    }
    // The deviations are written to their own precision, never rounded to
    // the nominal's: "+/-0" is not a looser way of saying +/-0.05, it is a
    // requirement no part can meet.
    DimensionFormat precise = format;
    precise.decimals = std::max(decimalsWithoutRounding(tolerance.lower, format.decimals),
                                decimalsWithoutRounding(tolerance.upper, format.decimals));
    if (isSymmetric(tolerance)) {
        auto magnitude = formatLength(tolerance.upper, precise);
        if (!magnitude) {
            return std::unexpected(magnitude.error());
        }
        return "±" + *magnitude;
    }
    auto upper = formatDeviation(tolerance.upper, precise);
    if (!upper) {
        return std::unexpected(upper.error());
    }
    auto lower = formatDeviation(tolerance.lower, precise);
    if (!lower) {
        return std::unexpected(lower.error());
    }
    return *upper + " " + *lower;
}

} // namespace bettercad::drawing
