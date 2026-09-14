#include <bettercad/io/DocumentFile.hpp>

#include "io/FileIo.hpp"

#include <format>

namespace bettercad::io {

Result<void> saveDocument(const Document& document, const std::filesystem::path& path) {
    auto json = documentToJson(document);
    if (!json) {
        return std::unexpected(json.error());
    }
    return detail::writeFileAtomically(path, *json);
}

Result<Document> loadDocument(const std::filesystem::path& path) {
    auto text = detail::readFile(path);
    if (!text) {
        return std::unexpected(text.error());
    }
    auto document = documentFromJson(*text);
    if (!document) {
        return makeError(document.error().code,
                         std::format("{}: {}", detail::displayPath(path.filename()), document.error().message));
    }
    return document;
}

} // namespace bettercad::io
