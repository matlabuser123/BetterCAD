#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Export.hpp>
#include <bettercad/core/units/Units.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace bettercad::standards {

/// An ISO general purpose metric screw thread (M, ISO 68-1) by its nominal
/// (basic major) diameter and pitch. A value is always one of the sizes that
/// ISO 965-2 gives limits of size for, all of them ISO 262 selected sizes:
/// - the coarse series, M1 to M64;
/// - the fine series, M8x1 to M64x4.
/// Get one from metricThreads() or parseMetricThread().
class BETTERCAD_CORE_EXPORT MetricThread {
public:
    [[nodiscard]] Length diameter() const noexcept;
    [[nodiscard]] Length pitch() const noexcept;
    /// Whether the pitch is the coarse pitch of the diameter (ISO 262).
    [[nodiscard]] bool coarse() const noexcept { return coarse_; }
    /// "M8" for a coarse pitch, which ISO 965-1 lets a designation omit;
    /// "M8x1" for a fine one.
    [[nodiscard]] std::string designation() const;

    friend bool operator==(const MetricThread&, const MetricThread&) = default;

private:
    friend struct MetricThreadTable;

    constexpr MetricThread(std::uint32_t diameterUm, std::uint32_t pitchUm, bool coarse) noexcept
        : diameterUm_(diameterUm), pitchUm_(pitchUm), coarse_(coarse) {}

    std::uint32_t diameterUm_;
    std::uint32_t pitchUm_;
    bool coarse_;
};

/// Every known size: the coarse series by diameter, then the fine series by
/// diameter and pitch.
[[nodiscard]] BETTERCAD_CORE_EXPORT std::span<const MetricThread> metricThreads() noexcept;

/// Parses a designation: "M", the nominal diameter in mm and, optionally,
/// "x" and the pitch in mm ("M8", "M8x1.25", "M1.6"). Without a pitch the
/// size is the coarse one. Fails with InvalidArgument for malformed text or
/// a size that is not known, naming the text.
[[nodiscard]] BETTERCAD_CORE_EXPORT Result<MetricThread> parseMetricThread(std::string_view text);

/// The basic diameters of a thread's profile (ISO 68-1): with the height of
/// the fundamental triangle H = sqrt(3)/2 P, the pitch diameter is
/// D2 = D - 3/4 H and the minor diameter D1 = D - 5/4 H.
struct ThreadDiameters {
    Length major{};
    Length pitch{};
    Length minor{};

    friend constexpr bool operator==(const ThreadDiameters&, const ThreadDiameters&) = default;
};

[[nodiscard]] BETTERCAD_CORE_EXPORT ThreadDiameters basicDiameters(const MetricThread& thread) noexcept;

/// The tolerance position of an internal thread (ISO 965-1): G has a positive
/// fundamental deviation EI, H has none.
enum class ThreadPosition {
    G,
    H,
};

/// A tolerance class of an internal thread (ISO 965-1): a tolerance grade
/// from 4 to 8 and a position, the same for the pitch and the minor diameter
/// ("6H"). Classes that give the two diameters different grades ("5H6H")
/// are not represented.
struct ThreadToleranceClass {
    int grade = 6;
    ThreadPosition position = ThreadPosition::H;

    friend constexpr bool operator==(const ThreadToleranceClass&, const ThreadToleranceClass&) = default;
};

/// "6H", "5G".
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string toString(const ThreadToleranceClass& tolerance);

/// Parses "6H" or "6G": a grade from 4 to 8 and the position. Fails with
/// InvalidArgument for anything else, naming the text.
[[nodiscard]] BETTERCAD_CORE_EXPORT Result<ThreadToleranceClass> parseThreadToleranceClass(std::string_view text);

/// The class ISO 965-2 gives an internal thread of the size: 5H up to and
/// including M1.4, 6H above.
[[nodiscard]] BETTERCAD_CORE_EXPORT ThreadToleranceClass standardToleranceClass(const MetricThread& thread) noexcept;

/// "M8-6H", "M8x1-6G": a designation with its tolerance class (ISO 965-1).
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string designation(const MetricThread& thread,
                                                            const ThreadToleranceClass& tolerance);

/// Lower and upper limits of a size.
struct SizeLimits {
    Length min{};
    Length max{};

    friend constexpr bool operator==(const SizeLimits&, const SizeLimits&) = default;
};

/// The limits of size of an internal thread. ISO 965-1 specifies no upper
/// limit for the major diameter.
struct InternalThreadLimits {
    SizeLimits pitchDiameter{};
    SizeLimits minorDiameter{};
    Length minimumMajorDiameter{};

    friend constexpr bool operator==(const InternalThreadLimits&, const InternalThreadLimits&) = default;
};

/// The limits of size of @p thread, an internal thread, in @p tolerance
/// (ISO 965-1): each lower limit is the basic diameter plus the fundamental
/// deviation EI (zero for H; Table 1 for G), and the upper limits of the
/// pitch and minor diameters add the tolerances TD2 (Table 4) and TD1
/// (Table 2) of the grade. The limits are not rounded; ISO 965-2 lists the
/// same limits for 5H and 6H rounded to 0.001 mm.
///
/// BetterCAD knows the tolerances of the grade ISO 965-2 gives the size (see
/// standardToleranceClass()) only, in either position. Another grade fails
/// with InvalidArgument.
[[nodiscard]] BETTERCAD_CORE_EXPORT Result<InternalThreadLimits>
internalThreadLimits(const MetricThread& thread, const ThreadToleranceClass& tolerance);

} // namespace bettercad::standards
