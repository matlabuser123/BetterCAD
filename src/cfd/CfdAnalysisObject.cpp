// The canonical CFD analysis, as a document object, and its semantic
// fingerprint (P19-DATA-001).

#include <bettercad/cfd/CfdAnalysisObject.hpp>

#include <bettercad/core/document/Document.hpp>

#include <algorithm>
#include <format>
#include <iterator>
#include <utility>

namespace bettercad::cfd {
namespace {

// --------------------------------------------------------------------------
// The canonical text
// --------------------------------------------------------------------------
//
// EVERY FIELD operator== COMPARES, AND NOTHING ELSE. The two must not drift: a
// field compared but not written would let two different definitions share a
// fingerprint, and one written but not compared would let two equal ones differ.
// `CfdData_TheFingerprintCoversEveryField` changes each field in turn, and the
// size mirrors in the tests break when a field is added to any record written
// here (including core's FaceName and FaceSelector).

/// The shortest decimal that reads back as the same double (std::format's
/// default for a double), with -0.0 written as 0: the two compare equal, so
/// they must digest alike.
[[nodiscard]] std::string number(double value) {
    return std::format("{}", value == 0.0 ? 0.0 : value);
}

template <typename Q>
[[nodiscard]] std::string number(Q quantity) {
    return number(quantity.si());
}

template <typename Q>
[[nodiscard]] std::string vector(const CartesianVector<Q>& v) {
    return std::format("{},{},{}", number(v.x), number(v.y), number(v.z));
}

[[nodiscard]] std::string point(const Point3D& p) {
    return std::format("{},{},{}", number(p.x), number(p.y), number(p.z));
}

template <typename IdType>
[[nodiscard]] std::string optionalId(const std::optional<IdType>& id) {
    return id.has_value() ? std::format("{}", id->value()) : std::string{"-"};
}

[[nodiscard]] std::string faceText(const FaceName& name) {
    std::string copies;
    for (const FaceCopy& copy : name.face.copies) {
        if (!copies.empty()) {
            copies += ',';
        }
        copies += std::format("{}:{}", copy.feature.value(), copy.instance);
    }
    return std::format("feature={};role={};entity={};along={};along_sketch={};edge={};copies=[{}]",
                       name.feature.value(), toString(name.face.role), optionalId(name.face.entity),
                       optionalId(name.face.along), optionalId(name.face.alongSketch), optionalId(name.face.edge),
                       copies);
}

void line(std::string& out, std::string_view key, std::string_view value) {
    out += key;
    out += '=';
    out += value;
    out += '\n';
}

void conditionText(std::string& out, std::size_t index, const CfdBoundaryCondition& condition) {
    const std::string prefix = std::format("bc[{}].", index);
    line(out, prefix + "id", std::format("{}", condition.id().value()));
    line(out, prefix + "kind", toString(condition.kind()));
    line(out, prefix + "face", faceText(condition.face()));
    if (const VelocityInlet* inlet = condition.velocityInlet()) {
        line(out, prefix + "normal_speed", number(inlet->normalSpeed));
    } else if (const PressureInlet* pressureInlet = condition.pressureInlet()) {
        line(out, prefix + "static_pressure", number(pressureInlet->staticPressure));
    } else if (const PressureOutlet* outlet = condition.pressureOutlet()) {
        line(out, prefix + "static_pressure", number(outlet->staticPressure));
    } else if (const MovingWall* wall = condition.movingWall()) {
        line(out, prefix + "velocity", vector(wall->velocity));
    }
    // A no-slip wall and a slip wall are a kind and a face; nothing more.
}

} // namespace

std::string_view toString(PressureDatumProblem problem) noexcept {
    switch (problem) {
    case PressureDatumProblem::Missing:
        return "pressure_datum_missing";
    case PressureDatumProblem::Overdetermined:
        return "pressure_datum_overdetermined";
    }
    return "unknown";
}

std::optional<PressureDatumProblem> pressureDatumProblem(const CfdAnalysisDefinition& definition) noexcept {
    const bool fixedByABoundary = std::ranges::any_of(
        definition.boundaryConditions, [](const CfdBoundaryCondition& condition) { return condition.fixesPressure(); });
    if (fixedByABoundary && definition.referencePressure.has_value()) {
        return PressureDatumProblem::Overdetermined;
    }
    if (!fixedByABoundary && !definition.referencePressure.has_value()) {
        return PressureDatumProblem::Missing;
    }
    return std::nullopt;
}

Result<void> validate(const CfdAnalysisDefinition& definition) {
    if (!definition.mesh.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "CFD analysis: the invalid handle cannot name the mesh to analyse");
    }
    switch (definition.mode) {
    case CfdAnalysisMode::Steady:
    case CfdAnalysisMode::Transient:
        break;
    default:
        return makeError(ErrorCode::InvalidArgument, "CFD analysis: the mode is not one this build recognises");
    }

