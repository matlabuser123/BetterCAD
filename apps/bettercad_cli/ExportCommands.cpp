#include "Commands.hpp"

#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/io/ModelExport.hpp>

#include <format>
#include <ostream>
#include <string>

namespace bettercad::cli {

namespace {

std::string listBodies(const io::ExportSummary& summary, bool withTriangles) {
    std::string text;
    for (const io::ExportedBody& body : summary.bodies) {
        text += text.empty() ? "" : ", ";
        text += withTriangles ? std::format("{}: {} triangles", body.name, body.triangles) : body.name;
    }
    return text;
}

std::string bodyCount(std::size_t count) {
    return std::format("{} {}", count, count == 1 ? "body" : "bodies");
}

/// What was written, said in the terms of the thing that was written.
///
/// An assembly's summary lists ONE body however many components place it,
/// because the part's geometry is written once. Reporting only that would
/// tell an engineer who exported a twelve-bracket frame that one body went
/// out, so the instances are named too (P13-STEP-001).
std::string describeContents(const io::ExportSummary& summary) {
    if (summary.components.empty()) {
        return std::format("{} ({})", bodyCount(summary.bodies.size()), listBodies(summary, false));
    }
    std::string instances;
    for (const io::ExportedComponent& component : summary.components) {
        instances += instances.empty() ? "" : ", ";
        instances += component.name;
    }
    return std::format("assembly of {} {} ({}) from {} ({})", summary.components.size(),
                       summary.components.size() == 1 ? "component" : "components", instances,
                       bodyCount(summary.bodies.size()), listBodies(summary, false));
}

} // namespace

ExitCode runExportStep(Args args, std::ostream& out, std::ostream& err) {
    auto parsed = parseArguments(args, {});
    if (!parsed) {
        return usageError("export-step", kExportStepUsage, parsed.error().message, err);
    }
    if (parsed->positional().size() != 2) {
        return usageError("export-step", kExportStepUsage, "expected a document file and an output file", err);
    }
    const std::filesystem::path input = pathFromArgument(parsed->positional()[0]);
    const std::filesystem::path output = pathFromArgument(parsed->positional()[1]);
    const auto document = io::loadDocument(input);
    if (!document) {
        return failure("export-step", document.error().message, err);
    }
    const auto summary = io::exportStep(*document, output);
    if (!summary) {
        return failure("export-step", summary.error().message, err);
    }
    out << std::format("Wrote {}: STEP AP214 (mm), {}, {} bytes\n", displayPath(output),
                       describeContents(*summary), summary->bytes);
    return ExitCode::Success;
}

ExitCode runExportStl(Args args, std::ostream& out, std::ostream& err) {
    auto parsed = parseArguments(args, {{"--ascii", false}, {"--tolerance", true}, {"--angle", true}});
    if (!parsed) {
        return usageError("export-stl", kExportStlUsage, parsed.error().message, err);
    }
    if (parsed->positional().size() != 2) {
        return usageError("export-stl", kExportStlUsage, "expected a document file and an output file", err);
    }
    io::StlExportOptions options;
    options.format = parsed->has("--ascii") ? io::StlFormat::Ascii : io::StlFormat::Binary;
    if (const auto tolerance = parsed->value("--tolerance")) {
        auto length = parseLength(*tolerance, units::mm);
        if (!length) {
            return usageError("export-stl", kExportStlUsage, std::format("--tolerance: {}", length.error().message),
                              err);
        }
        options.mesh.linearDeflection = *length;
    }
    if (const auto angle = parsed->value("--angle")) {
        auto value = parseAngle(*angle, units::deg);
        if (!value) {
            return usageError("export-stl", kExportStlUsage, std::format("--angle: {}", value.error().message), err);
        }
        options.mesh.angularDeflection = *value;
    }

    const std::filesystem::path input = pathFromArgument(parsed->positional()[0]);
    const std::filesystem::path output = pathFromArgument(parsed->positional()[1]);
    const auto document = io::loadDocument(input);
    if (!document) {
        return failure("export-stl", document.error().message, err);
    }
    const auto summary = io::exportStl(*document, output, options);
    if (!summary) {
        return failure("export-stl", summary.error().message, err);
    }
    out << std::format("Wrote {}: {} STL (mm), {} ({}), tolerance {:.6g} mm and {:.6g} deg, {} bytes\n",
                       displayPath(output), options.format == io::StlFormat::Ascii ? "ASCII" : "binary",
                       bodyCount(summary->bodies.size()), listBodies(*summary, true),
                       options.mesh.linearDeflection.in(units::mm), options.mesh.angularDeflection.in(units::deg),
                       summary->bytes);
    return ExitCode::Success;
}

} // namespace bettercad::cli
