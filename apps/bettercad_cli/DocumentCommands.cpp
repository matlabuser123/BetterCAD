#include "Commands.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/RevolveFeature.hpp>
#include <bettercad/features/Validation.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace bettercad::cli {

namespace {

using Row = std::vector<std::string>;

/// Left-aligned columns separated by two spaces, each row indented.
void printTable(std::ostream& out, const std::vector<Row>& rows) {
    std::vector<std::size_t> widths;
    for (const Row& row : rows) {
        widths.resize(std::max(widths.size(), row.size()), 0);
        for (std::size_t i = 0; i < row.size(); ++i) {
            widths[i] = std::max(widths[i], row[i].size());
        }
    }
    for (const Row& row : rows) {
        std::string line = "  ";
        for (std::size_t i = 0; i < row.size(); ++i) {
            line += row[i];
            if (i + 1 < row.size()) {
                line.append(widths[i] - row[i].size() + 2, ' ');
            }
        }
        while (line.ends_with(' ')) {
            line.pop_back();
        }
        out << line << '\n';
    }
}

std::string plural(std::size_t count, std::string_view singular, std::string_view pluralForm) {
    return std::format("{} {}", count, count == 1 ? singular : pluralForm);
}

std::string nameOrId(const Document& document, ObjectId id) {
    const auto name = document.nameOf(id);
    return name ? std::string{*name} : std::format("{} (missing)", id);
}

std::string describeParameter(const Parameter& parameter) {
    std::string text = parameter.displayUnit().symbol.empty()
                           ? std::format("{:.10g}", parameter.displayValue())
                           : std::format("{:.10g} {}", parameter.displayValue(), parameter.displayUnit().symbol);
    if (parameter.expression()) {
        text += std::format("  (expression: {})", *parameter.expression());
    }
    return text;
}

/// "new body", or the operation and its target: "cut Pad".
std::string describeOperation(const Document& document, features::FeatureOperation operation,
                              std::optional<FeatureId> target) {
    std::string text{features::toString(operation)};
    if (target) {
        text += " " + nameOrId(document, ObjectId{*target});
    }
    return text;
}

/// "equal distance 5 mm", "two distances 5 mm and 3 mm" or
/// "distance and angle 5 mm and 30 deg"; a driven distance shows its
/// parameter's name.
std::string describeChamferSize(const Document& document, const features::ChamferDefinition& d) {
    const std::string distance = d.distanceParameter ? nameOrId(document, ObjectId{*d.distanceParameter})
                                                     : std::format("{:.10g} mm", d.distance.in(units::mm));
    switch (d.mode) {
    case geometry::ChamferMode::EqualDistance:
        return std::format("equal distance {}", distance);
    case geometry::ChamferMode::TwoDistance:
        return std::format("two distances {} and {:.10g} mm", distance, d.distance2.in(units::mm));
    case geometry::ChamferMode::DistanceAngle:
        return std::format("distance and angle {} and {:.10g} deg", distance, d.angle.in(units::deg));
    }
    return distance;
}

std::string describeObject(const Document& document, const DocumentObject& object) {
    if (const auto* sketch = dynamic_cast<const sketch::Sketch*>(&object)) {
        const auto disabled = std::ranges::count_if(sketch->constraints(),
                                                    [](const sketch::Constraint& c) { return !c.enabled; });
        std::string text = std::format("{} entities, {} constraints", sketch->entityCount(), sketch->constraintCount());
        if (disabled > 0) {
            text += std::format(" ({} disabled)", disabled);
        }
        const auto drivers = sketch->dependencies();
        if (!drivers.empty()) {
            text += ", driven by ";
            for (std::size_t i = 0; i < drivers.size(); ++i) {
                text += (i == 0 ? "" : ", ") + nameOrId(document, drivers[i]);
            }
        }
        return text;
    }
    if (const auto* extrude = dynamic_cast<const features::ExtrudeFeature*>(&object)) {
        const features::ExtrudeDefinition& d = extrude->definition();
        const std::string depth = d.depthParameter ? nameOrId(document, ObjectId{*d.depthParameter})
                                                   : std::format("{:.10g} mm", d.depth.in(units::mm));
        return std::format("profile {}, depth {}, {}, {}", nameOrId(document, ObjectId{d.profile}), depth,
                           features::toString(d.direction), describeOperation(document, d.operation, d.target));
    }
    if (const auto* revolve = dynamic_cast<const features::RevolveFeature*>(&object)) {
        const features::RevolveDefinition& d = revolve->definition();
        const std::string axis = d.axis.kind == features::RevolveAxisKind::Line
                                     ? std::format("line {}", d.axis.line)
                                     : std::string{features::toString(d.axis.kind)};
        const std::string angle = d.angleParameter ? nameOrId(document, ObjectId{*d.angleParameter})
                                                   : std::format("{:.10g} deg", d.angle.in(units::deg));
        return std::format("profile {}, axis {}, angle {}, {}, {}", nameOrId(document, ObjectId{d.profile}), axis,
                           angle, features::toString(d.direction), describeOperation(document, d.operation, d.target));
    }
    if (const auto* chamfer = dynamic_cast<const features::ChamferFeature*>(&object)) {
        const features::ChamferDefinition& d = chamfer->definition();
        return std::format("target {}, {}, {}", nameOrId(document, ObjectId{d.target}),
                           plural(d.edges.size(), "edge", "edges"), describeChamferSize(document, d));
    }
    return {};
}

void printInfo(const Document& document, const std::filesystem::path& path, std::ostream& out) {
    out << std::format("Document: {}\n", document.name());
    out << std::format("File: {}\n", displayPath(path));
    out << std::format("ID: {}\n", document.id().value().toString());
    const DocumentMetadata& metadata = document.metadata();
    if (!metadata.description.empty()) {
        out << std::format("Description: {}\n", metadata.description);
    }
    if (!metadata.author.empty()) {
        out << std::format("Author: {}\n", metadata.author);
    }
    for (const auto& [key, value] : metadata.properties) {
        out << std::format("Property {}: {}\n", key, value);
    }

    out << std::format("\nParameters ({}):\n", document.parameters().size());
    std::vector<Row> parameters;
    for (const Parameter& parameter : document.parameters().all()) {
        parameters.push_back({parameter.name(), describeParameter(parameter)});
    }
    printTable(out, parameters);

    out << std::format("\nObjects ({}):\n", document.objectCount());
    std::vector<Row> objects;
    for (const DocumentObject& object : document.objects()) {
        objects.push_back({std::format("{}", object.id()), std::string{object.typeName()}, object.name(),
                           describeObject(document, object)});
    }
    printTable(out, objects);
}

std::string checkStatus(const features::ValidationReport& report, features::ValidationCheck check) {
    using features::Severity;
    using features::ValidationCheck;
    const std::size_t errors = report.count(check, Severity::Error);
    const std::size_t warnings = report.count(check, Severity::Warning);
    if (errors > 0) {
        return plural(errors, "error", "errors");
    }
    std::string status = warnings > 0 ? plural(warnings, "warning", "warnings") : "ok";
    if (check == ValidationCheck::FeatureRegeneration) {
        status += std::format(", {} regenerated", plural(report.regenerated, "object", "objects"));
    } else if (check == ValidationCheck::Geometry) {
        status += std::format(", {}", plural(report.bodies.size(), "result body", "result bodies"));
    }
    return status;
}

/// Millimetres to the micrometre, without trailing zeros: "100", "-21.776".
/// Kernel noise such as -1.5e-15 shows as "0".
std::string formatMm(const Length& length) {
    std::string text = std::format("{:.3f}", length.in(units::mm));
    while (text.ends_with('0')) {
        text.pop_back();
    }
    if (text.ends_with('.')) {
        text.pop_back();
    }
    return text == "-0" ? "0" : text;
}

std::string describeBody(const features::BodySummary& body) {
    std::string text = std::format("{} ({}): {}", body.name, body.feature,
                                   plural(body.topology.solids, "solid", "solids"));
    if (body.properties) {
        text += std::format(", volume {:.3f} mm^3, area {:.3f} mm^2", body.properties->volume.in(units::mm3),
                            body.properties->surfaceArea.in(units::mm2));
    }
    if (body.boundingBox) {
        const auto& box = *body.boundingBox;
        text += std::format(", bounds ({}, {}, {}) to ({}, {}, {}) mm", formatMm(box.min.x), formatMm(box.min.y),
                            formatMm(box.min.z), formatMm(box.max.x), formatMm(box.max.y), formatMm(box.max.z));
    }
    if (!body.valid) {
        text += ", INVALID";
    }
    return text;
}

void printValidation(const features::ValidationReport& report, std::ostream& out) {
    for (const features::ValidationCheck check : features::kValidationChecks) {
        out << std::format("  {:<22}{}\n", features::toString(check), checkStatus(report, check));
        for (const features::ValidationIssue& issue : report.issues) {
            if (issue.check == check) {
                out << std::format("    {}: {}\n", features::toString(issue.severity), issue.message);
            }
        }
    }
    if (!report.bodies.empty()) {
        out << std::format("Result bodies ({}):\n", report.bodies.size());
        for (const features::BodySummary& body : report.bodies) {
            out << "  " << describeBody(body) << '\n';
        }
    }
}

std::string resultLine(std::size_t errors, std::size_t warnings) {
    std::string counts;
    if (errors > 0) {
        counts = plural(errors, "error", "errors");
    }
    if (warnings > 0) {
        counts += (counts.empty() ? "" : ", ") + plural(warnings, "warning", "warnings");
    }
    return std::format("Result: {}{}\n", errors == 0 ? "valid" : "invalid",
                       counts.empty() ? "" : std::format(" ({})", counts));
}

} // namespace

