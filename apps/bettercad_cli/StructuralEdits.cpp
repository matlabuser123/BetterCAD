#include "EditSupport.hpp"
#include "Selectors.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/units/UnitCatalog.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/structural/StructuralAnalysisObject.hpp>
#include <bettercad/structural/StructuralCommands.hpp>
#include <bettercad/structural/StructuralLoad.hpp>
#include <bettercad/structural/StructuralRestraint.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The structural edits (P17-CLI-001).
//
// EACH ONE IS AN ADAPTER AND NOTHING ELSE, which is the rule MeshingEdits.cpp
// states for the meshing ones and which holds here for the same reason: it
// parses, resolves a selector, builds the P17-CMD-001 command object and hands
// it to EditSupport's execute() -- "so the CLI inherits that milestone's
// validation and its all-or-nothing execution instead of restating either".
//
// So there is NO rule of any kind in this file. A duplicate `LoadId`, a
// non-finite force, an empty component mask, a missing load on removal and an
// invalid mesh handle are each refused by the qualified validator reached
// through the command, with the diagnostic the GUI would show. A grep of this
// file for a threshold, a formula or a comparison against an engineering
// quantity finds none.
//
// AND NO DEFAULTS. There is no CLI force, no CLI pressure and no CLI solver
// tolerance anywhere here. An absent `--mode` means `linear_static`, which is
// `StructuralAnalysisDefinition`'s own default and is expressed by leaving the
// field alone rather than by a number this file knows.
//
// MESH-LOCAL IDENTITY CANNOT BE NAMED HERE, and that is structural rather than
// policed: the only load targets these verbs accept are a CAD face through the
// shared `face:` grammar and the whole body for gravity. There is no option
// spelled `--node`, no `--facet` and no coordinate selector, so a `NodeId`
// has no way in -- which also means `NodalForceLoad`, the one qualified load
// form that names one, is not creatable from the CLI. It is a fixture type
// (P17-LOAD-001) and ADR-048 refuses to persist it, so a CLI that could add
// one would be a CLI that produced unsavable documents.
//
// REGISTERING THESE IN EditRegistry MAKES THEM SCRIPTABLE. `batch` drives the
// same table, so each is a batch verb in the same one-transaction-or-nothing
// script as a mate verb, with nothing added to BatchCommand.cpp.
namespace bettercad::cli {

namespace {

/// The document's one structural analysis, or a diagnosis of why there is not
/// exactly one.
///
/// NEVER A FIRST MATCH. This is `theControl`'s shape, for the reason P16 gives
/// and the brief's §103 repeats: choosing between several needs a selector
/// this version does not have, and saying so by name beats analysing whichever
/// came first. The two codes are distinct so a script can tell "there is none"
/// from "there are several".
[[nodiscard]] std::expected<AnalysisId, EditFailure> theAnalysis(const Document& document) {
    const std::vector<AnalysisId> analyses = structural::structuralAnalysisIds(document);
    if (analyses.empty()) {
        return std::unexpected(rejected(
            "no_structural_analysis",
            Error{ErrorCode::NotFound,
                  "this document has no structural analysis; structural-analysis-add creates one"}));
    }
    if (analyses.size() > 1U) {
        return std::unexpected(
            rejected("ambiguous_structural_analysis",
                     Error{ErrorCode::FailedPrecondition,
                           std::format("this document has {} structural analyses, and choosing "
                                       "between them needs a selector this version does not have",
                                       analyses.size())}));
    }
    return analyses.front();
}

/// The document's one meshing control, which is what an analysis analyses.
[[nodiscard]] std::expected<MeshControlId, EditFailure> theControl(const Document& document) {
    const std::vector<MeshControlId> controls = meshing::meshControls(document);
    if (controls.empty()) {
        return std::unexpected(
            rejected("no_mesh_control",
                     Error{ErrorCode::NotFound,
                           "this document has no meshing control to analyse; mesh-control-add "
                           "creates one"}));
    }
    if (controls.size() > 1U) {
        return std::unexpected(rejected(
            "ambiguous_mesh_control",
            Error{ErrorCode::FailedPrecondition,
                  std::format("this document has {} meshing controls, and choosing between them "
                              "needs a selector this version does not have",
                              controls.size())}));
    }
    return controls.front();
}

/// A positive integer identity from the command line.
///
/// `LoadId` and `RestraintId` are the user's to choose (P17-CMD-001: the
/// caller supplies the id and a duplicate is refused by the command), so this
/// reads one and refuses anything that is not a plain positive integer. The
/// value 0 is the invalid id and is rejected by the core's own validator, not
/// here -- but a non-numeric token is a command-line fault and is exit 2.
[[nodiscard]] std::expected<std::uint64_t, EditFailure> readId(std::string_view text,
                                                               std::string_view what) {
    if (text.empty() || !std::ranges::all_of(text, [](const char c) {
            return c >= '0' && c <= '9';
        })) {
        return std::unexpected(
            malformed(std::format("{} must be a positive integer, not '{}'", what, text)));
    }
    std::uint64_t value = 0;
    for (const char c : text) {
        // Bounded by the digit count rather than by overflow arithmetic: a
        // twenty-digit id is a typo, and refusing it by length is simpler to
        // read than a multiplication that has to prove it did not wrap.
        if (text.size() > 18U) {
            return std::unexpected(
                malformed(std::format("{} is too large: '{}'", what, text)));
        }
        value = value * 10U + static_cast<std::uint64_t>(c - '0');
    }
    return value;
}

/// A restraint's component mask, from `ux`, `uy`, `uz`, `ux,uz`, ... or
/// `fixed`.
///
/// THE CANONICAL ORDER IS THE CORE'S AND NOT THIS PARSER'S. A mask is a
/// bitmask, so the order components are written in cannot survive into it and
/// `ux,uz` and `uz,ux` are the same value -- which is why this accepts either
/// without needing to sort anything.
///
/// An unknown spelling is a command-line fault (exit 2). An EMPTY mask is not
/// refused here: `validate(StructuralRestraint)` owns that rule -- "a
/// restraint that holds nothing at zero is not a weaker restraint, it is a
/// record whose intent cannot be acted on" -- and it is reached through the
/// command.
[[nodiscard]] std::expected<structural::RestraintComponents, EditFailure> parseComponents(
    std::string_view text) {
    if (text == "fixed") {
        return structural::RestraintComponents::fixed();
    }
    structural::RestraintComponents components;
    std::size_t at = 0;
    while (at <= text.size()) {
        const std::size_t comma = text.find(',', at);
        const std::string_view token =
            text.substr(at, comma == std::string_view::npos ? std::string_view::npos : comma - at);
        if (token == "ux") {
            components.add(structural::DofComponent::Ux);
        } else if (token == "uy") {
            components.add(structural::DofComponent::Uy);
        } else if (token == "uz") {
            components.add(structural::DofComponent::Uz);
        } else {
            return std::unexpected(malformed(std::format(
                "'{}' is not a displacement component; write ux, uy, uz, a comma-separated "
                "combination of them, or fixed",
                token)));
        }
        if (comma == std::string_view::npos) {
            break;
        }
        at = comma + 1;
    }
    return components;
}

/// Three comma-separated quantities, through the CLI's ONE quantity parser.
///
/// `parseQuantity` is dimension-generic and P15-CLI-001's comment says why
/// that matters: "a material property is therefore parsed by exactly the code
/// that parses a length, and cannot acquire a second spelling, a second unit
/// table or a second dimension check". So a traction accepts `0,0,-2.5MPa`
/// and `0,0,-2500000` as the same vector, a pressure accepts `2.5MPa`, and a
/// unit of the wrong dimension is refused by the catalogue rather than by
/// anything here.
///
/// `nan` and `inf` are NOT refused here. The canonical validator refuses them
/// -- `validate(StructuralLoad)` reports "carries a value that is not finite"
/// -- so the rule lives in one place and the CLI does not get a second opinion
/// about what finite means.
template <Dimension D>
[[nodiscard]] std::expected<std::array<double, 3>, EditFailure> parseTriple(
    std::string_view text, const Unit<D>& defaultUnit, std::string_view option) {
    std::array<double, 3> values{};
    std::size_t at = 0;
    for (std::size_t i = 0; i < 3U; ++i) {
        if (at > text.size()) {
            return std::unexpected(malformed(
                std::format("{} needs three comma-separated components, not '{}'", option, text)));
        }
        const std::size_t comma = text.find(',', at);
        const bool last = i == 2U;
        if (last != (comma == std::string_view::npos)) {
            return std::unexpected(malformed(
                std::format("{} needs three comma-separated components, not '{}'", option, text)));
        }
        const std::string_view token =
            text.substr(at, last ? std::string_view::npos : comma - at);
        auto value = parseQuantity<D>(token, defaultUnit);
        if (!value) {
            return std::unexpected(fromParse(option, value.error()));
        }
        values[i] = value->si();
        at = last ? text.size() + 1U : comma + 1U;
    }
    return values;
}

EditResult applyAnalysisAdd(Document& document, Args args) {
    auto parsed = parseArguments(args, {{"--name", true}, {"--mode", true}});
    if (!parsed) {
        return std::unexpected(fromParse(parsed.error()));
    }
    if (!parsed->positional().empty()) {
        return std::unexpected(
            malformed(std::format("unexpected argument '{}'", parsed->positional().front())));
    }
    if (!structural::structuralAnalysisIds(document).empty()) {
        return std::unexpected(
            rejected("structural_analysis_exists",
                     Error{ErrorCode::AlreadyExists,
                           "this document already has a structural analysis"}));
    }
    auto control = theControl(document);
    if (!control) {
        return std::unexpected(control.error());
    }

    structural::StructuralAnalysisDefinition definition;
    definition.mesh = *control;
    if (const auto mode = parsed->value("--mode")) {
        if (*mode == "linear-static") {
            definition.mode = structural::StructuralAnalysisMode::LinearStatic;
        } else if (*mode == "linear-static-with-gravity") {
            definition.mode = structural::StructuralAnalysisMode::LinearStaticWithGravity;
        } else {
            return std::unexpected(malformed(
                std::format("'{}' is not an analysis mode; write linear-static or "
                            "linear-static-with-gravity",
                            *mode)));
        }
    }
    // The solver settings are left at the definition's own defaults. There is
    // no CLI tolerance and no CLI pivot floor: P17-SOLVE-001 chose both with a
    // recorded reason, and a CLI that offered its own would be a second
    // opinion about numerical acceptance.
    auto name = namedOr(*parsed, document, "Study");
    if (!name) {
        return std::unexpected(fromParse(name.error()));
    }

    structural::CreateStructuralAnalysisCommand command{*name, definition};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Added structural analysis '{}' over {}, mode {}", *name,
                       label(document, ObjectId::fromValue(control->value())),
                       structural::toString(definition.mode));
}

EditResult applyLoadAdd(Document& document, Args args) {
    auto parsed = parseArguments(args, {{"--pressure", true},
                                        {"--traction", true},
                                        {"--gravity", true},
                                        {"--face", true}});
    if (!parsed) {
        return std::unexpected(fromParse(parsed.error()));
    }
    if (parsed->positional().size() != 1) {
        return std::unexpected(malformed("expected one load id"));
    }
    auto id = readId(parsed->positional()[0], "a load id");
    if (!id) {
        return std::unexpected(id.error());
    }
    auto analysis = theAnalysis(document);
    if (!analysis) {
        return std::unexpected(analysis.error());
    }

    // EXACTLY ONE KIND, named by the option that carries its value. Counting
    // first means "two kinds at once" and "no kind at all" are both reported
    // as what they are, rather than the first option found quietly winning.
    const int kinds = static_cast<int>(parsed->has("--pressure")) +
                      static_cast<int>(parsed->has("--traction")) +
                      static_cast<int>(parsed->has("--gravity"));
    if (kinds != 1) {
        return std::unexpected(malformed(
            "give exactly one of --pressure <Pa>, --traction <x,y,z in Pa> or "
            "--gravity <x,y,z in m/s2>"));
    }

    /// A face is required by the two that act on one, and refused for gravity.
    const auto faceOf = [&](std::string_view kind)
        -> std::expected<FaceName, EditFailure> {
        const auto text = parsed->value("--face");
        if (!text) {
            return std::unexpected(
                malformed(std::format("a {} load needs --face <face>", kind)));
        }
        // THE SHARED FACE GRAMMAR, the one drawings, mates and local mesh
        // sizing use. A load's target is a CAD face and never a NodeId, an
        // ElementId or a facet: those belong to one generated mesh and would
        // be wrong at the next remesh.
        auto face = parseFaceReference(document, *text);
        if (!face) {
            return std::unexpected(coded("bad_face_reference", face.error()));
        }
        return *face;
    };

    std::optional<structural::StructuralLoad> load;
    std::string described;
    if (const auto pressureText = parsed->value("--pressure")) {
        auto face = faceOf("pressure");
        if (!face) {
            return std::unexpected(face.error());
        }
        auto magnitude = parseQuantity<dimensions::pressure>(*pressureText, units::Pa);
        if (!magnitude) {
            return std::unexpected(fromParse("--pressure", magnitude.error()));
        }
        load = structural::StructuralLoad{
            LoadId::fromValue(*id),
            structural::PressureLoad{.face = *face, .magnitude = *magnitude}};
        described = std::format("pressure {} Pa on {}", magnitude->si(), *parsed->value("--face"));
    } else if (const auto tractionText = parsed->value("--traction")) {
        auto face = faceOf("traction");
        if (!face) {
            return std::unexpected(face.error());
        }
        auto vector = parseTriple<dimensions::pressure>(*tractionText, units::Pa, "--traction");
        if (!vector) {
            return std::unexpected(vector.error());
        }
        load = structural::StructuralLoad{
            LoadId::fromValue(*id),
            structural::SurfaceTractionLoad{
                .face = *face,
                .traction = Traction3D{.x = Pressure::fromSi((*vector)[0]),
                                       .y = Pressure::fromSi((*vector)[1]),
                                       .z = Pressure::fromSi((*vector)[2])}}};
        described = std::format("traction ({}, {}, {}) Pa on {}", (*vector)[0], (*vector)[1],
                                (*vector)[2], *parsed->value("--face"));
    } else {
        const auto gravityText = parsed->value("--gravity");
        if (parsed->has("--face")) {
            return std::unexpected(
                malformed("a gravity load acts on the whole body and takes no --face"));
        }
        auto vector =
            parseTriple<dimensions::acceleration>(*gravityText, units::m_per_s2, "--gravity");
        if (!vector) {
            return std::unexpected(vector.error());
        }
        load = structural::StructuralLoad{
            LoadId::fromValue(*id),
            structural::GravityLoad{.acceleration = Vector3D{
                                        .x = (*vector)[0], .y = (*vector)[1], .z = (*vector)[2]}}};
        described = std::format("gravity ({}, {}, {}) m/s2", (*vector)[0], (*vector)[1],
                                (*vector)[2]);
    }

    structural::AddStructuralLoadCommand command{*analysis, *load};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Added load {}: {}", *id, described);
}

EditResult applyLoadRemove(Document& document, Args args) {
    auto parsed = parseArguments(args, {});
    if (!parsed) {
        return std::unexpected(fromParse(parsed.error()));
    }
    if (parsed->positional().size() != 1) {
        return std::unexpected(malformed("expected one load id"));
    }
    auto id = readId(parsed->positional()[0], "a load id");
    if (!id) {
        return std::unexpected(id.error());
    }
    auto analysis = theAnalysis(document);
    if (!analysis) {
        return std::unexpected(analysis.error());
    }

    structural::RemoveStructuralLoadCommand command{*analysis, LoadId::fromValue(*id)};
    if (auto failed = execute(document, command)) {
        // NotFound for an id the analysis does not have, carried through with
        // the command's own message. NOT a silent no-op: asking to remove
        // something that is not there is a mistake worth reporting, and the
        // brief's §104 requires a nonzero exit for it.
        return std::unexpected(*failed);
    }
    return std::format("Removed load {}", *id);
}

EditResult applyRestraintAdd(Document& document, Args args) {
    auto parsed = parseArguments(args, {});
    if (!parsed) {
        return std::unexpected(fromParse(parsed.error()));
    }
    if (parsed->positional().size() != 3) {
        return std::unexpected(malformed("expected a restraint id, a face and the components"));
    }
    auto id = readId(parsed->positional()[0], "a restraint id");
    if (!id) {
        return std::unexpected(id.error());
    }
    auto analysis = theAnalysis(document);
    if (!analysis) {
        return std::unexpected(analysis.error());
    }
    auto face = parseFaceReference(document, parsed->positional()[1]);
    if (!face) {
        return std::unexpected(coded("bad_face_reference", face.error()));
    }
    auto components = parseComponents(parsed->positional()[2]);
    if (!components) {
        return std::unexpected(components.error());
    }

    structural::AddStructuralRestraintCommand command{
        *analysis, structural::StructuralRestraint{RestraintId::fromValue(*id), *face,
                                                   *components}};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Added restraint {} on {}, holding {}", *id, parsed->positional()[1],
                       structural::toString(*components));
}

