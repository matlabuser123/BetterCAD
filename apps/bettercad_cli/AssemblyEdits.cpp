#include "EditSupport.hpp"
#include "Edits.hpp"

#include "Commands.hpp"
#include "Selectors.hpp"

#include <bettercad/assembly/Commands.hpp>
#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Configurations.hpp>
#include <bettercad/assembly/Mate.hpp>
#include <bettercad/assembly/Mates.hpp>
#include <bettercad/core/Naming.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/Placement.hpp>
#include <bettercad/io/DocumentFile.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <format>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace bettercad::cli {

namespace {




/// A name the engineer gave, checked only for being a name at all -- whether
/// it is free is the document's question and is left to it.

// --- placement ----------------------------------------------------------

constexpr std::array<std::string_view, 3> kTranslationOptions{"--x", "--y", "--z"};
constexpr std::array<std::string_view, 3> kRotationOptions{"--rx", "--ry", "--rz"};

/// Whether @p text names a parameter rather than being a literal value.
///
/// The two can never be confused: a quantity starts with a digit, a sign or a
/// point, and a parameter name always starts with a letter or an underscore
/// (validateIdentifier). This is the same disjointness the selector rule
/// rests on (ADR-009).
bool namesParameter(std::string_view text) noexcept {
    return !text.empty() && (std::isalpha(static_cast<unsigned char>(text.front())) != 0 || text.front() == '_');
}

Result<ParameterId> parameterNamed(const Document& document, std::string_view name, std::string_view option) {
    const Parameter* found = document.parameters().findByName(name);
    if (found == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("{}: this document has no parameter named '{}'", option, name));
    }
    // The dimension is NOT checked here. resolvePlacement() reports a
    // mismatch when the placement is resolved, and restating the rule in the
    // CLI would be a second place for it to be wrong.
    return found->id();
}

/// Applies `--x`/`--y`/`--z` and `--rx`/`--ry`/`--rz` to @p placement.
/// Returns whether any of them was given.
Result<bool> applyPlacementOptions(const Document& document, const ParsedArguments& parsed,
                                   ComponentPlacement& placement) {
    bool any = false;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (const auto given = parsed.value(kTranslationOptions[axis])) {
            any = true;
            if (namesParameter(*given)) {
                auto parameter = parameterNamed(document, *given, kTranslationOptions[axis]);
                if (!parameter) {
                    return std::unexpected(parameter.error());
                }
                placement.translationParameters[axis] = *parameter;
            } else {
                auto value = parseLength(*given, units::mm);
                if (!value) {
                    return makeError(value.error().code,
                                     std::format("{}: {}", kTranslationOptions[axis], value.error().message));
                }
                placement.translation[axis] = *value;
                placement.translationParameters[axis] = std::nullopt;
            }
        }
        if (const auto given = parsed.value(kRotationOptions[axis])) {
            any = true;
            if (namesParameter(*given)) {
                auto parameter = parameterNamed(document, *given, kRotationOptions[axis]);
                if (!parameter) {
                    return std::unexpected(parameter.error());
                }
                placement.rotationParameters[axis] = *parameter;
            } else {
                auto value = parseAngle(*given, units::deg);
                if (!value) {
                    return makeError(value.error().code,
                                     std::format("{}: {}", kRotationOptions[axis], value.error().message));
                }
                placement.rotation[axis] = *value;
                placement.rotationParameters[axis] = std::nullopt;
            }
        }
    }
    return any;
}

std::string describePlacement(const Document& document, const ComponentPlacement& placement) {
    if (isIdentity(placement) && std::ranges::none_of(placement.translationParameters,
                                                      [](const auto& p) { return p.has_value(); }) &&
        std::ranges::none_of(placement.rotationParameters, [](const auto& p) { return p.has_value(); })) {
        return "at the origin";
    }
    const auto axis = [&](std::size_t i, bool rotation) {
        const auto& bound = rotation ? placement.rotationParameters[i] : placement.translationParameters[i];
        if (bound) {
            const Parameter* parameter = document.parameters().find(*bound);
            return parameter == nullptr ? std::format("{}", *bound) : parameter->name();
        }
        return rotation ? std::format("{:.6g} deg", placement.rotation[i].in(units::deg))
                        : std::format("{:.6g} mm", placement.translation[i].in(units::mm));
    };
    return std::format("moved ({}, {}, {}), turned ({}, {}, {})", axis(0, false), axis(1, false), axis(2, false),
                       axis(0, true), axis(1, true), axis(2, true));
}