    // EVERY RECORD THROUGH ITS OWN VALIDATOR. Not a second copy of the rules:
    // each `validate` owns what a usable value is.
    for (const Result<void>& checked :
         {validate(definition.initialCondition), validate(definition.iteration), validate(definition.convergence),
          validate(definition.linearSolver), validate(definition.output)}) {
        if (!checked.has_value()) {
            return std::unexpected(checked.error());
        }
    }
    if (definition.referencePressure.has_value()) {
        if (const Result<void> reference = validate(*definition.referencePressure); !reference.has_value()) {
            return std::unexpected(reference.error());
        }
    }
    if (definition.gravity.has_value()) {
        if (const Result<void> gravity = validate(*definition.gravity); !gravity.has_value()) {
            return std::unexpected(gravity.error());
        }
    }

    // EVERY BOUNDARY CONDITION ON ITS OWN TERMS, and the identities unique. The
    // order is kept as given; what is refused is two records claiming to be one.
    // Two conditions on the SAME FACE are not refused here: whether two faces
    // overlap is a question about the geometry, and P19-BC-001 refuses a facet
    // that two conditions claim.
    for (std::size_t i = 0; i < definition.boundaryConditions.size(); ++i) {
        if (const Result<void> condition = validate(definition.boundaryConditions[i]); !condition.has_value()) {
            return std::unexpected(condition.error());
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (definition.boundaryConditions[j].id() == definition.boundaryConditions[i].id()) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("CFD analysis: {} appears twice among the boundary conditions",
                                             definition.boundaryConditions[i].id()));
            }
        }
    }

    // THE PRESSURE DATUM: overdetermined is refused here, missing is not (see
    // `pressureDatumProblem`).
    if (pressureDatumProblem(definition) == PressureDatumProblem::Overdetermined) {
        return makeError(ErrorCode::InvalidArgument,
                         "CFD analysis: a reference pressure is given although a pressure inlet or outlet "
                         "already fixes the pressure; the datum would be overdetermined");
    }

    // MODE-COMPLETE. Each mode carries its own budget and not the other's.
    if (definition.mode == CfdAnalysisMode::Steady) {
        if (definition.transient.has_value()) {
            return makeError(ErrorCode::InvalidArgument,
                             "CFD analysis: a steady analysis has no time step or end time");
        }
        if (!definition.steady.has_value()) {
            return makeError(ErrorCode::InvalidArgument,
                             "CFD analysis: a steady analysis needs its steady settings (the iteration budget)");
        }
        return validate(*definition.steady);
    }
    if (definition.steady.has_value()) {
        return makeError(ErrorCode::InvalidArgument,
                         "CFD analysis: a transient analysis has no steady settings; its budget is per step");
    }
    if (!definition.transient.has_value()) {
        return makeError(ErrorCode::InvalidArgument,
                         "CFD analysis: a transient analysis needs a time step and an end time");
    }
    return validate(*definition.transient);
}

