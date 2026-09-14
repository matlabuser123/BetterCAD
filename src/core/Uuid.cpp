#include <bettercad/core/Uuid.hpp>

#include <random>

namespace bettercad {

namespace {

constexpr std::string_view kHexDigits = "0123456789abcdef";

// Byte offsets of the dashes in the canonical form.
constexpr bool isDashPosition(std::size_t position) noexcept {
    return position == 8 || position == 13 || position == 18 || position == 23;
}

constexpr int hexValue(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

} // namespace

Uuid Uuid::generateV4() {
    std::random_device device;
    return generateV4(device);
}

std::optional<Uuid> Uuid::parse(std::string_view text) noexcept {
    constexpr std::size_t kCanonicalLength = 36;
    if (text.size() != kCanonicalLength) {
        return std::nullopt;
    }

    Bytes bytes{};
    std::size_t byteIndex = 0;
    for (std::size_t i = 0; i < text.size();) {
        if (isDashPosition(i)) {
            if (text[i] != '-') {
                return std::nullopt;
            }
            ++i;
            continue;
        }
        const int high = hexValue(text[i]);
        const int low = hexValue(text[i + 1]);
        if (high < 0 || low < 0 || isDashPosition(i + 1)) {
            return std::nullopt;
        }
        bytes[byteIndex++] = static_cast<std::uint8_t>((high << 4) | low);
        i += 2;
    }
    return Uuid{bytes};
}

std::string Uuid::toString() const {
    std::string text;
    text.reserve(36);
    for (std::size_t i = 0; i < bytes_.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            text += '-';
        }
        text += kHexDigits[bytes_[i] >> 4];
        text += kHexDigits[bytes_[i] & 0x0Fu];
    }
    return text;
}

} // namespace bettercad