ExitCode runNew(Args args, std::ostream& out, std::ostream& err) {
    auto parsed = parseArguments(args, {{"--name", true}, {"--force", false}});
    if (!parsed) {
        return usageError("new", kNewUsage, parsed.error().message, err);
    }
    if (parsed->positional().size() != 1) {
        return usageError("new", kNewUsage, "expected one document file", err);
    }
    const std::filesystem::path path = pathFromArgument(parsed->positional().front());
    std::error_code ignored;
    if (std::filesystem::exists(path, ignored) && !parsed->has("--force")) {
        return failure("new", std::format("'{}' already exists (use --force to replace it)", displayPath(path)), err);
    }

    Document document;
    if (const auto name = parsed->value("--name")) {
        if (auto set = document.setName(std::string{*name}); !set) {
            return usageError("new", kNewUsage, set.error().message, err);
        }
    } else {
        // Named after the file; a file name that is not a valid document name
        // keeps the default name.
        [[maybe_unused]] const auto named = document.setName(displayPath(path.stem()));
    }
    if (auto saved = io::saveDocument(document, path); !saved) {
        return failure("new", saved.error().message, err);
    }
    out << std::format("Created {} (document '{}', ID {})\n", displayPath(path), document.name(),
                       document.id().value().toString());
    return ExitCode::Success;
}