/// The six placement options. A helper rather than a shared constant because
/// parseArguments() takes an initializer_list, and one built inside the call
/// is exactly as long-lived as it needs to be: the parse stores views into
/// `args`, never into the option list.
Result<ParsedArguments> parsePlacementArguments(Args args) {
    return parseArguments(
        args, {{"--x", true}, {"--y", true}, {"--z", true}, {"--rx", true}, {"--ry", true}, {"--rz", true}});
}

/// The options every mate command shares.
Result<ParsedArguments> parseMateArguments(Args args) {
    return parseArguments(args, {{"--type", true},
                                 {"--name", true},
                                 {"--component", true},
                                 {"--a", true},
                                 {"--b", true},
                                 {"--a2", true},
                                 {"--b2", true},
                                 {"--distance", true},
                                 {"--angle", true}});
}

// --- component-add ------------------------------------------------------

EditResult applyComponentAdd(Document& document, Args args) {
    auto parsed = parseArguments(args, {{"--part", true},
                                        {"--name", true},
                                        {"--x", true},
                                        {"--y", true},
                                        {"--z", true},
                                        {"--rx", true},
                                        {"--ry", true},
                                        {"--rz", true}});
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    if (!parsed->positional().empty()) {
        return std::unexpected(malformed(std::format("unexpected argument '{}'", parsed->positional().front())));
    }
    const auto part = parsed->value("--part");
    if (!part) {
        return std::unexpected(malformed("--part is required: the object this component places"));
    }
    auto partId = resolveObject(document, *part);
    if (!partId) {
        return std::unexpected(fromParse(partId.error()));
    }
    auto name = namedOr(*parsed, document, "Component");
    if (!name) {
        return std::unexpected(malformed(name.error().message));
    }

    assembly::ComponentDefinition definition{.part = ObjectReference{*partId}};
    auto placed = applyPlacementOptions(document, *parsed, definition.placement);
    if (!placed) {
        return std::unexpected(fromParse(placed.error()));
    }

    assembly::CreateComponentCommand command{*name, definition};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Created {} placing {}, {}", label(document, command.componentId()),
                       label(document, *partId), describePlacement(document, definition.placement));
}

// --- component-remove ---------------------------------------------------

EditResult applyComponentRemove(Document& document, Args args) {
    auto parsed = parseArguments(args, {});
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    if (parsed->positional().size() != 1) {
        return std::unexpected(malformed("expected one component"));
    }
    auto component = resolveComponent(document, parsed->positional().front());
    if (!component) {
        return std::unexpected(fromParse(component.error()));
    }
    // A mate that named a component that is gone would load and be reported
    // as broken (P13-PERSIST-001), which is the right behaviour for a file
    // that is already damaged and the wrong thing to create on purpose.
    // matesOf() exists for exactly this question.
    const std::vector<MateId> named = assembly::matesOf(document, *component);
    if (!named.empty()) {
        std::string list;
        for (const MateId mate : named) {
            list += list.empty() ? "" : ", ";
            list += label(document, mate);
        }
        return std::unexpected(rejected(std::format("{} is still mated by {}; remove {} first",
                                                    label(document, *component), list,
                                                    named.size() == 1 ? "it" : "them")));
    }

    const std::string described = label(document, *component);
    DeleteObjectCommand command{*component};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Removed {}", described);
}

// --- component-place ----------------------------------------------------

EditResult applyComponentPlace(Document& document, Args args) {
    auto parsed = parsePlacementArguments(args);
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    if (parsed->positional().size() != 1) {
        return std::unexpected(malformed("expected one component"));
    }
    auto component = resolveComponent(document, parsed->positional().front());
    if (!component) {
        return std::unexpected(fromParse(component.error()));
    }
    const assembly::Component* existing = assembly::findComponent(document, *component);
    // Edited in place: an option not given keeps what the component already
    // says, so moving one axis does not silently reset the other five.
    ComponentPlacement placement = existing->definition().placement;
    auto placed = applyPlacementOptions(document, *parsed, placement);
    if (!placed) {
        return std::unexpected(fromParse(placed.error()));
    }
    if (!*placed) {
        return std::unexpected(malformed("give at least one of --x --y --z --rx --ry --rz"));
    }

    assembly::SetComponentPlacementCommand command{*component, placement};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Placed {} {}", label(document, *component), describePlacement(document, placement));
}

// --- mates --------------------------------------------------------------

