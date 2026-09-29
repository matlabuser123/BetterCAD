#include "Commands.hpp"
#include "MaterialVocabulary.hpp"
#include "Selectors.hpp"

#include <bettercad/assembly/Resolution.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/ParameterExpressions.hpp>
#include <bettercad/core/materials/Completeness.hpp>
#include <bettercad/core/materials/Provenance.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/drawing/Regeneration.hpp>
#include <bettercad/features/MassProperties.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/io/DocumentFile.hpp>

#include <algorithm>
#include <format>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

// Reporting engineering data from a command line (P15-CLI-001).
//
// Five commands, none of which writes anything. That is not caution: every
// number here is either canonical state the document already holds or state
// DERIVED from it, and a report that saved its derivation would be caching an
// answer the next load has to recompute anyway. ADR-026 makes a mass derived,
// ADR-027 makes the shear and bulk moduli derived, and P15-PERSIST-001 proved
// the file contains none of them.
//
// Nor do they decide anything. Which properties a consumer needs, whether a
// material is Ready, what the effective material is, what a body weighs --
// every one of those questions is answered by a qualified core function, and
// this file's whole job is to turn the answer into text and an exit status. A
// number that appears here and nowhere else would be a CLI-only derivation,
// which is exactly what this milestone must not contain.
namespace bettercad::cli {

namespace {

using materials::ConsumerKind;
using materials::MechanicalPropertyKind;
using materials::ThermalPropertyKind;

/// Negative zero as 0, matching what info and status already print.
double tidy(double value) { return value == 0.0 ? 0.0 : value; }

/// The shortest text that reads back as the same double.
///
/// Shortest ROUND TRIP rather than a fixed precision, and that is a testability
/// decision as much as a display one: the number the CLI prints is then exactly
/// the double the core computed, so the equivalence tests can compare them for
/// equality instead of within a tolerance the test itself chose.
std::string number(double value) { return std::format("{}", tidy(value)); }

template <Dimension D>
std::string inUnit(Quantity<D> value, const Unit<D>& unit) {
    return std::format("{} {}", number(value.in(unit)), unit.symbol);
}

/// The code for a failed assignment lookup, from the state that failed.
std::string_view assignmentCode(features::MaterialAssignmentState state) noexcept {
    switch (state) {
    case features::MaterialAssignmentState::Unassigned:
        return "no_material_assigned";
    case features::MaterialAssignmentState::Unresolved:
        return "material_unresolved";
    case features::MaterialAssignmentState::Invalid:
        return "material_assignment_invalid";
    case features::MaterialAssignmentState::Resolved:
        break;
    }
    return "material_assignment_invalid";
}

/// "Steel (object:1)", the way every other report names an object.
std::string materialLabel(const Document& document, MaterialId id) {
    return label(document, ObjectId{id});
}

/// The name and ID of a material, then its metadata, one field per line and
/// only where there is one. An empty standard is not printed as "standard: ",
/// because a blank line is indistinguishable from a formatting fault.
void printMetadata(const features::MaterialDefinition& definition, std::ostream& out) {
    const auto field = [&out](std::string_view name, std::string_view value) {
        if (!value.empty()) {
            out << std::format("  {:<14}{}\n", name, value);
        }
    };
    field("designation", definition.designation);
    field("standard", definition.standard);
    field("family", definition.family);
    field("notes", definition.notes);
    if (definition.origin) {
        // Where the VALUES came from. Not a live link: ADR-025 makes the import
        // a copy, and nothing consults the library on the document's behalf
        // afterwards.
        out << std::format("  {:<14}{}/{} revision {}\n", "origin", definition.origin->library,
                           definition.origin->entry, definition.origin->revision);
    }
}

/// One provenance record as a line, or "none".
std::string describeProvenance(const materials::PropertyProvenance& provenance) {
    if (provenance.empty()) {
        return "none";
    }
    std::string text{materials::toString(provenance.kind)};
    const auto add = [&text](std::string_view value) {
        if (!value.empty()) {
            text += ", ";
            text += value;
        }
    };
    add(provenance.source);
    add(provenance.standard);
    add(provenance.reference);
    add(provenance.revision);
    if (provenance.date) {
        text += ", ";
        text += materials::toString(*provenance.date);
    }
    return text;
}

/// Every property of one material, mechanical then thermal, in enumeration
/// order. UNKNOWN where there is no value, "(derived)" where the value was
/// computed from other properties rather than supplied.
void printProperties(const Document& document, MaterialId id, const features::MaterialDefinition& definition,
                     bool withProvenance, std::ostream& out) {
    out << "Mechanical:\n";
    for (const PropertyKind& kind : allPropertyKinds()) {
        if (const auto* thermal = std::get_if<ThermalPropertyKind>(&kind)) {
            (void)thermal;
            continue;
        }
        const auto mechanical = std::get<MechanicalPropertyKind>(kind);
        out << std::format("  {:<32}{}\n", propertyCode(mechanical),
                           formatProperty(definition.mechanical, mechanical));
        if (withProvenance && !isDerivedProperty(kind)) {
            // A derived property has no provenance of its own and cannot: its
            // provenance is its inputs'. Asking for one would invent a record.
            if (auto provenance = features::materialPropertyProvenance(document, id, mechanical)) {
                out << std::format("      {:<28}{}\n", "from", describeProvenance(*provenance));
            }
        }
    }
    out << "Thermal:\n";
    for (const PropertyKind& kind : allPropertyKinds()) {
        const auto* thermal = std::get_if<ThermalPropertyKind>(&kind);
        if (thermal == nullptr) {
            continue;
        }
        out << std::format("  {:<32}{}\n", propertyCode(*thermal), formatProperty(definition.thermal, *thermal));
        if (withProvenance) {
            if (auto provenance = features::materialPropertyProvenance(document, id, *thermal)) {
                out << std::format("      {:<28}{}\n", "from", describeProvenance(*provenance));
            }
        }
    }
}

/// A loaded document, regenerated the way production regenerates one.
struct Regenerated {
    std::unique_ptr<Document> document;
    features::Regenerator regenerator;
    assembly::AssemblyRegeneration pass{};
};

/// Loads @p path, selects @p configuration if one was named, and regenerates.
///
/// The assembly and drawing handlers are registered for the same reason
/// AssemblyReports.cpp registers them: the CLI is the composition root (ADR-006,
/// ADR-008), and a regeneration missing a handler marks those objects UpToDate
/// without ever building them.
Result<std::unique_ptr<Regenerated>> openAndRegenerate(const std::filesystem::path& path,
                                                       std::optional<std::string_view> configuration) {
    auto loaded = io::loadDocument(path);
    if (!loaded) {
        return std::unexpected(loaded.error());
    }
    auto opened = std::make_unique<Regenerated>();
    opened->document = std::make_unique<Document>(std::move(*loaded));
    if (configuration) {
        auto id = resolveConfiguration(*opened->document, *configuration);
        if (!id) {
            return std::unexpected(id.error());
        }
        if (auto set = opened->document->setActiveConfiguration(*id); !set) {
            return std::unexpected(set.error());
        }
        (void)evaluateParameterExpressions(*opened->document);
    }
    assembly::registerHandlers(opened->regenerator, nullptr, &opened->pass);
    drawing::registerHandlers(opened->regenerator);
    if (auto report = opened->regenerator.regenerateAll(*opened->document); !report) {
        return std::unexpected(report.error());
    }
    return opened;
}

/// Loads @p path without regenerating, for the reports that read canonical
/// state only. A material is canonical, so asking what one says never needs
/// geometry -- and a part whose geometry is broken must still be able to
/// answer what it is made of.
Result<Document> open(const std::filesystem::path& path) { return io::loadDocument(path); }

/// The one positional document path, or a usage error.
Result<std::filesystem::path> documentArgument(const ParsedArguments& parsed, std::size_t index = 0) {
    if (parsed.positional().size() <= index) {
        return makeError(ErrorCode::InvalidArgument, "expected one document file");
    }
    return pathFromArgument(parsed.positional()[index]);
}

/// The inertia tensor, in kg mm^2, laid out as the symmetric matrix it is.
///
/// The six values are printed exactly as InertiaTensor holds them, which means
/// the off-diagonals are NEGATED products of inertia -- the convention the
/// matrix form uses. Saying so is the point: a caller who wants the positive
/// products has to negate them, and a report that stayed silent would let
/// someone assemble the wrong matrix from the right numbers.
void printInertia(std::string_view heading, const features::InertiaTensor& tensor, std::ostream& out) {
    out << std::format("    {} ({}, {}, {}) mm, kg mm^2:\n", heading, number(tensor.about.x.in(units::mm)),
                       number(tensor.about.y.in(units::mm)), number(tensor.about.z.in(units::mm)));
    out << std::format("      xx {}\n      yy {}\n      zz {}\n", number(tensor.xx.in(units::kg_mm2)),
                       number(tensor.yy.in(units::kg_mm2)), number(tensor.zz.in(units::kg_mm2)));
    out << std::format("      xy {}\n      xz {}\n      yz {}\n", number(tensor.xy.in(units::kg_mm2)),
                       number(tensor.xz.in(units::kg_mm2)), number(tensor.yz.in(units::kg_mm2)));
}

void printCompleteness(const materials::CompletenessReport& report, std::ostream& out) {
    const auto list = [&out](std::string_view heading, const auto& mechanical, const auto& thermal) {
        if (mechanical.empty() && thermal.empty()) {
            return;
        }
        out << std::format("  {} ({}):\n", heading, mechanical.size() + thermal.size());
        for (const auto kind : mechanical) {
            out << std::format("    {}\n", propertyCode(kind));
        }
        for (const auto kind : thermal) {
            out << std::format("    {}\n", propertyCode(kind));
        }
    };
    list("present", report.presentMechanical, report.presentThermal);
    list("missing", report.missingMechanical, report.missingThermal);
    if (!report.issues.empty()) {
        out << std::format("  issues ({}):\n", report.issues.size());
        for (const materials::MaterialIssue& issue : report.issues) {
            // The code first, then the sentence. A script matches the code; the
            // message is the core's own and is not re-worded here.
            out << std::format("    {:<44}{}\n", issueCode(issue), issue.message);
        }
    }
}

} // namespace

ExitCode runMaterialList(Args args, std::ostream& out, std::ostream& err) {
    auto parsed = parseArguments(args, {});
    if (!parsed) {
        return usageError("material-list", kMaterialListUsage, parsed.error().message, err);
    }
    auto path = documentArgument(*parsed);
    if (!path || parsed->positional().size() != 1) {
        return usageError("material-list", kMaterialListUsage, "expected one document file", err);
    }
    auto document = open(*path);
    if (!document) {
        return failure("material-list", "document_not_loaded", document.error().message, err);
    }

    // Ascending ID, from the document's ordered map. The order is the same in
    // every build and on every run, and it is presentation only -- a material's
    // identity is its MaterialId and never its position here.
    const std::vector<MaterialId> ids = features::materialIds(*document);
    const features::MaterialAssignment assignment = features::materialAssignment(*document);
    out << std::format("Materials of {} ({}):\n", document->name(), ids.size());
    for (const MaterialId& id : ids) {
        const features::Material* material = features::findMaterial(*document, id);
        const std::string_view designation =
            material->definition().designation.empty() ? std::string_view{"-"} : material->definition().designation;
        const bool assigned = assignment.material == id;
        // Trimmed: a line that ends in padding is a line whose exact bytes a
        // golden-text test would have to encode as invisible whitespace.
        std::string row = std::format("  {:<28}{:<34}{}", materialLabel(*document, id), designation,
                                      assigned ? "assigned" : "");
        while (row.ends_with(' ')) {
            row.pop_back();
        }
        out << row << '\n';
    }
    // A document with no materials is a normal document, not a fault: zero is
    // reported and the exit status stays 0.
    return ExitCode::Success;
}

ExitCode runMaterialShow(Args args, std::ostream& out, std::ostream& err) {
    auto parsed = parseArguments(args, {{"--provenance", false}});
    if (!parsed) {
        return usageError("material-show", kMaterialShowUsage, parsed.error().message, err);
    }
    if (parsed->positional().size() != 2) {
        return usageError("material-show", kMaterialShowUsage, "expected a document file and a material", err);
    }
    const std::filesystem::path path = pathFromArgument(parsed->positional()[0]);
    auto document = open(path);
    if (!document) {
        return failure("material-show", "document_not_loaded", document.error().message, err);
    }
    auto id = resolveMaterial(*document, parsed->positional()[1]);
    if (!id) {
        if (id.error().code == ErrorCode::InvalidArgument) {
            return usageError("material-show", kMaterialShowUsage, id.error().message, err);
        }
        return failure("material-show", materialSelectorCode(id.error()), id.error().message, err);
    }

    const features::Material* material = features::findMaterial(*document, *id);
    out << std::format("{}\n", materialLabel(*document, *id));
    printMetadata(material->definition(), out);
    printProperties(*document, *id, material->definition(), parsed->has("--provenance"), out);
    return ExitCode::Success;
}

ExitCode runMaterialEffective(Args args, std::ostream& out, std::ostream& err) {
    auto parsed = parseArguments(args, {});
    if (!parsed) {
        return usageError("material-effective", kMaterialEffectiveUsage, parsed.error().message, err);
    }
    if (parsed->positional().size() != 1) {
        return usageError("material-effective", kMaterialEffectiveUsage, "expected one document file", err);
    }
    auto document = open(pathFromArgument(parsed->positional()[0]));
    if (!document) {
        return failure("material-effective", "document_not_loaded", document.error().message, err);
    }

    // ONE resolution, the core's. There is no fallback layer, no default
    // material and no inheritance in P15, so "what is this made of" has exactly
    // one answer and this command does not compute a second one.
    const features::MaterialAssignment assignment = features::materialAssignment(*document);
    out << std::format("Document: {}\nAssignment: {}\n", document->name(),
                       features::toString(assignment.state));
    if (assignment.material) {
        // The INTENT is printed even when it does not resolve, because the
        // intent is what makes recovery possible: the ID names the material
        // that must come back, and nothing rewrites it to point elsewhere.
        out << std::format("Material: {}\n", assignment.state == features::MaterialAssignmentState::Resolved
                                                 ? materialLabel(*document, *assignment.material)
                                                 : std::format("material:{}", assignment.material->value()));
    }
    if (assignment.state == features::MaterialAssignmentState::Resolved) {
        printMetadata(features::findMaterial(*document, *assignment.material)->definition(), out);
        return ExitCode::Success;
    }
    if (assignment.state == features::MaterialAssignmentState::Unassigned) {
        // Not a fault. A part with no material chosen yet is a normal resting
        // state, so the report succeeds and says so.
        return ExitCode::Success;
    }
    return failure("material-effective", assignmentCode(assignment.state), assignment.diagnostic, err);
}

ExitCode runMassProperties(Args args, std::ostream& out, std::ostream& err) {
    auto parsed = parseArguments(args, {{"--configuration", true}});
    if (!parsed) {
        return usageError("mass-properties", kMassPropertiesUsage, parsed.error().message, err);
    }
    if (parsed->positional().empty() || parsed->positional().size() > 2) {
        return usageError("mass-properties", kMassPropertiesUsage,
                          "expected a document file and at most one feature", err);
    }
    const std::filesystem::path path = pathFromArgument(parsed->positional()[0]);
    auto opened = openAndRegenerate(path, parsed->value("--configuration"));
    if (!opened) {
        return failure("mass-properties", "document_not_loaded", opened.error().message, err);
    }
    const Document& document = *(*opened)->document;
    const features::Regenerator& regenerator = (*opened)->regenerator;

    // Which bodies. Either the one feature named, or every object that
    // regeneration actually produced a body for -- asked of the regenerator,
    // which is the only thing that knows.
    std::vector<ObjectId> features;
    if (parsed->positional().size() == 2) {
        auto object = resolveObject(document, parsed->positional()[1]);
        if (!object) {
            if (object.error().code == ErrorCode::InvalidArgument) {
                return usageError("mass-properties", kMassPropertiesUsage, object.error().message, err);
            }
            return failure("mass-properties", "feature_not_found", object.error().message, err);
        }
        features.push_back(*object);
    } else {
        // THE RESULT BODIES, which is not the same as every feature that has
        // one. A bored part is a chain -- the extrude makes a solid and the
        // hole consumes it -- so both have a body while only the last is a body
        // OF THE PART. Listing both reported the un-bored solid as though the
        // part had two, and the first of the two masses was the blank's.
        //
        // resultFeatures() is the product's own answer, already used by
        // validate and export-step, so this asks rather than re-deciding.
        // Found by RM-MAT-03 (P15-REFMOD-001).
        features = features::resultFeatures(document);
        std::erase_if(features, [&](ObjectId id) { return regenerator.body(id) == nullptr; });
        if (features.empty()) {
            return failure("mass-properties", "no_bodies",
                           std::format("{} has no body to weigh", document.name()), err);
        }
    }

    out << std::format("Mass properties of {}\n", document.name());
    ExitCode status = ExitCode::Success;
    for (const ObjectId& feature : features) {
        out << std::format("  {}\n", label(document, feature));
        // THE mass calculation, P15-MASS-001's. Not OCCT directly, and nothing
        // reimplemented here: a wrong mass reported by the CLI and a right one
        // reported by the core would be the exact failure this milestone is
        // meant to make impossible.
        auto properties = features::partMassProperties(document, regenerator, feature);
        if (!properties) {
            // Never a zero mass for a question that could not be answered. The
            // body is named, the reason is the core's own, and the exit status
            // carries the failure out of the process.
            out << std::format("    unavailable: {}\n", properties.error().message);
            err << std::format("{} mass-properties: {}: {}\n", kProgramName, "mass_properties_unavailable",
                               properties.error().message);
            status = ExitCode::Failure;
            continue;
        }
        out << std::format("    {:<18}{}\n", "material", materialLabel(document, properties->material));
        out << std::format("    {:<18}{}\n", "density", inUnit(properties->density, units::kg_per_m3));
        out << std::format("    {:<18}{}\n", "volume", inUnit(properties->volume, units::mm3));
        out << std::format("    {:<18}{}\n", "mass", inUnit(properties->mass, units::kg));
        out << std::format("    {:<18}({}, {}, {}) mm\n", "centre of mass",
                           number(properties->centreOfMass.x.in(units::mm)),
                           number(properties->centreOfMass.y.in(units::mm)),
                           number(properties->centreOfMass.z.in(units::mm)));
        // Two frames, named apart. The centroidal tensor is the invariant
        // one -- unchanged by translating the body -- and the one about
        // the origin is derived from it by Huygens. Printing both under
        // one heading would leave a reader to guess which six numbers
        // they were holding.
        printInertia("inertia about the centre of mass", properties->aboutCentreOfMass, out);
        printInertia("inertia about the origin", properties->aboutOrigin, out);
    }
    // NO TOTAL. Summing masses would be trivial and summing inertia tensors
    // would not -- it needs Huygens to a common point, which is a derivation
    // the core does not offer. Writing it here would be a CLI-only engineering
    // result, and a whole-part total is a core milestone rather than a
    // formatting decision. See AUDIT.md finding 4.
    return status;
}

ExitCode runMaterialCompleteness(Args args, std::ostream& out, std::ostream& err) {
    auto parsed = parseArguments(args, {{"--consumer", true}});
    if (!parsed) {
        return usageError("material-completeness", kMaterialCompletenessUsage, parsed.error().message, err);
    }
    if (parsed->positional().empty() || parsed->positional().size() > 2) {
        return usageError("material-completeness", kMaterialCompletenessUsage,
                          "expected a document file and at most one material", err);
    }
    auto document = open(pathFromArgument(parsed->positional()[0]));
    if (!document) {
        return failure("material-completeness", "document_not_loaded", document.error().message, err);
    }

    std::optional<ConsumerKind> consumer;
    if (const auto given = parsed->value("--consumer")) {
        auto kind = parseConsumer(*given);
        if (!kind) {
            return usageError("material-completeness", kMaterialCompletenessUsage, kind.error().message, err);
        }
        consumer = *kind;
    }

    // Which material. The one named, or the one the document is assigned --
    // and when it is the assignment, a missing or dangling one is a DIFFERENT
    // failure from an incomplete material. One means "choose a material", the
    // other "characterise it further", and they are not merged.
    MaterialId id{};
    if (parsed->positional().size() == 2) {
        auto resolved = resolveMaterial(*document, parsed->positional()[1]);
        if (!resolved) {
            if (resolved.error().code == ErrorCode::InvalidArgument) {
                return usageError("material-completeness", kMaterialCompletenessUsage, resolved.error().message, err);
            }
            return failure("material-completeness", materialSelectorCode(resolved.error()), resolved.error().message, err);
        }
        id = *resolved;
    } else {
        const features::MaterialAssignment assignment = features::materialAssignment(*document);
        if (assignment.state != features::MaterialAssignmentState::Resolved) {
            return failure("material-completeness", assignmentCode(assignment.state),
                           assignment.diagnostic.empty() ? "this document has no material assigned" : assignment.diagnostic,
                           err);
        }
        id = *assignment.material;
    }

    auto report = consumer ? features::materialCompleteness(*document, id, *consumer)
                           : features::materialReport(*document, id);
    if (!report) {
        return failure("material-completeness", "material_not_found", report.error().message, err);
    }

    out << std::format("{} {}: {}\n", materialLabel(*document, id),
                       consumer ? std::format("for {}", consumerCode(*consumer)) : "in general",
                       stateCode(report->state));
    printCompleteness(*report, out);

    // Exit 1 unless Ready, which is what `validate` does with a document that
    // has errors: a report that gates is usable from a script only if the gate
    // reaches the exit status. The two are still distinguishable, because a
    // report that ANSWERED wrote to stdout and one that could not answer wrote
    // to stderr and printed nothing here.
    return report->ready() ? ExitCode::Success : ExitCode::Failure;
}

} // namespace bettercad::cli