EditResult applyRestraintRemove(Document& document, Args args) {
    auto parsed = parseArguments(args, {});
    if (!parsed) {
        return std::unexpected(fromParse(parsed.error()));
    }
    if (parsed->positional().size() != 1) {
        return std::unexpected(malformed("expected one restraint id"));
    }
    auto id = readId(parsed->positional()[0], "a restraint id");
    if (!id) {
        return std::unexpected(id.error());
    }
    auto analysis = theAnalysis(document);
    if (!analysis) {
        return std::unexpected(analysis.error());
    }

    structural::RemoveStructuralRestraintCommand command{*analysis,
                                                         RestraintId::fromValue(*id)};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Removed restraint {}", *id);
}

constexpr std::array kEdits{
    EditCommand{"structural-analysis-add",
                "structural-analysis-add <file.bcad> [--name <name>] [--mode "
                "linear-static|linear-static-with-gravity]",
                "Create the document's structural analysis over its meshing control. The solver "
                "settings are P17-SOLVE-001's defaults; there is no CLI tolerance.",
                &applyAnalysisAdd},
    EditCommand{"structural-load-add",
                "structural-load-add <file.bcad> <id> --pressure <Pa> --face <face> | --traction "
                "<x,y,z in Pa> --face <face> | --gravity <x,y,z in m/s2>",
                "Add one load. Values accept units -- 2.5MPa or 2500000 -- and a bare number is "
                "Pa for a pressure or traction and m/s2 for gravity. A face is "
                "face:<feature>:<role>[:<entity>], the same way a dimension names one -- never a "
                "node or a facet.",
                &applyLoadAdd},
    EditCommand{"structural-load-remove", "structural-load-remove <file.bcad> <id>",
                "Remove one load by its id. Removing an id the analysis does not have is a "
                "failure, not a no-op.",
                &applyLoadRemove},
    EditCommand{"structural-restraint-add",
                "structural-restraint-add <file.bcad> <id> <face> ux|uy|uz|<combination>|fixed",
                "Hold displacement components at zero on one CAD face. Components are global "
                "axes; write them comma-separated in any order, or 'fixed' for all three.",
                &applyRestraintAdd},
    EditCommand{"structural-restraint-remove", "structural-restraint-remove <file.bcad> <id>",
                "Remove one restraint by its id. Removing an id the analysis does not have is a "
                "failure, not a no-op.",
                &applyRestraintRemove},
};

} // namespace

std::span<const EditCommand> structuralEditCommands() noexcept { return kEdits; }

} // namespace bettercad::cli
