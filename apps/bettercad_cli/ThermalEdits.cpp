#include "EditSupport.hpp"
#include "Selectors.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/units/UnitCatalog.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/MeshingCommands.hpp>
#include <bettercad/thermal/ThermalAnalysisObject.hpp>
#include <bettercad/thermal/ThermalCommands.hpp>
#include <bettercad/thermal/ThermalData.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The thermal edits (P18-CLI-001).
//
// EACH ONE IS AN ADAPTER AND NOTHING ELSE, which is the rule StructuralEdits.cpp states and which holds here for the same
// reason: it parses, resolves a face through the shared grammar, builds the P18-CMD-001 command object and hands it to
// EditSupport's `execute()` -- so the CLI inherits that milestone's validation and its all-or-nothing execution (a refused
// edit moves no revision and writes no file) instead of restating either.
//
// So there is NO rule of any kind about a heat load or a boundary condition in this file. A duplicate id, a non-finite flux, a
// non-positive convection coefficient, a temperature below absolute zero, a steady analysis given an initial temperature, a
// transient one given none, and a removal of an id that is not there are each refused by the qualified validator reached through
// the command, with the diagnostic the application would show. A grep of this file for a threshold, a formula or a comparison
// against an engineering quantity finds none.
//
// AND NO DEFAULTS. There is no CLI flux, no CLI coefficient, no CLI ambient, no CLI initial temperature, no CLI time step and no
// CLI solver tolerance anywhere here. An absent `--mode` means `steady`, which is `ThermalAnalysisDefinition`'s own default and
// is expressed by leaving the field alone; an absent transient option leaves the optional empty, and the data model's
// mode-completeness rule says whether that is acceptable.
//
// TEMPERATURES ARE KELVIN. A bare number is K, `350K` is accepted, and there is no Celsius: the unit catalogue has none, a
// conversion that existed only in this file would be exactly the thermal-only parser the brief forbids, and `degC` therefore
// fails in the catalogue like any other unit it does not know. A temperature is an ABSOLUTE one (the core refuses one below 0 K).
//
// MESH-LOCAL IDENTITY CANNOT BE NAMED HERE, structurally rather than by policing: the only target these verbs accept is a CAD
// face through the shared `face:` grammar. There is no `--node`, no `--facet` and no `--element`, so none has a way in.
//
// REGISTERING THESE IN EditRegistry MAKES THEM SCRIPTABLE. `batch` drives the same table, so each is a batch verb in the same
// one-transaction-or-nothing script as a mate verb, with nothing added to BatchCommand.cpp.
namespace bettercad::cli {

namespace {

/// The document's one thermal analysis, or a diagnosis of why there is not exactly one.
///
/// NEVER A FIRST MATCH, for the reason `theAnalysis` in StructuralEdits.cpp gives: choosing between several needs a selector this
/// version does not have, and saying so by name beats editing whichever came first. The two codes are distinct so a script can
/// tell "there is none" from "there are several".
[[nodiscard]] std::expected<ThermalAnalysisId, EditFailure> theAnalysis(const Document& document) {
    const std::vector<ThermalAnalysisId> analyses = thermal::thermalAnalysisIds(document);
    if (analyses.empty()) {
        return std::unexpected(rejected(
            "no_thermal_analysis",
            Error{ErrorCode::NotFound,
                  "this document has no thermal analysis; thermal-analysis-add creates one"}));
    }
    if (analyses.size() > 1U) {
        return std::unexpected(
            rejected("ambiguous_thermal_analysis",
                     Error{ErrorCode::FailedPrecondition,
                           std::format("this document has {} thermal analyses, and choosing "
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

/// A positive integer identity from the command line. The same reading StructuralEdits.cpp gives an id: a plain run of digits,
/// at most eighteen of them, and anything else is a command-line fault (exit 2). The value 0 is the invalid id and is refused by
/// the core's own validator through the command, not here.
[[nodiscard]] std::expected<std::uint64_t, EditFailure> readId(std::string_view text,
                                                               std::string_view what) {
    if (text.empty() || !std::ranges::all_of(text, [](const char c) {
            return c >= '0' && c <= '9';
        })) {
        return std::unexpected(
            malformed(std::format("{} must be a positive integer, not '{}'", what, text)));
    }
    if (text.size() > 18U) {
        return std::unexpected(malformed(std::format("{} is too large: '{}'", what, text)));
    }
    std::uint64_t value = 0;
    for (const char c : text) {
        value = value * 10U + static_cast<std::uint64_t>(c - '0');
    }
    // THE LARGEST IDENTIFIER A DOCUMENT FILE CAN HOLD (2^53 - 1, P18-PERSIST-001). A larger one would pass every check here and in
    // the command and then fail at SAVE, so `batch --dry-run` would say yes to a script the real run refuses. It is refused where the
    // command line is read, as a command-line fault.
    constexpr std::uint64_t kLargestFileId = 9007199254740991ULL;
    if (value > kLargestFileId) {
        return std::unexpected(malformed(std::format("{} is larger than a document can hold ({}): '{}'", what, kLargestFileId, text)));
    }
    return value;
}

/// Runs @p command and reports a refusal under a STABLE MACHINE CODE derived from the core's own error category.
///
/// WHY NOT EditSupport's `execute()`. It reports the command's message and no code, which is right for the verbs that predate
/// P15-CLI-001's rule that "only the code is an interface", and wrong for a surface whose brief asks for structured diagnostics.
/// The categories are the core's (`ErrorCode`), so this adds a spelling and decides nothing: which edit is a duplicate, which id
/// is missing and which value is invalid are the command's and the validator's to say, as before. The message stays what an
/// engineer reads; the code is what a pipeline matches.
[[nodiscard]] std::optional<EditFailure> executeEdit(Document& document, Command& command,
                                                          std::string_view alreadyExistsCode = "duplicate_id") {
    if (auto done = command.execute(document); !done) {
        std::string_view code = "edit_refused";
        switch (done.error().code) {
        case ErrorCode::AlreadyExists:
            // An id, or -- for the analysis -- an object NAME that another object has; the caller says which.
            code = alreadyExistsCode;
            break;
        case ErrorCode::NotFound:
            code = "id_not_found";
            break;
        case ErrorCode::InvalidArgument:
        case ErrorCode::DimensionMismatch:
            code = "invalid_value";
            break;
        default:
            break;
        }
        return rejected(code, done.error());
    }
    return std::nullopt;
}

/// One quantity through the CLI's ONE quantity parser, with the option named in any message. `nan` and `inf` are refused by the
/// parser itself (a command-line fault); a finite value of the wrong sign or magnitude is the CORE's to refuse.
template <Dimension D>
[[nodiscard]] std::expected<Quantity<D>, EditFailure> readQuantity(std::string_view text,
                                                                   const Unit<D>& defaultUnit,
                                                                   std::string_view option) {
    auto value = parseQuantity<D>(text, defaultUnit);
    if (!value) {
        return std::unexpected(fromParse(option, value.error()));
    }
    return *value;
}

/// `final-only` / `every-step`, and the underscore spellings `thermal-settings` prints (`final_only`, `every_step`), so what a report
/// shows can be fed back in.
[[nodiscard]] std::expected<thermal::TransientOutputPolicy, EditFailure> readOutputPolicy(std::string_view text) {
    if (text == "final-only" || text == "final_only") {
        return thermal::TransientOutputPolicy::FinalOnly;
    }
    if (text == "every-step" || text == "every_step") {
        return thermal::TransientOutputPolicy::EveryStep;
    }
    return std::unexpected(malformed(std::format("'{}' is not an output policy; write final-only or every-step", text)));
}

EditResult applyAnalysisAdd(Document& document, Args args) {
    // THE WHOLE COMMAND LINE IS READ BEFORE THE DOCUMENT IS LOOKED AT, so one command line gets one exit code whatever the file holds: a
    // fault in the arguments is 2 for a document with an analysis and for one without.
    auto parsed = parseArguments(args, {{"--name", true},
                                        {"--mode", true},
                                        {"--initial-temperature", true},
                                        {"--time-step", true},
                                        {"--end-time", true},
                                        {"--output", true}});
    if (!parsed) {
        return std::unexpected(fromParse(parsed.error()));
    }
    if (!parsed->positional().empty()) {
        return std::unexpected(
            malformed(std::format("unexpected argument '{}'", parsed->positional().front())));
    }

    thermal::ThermalAnalysisDefinition definition;
    if (const auto mode = parsed->value("--mode")) {
        if (*mode == "steady") {
            definition.mode = thermal::ThermalAnalysisMode::Steady;
        } else if (*mode == "transient") {
            definition.mode = thermal::ThermalAnalysisMode::Transient;
        } else {
            return std::unexpected(malformed(
                std::format("'{}' is not an analysis mode; write steady or transient", *mode)));
        }
    }

    // THE TRANSIENT FIELDS ARE SET WHEN GIVEN AND LEFT ALONE WHEN NOT. Which combinations are valid is the DATA MODEL's rule
    // (`validate(ThermalAnalysisDefinition)` is mode-complete: a transient analysis carries an initial temperature and time
    // settings, a steady one carries neither), and it is applied by the command below, so a steady analysis given
    // `--initial-temperature` and a transient one given none are both refused by the core with its own message.
    if (const auto text = parsed->value("--initial-temperature")) {
        auto value = readQuantity<dimensions::temperature>(*text, units::K, "--initial-temperature");
        if (!value) {
            return std::unexpected(value.error());
        }
        definition.initialCondition = thermal::UniformInitialTemperature{*value};
    }
    const bool timeGiven = parsed->has("--time-step") || parsed->has("--end-time") || parsed->has("--output");
    if (timeGiven) {
        // --output IS REQUIRED WITH ANY TIME SETTING, and there is no default for it. The data model''s own default keeps only the last
        // state, and the heat balance of a backward-Euler step needs the states on both sides of it, so a multi-step analysis left at
        // that default can never be accepted. Choosing for the user would be a hidden default; failing at solve time would be a trap.
        if (!parsed->has("--output")) {
            return std::unexpected(malformed(
                "time settings need --output final-only|every-step: final-only keeps only the last state, which the heat balance "
                "cannot accept for more than one step, so say which you mean"));
        }
        thermal::TransientSettings settings;
        if (const auto text = parsed->value("--time-step")) {
            auto value = readQuantity<dimensions::time>(*text, units::s, "--time-step");
            if (!value) {
                return std::unexpected(value.error());
            }
            settings.timeStep = *value;
        }
        if (const auto text = parsed->value("--end-time")) {
            auto value = readQuantity<dimensions::time>(*text, units::s, "--end-time");
            if (!value) {
                return std::unexpected(value.error());
            }
            settings.endTime = *value;
        }
        auto output = readOutputPolicy(*parsed->value("--output"));
        if (!output) {
            return std::unexpected(output.error());
        }
        settings.output = *output;
        // An absent --time-step or --end-time stays the zero Time{}, which `validate(TransientSettings)` refuses ("0 < dt <=
        // end"): the CLI supplies no step and no end of its own.
        definition.transient = settings;
    }
    // The solver settings are left at the definition's own defaults. There is no CLI tolerance and no CLI pivot floor: P18-SOLVE-001
    // chose both with a recorded reason, and a CLI that offered its own would be a second opinion about numerical acceptance.
    if (const auto given = parsed->value("--name")) {
        if (auto valid = validateIdentifier(*given, "object"); !valid) {
            return std::unexpected(fromParse(valid.error()));
        }
    }

    // NOW THE DOCUMENT.
    if (!thermal::thermalAnalysisIds(document).empty()) {
        return std::unexpected(rejected("thermal_analysis_exists",
                                        Error{ErrorCode::AlreadyExists,
                                              "this document already has a thermal analysis"}));
    }
    auto control = theControl(document);
    if (!control) {
        return std::unexpected(control.error());
    }
    definition.mesh = *control;
    auto name = namedOr(*parsed, document, "Thermal");
    if (!name) {
        return std::unexpected(fromParse(name.error()));
    }

    thermal::CreateThermalAnalysisCommand command{*name, definition};
    if (auto failed = executeEdit(document, command, "duplicate_name")) {
        return std::unexpected(*failed);
    }
    return std::format("Added thermal analysis '{}' over {}, mode {}", *name,
                       label(document, ObjectId::fromValue(control->value())),
                       thermal::toString(definition.mode));
}
EditResult applyLoadAdd(Document& document, Args args) {
    auto parsed = parseArguments(args, {{"--flux", true}, {"--generation", true}, {"--face", true}});
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

    // EXACTLY ONE KIND, named by the option that carries its value. Counting first means "two kinds at once" and "no kind at
    // all" are both reported as what they are, rather than the first option found quietly winning.
    const int kinds = static_cast<int>(parsed->has("--flux")) + static_cast<int>(parsed->has("--generation"));
    if (kinds != 1) {
        return std::unexpected(malformed(
            "give exactly one of --flux <W/m^2> (with --face) or --generation <W/m^3>"));
    }

    // THE COMMAND LINE IS READ IN FULL -- shape, then values -- BEFORE THE DOCUMENT IS LOOKED AT, so one command line gets one exit
    // code whatever the file holds.
    std::optional<HeatFlux> flux;
    std::optional<VolumetricHeatSource> generation;
    std::optional<std::string_view> faceText;
    if (const auto fluxText = parsed->value("--flux")) {
        faceText = parsed->value("--face");
        if (!faceText) {
            return std::unexpected(malformed("a surface heat flux needs --face <face>"));
        }
        auto value = readQuantity<dimensions::heatFlux>(*fluxText, units::W_per_m2, "--flux");
        if (!value) {
            return std::unexpected(value.error());
        }
        flux = *value;
    } else {
        if (parsed->has("--face")) {
            return std::unexpected(malformed(
                "a volumetric heat generation acts on the whole body and takes no --face"));
        }
        auto value = readQuantity<dimensions::volumetricHeatSource>(*parsed->value("--generation"), units::W_per_m3,
                                                                    "--generation");
        if (!value) {
            return std::unexpected(value.error());
        }
        generation = *value;
    }

    // NOW THE DOCUMENT.
    auto analysis = theAnalysis(document);
    if (!analysis) {
        return std::unexpected(analysis.error());
    }
    std::optional<thermal::ThermalLoad> load;
    std::string described;
    if (flux) {
        // THE SHARED FACE GRAMMAR, the one drawings, mates, local mesh sizing and structural loads use. A thermal load's target
        // is a CAD face and never a NodeId, an ElementId or a facet: those belong to one generated mesh and would be wrong at the
        // next remesh.
        auto face = parseFaceReference(document, *faceText);
        if (!face) {
            return std::unexpected(coded("bad_face_reference", face.error()));
        }
        load = thermal::ThermalLoad{ThermalLoadId::fromValue(*id),
                                    thermal::SurfaceHeatFluxLoad{.face = *face, .flux = *flux}};
        described = std::format("surface heat flux {} W/m^2 into the body on {}", flux->si(), *faceText);
    } else {
        load = thermal::ThermalLoad{ThermalLoadId::fromValue(*id),
                                    thermal::VolumetricHeatSourceLoad{.generation = *generation}};
        described = std::format("volumetric heat generation {} W/m^3", generation->si());
    }

    thermal::AddThermalLoadCommand command{*analysis, *load};
    if (auto failed = executeEdit(document, command)) {
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

    thermal::RemoveThermalLoadCommand command{*analysis, ThermalLoadId::fromValue(*id)};
    if (auto failed = executeEdit(document, command)) {
        // An id the analysis does not have is carried through with the command's own message. NOT a silent no-op: asking to remove
        // something that is not there is a mistake worth reporting, and the exit status says so.
        return std::unexpected(*failed);
    }
    return std::format("Removed load {}", *id);
}

EditResult applyBoundaryAdd(Document& document, Args args) {
    auto parsed = parseArguments(
        args, {{"--temperature", true}, {"--convection", true}, {"--ambient", true}});
    if (!parsed) {
        return std::unexpected(fromParse(parsed.error()));
    }
    if (parsed->positional().size() != 2) {
        return std::unexpected(malformed("expected a boundary condition id and a face"));
    }
    auto id = readId(parsed->positional()[0], "a boundary condition id");
    if (!id) {
        return std::unexpected(id.error());
    }

    // THE COMMAND LINE IS READ IN FULL BEFORE THE DOCUMENT IS LOOKED AT (see applyLoadAdd).
    const int kinds =
        static_cast<int>(parsed->has("--temperature")) + static_cast<int>(parsed->has("--convection"));
    if (kinds != 1) {
        return std::unexpected(malformed(
            "give exactly one of --temperature <K> or --convection <W/(m^2 K)> with --ambient <K>"));
    }
    std::optional<Temperature> temperature;
    std::optional<HeatTransferCoefficient> coefficient;
    std::optional<Temperature> ambient;
    if (const auto temperatureText = parsed->value("--temperature")) {
        if (parsed->has("--ambient")) {
            return std::unexpected(
                malformed("a prescribed temperature has no ambient; --ambient belongs to --convection"));
        }
        auto value = readQuantity<dimensions::temperature>(*temperatureText, units::K, "--temperature");
        if (!value) {
            return std::unexpected(value.error());
        }
        temperature = *value;
    } else {
        const auto ambientText = parsed->value("--ambient");
        if (!ambientText) {
            return std::unexpected(malformed("a convection condition needs --ambient <K>"));
        }
        auto h = readQuantity<dimensions::heatTransferCoefficient>(*parsed->value("--convection"), units::W_per_m2_K,
                                                                   "--convection");
        if (!h) {
            return std::unexpected(h.error());
        }
        auto t = readQuantity<dimensions::temperature>(*ambientText, units::K, "--ambient");
        if (!t) {
            return std::unexpected(t.error());
        }
        coefficient = *h;
        ambient = *t;
    }

    // NOW THE DOCUMENT.
    auto analysis = theAnalysis(document);
    if (!analysis) {
        return std::unexpected(analysis.error());
    }
    auto face = parseFaceReference(document, parsed->positional()[1]);
    if (!face) {
        return std::unexpected(coded("bad_face_reference", face.error()));
    }

    std::optional<thermal::ThermalBoundaryCondition> condition;
    std::string described;
    if (temperature) {
        condition = thermal::ThermalBoundaryCondition{
            ThermalBoundaryConditionId::fromValue(*id),
            thermal::PrescribedTemperature{.face = *face, .value = *temperature}};
        described = std::format("prescribed temperature {} K on {}", temperature->si(), parsed->positional()[1]);
    } else {
        condition = thermal::ThermalBoundaryCondition{
            ThermalBoundaryConditionId::fromValue(*id),
            thermal::ConvectionBoundary{.face = *face, .coefficient = *coefficient, .ambient = *ambient}};
        described = std::format("convection {} W/(m^2 K) to {} K on {}", coefficient->si(), ambient->si(),
                                parsed->positional()[1]);
    }

    thermal::AddThermalBoundaryConditionCommand command{*analysis, *condition};
    if (auto failed = executeEdit(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Added boundary condition {}: {}", *id, described);
}
EditResult applyBoundaryRemove(Document& document, Args args) {
    auto parsed = parseArguments(args, {});
    if (!parsed) {
        return std::unexpected(fromParse(parsed.error()));
    }
    if (parsed->positional().size() != 1) {
        return std::unexpected(malformed("expected one boundary condition id"));
    }
    auto id = readId(parsed->positional()[0], "a boundary condition id");
    if (!id) {
        return std::unexpected(id.error());
    }
    auto analysis = theAnalysis(document);
    if (!analysis) {
        return std::unexpected(analysis.error());
    }

    thermal::RemoveThermalBoundaryConditionCommand command{
        *analysis, ThermalBoundaryConditionId::fromValue(*id)};
    if (auto failed = executeEdit(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Removed boundary condition {}", *id);
}

constexpr std::array kEdits{
    EditCommand{"thermal-analysis-add",
                "thermal-analysis-add <file.bcad> [--name <name>] [--mode steady|transient] "
                "[--initial-temperature <K>] [--time-step <s>] [--end-time <s>] "
                "[--output final-only|every-step]",
                "Create the document's thermal analysis over its meshing control. A transient "
                "analysis needs --initial-temperature, --time-step, --end-time AND --output "
                "(final-only keeps only the last state, which the heat balance cannot accept "
                "for more than one step, so there is no default); a steady one takes none of "
                "them. The solver settings are P18-DATA-001's defaults; there is no CLI "
                "tolerance. Temperatures are absolute: a bare number is K and there is no "
                "Celsius.",
                &applyAnalysisAdd},
    EditCommand{"thermal-load-add",
                "thermal-load-add <file.bcad> <id> --flux <W/m^2> --face <face> | --generation "
                "<W/m^3>",
                "Add one heat load. --flux is a surface heat flux through a CAD face, positive "
                "INTO the body, and --generation a volumetric source in the whole body; either "
                "may be negative (a sink). Values accept units -- 500W/m^2, 1e5W/m^3 -- and a "
                "bare number is SI. A face is face:<feature>:<role>[:<entity>], the same way a "
                "dimension names one -- never a node or a facet.",
                &applyLoadAdd},
    EditCommand{"thermal-load-remove", "thermal-load-remove <file.bcad> <id>",
                "Remove one heat load by its id. Removing an id the analysis does not have is a "
                "failure, not a no-op.",
                &applyLoadRemove},
    EditCommand{"thermal-boundary-add",
                "thermal-boundary-add <file.bcad> <id> <face> --temperature <K> | --convection "
                "<W/(m^2 K)> --ambient <K>",
                "Add one boundary condition on a CAD face: a prescribed temperature, or "
                "convection to an ambient (q = h (T_ambient - T), positive into the body). A "
                "bare number is SI -- K, W/(m^2 K) -- and there is no Celsius. A unit with a "
                "space, such as 25W/(m^2 K), needs quoting in a shell.",
                &applyBoundaryAdd},
    EditCommand{"thermal-boundary-remove", "thermal-boundary-remove <file.bcad> <id>",
                "Remove one boundary condition by its id. Removing an id the analysis does not "
                "have is a failure, not a no-op.",
                &applyBoundaryRemove},
};

} // namespace

std::span<const EditCommand> thermalEditCommands() noexcept { return kEdits; }

} // namespace bettercad::cli
