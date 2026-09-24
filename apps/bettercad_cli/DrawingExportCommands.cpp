#include "Commands.hpp"
#include "Selectors.hpp"

#include <bettercad/assembly/Resolution.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/ParameterExpressions.hpp>
#include <bettercad/drawing/Regeneration.hpp>
#include <bettercad/drawing/SheetScene.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/io/DrawingExport.hpp>

#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

// Writing a drawing out from a command line (P14-EXPORT-001).
//
// One command per format, and one path through all three: load, regenerate,
// assemble the sheet's scene, hand it to the writer. The CLI chooses the
// writer and nothing else -- it builds no geometry, transcribes nothing and
// knows nothing about PDF, SVG or DXF beyond which function to call.
//
// THE EXIT CODE SAYS WHETHER A DRAWING WAS WRITTEN, not whether the process
// survived. A sheet that does not regenerate, a reference that does not
// resolve or a path that cannot be written are each a failure with a
// diagnostic, and no file is left behind.
namespace bettercad::cli {
namespace {

/// A loaded, regenerated document and the lookups a scene needs from it.
struct Regenerated {
    std::unique_ptr<Document> document;
    features::Regenerator regenerator;
    features::RegenerationReport report;
};

Result<std::unique_ptr<Regenerated>> openAndRegenerate(const std::filesystem::path& path,
                                                       std::optional<std::string_view> configuration) {
    auto file = io::loadDocument(path);
    if (!file) {
        return std::unexpected(file.error());
    }
    auto loaded = std::make_unique<Regenerated>();
    loaded->document = std::make_unique<Document>(std::move(*file));
    if (configuration) {
        auto id = resolveConfiguration(*loaded->document, *configuration);
        if (!id) {
            return std::unexpected(id.error());
        }
        if (auto set = loaded->document->setActiveConfiguration(*id); !set) {
            return std::unexpected(set.error());
        }
        (void)evaluateParameterExpressions(*loaded->document);
    }
    assembly::registerHandlers(loaded->regenerator, nullptr, nullptr);
    drawing::registerHandlers(loaded->regenerator);
    auto report = loaded->regenerator.regenerateAll(*loaded->document);
    if (!report) {
        return std::unexpected(report.error());
    }
    loaded->report = std::move(*report);
    return loaded;
}

struct ExportArguments {
    std::filesystem::path document;
    std::filesystem::path output;
    std::optional<std::string_view> sheet;
    std::optional<std::string_view> configuration;
};

Result<ExportArguments> parseExportArguments(Args args) {
    auto parsed = parseArguments(args, {{"--sheet", true}, {"--configuration", true}});
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    if (parsed->positional().size() != 2) {
        return makeError(ErrorCode::InvalidArgument,
                         "expected a document to read and a file to write");
    }
    ExportArguments result{.document = pathFromArgument(parsed->positional()[0]),
                           .output = pathFromArgument(parsed->positional()[1]),
                           .sheet = std::nullopt,
                           .configuration = std::nullopt};
    if (const auto sheet = parsed->value("--sheet")) {
        result.sheet = *sheet;
    }
    if (const auto configuration = parsed->value("--configuration")) {
        result.configuration = *configuration;
    }
    return result;
}

/// Which sheet to draw: the one named, or the only one there is.
///
/// A document with several sheets and no `--sheet` is refused rather than
/// guessed at: exporting the wrong page silently is worse than being asked
/// which.
Result<SheetId> chooseSheet(const Document& document, std::optional<std::string_view> named) {
    if (named) {
        auto object = resolveObject(document, *named);
        if (!object) {
            return std::unexpected(object.error());
        }
        const SheetId id = SheetId::fromValue(object->value());
        if (drawing::findSheet(document, id) == nullptr) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("{} is not a sheet", label(document, *object)));
        }
        return id;
    }
    const std::vector<SheetId> all = drawing::sheets(document);
    if (all.empty()) {
        return makeError(ErrorCode::FailedPrecondition,
                         "this document has no sheets, so there is no drawing to write");
    }
    if (all.size() > 1) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("this document has {} sheets; say which with --sheet",
                                     all.size()));
    }
    return all.front();
}

/// The one path all three commands take.
using Writer = Result<void> (*)(const drawing::DrawingScene&, const std::filesystem::path&);

ExitCode runDrawingExport(std::string_view command, std::string_view usage, std::string_view format,
                          Writer write, Args args, std::ostream& out, std::ostream& err) {
    auto parsed = parseExportArguments(args);
    if (!parsed) {
        return usageError(command, usage, parsed.error().message, err);
    }
    auto loaded = openAndRegenerate(parsed->document, parsed->configuration);
    if (!loaded) {
        return failure(command, loaded.error().message, err);
    }
    // A drawing that does not regenerate is not written. Exporting a sheet
    // whose references have stopped resolving would be publishing a drawing
    // nobody can trust.
    if (!(*loaded)->report.succeeded()) {
        return failure(command,
                       std::format("the drawing does not regenerate: {} failed, {} blocked",
                                   (*loaded)->report.failed.size(), (*loaded)->report.blocked.size()),
                       err);
    }
    auto sheet = chooseSheet(*(*loaded)->document, parsed->sheet);
    if (!sheet) {
        return sheet.error().code == ErrorCode::InvalidArgument
                   ? usageError(command, usage, sheet.error().message, err)
                   : failure(command, sheet.error().message, err);
    }
    const drawing::BodyLookup bodies = [&](ObjectId object) {
        return (*loaded)->regenerator.body(object);
    };
    const drawing::TransformLookup transforms = [&](ComponentId component) {
        return (*loaded)->regenerator.transform(component);
    };
    auto scene = drawing::sheetScene(*(*loaded)->document, *sheet, bodies, transforms);
    if (!scene) {
        return failure(command, scene.error().message, err);
    }
    if (auto written = write(*scene, parsed->output); !written) {
        return failure(command, written.error().message, err);
    }
    const std::size_t drawn =
        scene->items.lines.size() + scene->items.arcs.size() + scene->items.texts.size();
    out << std::format("Wrote {}: {} of {}, {:.3g} x {:.3g} mm, {}\n", displayPath(parsed->output),
                       format, label(*(*loaded)->document, ObjectId{*sheet}),
                       scene->width.in(units::mm), scene->height.in(units::mm),
                       plural(drawn, "item", "items"));
    return ExitCode::Success;
}

} // namespace

ExitCode runExportPdf(Args args, std::ostream& out, std::ostream& err) {
    return runDrawingExport("export-pdf", kExportPdfUsage, "PDF", &io::exportPdf, args, out, err);
}

ExitCode runExportSvg(Args args, std::ostream& out, std::ostream& err) {
    return runDrawingExport("export-svg", kExportSvgUsage, "SVG", &io::exportSvg, args, out, err);
}

ExitCode runExportDxf(Args args, std::ostream& out, std::ostream& err) {
    return runDrawingExport("export-dxf", kExportDxfUsage, "DXF (R12)", &io::exportDxf, args, out, err);
}

} // namespace bettercad::cli
