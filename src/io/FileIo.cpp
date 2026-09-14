#include "io/FileIo.hpp"

#include <format>
#include <fstream>
#include <iterator>
#include <system_error>

namespace bettercad::io::detail {

std::string displayPath(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

Result<void> writeFileAtomically(const std::filesystem::path& path, std::string_view bytes) {
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            return makeError(ErrorCode::IoError, std::format("cannot write '{}'", displayPath(temporary)));
        }
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return makeError(ErrorCode::IoError, std::format("writing '{}' failed", displayPath(temporary)));
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return makeError(ErrorCode::IoError,
                         std::format("cannot replace '{}': {}", displayPath(path), error.message()));
    }
    return {};
}

Result<std::string> readFile(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return makeError(ErrorCode::NotFound,
                         std::format("'{}' does not exist or is not a file", displayPath(path)));
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return makeError(ErrorCode::IoError, std::format("cannot open '{}'", displayPath(path)));
    }
    std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (in.bad()) {
        return makeError(ErrorCode::IoError, std::format("reading '{}' failed", displayPath(path)));
    }
    return text;
}

} // namespace bettercad::io::detail
