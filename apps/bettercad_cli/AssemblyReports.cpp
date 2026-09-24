#include "Commands.hpp"
#include "Selectors.hpp"

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Configurations.hpp>
#include <bettercad/assembly/Mate.hpp>
#include <bettercad/assembly/Mates.hpp>
#include <bettercad/assembly/Resolution.hpp>
#include <bettercad/drawing/Regeneration.hpp>
#include <bettercad/assembly/Solver.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/ParameterExpressions.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>

#include <algorithm>
#include <format>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

// Reporting an assembly from a command line (P13-CLI-001).
//
// These three commands NEVER write the file. ADR-005 makes a component's
// transform derived state, so there is nothing here for them to persist:
// they report, and their exit code says whether the assembly is sound.
//
// They are also where the CLI acts as the composition root ADR-006 and
// ADR-008 describe -- building a features::Regenerator and registering the
// assembly handlers on it. features cannot do that itself (it is layer 2 and
// assembly is layer 3), so something above both has to, and this is it.
namespace bettercad::cli {

namespace {

/// Negative zero as 0, matching what info already prints.
double tidy(double value) { return value == 0.0 ? 0.0 : value; }

std::string formatLength(Length value) { return std::format("{:.6g}", tidy(value.in(units::mm))); }

std::string describeTransform(const RigidTransform3D& transform) {
    const Translation3D& t = transform.translationPart();
    const Direction3D z = transform.apply(Direction3D::unitZ());
    return std::format("origin ({}, {}, {}) mm, z ({:.6g}, {:.6g}, {:.6g})", formatLength(t.x), formatLength(t.y),
                       formatLength(t.z), tidy(z.x()), tidy(z.y()), tidy(z.z()));
}

/// A loaded document, regenerated with the assembly handlers in place.
struct Assembly {
    std::unique_ptr<Document> document;
    features::Regenerator regenerator;
    assembly::AssemblyRegeneration pass{};
    features::RegenerationReport report{};
};

/// Loads @p path, selects @p configuration if one was named, and regenerates
/// with the assembly handlers registered.
///
/// The regeneration is what produces the bodies a face-named mate target is
/// resolved against, so it has to happen before any solve, and it is also the
/// production path an assembly takes everywhere else.
Result<std::unique_ptr<Assembly>> openAndRegenerate(const std::filesystem::path& path,
                                                    std::optional<std::string_view> configuration) {
    auto loaded = io::loadDocument(path);
    if (!loaded) {
        return std::unexpected(loaded.error());
    }
    auto assemblyDocument = std::make_unique<Assembly>();
    assemblyDocument->document = std::make_unique<Document>(std::move(*loaded));
    if (configuration) {
        auto id = resolveConfiguration(*assemblyDocument->document, *configuration);
        if (!id) {
            return std::unexpected(id.error());
        }
        if (auto set = assemblyDocument->document->setActiveConfiguration(*id); !set) {
            return std::unexpected(set.error());
        }
        (void)evaluateParameterExpressions(*assemblyDocument->document);
    }
    assembly::registerHandlers(assemblyDocument->regenerator, nullptr, &assemblyDocument->pass);
    // The drawing handlers too (P14-CLI-001). Without them every sheet, view,
    // dimension and annotation is silently marked UpToDate and never
    // validated, which is the defect ADR-014 named and P14-REGEN-001 built
    // the handlers to close -- but nothing in production registered them
    // until here. This is the line that makes a broken drawing reportable by
    // `regenerate`, `status` and `validate`.
    drawing::registerHandlers(assemblyDocument->regenerator);
    auto report = assemblyDocument->regenerator.regenerateAll(*assemblyDocument->document);
    if (!report) {
        return std::unexpected(report.error());
    }
    assemblyDocument->report = std::move(*report);
    return assemblyDocument;
}

void printRegeneration(const Assembly& assembly, std::ostream& out) {
    const features::RegenerationReport& report = assembly.report;
    out << std::format("Regeneration: {}, {} regenerated\n", report.succeeded() ? "ok" : "FAILED",
                       plural(report.regenerated.size(), "object", "objects"));
    for (const ObjectId id : report.failed) {
        const Error* error = assembly.regenerator.error(id);
        out << std::format("  failed   {}: {}\n", label(*assembly.document, id),
                           error == nullptr ? "no reason recorded" : error->message);
    }
    for (const ObjectId id : report.blocked) {
        out << std::format("  blocked  {}: something it depends on did not build\n", label(*assembly.document, id));
    }
    for (const std::vector<ObjectId>& cycle : report.cycles) {
        std::string names;
        for (const ObjectId id : cycle) {
            names += names.empty() ? "" : " -> ";
            names += label(*assembly.document, id);
        }
        out << std::format("  cycle    {}\n", names);
    }
}

/// The mates and components the assembly could not make sense of. These are
/// structured failures rather than solver outcomes (ADR-004), so they are
/// reported separately and before any solve is attempted.
std::size_t printUnresolved(const Document& document, const features::BodyLookup& bodies,
                            std::ostream& out) {
    std::size_t count = 0;
    for (const assembly::UnresolvedComponent& component : assembly::unresolvedComponents(document)) {
        ++count;
        out << std::format("  {} cannot resolve its part ({})\n", label(document, component.component),
                           toString(component.state));
    }
    // Given the bodies, so a face-named target that DOES resolve is not
    // reported as broken; a suppressed mate the model skips by itself.
    for (const assembly::UnresolvedMateTarget& target : assembly::unresolvedMateTargets(document, bodies)) {
        ++count;
        out << std::format("  {} cannot resolve target {} ({})\n", label(document, target.mate),
                           toString(target.side), toString(target.reason));
    }
    return count;
}

void printSolve(const Document& document, const assembly::AssemblySolveResult& result, std::ostream& out) {
    out << std::format("Solve: {}, {}, {}, {}, largest residual {} mm\n", toString(result.status),
                       plural(result.degreesOfFreedom, "degree of freedom", "degrees of freedom"),
                       plural(result.equations, "equation", "equations"),
                       plural(static_cast<std::size_t>(result.iterations), "iteration", "iterations"),
                       formatLength(result.maxResidual));
    if (!result.message.empty()) {
        out << std::format("  {}\n", result.message);
    }
    for (const MateId mate : result.conflicting) {
        out << std::format("  conflicting  {}\n", label(document, mate));
    }
    for (const MateId mate : result.redundant) {
        out << std::format("  redundant    {}\n", label(document, mate));
    }
    if (!result.transforms.empty()) {
        out << std::format("Transforms ({}):\n", result.transforms.size());
        for (const auto& [component, transform] : result.transforms) {
            out << std::format("  {:<28}{}\n", label(document, component), describeTransform(transform));
        }
    }
}

/// The bodies this regeneration produced, so a face-named target has
/// something to resolve against.
features::BodyLookup bodiesOf(const Assembly& assembly) {
    const features::Regenerator* regenerator = &assembly.regenerator;
    return [regenerator](ObjectId object) { return regenerator->body(object); };
}

Result<assembly::AssemblySolveResult> solveWithBodies(const Assembly& assembly) {
    return assembly::solve(*assembly.document, {}, bodiesOf(assembly));
}

/// Exit status of a solve. Under- and fully constrained are answers; the
/// other three are not. OverConstrained is a failure here because the solver
/// publishes no transforms for one, so there is nothing to report as a
/// result -- the redundant mates it names are the thing to fix.
ExitCode statusOf(const assembly::AssemblySolveResult& result) {
    return result.solved() ? ExitCode::Success : ExitCode::Failure;
}

/// Parses `<file> [--configuration <name>]`, which all three share.
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
        return makeError(ErrorCode::InvalidArgument, "expected one document file");
    }
    return ReportArguments{.path = pathFromArgument(parsed->positional().front()),
                           .configuration = parsed->value("--configuration")};
}

} // namespace

