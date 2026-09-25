#include "Commands.hpp"
#include "Selectors.hpp"

#include <bettercad/assembly/Resolution.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/ParameterExpressions.hpp>
#include <bettercad/drawing/Annotations.hpp>
#include <bettercad/drawing/Bom.hpp>
#include <bettercad/drawing/Dimensions.hpp>
#include <bettercad/drawing/Regeneration.hpp>
#include <bettercad/drawing/Resolution.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/drawing/Views.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>

#include <cstddef>
#include <format>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

// Reporting a drawing from a command line (P14-CLI-001).
//
// It reports and never writes. Everything it prints is DERIVED -- a measured
// dimension, a BOM row, a resolution state -- and derived state is recomputed
// from the document on every call, never stored (ADR-011, ADR-014). So this
// command has nothing to save, and the exit code says whether the drawing is
// sound rather than whether a file was written.
//
// It computes none of it. The numbers come from drawing::measure(), the rows
// from drawing::billOfMaterials(), the states from drawing::*Resolution(), and
// this file turns them into lines. A CLI that worked out a quantity would be a
// second answer to what the assembly already says.
namespace bettercad::cli {
namespace {

struct Loaded {
    std::unique_ptr<Document> document;
    features::Regenerator regenerator;
    features::RegenerationReport report;
};

Result<std::unique_ptr<Loaded>> openAndRegenerate(const std::filesystem::path& path,
                                                  std::optional<std::string_view> configuration) {
    auto file = io::loadDocument(path);
    if (!file) {
        return std::unexpected(file.error());
    }
    auto loaded = std::make_unique<Loaded>();
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
    // BOTH modules' handlers: an assembly drawing resolves through the
    // components, and the drawing objects have to be validated rather than
    // passed over (P14-REGEN-001).
    assembly::registerHandlers(loaded->regenerator, nullptr, nullptr);
    drawing::registerHandlers(loaded->regenerator);
    auto report = loaded->regenerator.regenerateAll(*loaded->document);
    if (!report) {
        return std::unexpected(report.error());
    }
    loaded->report = std::move(*report);
    return loaded;
}

struct ReportArguments {
    std::filesystem::path path;
    std::optional<std::string_view> configuration;
};

Result<ReportArguments> parseReportArguments(Args args) {
    auto parsed = parseArguments(args, {{"--configuration", true}});
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    if (parsed->positional().size() != 1) {
        return makeError(ErrorCode::InvalidArgument, "expected exactly one document path");
    }
    ReportArguments result{.path = pathFromArgument(parsed->positional().front()),
                            .configuration = std::nullopt};
    if (const auto configuration = parsed->value("--configuration")) {
        result.configuration = *configuration;
    }
    return result;
}

/// The state of one reference, in the three words P14-STREF-001 defined.
std::string_view stateWord(const drawing::Resolution& resolution) {
    return drawing::toString(resolution.state);
}

/// @p text padded to @p width, and ALWAYS followed by at least one space.
///
/// `{:<28}` pads to 28 and stops, so a label of exactly 28 characters runs
/// straight into the next column and a longer one swallows it --
/// `ClearanceCallout (object:21)hole_callout`, which P14-REFMOD-001 produced
/// the moment a reference model used a name of a realistic length. A report
/// whose columns can merge is a report that cannot be read, so the separator
/// is guaranteed here rather than assumed to fall out of the width.
std::string column(std::string text, std::size_t width) {
    text.append(text.size() >= width ? 1 : width - text.size(), ' ');
    return text;
}

} // namespace

ExitCode runDrawing(Args args, std::ostream& out, std::ostream& err) {
    auto parsed = parseReportArguments(args);
    if (!parsed) {
        return usageError("drawing", kDrawingUsage, parsed.error().message, err);
    }
    auto loaded = openAndRegenerate(parsed->path, parsed->configuration);
    if (!loaded) {
        return failure("drawing", loaded.error().message, err);
    }
    const Document& document = *(*loaded)->document;
    const drawing::BodyLookup bodies = [&](ObjectId object) {
        return (*loaded)->regenerator.body(object);
    };
    const drawing::TransformLookup transforms = [&](ComponentId component) {
        return (*loaded)->regenerator.transform(component);
    };

    out << std::format("Drawing of {}\n", document.name());
    if (const auto active = document.activeConfiguration()) {
        const Configuration* configuration = document.configurations().find(*active);
        out << std::format("Configuration: {}\n",
                           configuration == nullptr ? "unknown" : configuration->name());
    } else {
        out << "Configuration: none (base values)\n";
    }

    const std::vector<SheetId> allSheets = drawing::sheets(document);
    out << std::format("Sheets ({}):\n", allSheets.size());
    for (const SheetId id : allSheets) {
        const drawing::SheetDefinition& d = drawing::findSheet(document, id)->definition();
        out << std::format("  {}{} {}, scale {}\n", column(label(document, ObjectId{id}), 28),
                           drawing::toString(d.format), drawing::toString(d.orientation),
                           d.scale.label());
    }

    const std::vector<ViewId> allViews = drawing::views(document);
    out << std::format("Views ({}):\n", allViews.size());
    for (const ViewId id : allViews) {
        const drawing::ViewDefinition& d = drawing::findView(document, id)->definition();
        const drawing::Resolution state = drawing::viewResolution(document, id, bodies, transforms);
        std::string scale{"sheet"};
        if (const auto effective = drawing::effectiveScale(document, id)) {
            scale = effective->label();
        }
        out << std::format("  {}{}on {}scale {}{}\n", column(label(document, ObjectId{id}), 28),
                           column(std::string{drawing::toString(d.kind)}, 10),
                           column(label(document, ObjectId{d.sheet}), 20), column(scale, 8),
                           stateWord(state));
        if (!state.resolved()) {
            out << std::format("      {}\n", state.diagnostic);
        }
    }

    const std::vector<DimensionId> allDimensions = drawing::dimensions(document);
    out << std::format("Dimensions ({}):\n", allDimensions.size());
    for (const DimensionId id : allDimensions) {
        const drawing::DimensionDefinition& d = drawing::findDimension(document, id)->definition();
        // The VALUE comes from measure(), which resolves against the model as
        // it is now. Nothing here computes it.
        auto measured = drawing::measure(document, id, bodies, transforms);
        const std::string value = measured ? measured->text : std::string{"--"};
        out << std::format("  {}{}on {}{}\n", column(label(document, ObjectId{id}), 28),
                           column(std::string{drawing::toString(d.type)}, 12),
                           column(label(document, ObjectId{d.view}), 20), value);
        if (!measured) {
            out << std::format("      {}\n", measured.error().message);
        }
    }

    const std::vector<AnnotationId> allAnnotations = drawing::annotations(document);
    out << std::format("Annotations ({}):\n", allAnnotations.size());
    for (const AnnotationId id : allAnnotations) {
        const drawing::AnnotationDefinition& d = drawing::findAnnotation(document, id)->definition();
        const drawing::Resolution state =
            drawing::annotationResolution(document, id, bodies, transforms);
        out << std::format("  {}{}on {}{}\n", column(label(document, ObjectId{id}), 28),
                           column(std::string{drawing::toString(d.type)}, 22),
                           column(label(document, ObjectId{d.view}), 20), stateWord(state));
        if (!state.resolved()) {
            out << std::format("      {}\n", state.diagnostic);
        }
    }

    // One bill of materials per assembly view, computed from the active
    // occurrence set on every call (ADR-022). A quantity is not stored and is
    // not worked out here.
    for (const ViewId id : allViews) {
        const auto subject = drawing::effectiveSubject(document, id);
        if (!subject || *subject != drawing::ViewSubject::Assembly) {
            continue;
        }
        auto bom = drawing::billOfMaterials(document, id);
        if (!bom) {
            out << std::format("Bill of materials for {}: {}\n", label(document, ObjectId{id}),
                               bom.error().message);
            continue;
        }
        out << std::format("Bill of materials for {} ({}):\n", label(document, ObjectId{id}),
                           plural(bom->totalOccurrences(), "occurrence", "occurrences"));
        for (const drawing::BomRow& row : bom->rows) {
            out << std::format("  {:>4}  {}x{}\n", row.item, column(row.name, 28), row.quantity());
        }
    }

    out << std::format("Regeneration: {}, {} regenerated\n",
                       (*loaded)->report.succeeded() ? "ok" : "FAILED",
                       plural((*loaded)->report.regenerated.size(), "object", "objects"));
    for (const ObjectId id : (*loaded)->report.failed) {
        const Error* error = (*loaded)->regenerator.error(id);
        out << std::format("  failed   {}: {}\n", label(document, id),
                           error == nullptr ? "no reason recorded" : error->message);
    }
    for (const ObjectId id : (*loaded)->report.blocked) {
        out << std::format("  blocked  {}: something it depends on did not build\n",
                           label(document, id));
    }

    // The exit code says whether the DRAWING is sound, not whether the command
    // ran. A sheet that cannot be drawn is a failure even though nothing threw.
    return (*loaded)->report.succeeded() ? ExitCode::Success : ExitCode::Failure;
}

} // namespace bettercad::cli
