#pragma once

#include <bettercad/core/Error.hpp>

#include <filesystem>
#include <string>
#include <string_view>

// File access shared by every writer and reader in bettercad_io.
namespace bettercad::io::detail {

/// UTF-8 form of a path, for messages (path::string() may use a narrow code
/// page on Windows).
[[nodiscard]] std::string displayPath(const std::filesystem::path& path);

/// Writes @p bytes to a sibling temporary file and renames it over @p path,
/// so a failure never leaves a truncated file behind. Fails with IoError.
[[nodiscard]] Result<void> writeFileAtomically(const std::filesystem::path& path, std::string_view bytes);

/// Whole file contents. Fails with NotFound if @p path is not an existing
/// regular file and IoError if it cannot be read.
[[nodiscard]] Result<std::string> readFile(const std::filesystem::path& path);

} // namespace bettercad::io::detail