ExitCode runRegenerate(Args args, std::ostream& out, std::ostream& err) {
    auto parsed = parseReportArguments(args);
    if (!parsed) {
        return usageError("regenerate", kRegenerateUsage, parsed.error().message, err);
    }
    auto assembly = openAndRegenerate(parsed->path, parsed->configuration);
    if (!assembly) {
        return failure("regenerate", assembly.error().message, err);
    }
    out << std::format("Regenerating {}\n", displayPath(parsed->path));
    printRegeneration(**assembly, out);
    const assembly::AssemblyRegeneration& pass = (*assembly)->pass;
    out << std::format("Assembly pass: {}", toString(pass.trigger));
    if (pass.status) {
        out << std::format(", {}, {}, {} published", toString(*pass.status),
                           pass.degreesOfFreedom == 1 ? "1 degree of freedom"
                                                      : std::format("{} degrees of freedom", pass.degreesOfFreedom),
                           pass.transforms == 1 ? "1 transform" : std::format("{} transforms", pass.transforms));
    }
    out << '\n';
    // Nothing is written: a regeneration produces derived state, and ADR-005
    // keeps derived state out of the file.
    return (*assembly)->report.succeeded() ? ExitCode::Success : ExitCode::Failure;
}

ExitCode runSolve(Args args, std::ostream& out, std::ostream& err) {
    auto parsed = parseReportArguments(args);
    if (!parsed) {
        return usageError("solve", kSolveUsage, parsed.error().message, err);
    }
    auto assembly = openAndRegenerate(parsed->path, parsed->configuration);
    if (!assembly) {
        return failure("solve", assembly.error().message, err);
    }
    const Document& document = *(*assembly)->document;
    out << std::format("Solving {}\n", displayPath(parsed->path));
    // A part that failed to build makes any answer about where its instances
    // sit misleading: the constraint system may well still solve, because
    // most targets do not need a body at all. So the regeneration is reported
    // and the command fails, rather than printing transforms for an assembly
    // of something that is not there.
    if (!(*assembly)->report.succeeded()) {
        printRegeneration(**assembly, out);
        return failure("solve", "the document did not regenerate; nothing was solved", err);
    }
    if (assembly::components(document).empty()) {
        out << "This document has no components.\n";
        return ExitCode::Success;
    }
    auto result = solveWithBodies(**assembly);
    if (!result) {
        // A reference that does not resolve is not a solver outcome, and is
        // kept distinct from one (ADR-004).
        (void)printUnresolved(document, bodiesOf(**assembly), out);
        return failure("solve", result.error().message, err);
    }
    printSolve(document, *result, out);
    return statusOf(*result);
}

