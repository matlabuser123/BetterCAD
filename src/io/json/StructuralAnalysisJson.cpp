#include "io/json/ObjectJson.hpp"

#include <array>
#include <cstddef>
#include <format>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The persisted form of a structural analysis's canonical intent
// (P17-PERSIST-001).
//
// WHAT IS PERSISTED: the meshing control analysed, the analysis mode, the
// loads with their CAD targets and physical values, the restraints with their
// CAD targets and component masks, and the solver settings.
//
// WHAT IS NOT, and could not be even deliberately: `NodeId`, `ElementId`,
// `DofIndex`, `FreeEquationIndex`, boundary facets, `PreparedStructuralLoads`,
// `PreparedStructuralRestraints`, `ConstraintSet`, `FreeEquationMap`, `K`, `F`,
// `Kff`, `Ff`, a factorisation, `u`, `uf`, strain, stress, principal stress,
// von Mises, reactions, a validation report or a render buffer.
//
// THE EXCLUSION IS STRUCTURAL, which is the same argument P16-PERSIST-001 made
// and the reason this file is short. A `StructuralAnalysisDefinition` has
// exactly five members -- a `MeshControlId`, a mode, the loads, the restraints
// and the solver settings -- and no member for any derived thing. A
// `StructuralResult` does not live on the document object at all: it is
// returned by `solveStructuralAnalysis` and held by the caller. So there is no
// result HERE to exclude, and the derived-state rule is not a discipline this
// file maintains but a shape the data model already has:
//
//     save  ->  structural-analysis intent
//     load  ->  restore intent; regenerate the mesh through P16; solve when asked
//
// VALIDATION IS THE CORE'S, AND WHERE IT IS NOT, THAT IS MEASURED. Reading ends
// in `StructuralAnalysis::create`, which calls
// `validate(StructuralAnalysisDefinition)` -- and that function checks the mesh
// handle and the solver settings and NOTHING ELSE. P17-CMD-001's probe C11
// measured the same asymmetry from the other side. So this file calls
// `validate(StructuralLoad)` and `validate(StructuralRestraint)` itself, which
// are the core's own rules and not copies of them, and rejects a duplicate
// `LoadId` or `RestraintId` -- a FILE integrity rule, and exactly where P16 put
// its equivalent ("the metric '{}' is given a threshold twice").
//
// A NODAL FORCE LOAD IS REFUSED ON SAVE, and that is the only thing in P17
// persistence that is genuinely new rather than inherited. It carries a
// `MeshStamp` and a `NodeId`; ADR-031 says those are "never persisted",
// P17-LOAD-001 declares the type mesh-local and explicitly "NOT durable
// point-load intent on the model", and a stamp written to a file would name a
// mesh generation that cannot exist after a reload -- so the load would be
// refused by P17-LOAD-001's own rule for the rest of the document's life.
// Writing it would be a quiet way of storing a permanently dead load. The save
// fails instead, naming the load, through the `Result<Json>` that
// `objectToJson` has always returned. See NODAL_FORCE_POLICY.md.
//
// FACE REFERENCES GO THROUGH faceNameToJson, for the reason P16 gives: the
// canonical face reference already has a qualified serialization used by
// datums, drafts, shells, dimensions, annotations and meshing controls, and a
// second encoding here would be a second contract.
//
// DETERMINISM. `Json` is `nlohmann::ordered_json`, so key order is this file's
// insertion order and array order is whatever order it is given. The loads and
// the restraints are written in the definition's own order, which
// `StructuralAnalysisDefinition` declares is "THE USER'S AND IS PRESERVED" over
// both vectors -- the opposite of P16's local sizing controls, whose order
// P16-SIZE-001 declares meaningless and which are therefore sorted. Equal
// intent still gives equal bytes, because here the order IS part of the intent.
// P17-CMD-001's probe C8 is why that matters: an edit that silently reordered
// the loads was a real defect.
namespace bettercad::io::detail {

namespace {

// The dispatch in DocumentJson.cpp compares against the literal
// "structural-analysis", never against kTypeName: binding a reference to a
// dll-imported constexpr static does not link in a shared build, which the
// debug-shared preset found for components, again for materials and again for
// meshing controls. This keeps the two from drifting.
static_assert(structural::StructuralAnalysis::kTypeName ==
              std::string_view{"structural-analysis"});

/// A stable file key for every analysis mode.
///
/// STRINGS, NOT INTEGERS, and not `toString(StructuralAnalysisMode)` either --
/// P16's quality metrics settled both halves of this. An integer would
/// silently become a different mode the day an enumerator is inserted in the
/// middle; the display string is text a later milestone may reword or
/// translate, and a file key has to outlive that.
///
/// A table-size check cannot catch a WRONG entry, so the guard is
/// StructuralPersistence_EveryAnalysisModeRoundTrips, which puts each of these
/// on an analysis in turn.
constexpr std::array<std::pair<structural::StructuralAnalysisMode, std::string_view>, 2> kModes{{
    {structural::StructuralAnalysisMode::LinearStatic, "linear_static"},
    {structural::StructuralAnalysisMode::LinearStaticWithGravity, "linear_static_with_gravity"},
}};

/// A stable file key for every load kind.
///
/// `NodalForce` is in the table so that the name lookup is total and a
/// diagnostic can name it -- it is NOT a key the writer ever emits, because
/// writing a nodal force load fails before it reaches here, and NOT a key the
/// reader accepts.
constexpr std::array<std::pair<structural::LoadKind, std::string_view>, 4> kLoadKinds{{
    {structural::LoadKind::NodalForce, "nodal_force"},
    {structural::LoadKind::SurfaceTraction, "surface_traction"},
    {structural::LoadKind::Pressure, "pressure"},
    {structural::LoadKind::Gravity, "gravity"},
}};

/// A stable file key for every restrainable displacement component.
///
/// Written in `kDofComponents` order, which is P17-DOF-001's, so the file's
/// component order cannot drift from the degree-of-freedom ordering it names.
constexpr std::array<std::pair<structural::DofComponent, std::string_view>, 3> kComponents{{
    {structural::DofComponent::Ux, "ux"},
    {structural::DofComponent::Uy, "uy"},
    {structural::DofComponent::Uz, "uz"},
}};

/// A stable file key for every solver algorithm.
constexpr std::array<std::pair<structural::SolverAlgorithm, std::string_view>, 1> kAlgorithms{{
    {structural::SolverAlgorithm::SimplicialLdlt, "simplicial_ldlt"},
}};

/// The name for @p value from @p table, or "unknown" for a value the table
/// does not carry. A value reaching "unknown" would serialize silently and
/// then fail to load, which is what the per-enumerator round-trip tests exist
/// to prevent.
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

/// Reads a string key from @p table. @p what names the enumeration in the
/// diagnostic, so an unknown value says which one refused it.
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
// Vector quantities
// ---------------------------------------------------------------------------

/// [x, y, z] in pascals. A traction's components are global, never
/// surface-local (P17-LOAD-001 freezes that, and it is what distinguishes a
/// traction from a pressure), so there is no basis to record alongside them.
[[nodiscard]] Json tractionToJson(const Traction3D& traction) {
    Json json = Json::array();
    json.push_back(traction.x.si());
    json.push_back(traction.y.si());
    json.push_back(traction.z.si());
    return json;
}

/// [x, y, z] in m/s^2.
[[nodiscard]] Json accelerationToJson(const Vector3D& acceleration) {
    Json json = Json::array();
    json.push_back(acceleration.x);
    json.push_back(acceleration.y);
    json.push_back(acceleration.z);
    return json;
}

[[nodiscard]] Result<Traction3D> tractionFromJson(const Json& object, std::string_view key,
                                                  std::string_view path) {
    auto values = readNumbers(object, key, path, 3U);
    if (!values) {
        return std::unexpected(values.error());
    }
    return Traction3D{.x = Pressure::fromSi((*values)[0]),
                      .y = Pressure::fromSi((*values)[1]),
                      .z = Pressure::fromSi((*values)[2])};
}

[[nodiscard]] Result<Vector3D> accelerationFromJson(const Json& object, std::string_view key,
                                                    std::string_view path) {
    auto values = readNumbers(object, key, path, 3U);
    if (!values) {
        return std::unexpected(values.error());
    }
    return Vector3D{.x = (*values)[0], .y = (*values)[1], .z = (*values)[2]};
}

// ---------------------------------------------------------------------------
// Loads
// ---------------------------------------------------------------------------

/// One load. Fails for a nodal force load, which this format does not carry.
[[nodiscard]] Result<Json> loadToJson(const structural::StructuralLoad& load) {
    Json json = Json::object();
    json["id"] = load.id().value();
    json["type"] = enumName(kLoadKinds, load.kind());
    if (const structural::SurfaceTractionLoad* traction = load.traction()) {
        json["face"] = faceNameToJson(traction->face);
        json["traction"] = tractionToJson(traction->traction);
        return json;
    }
    if (const structural::PressureLoad* pressure = load.pressure()) {
        json["face"] = faceNameToJson(pressure->face);
        // A SCALAR, SIGNED, and never a frozen traction vector: the direction
        // comes from the normal of whatever mesh is current, which is what
        // lets a pressure survive a remesh or a rigid transform
        // (P17-LOAD-001). A negative value is outward suction and is ordinary.
        json["magnitude"] = pressure->magnitude.si();
        return json;
    }
    if (const structural::GravityLoad* gravity = load.gravity()) {
        // THE ACCELERATION, NOT THE DENSITY AND NOT THE BODY FORCE. The
        // density is P15's canonical material data and ADR-028 forbids a
        // solver holding material data of its own, so `b = rho g` is formed at
        // preparation time and never stored (P17-LOAD-001).
        json["acceleration"] = accelerationToJson(gravity->acceleration);
        return json;
    }
    // A nodal force load. Refused, with the reason, rather than written or
    // silently dropped.
    return makeError(
        ErrorCode::InvalidArgument,
        std::format("{} is a nodal force load, which names a mesh node and the mesh generation "
                    "it belongs to; neither is canonical CAD intent and neither can be saved "
                    "(ADR-031). Apply the force to a CAD face as a traction or a pressure.",
                    load.id()));
}

[[nodiscard]] Result<structural::StructuralLoad> loadFromJson(const Json& value,
                                                              std::string_view path) {
    if (auto object = requireObject(
            value, path, {"id", "type", "face", "traction", "magnitude", "acceleration"});
        !object) {
        return std::unexpected(object.error());
    }
    auto id = readId(value, "id", path);
    if (!id) {
        return std::unexpected(id.error());
    }
    const LoadId loadId = LoadId::fromValue(*id);
    auto kind = readEnum(kLoadKinds, value, "type", path, "load type");
    if (!kind) {
        return std::unexpected(kind.error());
    }

    const auto faceOf = [&value, path]() -> Result<FaceName> {
        auto field = requireField(value, "face", path);
        if (!field) {
            return std::unexpected(field.error());
        }
        return faceNameFromJson(**field, childPath(path, "face"));
    };
    // The keys this kind must NOT carry. Written out rather than inferred, so
    // a pressure given a traction vector is a parse error naming the key
    // instead of a load whose extra field was quietly ignored. `requireObject`
    // above allows the union of the keys any kind uses, so without this a
    // mismatched one would pass.
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

    Result<structural::StructuralLoad> load =
        makeError(ErrorCode::Internal, "no load was decoded");
    switch (*kind) {
    case structural::LoadKind::SurfaceTraction: {
        if (auto extras = refuseExtras({"magnitude", "acceleration"}, "surface traction");
            !extras) {
            return std::unexpected(extras.error());
        }
        auto face = faceOf();
        auto traction = tractionFromJson(value, "traction", path);
        if (!face || !traction) {
            return std::unexpected(!face ? face.error() : traction.error());
        }
        load = structural::StructuralLoad{
            loadId,
            structural::SurfaceTractionLoad{.face = std::move(*face), .traction = *traction}};
        break;
    }
    case structural::LoadKind::Pressure: {
        if (auto extras = refuseExtras({"traction", "acceleration"}, "pressure"); !extras) {
            return std::unexpected(extras.error());
        }
        auto face = faceOf();
        auto magnitude = readNumber(value, "magnitude", path);
        if (!face || !magnitude) {
            return std::unexpected(!face ? face.error() : magnitude.error());
        }
        load = structural::StructuralLoad{
            loadId, structural::PressureLoad{.face = std::move(*face),
                                             .magnitude = Pressure::fromSi(*magnitude)}};
        break;
    }
    case structural::LoadKind::Gravity: {
        if (auto extras = refuseExtras({"face", "traction", "magnitude"}, "gravity"); !extras) {
            return std::unexpected(extras.error());
        }
        auto acceleration = accelerationFromJson(value, "acceleration", path);
        if (!acceleration) {
            return std::unexpected(acceleration.error());
        }
        load = structural::StructuralLoad{loadId,
                                          structural::GravityLoad{.acceleration = *acceleration}};
        break;
    }
    case structural::LoadKind::NodalForce:
        // The writer cannot emit this key and the reader will not accept it.
        // Refused by name rather than falling through to "unknown load type",
        // so the diagnostic says what is wrong and not merely what is
        // unrecognised.
        return parseError(childPath(path, "type"),
                          "a nodal force load names a mesh node and cannot appear in a document "
                          "file (ADR-031)");
    }
    if (!load) {
        return std::unexpected(load.error());
    }

    // THE CORE'S OWN RULE, CALLED RATHER THAN COPIED.
    // `validate(StructuralLoad)` owns what a valid id and a finite value are --
    // the same function the command layer and the prepare path use (ADR-047).
    // It is needed here because `validate(StructuralAnalysisDefinition)` does
    // not check the loads, which is measured rather than assumed.
    //
    // ON NON-FINITE VALUES IT IS THE SECOND LINE, NOT THE FIRST, and that was
    // measured rather than reasoned: the parser refuses an out-of-range
    // literal outright ("number overflow parsing '-1e999'"), so an infinity
    // never becomes a number for `readNumber` to pass on. The validator still
    // matters -- it owns the finite-but-invalid cases, and it would catch such
    // a token if a future reader ever accepted one -- but the claim that it is
    // what stops 1e999 would be wrong.
    if (const Result<void> valid = structural::validate(*load); !valid) {
        return atPath(path, valid.error());
    }
    return load;
}

[[nodiscard]] Result<Json> loadsToJson(const std::vector<structural::StructuralLoad>& loads) {
    Json array = Json::array();
    for (const structural::StructuralLoad& load : loads) {
        Result<Json> entry = loadToJson(load);
        if (!entry) {
            return std::unexpected(entry.error());
        }
        array.push_back(std::move(*entry));
    }
    return array;
}

[[nodiscard]] Result<std::vector<structural::StructuralLoad>> loadsFromJson(
    const Json& value, std::string_view path) {
    if (!value.is_array()) {
        return parseError(path, "expected an array");
    }
    std::vector<structural::StructuralLoad> loads;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const std::string itemPath = indexPath(path, i);
        Result<structural::StructuralLoad> load = loadFromJson(value[i], itemPath);
        if (!load) {
            return std::unexpected(load.error());
        }
        // REFUSED, NOT MERGED and not last-write-wins. Two loads with one id
        // is a modelling mistake in the file, and choosing one would hide it --
        // exactly the reason P16 gives for refusing a repeated quality metric.
        // A `LoadId` is unique within its owning analysis (`Id.hpp`), so this
        // is the right scope to ask the question in.
        for (const structural::StructuralLoad& seen : loads) {
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
// Restraints
// ---------------------------------------------------------------------------

/// The component mask as an array of names in `kDofComponents` order.
///
/// AN ARRAY OF NAMES, NOT A BIT INTEGER. `7` meaning "fixed" needs a reader to
/// know the bit order; `["ux", "uy", "uz"]` does not, and a renumbered
/// `DofComponent` cannot silently change what an old file means.
///
/// THE ORDER IS CANONICAL AND NOT AN INSERTION ORDER, which
/// `RestraintComponents` makes free: it is a bitmask with no insertion order to
/// lose, so iterating `kComponents` is the only order it can produce. Two
/// restraints built by adding the same components in different orders write
/// identical bytes.
[[nodiscard]] Json componentsToJson(const structural::RestraintComponents& components) {
    Json array = Json::array();
    for (const auto& [component, name] : kComponents) {
        if (components.holds(component)) {
            array.push_back(std::string{name});
        }
    }
    return array;
}

[[nodiscard]] Result<structural::RestraintComponents> componentsFromJson(const Json& value,
                                                                        std::string_view path) {
    if (!value.is_array()) {
        return parseError(path, "expected an array");
    }
    structural::RestraintComponents components;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const std::string itemPath = indexPath(path, i);
        if (!value[i].is_string()) {
            return parseError(itemPath, "expected a string");
        }
        const auto text = value[i].get<std::string>();
        bool known = false;
        for (const auto& [component, name] : kComponents) {
            if (name != text) {
                continue;
            }
            // REFUSED, not absorbed. `RestraintComponents::add` is idempotent,
            // so a repeated component would be silently harmless -- and a file
            // saying "ux" twice is still a file nobody wrote on purpose.
            if (components.holds(component)) {
                return parseError(itemPath,
                                  std::format("the component '{}' is listed twice", text));
            }
            components.add(component);
            known = true;
            break;
        }
        if (!known) {
            return parseError(itemPath, std::format("unknown displacement component '{}'", text));
        }
    }
    // NOT CHECKED HERE: an empty mask. That is
    // `validate(StructuralRestraint)`'s rule -- "a restraint that holds
    // nothing at zero is not a weaker restraint, it is a record whose intent
    // cannot be acted on" -- and it runs below.
    return components;
}

[[nodiscard]] Json restraintToJson(const structural::StructuralRestraint& restraint) {
    Json json = Json::object();
    json["id"] = restraint.id().value();
    json["face"] = faceNameToJson(restraint.face());
    json["components"] = componentsToJson(restraint.components());
    return json;
}

[[nodiscard]] Result<structural::StructuralRestraint> restraintFromJson(const Json& value,
                                                                        std::string_view path) {
    if (auto object = requireObject(value, path, {"id", "face", "components"}); !object) {
        return std::unexpected(object.error());
    }
    auto id = readId(value, "id", path);
    auto faceField = requireField(value, "face", path);
    auto componentsField = requireField(value, "components", path);
    if (!id || !faceField || !componentsField) {
        const Error& error =
            !id ? id.error() : !faceField ? faceField.error() : componentsField.error();
        return std::unexpected(error);
    }
    auto face = faceNameFromJson(**faceField, childPath(path, "face"));
    if (!face) {
        return std::unexpected(face.error());
    }
    auto components = componentsFromJson(**componentsField, childPath(path, "components"));
    if (!components) {
        return std::unexpected(components.error());
    }
    structural::StructuralRestraint restraint{RestraintId::fromValue(*id), std::move(*face),
                                              *components};
    // The core's own rule again, and needed here for the same measured reason:
    // `validate(StructuralAnalysisDefinition)` does not check the restraints.
    if (const Result<void> valid = structural::validate(restraint); !valid) {
        return atPath(path, valid.error());
    }
    return restraint;
}

[[nodiscard]] Json restraintsToJson(
    const std::vector<structural::StructuralRestraint>& restraints) {
    Json array = Json::array();
    for (const structural::StructuralRestraint& restraint : restraints) {
        array.push_back(restraintToJson(restraint));
    }
    return array;
}

[[nodiscard]] Result<std::vector<structural::StructuralRestraint>> restraintsFromJson(
    const Json& value, std::string_view path) {
    if (!value.is_array()) {
        return parseError(path, "expected an array");
    }
    std::vector<structural::StructuralRestraint> restraints;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const std::string itemPath = indexPath(path, i);
        Result<structural::StructuralRestraint> restraint =
            restraintFromJson(value[i], itemPath);
        if (!restraint) {
            return std::unexpected(restraint.error());
        }
        for (const structural::StructuralRestraint& seen : restraints) {
            if (seen.id() == restraint->id()) {
                return parseError(childPath(itemPath, "id"),
                                  std::format("the restraint id {} is used twice in this analysis",
                                              restraint->id().value()));
            }
        }
        restraints.push_back(std::move(*restraint));
    }
    return restraints;
}

// ---------------------------------------------------------------------------
// Solver settings
// ---------------------------------------------------------------------------

/// ALWAYS WRITTEN, ALL THREE FIELDS, and the reason is P16's for its surface
/// deflections: an engineering document is better off carrying the numbers it
/// was solved with than reconstructing them from whatever this build's default
/// happens to be later. A file is read by builds that do not exist yet.
///
/// The brief asked for this choice explicitly -- "do not depend on future
/// library defaults to reconstruct an old analysis" -- and it also removes a
/// whole class of silent test pass: a dropped field cannot be hidden by a
/// default that happens to match.
[[nodiscard]] Json solverToJson(const structural::SolverSettings& solver) {
    Json json = Json::object();
    json["algorithm"] = enumName(kAlgorithms, solver.algorithm);
    json["relative_residual_tolerance"] = solver.relativeResidualTolerance;
    json["pivot_floor"] = solver.pivotFloor;
    return json;
}

[[nodiscard]] Result<structural::SolverSettings> solverFromJson(const Json& value,
                                                                std::string_view path) {
    if (auto object =
            requireObject(value, path, {"algorithm", "relative_residual_tolerance", "pivot_floor"});
        !object) {
        return std::unexpected(object.error());
    }
    // EVERY FIELD REQUIRED, because every field is always written. There is no
    // version of this schema in which one is absent, so a missing one is a
    // malformed file rather than an older shape -- and reading it as "whatever
    // this build defaults to" is precisely the silent substitution the
    // always-write rule above exists to prevent. An unrecognised algorithm is
    // refused by name and never falls back to the default solver.
    auto algorithm = readEnum(kAlgorithms, value, "algorithm", path, "solver algorithm");
    auto tolerance = readNumber(value, "relative_residual_tolerance", path);
    auto pivot = readNumber(value, "pivot_floor", path);
    if (!algorithm || !tolerance || !pivot) {
        const Error& error =
            !algorithm ? algorithm.error() : !tolerance ? tolerance.error() : pivot.error();
        return std::unexpected(error);
    }
    // NOT CHECKED HERE: finite and positive. Both are
    // `validate(SolverSettings)`'s rules, and
    // `validate(StructuralAnalysisDefinition)` calls it inside
    // `StructuralAnalysis::create` below -- which is the one part of the
    // definition the core does validate for us.
    return structural::SolverSettings{.algorithm = *algorithm,
                                      .relativeResidualTolerance = *tolerance,
                                      .pivotFloor = *pivot};
}

} // namespace

Result<Json> structuralAnalysisToJson(const structural::StructuralAnalysis& analysis) {
    const structural::StructuralAnalysisDefinition& d = analysis.definition();
    Json json = Json::object();
    // Keys inserted in a fixed order, because ordered_json writes insertion
    // order and this file's determinism claim rests on it.
    json["mesh"] = d.mesh.value();
    json["mode"] = enumName(kModes, d.mode);
    // Omitted when empty, which is P16's convention for a collection that says
    // nothing. An absent array reads back as an empty vector, so an analysis
    // with no loads -- valid canonical intent that P17-VALID refuses to SOLVE,
    // which is a different question -- round-trips either way.
    if (!d.loads.empty()) {
        Result<Json> loads = loadsToJson(d.loads);
        if (!loads) {
            return std::unexpected(loads.error());
        }
        json["loads"] = std::move(*loads);
    }
    if (!d.restraints.empty()) {
        json["restraints"] = restraintsToJson(d.restraints);
    }
    json["solver"] = solverToJson(d.solver);
    return json;
}

Result<std::unique_ptr<structural::StructuralAnalysis>> structuralAnalysisFromJson(
    const Json& data, std::string name, std::string_view path) {
    if (auto object = requireObject(data, path, {"mesh", "mode", "loads", "restraints", "solver"});
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

    structural::StructuralAnalysisDefinition definition;
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
    if (data.contains("restraints")) {
        auto restraints = restraintsFromJson(data["restraints"], childPath(path, "restraints"));
        if (!restraints) {
            return std::unexpected(restraints.error());
        }
        definition.restraints = std::move(*restraints);
    }

    // THROUGH StructuralAnalysis::create, so the file meets the same gate the
    // API does for the two things that gate covers: a valid mesh handle and
    // usable solver settings. The loads and the restraints were validated
    // above, by the core's own per-record rules, because this one does not
    // reach them.
    auto analysis = structural::StructuralAnalysis::create(std::move(name), std::move(definition));
    if (!analysis) {
        return atPath(path, analysis.error());
    }
    return std::move(*analysis);
}

} // namespace bettercad::io::detail
