#include "io/json/ObjectJson.hpp"

#include <array>
#include <cstddef>
#include <format>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The persisted form of a thermal analysis's canonical intent (P18-PERSIST-001).
//
// WHAT IS PERSISTED: the meshing control analysed, the analysis mode, the loads
// with their CAD targets and signed values, the boundary conditions with their
// CAD targets and values, the uniform initial temperature (transient only), the
// solver settings and the transient settings (transient only).
//
// WHAT IS NOT, and could not be even deliberately: `NodeId`, `ElementId`,
// `ThermalDofIndex`, `ThermalFreeEquationIndex`, boundary facets, prepared loads,
// prepared boundary conditions, a DOF map, K, C, Q, a factorisation, a
// temperature, a temperature history, a gradient, a heat flux, a reaction, a heat
// balance, a validation report or a render buffer.
//
// THE EXCLUSION IS STRUCTURAL, which is the argument P16-PERSIST-001 and
// P17-PERSIST-001 made and the reason this file is short. A
// `ThermalAnalysisDefinition` has exactly seven members -- a `MeshControlId`, a
// mode, the loads, the boundary conditions, an optional initial condition, the
// solver settings and optional transient settings -- and none for a derived
// thing. A thermal RESULT does not live on the document object at all: it is
// built by the solve, post and balance functions and held by the caller. The
// writer's only argument is the analysis object, so there is no expression in
// this file that could depend on whether a solve has run:
//
//     save  ->  thermal-analysis intent
//     load  ->  restore intent; regenerate the mesh through P16; solve when asked
//
// MATERIAL IS NOT HERE. The analysis names a meshing control, the control names
// a body, and the body's material is P15's document-level assignment (P18-MAT-001:
// P15 is the sole authority for k, rho and cp). Copying a conductivity into this
// schema would be a second authority.
//
// UNITS ARE FIXED SI AND NAMED BY THE KEY, not by a unit string a reader could
// get wrong: `flux` is W/m^2, `generation` is W/m^3, `temperature`, `ambient`
// and `initial_temperature` are K (absolute; there is no Celsius anywhere),
// `coefficient` is W/(m^2 K), `time_step` and `end_time` are s. The writer reads
// `.si()` and the reader calls `fromSi`, so no conversion factor exists in the
// file to be wrong.
//
// VALIDATION IS THE CORE'S. Reading ends in `ThermalAnalysis::create`, which
// calls `validate(ThermalAnalysisDefinition)` -- loads, conditions, duplicate
// identities, the mode-complete rule and the settings. Unlike P17's equivalent,
// that function covers the records and the identities, but this file still calls
// the per-record validators itself so that a refusal names the record's PATH in
// the file ("data.loads[1]: ...") rather than only the definition. They are the
// core's own functions, called and not copied. The duplicate-identity check is
// repeated here for the same reason: the path.
//
// FACE REFERENCES GO THROUGH faceNameToJson, the canonical encoding datums,
// drafts, shells, dimensions, annotations, meshing controls and structural
// analyses already use. An unresolvable reference is preserved exactly; nothing
// here can rebind it, because the reader receives no document, no regenerator
// and no mesher.
//
// DETERMINISM. `Json` is `nlohmann::ordered_json`, so key order is this file's
// insertion order and array order is the definition's own order, which
// `ThermalAnalysisDefinition` declares is the user's and preserved. Floating
// values go through the same shortest-round-trip formatter every other object
// uses, so the same canonical state gives the same bytes.
namespace bettercad::io::detail {

namespace {

// The dispatch in DocumentJson.cpp compares against the literal
// "thermal-analysis", never against kTypeName: binding a reference to a
// dll-imported constexpr static does not link in a shared build (components,
// materials, meshing controls and structural analyses all found it). This keeps
// the two from drifting.
static_assert(thermal::ThermalAnalysis::kTypeName == std::string_view{"thermal-analysis"});

/// A stable file key for every analysis mode. STRINGS, NOT INTEGERS and not
/// `toString(...)`: an integer silently becomes another mode the day an
/// enumerator is inserted, and a display string is text a later milestone may
/// reword. The guard against a WRONG entry is ThermalPersistence_EveryEnumeratorRoundTrips.
constexpr std::array<std::pair<thermal::ThermalAnalysisMode, std::string_view>, 2> kModes{{
    {thermal::ThermalAnalysisMode::Steady, "steady"},
    {thermal::ThermalAnalysisMode::Transient, "transient"},
}};

constexpr std::array<std::pair<thermal::ThermalLoadKind, std::string_view>, 2> kLoadKinds{{
    {thermal::ThermalLoadKind::SurfaceHeatFlux, "surface_heat_flux"},
    {thermal::ThermalLoadKind::VolumetricHeatSource, "volumetric_heat_source"},
}};

constexpr std::array<std::pair<thermal::ThermalBoundaryConditionKind, std::string_view>, 2>
    kConditionKinds{{
        {thermal::ThermalBoundaryConditionKind::PrescribedTemperature, "prescribed_temperature"},
        {thermal::ThermalBoundaryConditionKind::Convection, "convection"},
    }};

constexpr std::array<std::pair<thermal::TransientOutputPolicy, std::string_view>, 2> kOutputs{{
    {thermal::TransientOutputPolicy::FinalOnly, "final_only"},
    {thermal::TransientOutputPolicy::EveryStep, "every_step"},
}};

/// The name for @p value from @p table, or "unknown" for a value the table does
/// not carry. A value reaching "unknown" would serialize silently and then fail
/// to load, which the per-enumerator round-trip test exists to prevent.
template <typename Enum, std::size_t N>
[[nodiscard]] std::string enumName(const std::array<std::pair<Enum, std::string_view>, N>& table,
                                   Enum value) {
    for (const auto& [item, name] : table) {
        if (item == value) {
            return std::string{name};
        }
    }
    return "unknown";
}

template <typename Enum, std::size_t N>
[[nodiscard]] Result<Enum> readEnum(const std::array<std::pair<Enum, std::string_view>, N>& table,
                                    const Json& object, std::string_view key,
                                    std::string_view path, std::string_view what) {
    auto text = readString(object, key, path);
    if (!text) {
        return std::unexpected(text.error());
    }
    for (const auto& [value, name] : table) {
        if (name == *text) {
            return value;
        }
    }
    return parseError(childPath(path, key), std::format("unknown {} '{}'", what, *text));
}

// ---------------------------------------------------------------------------
// Targets
// ---------------------------------------------------------------------------

/// A canonical target: a `FaceName` that NAMES A FEATURE.
///
/// The encoding is the shared one, and a reference to a face of a feature that does not exist
/// (or no longer exists) is ordinary canonical intent that loads and is reported unresolved
/// later. What is refused here is the INVALID feature handle: it names nothing, so it is not a
/// target to be unresolved but a malformed record -- the same line the command layer draws
/// (P18-CMD-001). The data model's `validate` lets it through, and that qualified behaviour is
/// left alone; the file refuses it at its own boundary.
[[nodiscard]] Result<FaceName> readFace(const Json& field, std::string_view path) {
    auto face = faceNameFromJson(field, path);
    if (!face) {
        return std::unexpected(face.error());
    }
    if (!face->feature.isValid()) {
        return parseError(childPath(path, "feature"),
                          "a target must name a feature; the invalid feature handle is not a face "
                          "that is missing but a record that names nothing");
    }
    return face;
}
// ---------------------------------------------------------------------------
// Loads
// ---------------------------------------------------------------------------

[[nodiscard]] Json loadToJson(const thermal::ThermalLoad& load) {
    Json json = Json::object();
    json["id"] = load.id().value();
    json["type"] = enumName(kLoadKinds, load.kind());
    if (const thermal::SurfaceHeatFluxLoad* surface = load.surfaceHeatFlux()) {
        json["face"] = faceNameToJson(surface->face);
        // W/m^2, signed: positive heats the body, negative is heat leaving
        // through the face. Written as given -- never abs(), never clamped.
        json["flux"] = surface->flux.si();
    } else if (const thermal::VolumetricHeatSourceLoad* source = load.volumetricHeatSource()) {
        // W/m^3, signed; no target (the body is the whole analysed domain).
        json["generation"] = source->generation.si();
    }
    return json;
}

[[nodiscard]] Result<thermal::ThermalLoad> loadFromJson(const Json& value, std::string_view path) {
    if (auto object = requireObject(value, path, {"id", "type", "face", "flux", "generation"});
        !object) {
        return std::unexpected(object.error());
    }
    auto id = readId(value, "id", path);
    if (!id) {
        return std::unexpected(id.error());
    }
    const ThermalLoadId loadId = ThermalLoadId::fromValue(*id);
    auto kind = readEnum(kLoadKinds, value, "type", path, "load type");
    if (!kind) {
        return std::unexpected(kind.error());
    }
    // The keys this kind must NOT carry, written out rather than inferred:
    // `requireObject` allows the union, so without this a volumetric source
    // given a face would pass with the face quietly ignored.
    const auto refuseExtras = [&value, path](std::initializer_list<std::string_view> keys,
                                             std::string_view kindName) -> Result<void> {
        for (const std::string_view key : keys) {
            if (value.contains(std::string{key})) {
                return parseError(childPath(path, key),
                                  std::format("a {} load does not carry this", kindName));
            }
        }
        return Result<void>{};
    };

    Result<thermal::ThermalLoad> load = makeError(ErrorCode::Internal, "no load was decoded");
    switch (*kind) {
    case thermal::ThermalLoadKind::SurfaceHeatFlux: {
        if (auto extras = refuseExtras({"generation"}, "surface heat flux"); !extras) {
            return std::unexpected(extras.error());
        }
        auto faceField = requireField(value, "face", path);
        auto flux = readNumber(value, "flux", path);
        if (!faceField || !flux) {
            return std::unexpected(!faceField ? faceField.error() : flux.error());
        }
        auto face = readFace(**faceField, childPath(path, "face"));
        if (!face) {
            return std::unexpected(face.error());
        }
        load = thermal::ThermalLoad{
            loadId, thermal::SurfaceHeatFluxLoad{.face = std::move(*face),
                                                 .flux = HeatFlux::fromSi(*flux)}};
        break;
    }
    case thermal::ThermalLoadKind::VolumetricHeatSource: {
        if (auto extras = refuseExtras({"face", "flux"}, "volumetric heat source"); !extras) {
            return std::unexpected(extras.error());
        }
        auto generation = readNumber(value, "generation", path);
        if (!generation) {
            return std::unexpected(generation.error());
        }
        load = thermal::ThermalLoad{
            loadId, thermal::VolumetricHeatSourceLoad{
                        .generation = VolumetricHeatSource::fromSi(*generation)}};
        break;
    }
    }
    if (!load) {
        return std::unexpected(load.error());
    }
    // THE CORE'S OWN RULE, CALLED RATHER THAN COPIED: a valid identity, a valid
    // face selector and a finite value. A NEGATIVE value is a sink and passes.
    // On non-finite values this is the second line: the parser refuses an
    // out-of-range literal outright, so an infinity never reaches `readNumber`.
    if (const Result<void> valid = thermal::validate(*load); !valid) {
        return atPath(path, valid.error());
    }
    return load;
}

[[nodiscard]] Json loadsToJson(const std::vector<thermal::ThermalLoad>& loads) {
    Json array = Json::array();
    for (const thermal::ThermalLoad& load : loads) {
        array.push_back(loadToJson(load));
    }
    return array;
}

[[nodiscard]] Result<std::vector<thermal::ThermalLoad>> loadsFromJson(const Json& value,
                                                                      std::string_view path) {
    if (!value.is_array()) {
        return parseError(path, "expected an array");
    }
    std::vector<thermal::ThermalLoad> loads;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const std::string itemPath = indexPath(path, i);
        Result<thermal::ThermalLoad> load = loadFromJson(value[i], itemPath);
        if (!load) {
            return std::unexpected(load.error());
        }
        // REFUSED, NOT MERGED and not last-write-wins: two loads with one id is
        // a mistake in the file and choosing one would hide it. Identities are
        // unique within the owning analysis.
        for (const thermal::ThermalLoad& seen : loads) {
            if (seen.id() == load->id()) {
                return parseError(childPath(itemPath, "id"),
                                  std::format("the load id {} is used twice in this analysis",
                                              load->id().value()));
            }
        }
        loads.push_back(std::move(*load));
    }
    return loads;
}

// ---------------------------------------------------------------------------
// Boundary conditions
// ---------------------------------------------------------------------------

[[nodiscard]] Json conditionToJson(const thermal::ThermalBoundaryCondition& condition) {
    Json json = Json::object();
    json["id"] = condition.id().value();
    json["type"] = enumName(kConditionKinds, condition.kind());
    json["face"] = faceNameToJson(condition.face());
    if (const thermal::PrescribedTemperature* held = condition.prescribedTemperature()) {
        json["temperature"] = held->value.si();   // K, absolute
    } else if (const thermal::ConvectionBoundary* convection = condition.convection()) {
        json["coefficient"] = convection->coefficient.si();   // W/(m^2 K)
        json["ambient"] = convection->ambient.si();            // K, absolute
    }
    return json;
}

[[nodiscard]] Result<thermal::ThermalBoundaryCondition>
conditionFromJson(const Json& value, std::string_view path) {
    if (auto object = requireObject(
            value, path, {"id", "type", "face", "temperature", "coefficient", "ambient"});
        !object) {
        return std::unexpected(object.error());
    }
    auto id = readId(value, "id", path);
    if (!id) {
        return std::unexpected(id.error());
    }
    const ThermalBoundaryConditionId conditionId = ThermalBoundaryConditionId::fromValue(*id);
    auto kind = readEnum(kConditionKinds, value, "type", path, "boundary condition type");
    if (!kind) {
        return std::unexpected(kind.error());
    }
    auto faceField = requireField(value, "face", path);
    if (!faceField) {
        return std::unexpected(faceField.error());
    }
    auto face = readFace(**faceField, childPath(path, "face"));
    if (!face) {
        return std::unexpected(face.error());
    }
    const auto refuseExtras = [&value, path](std::initializer_list<std::string_view> keys,
                                             std::string_view kindName) -> Result<void> {
        for (const std::string_view key : keys) {
            if (value.contains(std::string{key})) {
                return parseError(childPath(path, key),
                                  std::format("a {} condition does not carry this", kindName));
            }
        }
        return Result<void>{};
    };

    Result<thermal::ThermalBoundaryCondition> condition =
        makeError(ErrorCode::Internal, "no boundary condition was decoded");
    switch (*kind) {
    case thermal::ThermalBoundaryConditionKind::PrescribedTemperature: {
        if (auto extras = refuseExtras({"coefficient", "ambient"}, "prescribed temperature");
            !extras) {
            return std::unexpected(extras.error());
        }
        auto temperature = readNumber(value, "temperature", path);
        if (!temperature) {
            return std::unexpected(temperature.error());
        }
        condition = thermal::ThermalBoundaryCondition{
            conditionId, thermal::PrescribedTemperature{
                             .face = std::move(*face),
                             .value = Temperature::fromSi(*temperature)}};
        break;
    }
    case thermal::ThermalBoundaryConditionKind::Convection: {
        if (auto extras = refuseExtras({"temperature"}, "convection"); !extras) {
            return std::unexpected(extras.error());
        }
        auto coefficient = readNumber(value, "coefficient", path);
        auto ambient = readNumber(value, "ambient", path);
        if (!coefficient || !ambient) {
            return std::unexpected(!coefficient ? coefficient.error() : ambient.error());
        }
        condition = thermal::ThermalBoundaryCondition{
            conditionId,
            thermal::ConvectionBoundary{.face = std::move(*face),
                                        .coefficient = HeatTransferCoefficient::fromSi(*coefficient),
                                        .ambient = Temperature::fromSi(*ambient)}};
        break;
    }
    }
    if (!condition) {
        return std::unexpected(condition.error());
    }
    // The core's own rule: a finite absolute temperature and, for convection, a
    // finite positive coefficient. A zero or negative h is refused, never abs()'d.
    if (const Result<void> valid = thermal::validate(*condition); !valid) {
        return atPath(path, valid.error());
    }
    return condition;
}

[[nodiscard]] Json conditionsToJson(const std::vector<thermal::ThermalBoundaryCondition>& conditions) {
    Json array = Json::array();
    for (const thermal::ThermalBoundaryCondition& condition : conditions) {
        array.push_back(conditionToJson(condition));
    }
    return array;
}

[[nodiscard]] Result<std::vector<thermal::ThermalBoundaryCondition>>
conditionsFromJson(const Json& value, std::string_view path) {
    if (!value.is_array()) {
        return parseError(path, "expected an array");
    }
    std::vector<thermal::ThermalBoundaryCondition> conditions;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const std::string itemPath = indexPath(path, i);
        Result<thermal::ThermalBoundaryCondition> condition = conditionFromJson(value[i], itemPath);
        if (!condition) {
            return std::unexpected(condition.error());
        }
        for (const thermal::ThermalBoundaryCondition& seen : conditions) {
            if (seen.id() == condition->id()) {
                return parseError(
                    childPath(itemPath, "id"),
                    std::format("the boundary condition id {} is used twice in this analysis",
                                condition->id().value()));
            }
        }
        conditions.push_back(std::move(*condition));
    }
    return conditions;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

/// ALWAYS WRITTEN, BOTH FIELDS, for P17's reason: an engineering document is
/// better off carrying the numbers it was solved with than reconstructing them
/// from whatever this build's default happens to be later, and a dropped field
/// cannot then be hidden by a default that happens to match.
[[nodiscard]] Json solverToJson(const thermal::ThermalSolverSettings& solver) {
    Json json = Json::object();
    json["relative_residual_tolerance"] = solver.relativeResidualTolerance;
    json["pivot_floor"] = solver.pivotFloor;
    return json;
}

[[nodiscard]] Result<thermal::ThermalSolverSettings> solverFromJson(const Json& value,
                                                                    std::string_view path) {
    if (auto object = requireObject(value, path, {"relative_residual_tolerance", "pivot_floor"});
        !object) {
        return std::unexpected(object.error());
    }
    // EVERY FIELD REQUIRED, because every field is always written: a missing one
    // is a malformed file, and reading it as "whatever this build defaults to" is
    // the silent substitution the always-write rule exists to prevent.
    auto tolerance = readNumber(value, "relative_residual_tolerance", path);
    auto pivot = readNumber(value, "pivot_floor", path);
    if (!tolerance || !pivot) {
        return std::unexpected(!tolerance ? tolerance.error() : pivot.error());
    }
    const thermal::ThermalSolverSettings solver{.relativeResidualTolerance = *tolerance,
                                                .pivotFloor = *pivot};
    if (const Result<void> valid = thermal::validate(solver); !valid) {
        return atPath(path, valid.error());
    }
    return solver;
}

[[nodiscard]] Json transientToJson(const thermal::TransientSettings& transient) {
    Json json = Json::object();
    json["time_step"] = transient.timeStep.si();   // s
    json["end_time"] = transient.endTime.si();     // s
    json["output"] = enumName(kOutputs, transient.output);
    return json;
}

[[nodiscard]] Result<thermal::TransientSettings> transientFromJson(const Json& value,
                                                                   std::string_view path) {
    if (auto object = requireObject(value, path, {"time_step", "end_time", "output"}); !object) {
        return std::unexpected(object.error());
    }
    auto step = readNumber(value, "time_step", path);
    auto end = readNumber(value, "end_time", path);
    auto output = readEnum(kOutputs, value, "output", path, "output policy");
    if (!step || !end || !output) {
        return std::unexpected(!step ? step.error() : !end ? end.error() : output.error());
    }
    const thermal::TransientSettings transient{.timeStep = Time::fromSi(*step),
                                               .endTime = Time::fromSi(*end),
                                               .output = *output};
    // 0 < step <= end, both finite -- the core's rule. Never replaced by a
    // default, never reordered.
    if (const Result<void> valid = thermal::validate(transient); !valid) {
        return atPath(path, valid.error());
    }
    return transient;
}

} // namespace