// --------------------------------------------------------------------------
// The fingerprint
// --------------------------------------------------------------------------

std::string canonicalText(const CfdAnalysisDefinition& definition) {
    std::string out = "cfd-definition-v1\n";
    line(out, "mesh", std::format("{}", definition.mesh.value()));
    line(out, "mode", toString(definition.mode));
    line(out, "boundary_conditions", std::format("{}", definition.boundaryConditions.size()));
    for (std::size_t i = 0; i < definition.boundaryConditions.size(); ++i) {
        conditionText(out, i, definition.boundaryConditions[i]);
    }
    line(out, "initial.velocity", vector(definition.initialCondition.velocity));
    line(out, "initial.pressure", number(definition.initialCondition.pressure));
    if (definition.referencePressure.has_value()) {
        line(out, "reference_pressure.value", number(definition.referencePressure->value));
        line(out, "reference_pressure.location", point(definition.referencePressure->location));
    } else {
        line(out, "reference_pressure", "none");
    }
    if (definition.gravity.has_value()) {
        line(out, "gravity.acceleration", vector(definition.gravity->acceleration));
        line(out, "gravity.reference_height", number(definition.gravity->referenceHeight));
    } else {
        line(out, "gravity", "none");
    }
    line(out, "iteration.velocity_relaxation", number(definition.iteration.velocityRelaxation));
    line(out, "iteration.pressure_relaxation", number(definition.iteration.pressureRelaxation));
    line(out, "convergence.momentum_residual_tolerance", number(definition.convergence.momentumResidualTolerance));
    line(out, "convergence.continuity_residual_tolerance", number(definition.convergence.continuityResidualTolerance));
    line(out, "convergence.divergence_factor", number(definition.convergence.divergenceFactor));
    line(out, "linear_solver.relative_residual_tolerance", number(definition.linearSolver.relativeResidualTolerance));
    if (definition.steady.has_value()) {
        line(out, "steady.max_iterations", std::format("{}", definition.steady->maxIterations));
    } else {
        line(out, "steady", "none");
    }
    if (definition.transient.has_value()) {
        line(out, "transient.time_step", number(definition.transient->timeStep));
        line(out, "transient.end_time", number(definition.transient->endTime));
        line(out, "transient.max_iterations_per_step", std::format("{}", definition.transient->maxIterationsPerStep));
        line(out, "transient.sampling", toString(definition.transient->sampling));
    } else {
        line(out, "transient", "none");
    }
    line(out, "output.residual_history", toString(definition.output.residualHistory));
    return out;
}

CfdDefinitionFingerprint fingerprint(const CfdAnalysisDefinition& definition) {
    // FNV-1a, 64-bit: the digest `thermalValidationPolicyFingerprint` uses, and
    // integer arithmetic only, so it is the same in every preset.
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char c : canonicalText(definition)) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 1099511628211ULL;
    }
    return CfdDefinitionFingerprint{hash};
}

std::string toString(CfdDefinitionFingerprint fingerprint) {
    return std::format("cfd-definition-v1:{:016x}", fingerprint.value);
}

// --------------------------------------------------------------------------
// The document object
// --------------------------------------------------------------------------

Result<std::unique_ptr<CfdAnalysis>> CfdAnalysis::create(std::string name, CfdAnalysisDefinition definition) {
    if (const Result<void> checked = validate(definition); !checked.has_value()) {
        return std::unexpected(checked.error());
    }
    return std::unique_ptr<CfdAnalysis>{new CfdAnalysis{std::move(name), std::move(definition)}};
}

CfdAnalysis::CfdAnalysis(std::string name, CfdAnalysisDefinition definition)
    : DocumentObject(std::move(name)), definition_(std::move(definition)) {}

std::unique_ptr<DocumentObject> CfdAnalysis::clone() const {
    // THE COPY CONSTRUCTOR, because a clone must carry the ID: redo of an added
    // object re-inserts clone(), and an object with no ID cannot be re-inserted
    // (P16-CMD-001's finding, inherited by every analysis since).
    return std::unique_ptr<DocumentObject>(new CfdAnalysis(*this));
}