ExitCode runInfo(Args args, std::ostream& out, std::ostream& err) {
    auto parsed = parseArguments(args, {});
    if (!parsed) {
        return usageError("info", kInfoUsage, parsed.error().message, err);
    }
    if (parsed->positional().size() != 1) {
        return usageError("info", kInfoUsage, "expected one document file", err);
    }
    const std::filesystem::path path = pathFromArgument(parsed->positional().front());
    const auto document = io::loadDocument(path);
    if (!document) {
        return failure("info", document.error().message, err);
    }
    printInfo(*document, path, out);
    return ExitCode::Success;
}

ExitCode runValidate(Args args, std::ostream& out, std::ostream& err) {
    auto parsed = parseArguments(args, {});
    if (!parsed) {
        return usageError("validate", kValidateUsage, parsed.error().message, err);
    }
    if (parsed->positional().size() != 1) {
        return usageError("validate", kValidateUsage, "expected one document file", err);
    }
    const std::filesystem::path path = pathFromArgument(parsed->positional().front());
    out << std::format("Validating {}\n", displayPath(path));
    const auto document = io::loadDocument(path);
    if (!document) {
        // A file that does not load is the most basic consistency failure.
        out << std::format("  {:<22}{}\n    error: {}\n",
                           features::toString(features::ValidationCheck::DocumentConsistency), "1 error",
                           document.error().message);
        out << resultLine(1, 0);
        return ExitCode::Failure;
    }
    const features::ValidationReport report = features::validateDocument(*document);
    printValidation(report, out);
    out << resultLine(report.count(features::Severity::Error), report.count(features::Severity::Warning));
    return report.valid() ? ExitCode::Success : ExitCode::Failure;
}

} // namespace bettercad::cli