Result<assembly::MateType> parseMateType(std::string_view text) {
    using assembly::MateType;
    constexpr std::array<MateType, 11> kTypes{MateType::Fixed,       MateType::Coincident,
                                              MateType::Concentric,  MateType::Parallel,
                                              MateType::Perpendicular, MateType::Distance,
                                              MateType::Angle,       MateType::Revolute,
                                              MateType::Slider,      MateType::Cylindrical,
                                              MateType::Planar};
    for (const MateType type : kTypes) {
        if (toString(type) == text) {
            return type;
        }
    }
    std::string known;
    for (const MateType type : kTypes) {
        known += known.empty() ? "" : ", ";
        known += toString(type);
    }
    return makeError(ErrorCode::InvalidArgument,
                     std::format("'{}' is not a mate type; expected one of {}", text, known));
}

/// Applies the mate options present in @p parsed to @p definition, leaving
/// the rest as they were. `mate-add` starts from a default definition and
/// `mate-set` from the mate's own, which is what makes an edit an edit.
std::optional<EditFailure> applyMateOptions(const Document& document, const ParsedArguments& parsed,
                                            assembly::MateDefinition& definition) {
    if (const auto given = parsed.value("--type")) {
        auto type = parseMateType(*given);
        if (!type) {
            return malformed(type.error().message);
        }
        definition.type = *type;
    }
    if (const auto given = parsed.value("--component")) {
        auto component = resolveComponent(document, *given);
        if (!component) {
            return fromParse(component.error());
        }
        definition.component = *component;
    }
    const std::array<std::pair<std::string_view, std::optional<MateTarget>*>, 4> targets{
        {{"--a", &definition.a}, {"--b", &definition.b}, {"--a2", &definition.a2}, {"--b2", &definition.b2}}};
    for (const auto& [option, slot] : targets) {
        if (const auto given = parsed.value(option)) {
            auto target = parseMateTarget(document, *given);
            if (!target) {
                return fromParse(option, target.error());
            }
            *slot = *target;
        }
    }
    if (const auto given = parsed.value("--distance")) {
        auto value = parseLength(*given, units::mm);
        if (!value) {
            return malformed(std::format("--distance: {}", value.error().message));
        }
        definition.distance = *value;
    }
    if (const auto given = parsed.value("--angle")) {
        auto value = parseAngle(*given, units::deg);
        if (!value) {
            return malformed(std::format("--angle: {}", value.error().message));
        }
        definition.angle = *value;
    }
    return std::nullopt;
}

/// "coincident, Base:origin:xy to Arm:origin:xy" -- what the mate says, from
/// the definition rather than from what the command line happened to name.
std::string describeMate(const Document& document, const assembly::MateDefinition& definition) {
    std::string text{toString(definition.type)};
    if (definition.type == assembly::MateType::Fixed) {
        return std::format("{}, holding {}", text, label(document, definition.component));
    }
    if (definition.a && definition.b) {
        text += std::format(", relating {} to {}", formatMateTarget(document, *definition.a),
                            formatMateTarget(document, *definition.b));
    }
    if (definition.distance) {
        text += std::format(" at {:.6g} mm", definition.distance->in(units::mm));
    }
    if (definition.angle) {
        text += std::format(" at {:.6g} deg", definition.angle->in(units::deg));
    }
    return text;
}

EditResult applyMateAdd(Document& document, Args args) {
    auto parsed = parseMateArguments(args);
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    if (!parsed->positional().empty()) {
        return std::unexpected(malformed(std::format("unexpected argument '{}'", parsed->positional().front())));
    }
    if (!parsed->value("--type")) {
        return std::unexpected(malformed("--type is required: the kind of mate"));
    }
    auto name = namedOr(*parsed, document, "Mate");
    if (!name) {
        return std::unexpected(malformed(name.error().message));
    }

    assembly::MateDefinition definition{};
    if (auto failed = applyMateOptions(document, *parsed, definition)) {
        return std::unexpected(*failed);
    }

    assembly::CreateMateCommand command{*name, definition};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Created {}: {}", label(document, command.mateId()), describeMate(document, definition));
}

EditResult applyMateSet(Document& document, Args args) {
    auto parsed = parseMateArguments(args);
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    if (parsed->positional().size() != 1) {
        return std::unexpected(malformed("expected one mate"));
    }
    auto mate = resolveMate(document, parsed->positional().front());
    if (!mate) {
        return std::unexpected(fromParse(mate.error()));
    }
    if (parsed->value("--name")) {
        return std::unexpected(malformed("--name does not rename a mate; mate-set edits the constraint"));
    }
    // Edited in place, from what the mate already says.
    assembly::MateDefinition definition = assembly::findMate(document, *mate)->definition();
    if (auto failed = applyMateOptions(document, *parsed, definition)) {
        return std::unexpected(*failed);
    }

    assembly::SetMateDefinitionCommand command{*mate, definition};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Set {}: {}", label(document, *mate), describeMate(document, definition));
}