Result<Json> thermalAnalysisToJson(const thermal::ThermalAnalysis& analysis) {
    const thermal::ThermalAnalysisDefinition& d = analysis.definition();
    // DEFENCE IN DEPTH. An analysis can only hold a definition that passed
    // `validate`, so this cannot fail today; it is here so that the writer can
    // never emit a state the reader would refuse (a NaN would dump as `null`).
    if (const Result<void> valid = thermal::validate(d); !valid) {
        return std::unexpected(valid.error());
    }
    // THE WRITER MUST NOT EMIT WHAT THE READER REFUSES. `validate` only asks that an
    // identity be valid, and `readId` caps a file identifier at 2^53 - 1 (the largest
    // integer every JSON implementation represents exactly), so an identity above the cap
    // would save and then never load. Refused here, by name, before anything is written.
    const auto fits = [](std::uint64_t value, std::string_view what) -> Result<void> {
        if (value > kMaxFileId) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} {} exceeds the largest identifier a file can hold ({}); "
                                         "it could be written but never read back",
                                         what, value, kMaxFileId));
        }
        return Result<void>{};
    };
    if (const Result<void> ok = fits(d.mesh.value(), "the meshing control"); !ok) {
        return std::unexpected(ok.error());
    }
    // THE SAME RULE FOR A TARGET. The data model's `validate` admits a FaceName whose feature is the invalid handle (the
    // reader and the command layer refuse it) and identifiers inside a GeometryReference above the cap; both would save
    // and then never load. A target is writable exactly when `readFace` would read it back.
    const auto faceWritable = [&fits](const FaceName& face, std::string_view what) -> Result<void> {
        if (!face.feature.isValid()) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} targets a face of no feature (the invalid feature handle); the file would refuse it on "
                                         "load, so it is not written",
                                         what));
        }
        const auto check = [&fits, what](std::uint64_t value, std::string_view part) {
            return fits(value, std::format("{}: the {}", what, part));
        };
        if (const Result<void> ok = check(face.feature.value(), "feature"); !ok) {
            return ok;
        }
        const FaceSelector& selector = face.face;
        if (selector.entity.has_value()) {
            if (const Result<void> ok = check(selector.entity->value(), "entity"); !ok) {
                return ok;
            }
        }
        if (selector.along.has_value()) {
            if (const Result<void> ok = check(selector.along->value(), "path edge"); !ok) {
                return ok;
            }
        }
        if (selector.alongSketch.has_value()) {
            if (const Result<void> ok = check(selector.alongSketch->value(), "path sketch"); !ok) {
                return ok;
            }
        }
        if (selector.edge.has_value()) {
            if (const Result<void> ok = check(selector.edge->value(), "chamfer edge"); !ok) {
                return ok;
            }
        }
        for (const FaceCopy& copy : selector.copies) {
            if (const Result<void> ok = check(copy.feature.value(), "copying feature"); !ok) {
                return ok;
            }
            if (const Result<void> ok = check(static_cast<std::uint64_t>(copy.instance), "copy instance"); !ok) {
                return ok;
            }
        }
        return Result<void>{};
    };
    for (const thermal::ThermalLoad& load : d.loads) {
        if (const Result<void> ok = fits(load.id().value(), "the load id"); !ok) {
            return std::unexpected(ok.error());
        }
        if (const std::optional<FaceName> target = load.target(); target.has_value()) {
            if (const Result<void> ok = faceWritable(*target, "a load"); !ok) {
                return std::unexpected(ok.error());
            }
        }
    }
    for (const thermal::ThermalBoundaryCondition& condition : d.boundaryConditions) {
        if (const Result<void> ok = fits(condition.id().value(), "the boundary condition id"); !ok) {
            return std::unexpected(ok.error());
        }
        if (const Result<void> ok = faceWritable(condition.face(), "a boundary condition"); !ok) {
            return std::unexpected(ok.error());
        }
    }
    Json json = Json::object();
    // Keys inserted in a fixed order, because ordered_json writes insertion order
    // and this file's determinism claim rests on it.
    json["mesh"] = d.mesh.value();
    json["mode"] = enumName(kModes, d.mode);
    // Omitted when empty, P16/P17's convention for a collection that says
    // nothing. An absent array reads back as an empty vector.
    if (!d.loads.empty()) {
        json["loads"] = loadsToJson(d.loads);
    }
    if (!d.boundaryConditions.empty()) {
        json["boundary_conditions"] = conditionsToJson(d.boundaryConditions);
    }
    if (d.initialCondition.has_value()) {
        json["initial_temperature"] = d.initialCondition->value.si();   // K
    }
    json["solver"] = solverToJson(d.solver);
    if (d.transient.has_value()) {
        json["transient"] = transientToJson(*d.transient);
    }
    return json;
}

