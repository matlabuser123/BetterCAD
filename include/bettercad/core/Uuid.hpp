#pragma once

#include <bettercad/core/Export.hpp>

#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <string_view>

namespace bettercad {

/// 128-bit universally unique identifier (RFC 9562), used where identity must
/// be unique across documents and machines.
class Uuid {
public:
    using Bytes = std::array<std::uint8_t, 16>;

    /// The nil UUID (all zero).
    constexpr Uuid() noexcept = default;
    constexpr explicit Uuid(const Bytes& bytes) noexcept : bytes_(bytes) {}

    /// Random version-4 UUID from the operating system's entropy source.
    [[nodiscard]] BETTERCAD_CORE_EXPORT static Uuid generateV4();

    /// Version-4 UUID drawn from @p rng. Uses raw generator output (never a
    /// distribution), so a seeded standard engine gives identical UUIDs on
    /// every platform, which makes tests reproducible.
    template <std::uniform_random_bit_generator Generator>
    [[nodiscard]] static Uuid generateV4(Generator& rng);

    /// Parses the canonical 8-4-4-4-12 hexadecimal form (either case).
    [[nodiscard]] BETTERCAD_CORE_EXPORT static std::optional<Uuid> parse(std::string_view text) noexcept;

    /// Canonical lowercase 8-4-4-4-12 form.
    [[nodiscard]] BETTERCAD_CORE_EXPORT std::string toString() const;

    [[nodiscard]] constexpr const Bytes& bytes() const noexcept { return bytes_; }
    [[nodiscard]] constexpr bool isNil() const noexcept { return bytes_ == Bytes{}; }
    [[nodiscard]] constexpr int version() const noexcept { return bytes_[6] >> 4; }
    /// True for the RFC 9562 variant (bits 10xx in byte 8).
    [[nodiscard]] constexpr bool isRfcVariant() const noexcept { return (bytes_[8] & 0xC0) == 0x80; }

    friend constexpr bool operator==(const Uuid&, const Uuid&) = default;
    friend constexpr auto operator<=>(const Uuid&, const Uuid&) = default;

private:
    Bytes bytes_{};
};

template <std::uniform_random_bit_generator Generator>
Uuid Uuid::generateV4(Generator& rng) {
    using Result = typename Generator::result_type;
    static_assert(Generator::min() == 0 && Generator::max() == std::numeric_limits<Result>::max(),
                  "generator must produce uniformly distributed full-width values");

    Bytes bytes{};
    std::size_t filled = 0;
    while (filled < bytes.size()) {
        Result bits = rng();
        for (std::size_t i = 0; i < sizeof(Result) && filled < bytes.size(); ++i) {
            bytes[filled++] = static_cast<std::uint8_t>(bits & 0xFFu);
            bits = static_cast<Result>(bits >> 8);
        }
    }
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0Fu) | 0x40u); // version 4
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3Fu) | 0x80u); // RFC variant
    return Uuid{bytes};
}

} // namespace bettercad

template <>
struct std::hash<bettercad::Uuid> {
    std::size_t operator()(const bettercad::Uuid& uuid) const noexcept {
        std::uint64_t digest = 14695981039346656037ull; // FNV-1a
        for (const std::uint8_t byte : uuid.bytes()) {
            digest = (digest ^ byte) * 1099511628211ull;
        }
        return static_cast<std::size_t>(digest);
    }
};

template <>
struct std::formatter<bettercad::Uuid> : std::formatter<std::string_view> {
    template <typename FormatContext>
    auto format(const bettercad::Uuid& uuid, FormatContext& ctx) const {
        return std::formatter<std::string_view>::format(uuid.toString(), ctx);
    }
};