EditResult applyMateRemove(Document& document, Args args) {
    auto parsed = parseArguments(args, {});
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    if (parsed->positional().size() != 1) {
        return std::unexpected(malformed("expected one mate"));
    }
    auto mate = resolveMate(document, parsed->positional().front());
    if (!mate) {
        return std::unexpected(fromParse(mate.error()));
    }
    const std::string described = label(document, *mate);
    DeleteObjectCommand command{*mate};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Removed {}", described);
}

// --- suppression --------------------------------------------------------

/// Sets, clears or unsets suppression for whichever of a component and a mate
/// @p selector names.
///
/// @p state empty means "clear the override", which is the third state
/// P13-CONF-001 distinguishes: false says "in this build, present"; cleared
/// says "this build has no opinion".
EditResult applySuppression(Document& document, Args args, std::optional<bool> state, std::string_view verb) {
    auto parsed = parseArguments(args, {{"--configuration", true}});
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    if (parsed->positional().size() != 1) {
        return std::unexpected(malformed("expected one component or mate"));
    }
    auto object = resolveObject(document, parsed->positional().front());
    if (!object) {
        return std::unexpected(fromParse(object.error()));
    }
    const bool isComponent = document.findObjectAs<assembly::Component>(*object) != nullptr;
    const bool isMate = document.findObjectAs<assembly::Mate>(*object) != nullptr;
    if (!isComponent && !isMate) {
        return std::unexpected(rejected(std::format("{} has type '{}'; only a component or a mate is suppressed",
                                                    label(document, *object),
                                                    document.findObject(*object)->typeName())));
    }

    const auto configuration = parsed->value("--configuration");
    if (!configuration) {
        if (!state) {
            return std::unexpected(malformed(
                "suppress-clear needs --configuration: only a configuration's override can be cleared, and the "
                "base state is either suppressed or not"));
        }
        // The base state, which is the object's own definition rather than a
        // configuration's opinion of it.
        if (isComponent) {
            const ComponentId id = ComponentId::fromValue(object->value());
            assembly::ComponentDefinition definition = assembly::findComponent(document, id)->definition();
            definition.suppressed = *state;
            if (auto set = assembly::setComponentDefinition(document, id, definition); !set) {
                return std::unexpected(rejected(set.error()));
            }
        } else {
            const MateId id = MateId::fromValue(object->value());
            assembly::MateDefinition definition = assembly::findMate(document, id)->definition();
            definition.suppressed = *state;
            assembly::SetMateDefinitionCommand command{id, definition};
            if (auto failed = execute(document, command)) {
                return std::unexpected(*failed);
            }
        }
        return std::format("{} {}, in every configuration that has no opinion", verb, label(document, *object));
    }

    auto id = resolveConfiguration(document, *configuration);
    if (!id) {
        return std::unexpected(fromParse(id.error()));
    }
    if (isComponent) {
        assembly::SuppressComponentCommand command{*id, ComponentId::fromValue(object->value()), state};
        if (auto failed = execute(document, command)) {
            return std::unexpected(*failed);
        }
    } else {
        assembly::SuppressMateCommand command{*id, MateId::fromValue(object->value()), state};
        if (auto failed = execute(document, command)) {
            return std::unexpected(*failed);
        }
    }
    return std::format("{} {} in configuration '{}'", verb, label(document, *object), *configuration);
}

EditResult applySuppress(Document& document, Args args) {
    return applySuppression(document, args, true, "Suppressed");
}

EditResult applyUnsuppress(Document& document, Args args) {
    return applySuppression(document, args, false, "Unsuppressed");
}

EditResult applySuppressClear(Document& document, Args args) {
    return applySuppression(document, args, std::nullopt, "Cleared the suppression of");
}

// --- configurations -----------------------------------------------------

EditResult applyConfigurationAdd(Document& document, Args args) {
    auto parsed = parseArguments(args, {});
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    if (parsed->positional().size() != 1) {
        return std::unexpected(malformed("expected one configuration name"));
    }
    const std::string name{parsed->positional().front()};
    CreateConfigurationCommand command{name};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Created configuration '{}'", name);
}

