#include "EditSupport.hpp"
#include "MaterialVocabulary.hpp"
#include "Selectors.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/MaterialCommands.hpp>
#include <bettercad/features/Materials.hpp>

#include <array>
#include <format>
#include <string>
#include <string_view>
#include <vector>

// Changing engineering data from a command line (P15-CLI-001).
//
// Seven verbs, and not one of them decides anything about materials. Each is a
// parse followed by a call:
//
//     material-create    CreateMaterialCommand
//     material-delete    DeleteMaterialCommand
//     material-assign    AssignMaterialCommand
//     material-unassign  RemoveMaterialAssignmentCommand
//     material-set       setMaterialDefinition / setMaterialMechanical / setMaterialThermal
//     material-unset     removeMaterialProperty
//     material-clone     cloneMaterial
//
// The first four go through P15-CMD-001's command objects because those exist,
// so the CLI inherits that milestone's preconditions and its all-or-nothing
// execution rather than restating either (ADR-009, and EditSupport::execute).
//
// The last three go through P15-CUSTOM-001's and P15-PROV-001's functions
// instead, and that is the same principle rather than an exception to it. Each
// of those carries a COUPLING that a command object would not: cloneMaterial
// copies every property and carries the origin key while minting a new
// identity; removeMaterialProperty removes the value AND the provenance that
// described it; setMaterialMechanical clears the provenance of any value it
// changes, because a citation for a number that is no longer there is a lie.
// Rebuilding a whole MaterialDefinition here and handing it to
// EditMaterialCommand would mean reimplementing those three couplings in the
// CLI -- which is precisely the CLI-only semantics this milestone must not
// have. The rule is not "always use a command object"; it is "never restate
// what a qualified function already decides".
//
// Atomicity is the spine's, not each verb's: runEdit and runBatch save only
// when every edit succeeded, so a verb that changes metadata and then fails on
// a value leaves the FILE exactly as it was.
namespace bettercad::cli {

namespace {

using materials::MechanicalPropertyKind;
using materials::ThermalPropertyKind;

/// Applies whichever metadata options were given to @p definition, and says
/// whether any was. What is not given is left exactly as it was: an edit that
/// blanked a standard nobody mentioned would be a silent data loss.
bool applyMetadata(const ParsedArguments& parsed, features::MaterialDefinition& definition) {
    bool changed = false;
    const auto set = [&](std::string_view option, std::string& field) {
        if (const auto value = parsed.value(option)) {
            field = std::string{*value};
            changed = true;
        }
    };
    set("--designation", definition.designation);
    set("--standard", definition.standard);
    set("--family", definition.family);
    set("--notes", definition.notes);
    return changed;
}

/// The trailing `<property> <value>` pairs of a command line, from @p first.
///
/// Pairs rather than sixteen options, because sixteen options is a table that
/// drifts from the model and a pair list is the model's own vocabulary used
/// directly. An odd count is the mistake it looks like and is named as one.
struct PropertyAssignment {
    PropertyKind kind;
    std::string_view value;
};

std::expected<std::vector<PropertyAssignment>, EditFailure>
parseAssignments(const std::vector<std::string_view>& positional, std::size_t first) {
    std::vector<PropertyAssignment> assignments;
    const std::size_t count = positional.size() - first;
    if (count % 2 != 0) {
        return std::unexpected(malformed(std::format(
            "'{}' has no value; properties are given in <property> <value> pairs", positional.back())));
    }
    for (std::size_t at = first; at < positional.size(); at += 2) {
        auto kind = parsePropertyCode(positional[at]);
        if (!kind) {
            return std::unexpected(fromParse(kind.error()));
        }
        assignments.push_back({*kind, positional[at + 1]});
    }
    return assignments;
}

/// Stores @p assignments into a definition's property blocks.
///
/// Used by material-create, where the material does not exist yet and there is
/// nothing qualified to call: validation happens when the command executes, on
/// the whole definition at once, which is the same check an edit gets.
std::optional<EditFailure> storeAll(const std::vector<PropertyAssignment>& assignments,
                                    features::MaterialDefinition& definition) {
    for (const PropertyAssignment& assignment : assignments) {
        if (const auto* mechanical = std::get_if<MechanicalPropertyKind>(&assignment.kind)) {
            if (auto stored = setProperty(definition.mechanical, *mechanical, assignment.value); !stored) {
                return fromParse(propertyCode(*mechanical), stored.error());
            }
        } else {
            const auto thermal = std::get<ThermalPropertyKind>(assignment.kind);
            if (auto stored = setProperty(definition.thermal, thermal, assignment.value); !stored) {
                return fromParse(propertyCode(thermal), stored.error());
            }
        }
    }
    return std::nullopt;
}

/// "Steel (object:1)".
std::string materialLabel(const Document& document, MaterialId id) { return label(document, ObjectId{id}); }

/// A material selector that did not resolve, as an edit failure.
///
/// InvalidArgument means the CLI could not read the selector -- exit 2. Not
/// found, ambiguous and wrong-type are all the document answering a
/// well-formed question -- exit 1. fromParse() already draws that line and is
/// used rather than a second copy of it.
EditFailure selectorFailure(const Error& error) { return coded(materialSelectorCode(error), error); }

// --- the verbs ----------------------------------------------------------

EditResult applyMaterialCreate(Document& document, Args args) {
    auto parsed = parseArguments(args, {{"--designation", true},
                                        {"--standard", true},
                                        {"--family", true},
                                        {"--notes", true},
                                        {"--name", true}});
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    auto name = namedOr(*parsed, document, "Material");
    if (!name) {
        return std::unexpected(fromParse(name.error()));
    }
    auto assignments = parseAssignments(parsed->positional(), 0);
    if (!assignments) {
        return std::unexpected(assignments.error());
    }

    features::MaterialDefinition definition;
    (void)applyMetadata(*parsed, definition);
    if (auto stored = storeAll(*assignments, definition)) {
        return std::unexpected(*stored);
    }

    // The command validates the whole definition on execute and consumes no ID
    // if it refuses, so a rejected create does not perturb the IDs a later one
    // hands out.
    features::CreateMaterialCommand command{*name, definition};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Created {}", materialLabel(document, command.materialId()));
}

EditResult applyMaterialClone(Document& document, Args args) {
    auto parsed = parseArguments(args, {{"--name", true}});
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    if (parsed->positional().size() != 1) {
        return std::unexpected(malformed("expected one material to clone"));
    }
    auto source = resolveMaterial(document, parsed->positional()[0]);
    if (!source) {
        return std::unexpected(selectorFailure(source.error()));
    }
    auto name = namedOr(*parsed, document, "Material");
    if (!name) {
        return std::unexpected(fromParse(name.error()));
    }

    // A NEW IDENTITY, always, and nothing afterwards refers to the source: the
    // definition is copied by value, so editing either material cannot reach
    // the other. That is P15-CUSTOM-001's invariant and it is not restated
    // here -- cloneMaterial is what holds it.
    auto clone = features::cloneMaterial(document, *source, *name);
    if (!clone) {
        return std::unexpected(rejected(clone.error()));
    }
    return std::format("Cloned {} as {}", materialLabel(document, *source), materialLabel(document, *clone));
}

EditResult applyMaterialSet(Document& document, Args args) {
    auto parsed = parseArguments(args, {{"--designation", true},
                                        {"--standard", true},
                                        {"--family", true},
                                        {"--notes", true}});
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    if (parsed->positional().empty()) {
        return std::unexpected(malformed("expected a material"));
    }
    auto id = resolveMaterial(document, parsed->positional()[0]);
    if (!id) {
        return std::unexpected(selectorFailure(id.error()));
    }
    auto assignments = parseAssignments(parsed->positional(), 1);
    if (!assignments) {
        return std::unexpected(assignments.error());
    }

    std::vector<std::string> changes;
    // Metadata first, through the one supported way to change it. The current
    // values are carried forward untouched, so renaming a designation cannot
    // disturb a modulus.
    features::MaterialDefinition definition = features::findMaterial(document, *id)->definition();
    if (applyMetadata(*parsed, definition)) {
        if (auto set = features::setMaterialDefinition(document, *id, definition); !set) {
            return std::unexpected(rejected(set.error()));
        }
        changes.emplace_back("metadata");
    }

    // Then the values, one property at a time, through the setters that clear
    // the provenance of whatever they change.
    for (const PropertyAssignment& assignment : *assignments) {
        if (const auto* mechanical = std::get_if<MechanicalPropertyKind>(&assignment.kind)) {
            materials::MechanicalProperties properties = features::findMaterial(document, *id)->definition().mechanical;
            if (auto stored = setProperty(properties, *mechanical, assignment.value); !stored) {
                return std::unexpected(fromParse(propertyCode(*mechanical), stored.error()));
            }
            if (auto set = features::setMaterialMechanical(document, *id, properties); !set) {
                return std::unexpected(rejected(set.error()));
            }
            changes.emplace_back(propertyCode(*mechanical));
        } else {
            const auto thermal = std::get<ThermalPropertyKind>(assignment.kind);
            materials::ThermalProperties properties = features::findMaterial(document, *id)->definition().thermal;
            if (auto stored = setProperty(properties, thermal, assignment.value); !stored) {
                return std::unexpected(fromParse(propertyCode(thermal), stored.error()));
            }
            if (auto set = features::setMaterialThermal(document, *id, properties); !set) {
                return std::unexpected(rejected(set.error()));
            }
            changes.emplace_back(propertyCode(thermal));
        }
    }
    if (changes.empty()) {
        return std::unexpected(malformed("nothing to set; give --designation, --standard, --family, --notes, or "
                                         "<property> <value> pairs"));
    }

    std::string listed;
    for (const std::string& change : changes) {
        listed += listed.empty() ? "" : ", ";
        listed += change;
    }
    return std::format("Set {} of {}", listed, materialLabel(document, *id));
}

EditResult applyMaterialUnset(Document& document, Args args) {
    auto parsed = parseArguments(args, {});
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    if (parsed->positional().size() < 2) {
        return std::unexpected(malformed("expected a material and at least one property"));
    }
    auto id = resolveMaterial(document, parsed->positional()[0]);
    if (!id) {
        return std::unexpected(selectorFailure(id.error()));
    }

    std::string listed;
    for (std::size_t at = 1; at < parsed->positional().size(); ++at) {
        auto kind = parsePropertyCode(parsed->positional()[at]);
        if (!kind) {
            return std::unexpected(fromParse(kind.error()));
        }
        // REMOVAL IS NOT ZEROING. The property becomes Unknown -- a material
        // nobody has weighed -- and a consumer then fails saying it has no
        // density, rather than computing a massless solid.
        const auto removed = std::get_if<MechanicalPropertyKind>(&*kind) != nullptr
                                 ? features::removeMaterialProperty(document, *id,
                                                                    std::get<MechanicalPropertyKind>(*kind))
                                 : features::removeMaterialProperty(document, *id,
                                                                    std::get<ThermalPropertyKind>(*kind));
        if (!removed) {
            return std::unexpected(rejected(removed.error()));
        }
        listed += listed.empty() ? "" : ", ";
        listed += propertyCode(*kind);
    }
    return std::format("Unset {} of {}", listed, materialLabel(document, *id));
}

EditResult applyMaterialAssign(Document& document, Args args) {
    auto parsed = parseArguments(args, {});
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    if (parsed->positional().size() != 1) {
        return std::unexpected(malformed("expected one material"));
    }
    auto id = resolveMaterial(document, parsed->positional()[0]);
    if (!id) {
        return std::unexpected(selectorFailure(id.error()));
    }
    // BY ID. The selector may have been a name or a designation, but what is
    // stored is the MaterialId it resolved to, so a later rename cannot move
    // the assignment and a same-designation material cannot inherit it.
    features::AssignMaterialCommand command{*id};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Assigned {} to {}", materialLabel(document, *id), document.name());
}

EditResult applyMaterialUnassign(Document& document, Args args) {
    auto parsed = parseArguments(args, {});
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    if (!parsed->positional().empty()) {
        return std::unexpected(malformed(std::format("unexpected argument '{}'", parsed->positional().front())));
    }
    features::RemoveMaterialAssignmentCommand command;
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Removed the material assignment of {}", document.name());
}

EditResult applyMaterialDelete(Document& document, Args args) {
    auto parsed = parseArguments(args, {});
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    if (parsed->positional().size() != 1) {
        return std::unexpected(malformed("expected one material"));
    }
    auto id = resolveMaterial(document, parsed->positional()[0]);
    if (!id) {
        return std::unexpected(selectorFailure(id.error()));
    }
    const std::string named = materialLabel(document, *id);
    // Deleting an ASSIGNED material is allowed and leaves the assignment
    // Unresolved rather than silently rebinding it (P15-ASSIGN-001). That
    // policy is the qualified one and is not restated here.
    features::DeleteMaterialCommand command{*id};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Deleted {}", named);
}

// --- the table ----------------------------------------------------------

constexpr std::array kEdits{
    EditCommand{"material-create",
                "material-create <file.bcad> [--name <name>] [--designation <text>] [--standard <text>] "
                "[--family <text>] [--notes <text>] [<property> <value>]...",
                "Define a material. A bare number is SI; write a unit for another. Prints the ID it was given.",
                &applyMaterialCreate},
    EditCommand{"material-clone", "material-clone <file.bcad> <material> [--name <name>]",
                "Copy a material into a new one with a NEW identity. Editing either cannot reach the other.",
                &applyMaterialClone},
    EditCommand{"material-set",
                "material-set <file.bcad> <material> [--designation <text>] [--standard <text>] "
                "[--family <text>] [--notes <text>] [<property> <value>]...",
                "Change a material. What is not given is left alone; changing a value clears the provenance "
                "that described the old one.",
                &applyMaterialSet},
    EditCommand{"material-unset", "material-unset <file.bcad> <material> <property>...",
                "Make properties Unknown again, with the provenance that described them. Not zeroing: a "
                "consumer then reports the property missing.",
                &applyMaterialUnset},
    EditCommand{"material-assign", "material-assign <file.bcad> <material>",
                "Say what the part is made of. Stored by ID, so a later rename cannot move it.",
                &applyMaterialAssign},
    EditCommand{"material-unassign", "material-unassign <file.bcad>",
                "Leave the part with no material. The only way an assignment goes away -- deleting the "
                "material leaves it unresolved instead.",
                &applyMaterialUnassign},
    EditCommand{"material-delete", "material-delete <file.bcad> <material>",
                "Remove a material. An assignment to it becomes unresolved, keeping the intent, and the ID is "
                "never reused.",
                &applyMaterialDelete},
};

} // namespace

std::span<const EditCommand> materialEditCommands() noexcept { return kEdits; }

} // namespace bettercad::cli