ExitCode runStatus(Args args, std::ostream& out, std::ostream& err) {
    auto parsed = parseReportArguments(args);
    if (!parsed) {
        return usageError("status", kStatusUsage, parsed.error().message, err);
    }
    auto assembly = openAndRegenerate(parsed->path, parsed->configuration);
    if (!assembly) {
        return failure("status", assembly.error().message, err);
    }
    const Document& document = *(*assembly)->document;

    out << std::format("Assembly of {}\n", document.name());
    if (const auto active = document.activeConfiguration()) {
        const Configuration* configuration = document.configurations().find(*active);
        out << std::format("Configuration: {}\n",
                           configuration == nullptr ? "unknown" : configuration->name());
    } else {
        out << "Configuration: none (base values)\n";
    }

    const std::vector<ComponentId> allComponents = assembly::components(document);
    out << std::format("Components ({}):\n", allComponents.size());
    for (const ComponentId id : allComponents) {
        const assembly::Component* component = assembly::findComponent(document, id);
        const bool suppressed = assembly::isComponentSuppressed(document, id);
        out << std::format("  {:<28}places {:<28}{}\n", label(document, id),
                           label(document, component->definition().part.object),
                           suppressed ? "suppressed" : "in force");
    }

    const std::vector<MateId> allMates = assembly::mates(document);
    out << std::format("Mates ({}):\n", allMates.size());
    for (const MateId id : allMates) {
        const assembly::Mate* mate = assembly::findMate(document, id);
        const assembly::MateDefinition& definition = mate->definition();
        std::string what{toString(definition.type)};
        if (definition.type == assembly::MateType::Fixed) {
            what += std::format(" {}", label(document, definition.component));
        } else if (definition.a && definition.b) {
            what += std::format(" {} to {}", formatMateTarget(document, *definition.a),
                                formatMateTarget(document, *definition.b));
        }
        out << std::format("  {:<28}{:<56}{}\n", label(document, id), what,
                           assembly::isMateActive(document, id) ? "in force" : "suppressed");
    }

    printRegeneration(**assembly, out);
    const std::size_t unresolved = printUnresolved(document, bodiesOf(**assembly), out);

    if (allComponents.empty()) {
        out << "No components, so nothing to solve.\n";
        return (*assembly)->report.succeeded() ? ExitCode::Success : ExitCode::Failure;
    }
    auto result = solveWithBodies(**assembly);
    if (!result) {
        return failure("status", result.error().message, err);
    }
    printSolve(document, *result, out);
    if (unresolved > 0 || !(*assembly)->report.succeeded()) {
        return ExitCode::Failure;
    }
    return statusOf(*result);
}

} // namespace bettercad::cli
