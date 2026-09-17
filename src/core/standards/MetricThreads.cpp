// ISO general purpose metric screw threads (P12-HOLE-001): the sizes of
// ISO 965-2 and the internal thread tolerances of their ISO 965-2 classes,
// transcribed from ISO 965-1:2013 (Tables 1, 2 and 4) and ISO 965-2 (Tables
// 1 and 3); see docs/verification/P12-HOLE-001/ for the sources and their
// cross-checks.
#include "core/standards/Designations.hpp"

#include <bettercad/core/standards/MetricThreads.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <numbers>
#include <optional>

namespace bettercad::standards {

struct MetricThreadTable {
    static constexpr MetricThread make(std::uint32_t diameterUm, std::uint32_t pitchUm, bool coarse) noexcept {
        return MetricThread{diameterUm, pitchUm, coarse};
    }
    static constexpr std::uint32_t diameterUm(const MetricThread& thread) noexcept { return thread.diameterUm_; }
    static constexpr std::uint32_t pitchUm(const MetricThread& thread) noexcept { return thread.pitchUm_; }
};

namespace {

using Table = MetricThreadTable;

constexpr MetricThread coarse(std::uint32_t diameterUm, std::uint32_t pitchUm) noexcept {
    return Table::make(diameterUm, pitchUm, true);
}

constexpr MetricThread fine(std::uint32_t diameterUm, std::uint32_t pitchUm) noexcept {
    return Table::make(diameterUm, pitchUm, false);
}

/// The known sizes in micrometres: ISO 965-2:1998, Table 1 (coarse) and
/// Table 3 (fine); ISO 965-2:2024 lists the same sizes up to M64 in its
/// Table 1.
constexpr std::array<MetricThread, 60> kSizes{{
    coarse(1000, 250),   coarse(1200, 250),   coarse(1400, 300),   coarse(1600, 350),   coarse(1800, 350),
    coarse(2000, 400),   coarse(2500, 450),   coarse(3000, 500),   coarse(3500, 600),   coarse(4000, 700),
    coarse(5000, 800),   coarse(6000, 1000),  coarse(7000, 1000),  coarse(8000, 1250),  coarse(10000, 1500),
    coarse(12000, 1750), coarse(14000, 2000), coarse(16000, 2000), coarse(18000, 2500), coarse(20000, 2500),
    coarse(22000, 2500), coarse(24000, 3000), coarse(27000, 3000), coarse(30000, 3500), coarse(33000, 3500),
    coarse(36000, 4000), coarse(39000, 4000), coarse(42000, 4500), coarse(45000, 4500), coarse(48000, 5000),
    coarse(52000, 5000), coarse(56000, 5500), coarse(60000, 5500), coarse(64000, 6000),
    fine(8000, 1000),    fine(10000, 1000),   fine(10000, 1250),   fine(12000, 1250),   fine(12000, 1500),
    fine(14000, 1500),   fine(16000, 1500),   fine(18000, 1500),   fine(18000, 2000),   fine(20000, 1500),
    fine(20000, 2000),   fine(22000, 1500),   fine(22000, 2000),   fine(24000, 2000),   fine(27000, 2000),
    fine(30000, 2000),   fine(33000, 2000),   fine(36000, 3000),   fine(39000, 3000),   fine(42000, 3000),
    fine(45000, 3000),   fine(48000, 3000),   fine(52000, 4000),   fine(56000, 4000),   fine(60000, 4000),
    fine(64000, 4000),
}};

/// By pitch: the fundamental deviation EI of position G (ISO 965-1, Table 1)
/// and the minor diameter tolerance TD1 (Table 2) of the grade ISO 965-2
/// gives the sizes with the pitch: grade 5 for 0.25 and 0.3 mm (M1 to M1.4
/// only), grade 6 for the others. Micrometres.
struct PitchData {
    std::uint32_t pitchUm;
    std::int32_t positionG;
    std::int32_t minorTolerance;
};

constexpr std::array<PitchData, 22> kPitches{{
    {250, 18, 56},   {300, 18, 67},   {350, 19, 100},  {400, 19, 112},  {450, 20, 125},  {500, 20, 140},
    {600, 21, 160},  {700, 22, 180},  {800, 24, 200},  {1000, 26, 236}, {1250, 28, 265}, {1500, 32, 300},
    {1750, 34, 335}, {2000, 38, 375}, {2500, 42, 450}, {3000, 48, 500}, {3500, 53, 560}, {4000, 60, 600},
    {4500, 63, 670}, {5000, 71, 710}, {5500, 75, 750}, {6000, 80, 800},
}};

/// The pitch diameter tolerance TD2 (ISO 965-1, Table 4) by basic major
/// diameter range (above the previous limit, up to and including this one)
/// and pitch, for the grade ISO 965-2 gives the sizes: grade 5 up to 1.4 mm,
/// grade 6 above. Micrometres. The values up to 2.8 mm are those of the
/// published extract of Table 4; the others are the differences of the
/// limits ISO 965-2 lists (see the evidence).
struct PitchDiameterTolerance {
    std::uint32_t rangeLimitUm;
    std::uint32_t pitchUm;
    std::int32_t tolerance;
};

constexpr std::array<PitchDiameterTolerance, 27> kPitchDiameterTolerances{{
    {1400, 250, 56},    {1400, 300, 60},    {2800, 350, 85},    {2800, 400, 90},    {2800, 450, 95},
    {5600, 500, 100},   {5600, 600, 112},   {5600, 700, 118},   {5600, 800, 125},   {11200, 1000, 150},
    {11200, 1250, 160}, {11200, 1500, 180}, {22400, 1250, 180}, {22400, 1500, 190}, {22400, 1750, 200},
    {22400, 2000, 212}, {22400, 2500, 224}, {45000, 2000, 224}, {45000, 3000, 265}, {45000, 3500, 280},
    {45000, 4000, 300}, {45000, 4500, 315}, {90000, 3000, 280}, {90000, 4000, 315}, {90000, 5000, 335},
    {90000, 5500, 355}, {90000, 6000, 375},
}};

/// The limits of the diameter ranges of Table 4, in micrometres; the first
/// range starts above 0.99 mm.
constexpr std::array<std::uint32_t, 7> kRangeLimitsUm{1400, 2800, 5600, 11200, 22400, 45000, 90000};

// ISO 965-2 gives grade 5 to sizes up to and including this diameter.
constexpr std::uint32_t kFineGradeLimitUm = 1400;

/// "8", "1.25": micrometres as millimetres without trailing zeros.
std::string millimetres(std::uint32_t micrometres) {
    std::string text = std::format("{}.{:03}", micrometres / 1000, micrometres % 1000);
    while (text.back() == '0') {
        text.pop_back();
    }
    if (text.back() == '.') {
        text.pop_back();
    }
    return text;
}

Length fromMicrometres(std::int64_t micrometres) {
    return Length::fromSi(static_cast<double>(micrometres) / 1e6);
}

const PitchData& pitchData(const MetricThread& thread) {
    // Every size's pitch is in the table (checked by the tests).
    return *std::ranges::find(kPitches, Table::pitchUm(thread), &PitchData::pitchUm);
}

std::int32_t pitchDiameterTolerance(const MetricThread& thread) {
    const std::uint32_t diameter = Table::diameterUm(thread);
    const std::uint32_t range = *std::ranges::find_if(kRangeLimitsUm, [&](std::uint32_t limit) {
        return diameter <= limit;
    });
    // Every size's range and pitch are in the table (checked by the tests).
    return std::ranges::find_if(kPitchDiameterTolerances, [&](const PitchDiameterTolerance& entry) {
               return entry.rangeLimitUm == range && entry.pitchUm == Table::pitchUm(thread);
           })->tolerance;
}

std::optional<MetricThread> find(std::uint32_t diameterUm, std::optional<std::uint32_t> pitchUm) {
    for (const MetricThread& thread : kSizes) {
        if (Table::diameterUm(thread) == diameterUm &&
            (pitchUm ? Table::pitchUm(thread) == *pitchUm : thread.coarse())) {
            return thread;
        }
    }
    return std::nullopt;
}

} // namespace

Length MetricThread::diameter() const noexcept {
    return fromMicrometres(diameterUm_);
}

Length MetricThread::pitch() const noexcept {
    return fromMicrometres(pitchUm_);
}

std::string MetricThread::designation() const {
    if (coarse_) {
        return "M" + millimetres(diameterUm_);
    }
    return std::format("M{}x{}", millimetres(diameterUm_), millimetres(pitchUm_));
}

std::span<const MetricThread> metricThreads() noexcept {
    return kSizes;
}

Result<MetricThread> parseMetricThread(std::string_view text) {
    const auto invalid = [&](std::string_view why) {
        return makeError(ErrorCode::InvalidArgument, std::format("'{}' is not a metric thread size: {}", text, why));
    };
    if (text.empty() || text.front() != 'M') {
        return invalid("a designation starts with M, e.g. M8 or M8x1");
    }
    std::string_view rest = text.substr(1);
    std::optional<std::string_view> pitchText;
    // "x", or the multiplication sign of ISO 965-1.
    constexpr std::string_view kTimes = "\xC3\x97";
    if (const std::size_t x = rest.find('x'); x != std::string_view::npos) {
        pitchText = rest.substr(x + 1);
        rest = rest.substr(0, x);
    } else if (const std::size_t times = rest.find(kTimes); times != std::string_view::npos) {
        pitchText = rest.substr(times + kTimes.size());
        rest = rest.substr(0, times);
    }
    const auto diameter = detail::parseMillimetresInMicrometres(rest);
    if (!diameter) {
        return invalid("the diameter must be a number of millimetres, e.g. 8 or 1.6");
    }
    std::optional<std::uint32_t> pitch;
    if (pitchText) {
        pitch = detail::parseMillimetresInMicrometres(*pitchText);
        if (!pitch) {
            return invalid("the pitch must be a number of millimetres, e.g. 1 or 1.25");
        }
    }
    const auto thread = find(*diameter, pitch);
    if (!thread) {
        return invalid("BetterCAD knows the sizes of ISO 965-2 (coarse M1 to M64, fine M8x1 to M64x4)");
    }
    return *thread;
}

ThreadDiameters basicDiameters(const MetricThread& thread) noexcept {
    const double d = thread.diameter().si();
    const double h = std::numbers::sqrt3 / 2.0 * thread.pitch().si();
    return {
        .major = thread.diameter(),
        .pitch = Length::fromSi(d - 0.75 * h),
        .minor = Length::fromSi(d - 1.25 * h),
    };
}

std::string toString(const ThreadToleranceClass& tolerance) {
    return std::format("{}{}", tolerance.grade, tolerance.position == ThreadPosition::G ? 'G' : 'H');
}

Result<ThreadToleranceClass> parseThreadToleranceClass(std::string_view text) {
    if (text.size() != 2 || text[0] < '4' || text[0] > '8' || (text[1] != 'G' && text[1] != 'H')) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("'{}' is not an internal thread tolerance class: a grade from 4 to 8 and the "
                                     "position G or H, e.g. 6H",
                                     text));
    }
    return ThreadToleranceClass{.grade = text[0] - '0',
                                .position = text[1] == 'G' ? ThreadPosition::G : ThreadPosition::H};
}