bool CfdAnalysis::contentEquals(const DocumentObject& other) const {
    const auto* analysis = dynamic_cast<const CfdAnalysis*>(&other);
    // Semantic equality of the INTENT. There is nothing derived in it to exclude.
    return analysis != nullptr && definition_ == analysis->definition_;
}

std::vector<ObjectId> CfdAnalysis::dependencies() const {
    if (!definition_.mesh.isValid()) {
        return {};
    }
    // The meshing control, so editing the control -- or the body under it --
    // reaches this analysis.
    return {ObjectId::fromValue(definition_.mesh.value())};
}

Result<bool> CfdAnalysis::setDefinition(CfdAnalysisDefinition definition) {
    if (const Result<void> checked = validate(definition); !checked.has_value()) {
        return std::unexpected(checked.error());
    }
    if (definition == definition_) {
        // NO EFFECTIVE CHANGE: no revision moves and no result is invalidated.
        return false;
    }
    definition_ = std::move(definition);
    return true;
}

const CfdAnalysis* findCfdAnalysis(const Document& document, CfdAnalysisId id) noexcept {
    for (const DocumentObject& object : document.objects()) {
        if (object.id().value() == id.value()) {
            // A dynamic_cast rather than a trusted ID: a thermal analysis carrying
            // this number is not a CFD analysis.
            return dynamic_cast<const CfdAnalysis*>(&object);
        }
    }
    return nullptr;
}

std::vector<CfdAnalysisId> cfdAnalysisIds(const Document& document) {
    std::vector<CfdAnalysisId> analyses;
    for (const DocumentObject& object : document.objects()) {
        if (dynamic_cast<const CfdAnalysis*>(&object) != nullptr) {
            analyses.push_back(CfdAnalysisId::fromValue(object.id().value()));
        }
    }
    std::ranges::sort(analyses, [](CfdAnalysisId a, CfdAnalysisId b) { return a.value() < b.value(); });
    return analyses;
}

// --------------------------------------------------------------------------
// Provenance
// --------------------------------------------------------------------------

std::string_view toString(StaleReason reason) noexcept {
    switch (reason) {
    case StaleReason::Body:
        return "body";
    case StaleReason::Control:
        return "mesh_control";
    case StaleReason::Geometry:
        return "geometry";
    case StaleReason::Mesh:
        return "mesh";
    case StaleReason::Fluid:
        return "fluid";
    case StaleReason::Analysis:
        return "analysis";
    case StaleReason::Definition:
        return "definition";
    case StaleReason::Mode:
        return "mode";
    }
    return "unknown";
}

std::vector<StaleReason> staleReasons(const CfdResultSource& result, const CfdResultSource& current) {
    // EVERY MISMATCH, NOT THE FIRST, in the enum's order.
    std::vector<StaleReason> reasons;
    if (result.body != current.body) {
        reasons.push_back(StaleReason::Body);
    }
    if (result.control != current.control) {
        reasons.push_back(StaleReason::Control);
    }
    if (!(result.geometry == current.geometry)) {
        reasons.push_back(StaleReason::Geometry);
    }
    if (!(result.mesh == current.mesh)) {
        reasons.push_back(StaleReason::Mesh);
    }
    if (result.fluid != current.fluid || result.fluidRevision != current.fluidRevision) {
        reasons.push_back(StaleReason::Fluid);
    }
    if (result.analysis != current.analysis || result.analysisRevision != current.analysisRevision) {
        reasons.push_back(StaleReason::Analysis);
    }
    if (result.definition != current.definition) {
        reasons.push_back(StaleReason::Definition);
    }
    if (result.mode != current.mode) {
        reasons.push_back(StaleReason::Mode);
    }
    return reasons;
}

} // namespace bettercad::cfd