Result<std::unique_ptr<thermal::ThermalAnalysis>>
thermalAnalysisFromJson(const Json& data, std::string name, std::string_view path) {
    if (auto object = requireObject(data, path,
                                    {"mesh", "mode", "loads", "boundary_conditions",
                                     "initial_temperature", "solver", "transient"});
        !object) {
        return std::unexpected(object.error());
    }
    auto mesh = readId(data, "mesh", path);
    if (!mesh) {
        return std::unexpected(mesh.error());
    }
    auto mode = readEnum(kModes, data, "mode", path, "analysis mode");
    if (!mode) {
        return std::unexpected(mode.error());
    }
    auto solverField = requireField(data, "solver", path);
    if (!solverField) {
        return std::unexpected(solverField.error());
    }
    auto solver = solverFromJson(**solverField, childPath(path, "solver"));
    if (!solver) {
        return std::unexpected(solver.error());
    }

    thermal::ThermalAnalysisDefinition definition;
    definition.mesh = MeshControlId::fromValue(*mesh);
    definition.mode = *mode;
    definition.solver = *solver;

    if (data.contains("loads")) {
        auto loads = loadsFromJson(data["loads"], childPath(path, "loads"));
        if (!loads) {
            return std::unexpected(loads.error());
        }
        definition.loads = std::move(*loads);
    }
    if (data.contains("boundary_conditions")) {
        auto conditions =
            conditionsFromJson(data["boundary_conditions"], childPath(path, "boundary_conditions"));
        if (!conditions) {
            return std::unexpected(conditions.error());
        }
        definition.boundaryConditions = std::move(*conditions);
    }
    if (data.contains("initial_temperature")) {
        auto initial = readNumber(data, "initial_temperature", path);
        if (!initial) {
            return std::unexpected(initial.error());
        }
        const thermal::UniformInitialTemperature condition{Temperature::fromSi(*initial)};
        if (const Result<void> valid = thermal::validate(condition); !valid) {
            return atPath(childPath(path, "initial_temperature"), valid.error());
        }
        definition.initialCondition = condition;
    }
    if (data.contains("transient")) {
        auto transient = transientFromJson(data["transient"], childPath(path, "transient"));
        if (!transient) {
            return std::unexpected(transient.error());
        }
        definition.transient = *transient;
    }

    // THROUGH ThermalAnalysis::create, so the file meets the gate the API does:
    // a valid control handle, usable settings, unique identities and -- the one
    // rule no per-record check can express -- the mode-complete rule (a steady
    // analysis carries no initial temperature or transient settings, a
    // transient one carries both).
    auto analysis = thermal::ThermalAnalysis::create(std::move(name), std::move(definition));
    if (!analysis) {
        return atPath(path, analysis.error());
    }
    return std::move(*analysis);
}

} // namespace bettercad::io::detail