ThreadToleranceClass standardToleranceClass(const MetricThread& thread) noexcept {
    return {.grade = Table::diameterUm(thread) <= kFineGradeLimitUm ? 5 : 6, .position = ThreadPosition::H};
}

std::string designation(const MetricThread& thread, const ThreadToleranceClass& tolerance) {
    return std::format("{}-{}", thread.designation(), toString(tolerance));
}

Result<InternalThreadLimits> internalThreadLimits(const MetricThread& thread, const ThreadToleranceClass& tolerance) {
    const int grade = standardToleranceClass(thread).grade;
    if (tolerance.grade != grade) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("BetterCAD knows the {} thread tolerances of grade {} only (the grade of "
                                     "ISO 965-2), not {}",
                                     thread.designation(), grade, toString(tolerance)));
    }
    const PitchData& data = pitchData(thread);
    const std::int32_t deviation = tolerance.position == ThreadPosition::G ? data.positionG : 0;
    const ThreadDiameters basic = basicDiameters(thread);
    const Length ei = fromMicrometres(deviation);
    return InternalThreadLimits{
        .pitchDiameter = {.min = basic.pitch + ei,
                          .max = basic.pitch + fromMicrometres(deviation + pitchDiameterTolerance(thread))},
        .minorDiameter = {.min = basic.minor + ei,
                          .max = basic.minor + fromMicrometres(deviation + data.minorTolerance)},
        .minimumMajorDiameter = basic.major + ei,
    };
}

} // namespace bettercad::standards