EditResult applyConfigurationActivate(Document& document, Args args) {
    auto parsed = parseArguments(args, {{"--none", false}});
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    if (parsed->has("--none")) {
        if (!parsed->positional().empty()) {
            return std::unexpected(malformed("--none takes no configuration name"));
        }
        SetActiveConfigurationCommand command{std::nullopt};
        if (auto failed = execute(document, command)) {
            return std::unexpected(*failed);
        }
        return std::string{"Activated no configuration; the base values are in force"};
    }
    if (parsed->positional().size() != 1) {
        return std::unexpected(malformed("expected one configuration name, or --none"));
    }
    auto id = resolveConfiguration(document, parsed->positional().front());
    if (!id) {
        return std::unexpected(fromParse(id.error()));
    }
    SetActiveConfigurationCommand command{*id};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Activated configuration '{}'", parsed->positional().front());
}

// --- the table ----------------------------------------------------------

constexpr std::array kEdits{
    EditCommand{"component-add",
                "component-add <file.bcad> --part <selector> [--name <name>] [--x|--y|--z <length|parameter>] "
                "[--rx|--ry|--rz <angle|parameter>]",
                "Place an instance of a part. Prints the ID it was given.", &applyComponentAdd},
    EditCommand{"component-place",
                "component-place <file.bcad> <selector> [--x|--y|--z <length|parameter>] "
                "[--rx|--ry|--rz <angle|parameter>]",
                "Move a component, by editing its placement intent. Axes not given are left alone.",
                &applyComponentPlace},
    EditCommand{"component-remove", "component-remove <file.bcad> <selector>",
                "Remove a component. Refused while a mate still names it.", &applyComponentRemove},
    EditCommand{"mate-add",
                "mate-add <file.bcad> --type <kind> [--name <name>] [--component <selector>] [--a <target>] "
                "[--b <target>] [--a2 <target>] [--b2 <target>] [--distance <length>] [--angle <angle>]",
                "Constrain two components. Prints the ID it was given.", &applyMateAdd},
    EditCommand{"mate-set",
                "mate-set <file.bcad> <selector> [--type <kind>] [--component <selector>] [--a <target>] "
                "[--b <target>] [--a2 <target>] [--b2 <target>] [--distance <length>] [--angle <angle>]",
                "Edit a mate. What is not given is left as it was.", &applyMateSet},
    EditCommand{"mate-remove", "mate-remove <file.bcad> <selector>",
                "Remove a mate. The components it related are untouched.", &applyMateRemove},
    EditCommand{"suppress", "suppress <file.bcad> <selector> [--configuration <name>]",
                "Suppress a component or a mate, in one configuration or at the base.", &applySuppress},
    EditCommand{"unsuppress", "unsuppress <file.bcad> <selector> [--configuration <name>]",
                "Unsuppress a component or a mate.", &applyUnsuppress},
    EditCommand{"suppress-clear", "suppress-clear <file.bcad> <selector> --configuration <name>",
                "Clear a configuration's suppression override, so the base state applies again.",
                &applySuppressClear},
    EditCommand{"configuration-add", "configuration-add <file.bcad> <name>", "Add a configuration with no overrides.",
                &applyConfigurationAdd},
    EditCommand{"configuration-activate", "configuration-activate <file.bcad> <name>|--none",
                "Set which configuration the document is in.", &applyConfigurationActivate},
};

} // namespace

std::span<const EditCommand> assemblyEditCommands() noexcept { return kEdits; }


ExitCode runEdit(const EditCommand& command, Args args, std::ostream& out, std::ostream& err) {
    if (args.empty()) {
        return usageError(command.name, command.usage, "expected a document file", err);
    }
    const std::filesystem::path path = pathFromArgument(args.front());
    auto document = io::loadDocument(path);
    if (!document) {
        return failure(command.name, document.error().message, err);
    }
    auto applied = command.apply(*document, args.subspan(1));
    if (!applied) {
        // Nothing has been written, and nothing will be: the loaded copy is
        // discarded and the file on disk is exactly as it was.
        return applied.error().usage ? usageError(command.name, command.usage, applied.error().message, err)
                                     : failure(command.name, applied.error().message, err);
    }
    if (auto saved = io::saveDocument(*document, path); !saved) {
        return failure(command.name, saved.error().message, err);
    }
    // Two lines: what changed, then what was written. The same shape batch
    // uses, and it keeps "the edit succeeded" distinct from "the file was
    // saved", which are separate failures.
    out << *applied << "\n" << std::format("Wrote {}\n", displayPath(path));
    return ExitCode::Success;
}

} // namespace bettercad::cli
