// The structural acceptance policy and the one path that runs every gate
// (P17-VALID-001).
//
// NO METRIC IS COMPUTED IN THIS FILE. Every quality number is read out of
// `MeshQualityReport::summaries`, which P16 fills whatever policy was active.
// A grep here for `radiusRatio =`, `sqrt`, a dihedral or a cross product finds
// nothing: one definition of each metric exists in the repository and it is in
// `src/meshing/MeshQuality.cpp`.
//
// NOTHING IS REPAIRED. There is no node move, no node swap, no `abs()` on a
// volume, no element removal, no merge and no remesh in this file. Every
// entry point takes const references and returns a report. The brief forbids
// all of it inside validation and the absence is checkable by grep.
//
// NO TOLERANCE IS LOOSENED OR DUPLICATED. The solver's residual gate, the
// equilibrium gate and the result's finiteness gate belong to
// `P17-SOLVE-001`, `P17-REACTION-001` and `P17-DATA-001`. This file calls
// them, records what they measured, and defines no second threshold beside
// any of them.
//
// THE ORDER IS THE POLICY. Stages are dependent, so the chain stops at the
// first refusing stage; within a stage every finding is reported.

#include <bettercad/structural/StructuralValidation.hpp>

#include <bettercad/structural/StructuralMaterial.hpp>
#include <bettercad/structural/StructuralSystem.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace bettercad::structural {

std::string_view toString(ValidationCode code) noexcept {
    switch (code) {
    case ValidationCode::AnalysisNotFound:
        return "analysis_not_found";
    case ValidationCode::AnalysisDefinitionInvalid:
        return "analysis_definition_invalid";
    case ValidationCode::InputsUnavailable:
        return "inputs_unavailable";
    case ValidationCode::MeshStructurallyInvalid:
        return "mesh_structurally_invalid";
    case ValidationCode::MeshHasInvalidElements:
        return "mesh_has_invalid_elements";
    case ValidationCode::QualityPolicyUnusable:
        return "quality_policy_unusable";
    case ValidationCode::ElementAccuracyBelowFloor:
        return "element_accuracy_below_floor";
    case ValidationCode::ElementOutsideQualifiedEnvelope:
        return "element_outside_qualified_envelope";
    case ValidationCode::MaterialUnusable:
        return "material_unusable";
    case ValidationCode::RestraintsUnresolvable:
        return "restraints_unresolvable";
    case ValidationCode::InsufficientRestraint:
        return "insufficient_restraint";
    case ValidationCode::LoadsUnresolvable:
        return "loads_unresolvable";
    case ValidationCode::NoLoadsApplied:
        return "no_loads_applied";
    }
    return "unknown";
}

std::string_view toString(ValidationSeverity severity) noexcept {
    switch (severity) {
    case ValidationSeverity::Warning:
        return "warning";
    case ValidationSeverity::Failure:
        return "failure";
    }
    return "unknown";
}

std::string_view toString(ValidationStatus status) noexcept {
    switch (status) {
    case ValidationStatus::Accepted:
        return "accepted";
    case ValidationStatus::AcceptedWithWarnings:
        return "accepted_with_warnings";
    case ValidationStatus::Rejected:
        return "rejected";
    }
    return "unknown";
}

std::string_view toString(StructuralValidationReport::Stage stage) noexcept {
    switch (stage) {
    case StructuralValidationReport::Stage::Analysis:
        return "analysis";
    case StructuralValidationReport::Stage::Inputs:
        return "inputs";
    case StructuralValidationReport::Stage::Quality:
        return "quality";
    case StructuralValidationReport::Stage::Material:
        return "material";
    case StructuralValidationReport::Stage::Restraints:
        return "restraints";
    case StructuralValidationReport::Stage::Loads:
        return "loads";
    case StructuralValidationReport::Stage::Complete:
        return "complete";
    }
    return "unknown";
}

ValidationSeverity severityOf(ValidationCode code) noexcept {
    // EXACTLY THREE WARNINGS, and they are listed rather than defaulted: a
    // new code added to the enum falls through to `Failure`, which is the safe
    // direction. A default of `Warning` would make a forgotten code silently
    // non-blocking, which is how a gate stops being a gate.
    //
    // `QualityPolicyUnusable` is the third and was a REFUSAL until adversarial
    // review found that a mesh control carries its own thresholds, so a user's
    // broken reporting preferences would have blocked a sound solve. P16 still
    // measures every metric in that case; only its own classification is
    // skipped, and this module reads the measurements.
    switch (code) {
    case ValidationCode::ElementOutsideQualifiedEnvelope:
    case ValidationCode::NoLoadsApplied:
    case ValidationCode::QualityPolicyUnusable:
        return ValidationSeverity::Warning;
    case ValidationCode::AnalysisNotFound:
    case ValidationCode::AnalysisDefinitionInvalid:
    case ValidationCode::InputsUnavailable:
    case ValidationCode::MeshStructurallyInvalid:
    case ValidationCode::MeshHasInvalidElements:
    case ValidationCode::ElementAccuracyBelowFloor:
    case ValidationCode::MaterialUnusable:
    case ValidationCode::RestraintsUnresolvable:
    case ValidationCode::InsufficientRestraint:
    case ValidationCode::LoadsUnresolvable:
        break;
    }
    return ValidationSeverity::Failure;
}

meshing::QualityThresholds structuralQualityThresholds() {
    // THE NUMBERS AND WHERE EACH COMES FROM. The failure bound is derived from
    // the measured accuracy law; the warning bounds are the measured edge of
    // the qualified envelope. Both are in
    // `docs/verification/P17-VALID-001/`; neither is a preference.
    //
    // RADIANS, because a `QualityThresholds` bound is SI and P16's comparison
    // does not convert. The degree figures are in the comments only.
    meshing::QualityThresholds thresholds{};

    // 3r/R. Warning at the qualified worst (RM-MESH-05 thin plate,
    // 0.000322641), failure at the accuracy floor.
    thresholds.limits[meshing::QualityMetric::TetRadiusRatio] =
        meshing::QualityThreshold{.warning = 3.0e-4, .failure = kStructuralRadiusRatioFloor};

    // Minimum internal dihedral. Qualified worst 0.006330012 rad
    // (0.362683 deg, RM-MESH-02 cylinder). NO FAILURE BOUND: the measured
    // distributions show this metric cannot separate that cylinder from a
    // 1.98-degree sliver, so a rejection line here would refuse BetterCAD's
    // own output.
    thresholds.limits[meshing::QualityMetric::TetMinDihedralAngle] =
        meshing::QualityThreshold{.warning = 6.0e-3, .failure = std::nullopt};

    // Maximum internal dihedral. Qualified worst 3.119060453 rad
    // (178.709 deg, RM-MESH-05). pi is 3.141592654, so this bound is close to
    // the metric's own limit -- which is a fact about the thin plate, not a
    // lenient choice.
    thresholds.limits[meshing::QualityMetric::TetMaxDihedralAngle] =
        meshing::QualityThreshold{.warning = 3.125, .failure = std::nullopt};

    // l_max / l_min. Warning at the qualified worst (80.0062, RM-MESH-05),
    // FAILURE at the needle family's accuracy ceiling.
    //
    // THIS METRIC SATURATES AT sqrt(3) FOR A FLATTENING ELEMENT and so sees
    // nothing of that family -- which is why the radius ratio carries its own
    // bound. But for a STRETCHING element it is the good predictor: the
    // measured law is `err ~ C * aspect` with C <= 4.45e-16 over seven orders,
    // and the radius ratio understates a needle's error by 38x. A policy with
    // only the radius-ratio bound accepted a needle whose recovered strain was
    // wrong by 2.5e-09, which adversarial review measured and this closes.
    thresholds.limits[meshing::QualityMetric::TetAspectRatio] =
        meshing::QualityThreshold{.warning = 81.0,
                                  .failure = kStructuralAspectRatioCeiling};

    return thresholds;
}

namespace {

/// A finding with its severity taken from the code, never chosen.
[[nodiscard]] ValidationFinding finding(ValidationCode code, std::string message) {
    return ValidationFinding{.code = code,
                             .severity = severityOf(code),
                             .metric = std::nullopt,
                             .element = std::nullopt,
                             .value = std::nullopt,
                             .threshold = std::nullopt,
                             .message = std::move(message)};
}

/// Orders findings for the report: refusals first, then by code, then by
/// element. DETERMINISTIC, and documented as such because a report whose order
/// moves cannot be diffed. `std::stable_sort`, so findings that tie keep the
/// order the stages produced them in.
void order(std::vector<ValidationFinding>& findings) {
    std::stable_sort(findings.begin(), findings.end(),
                     [](const ValidationFinding& a, const ValidationFinding& b) {
                         if (a.severity != b.severity) {
                             // Failure sorts before Warning, so the enum's own
                             // order is reversed here deliberately.
                             return a.severity == ValidationSeverity::Failure;
                         }
                         if (a.code != b.code) {
                             return a.code < b.code;
                         }
                         const std::uint64_t left = a.element ? a.element->value() : 0;
                         const std::uint64_t right = b.element ? b.element->value() : 0;
                         return left < right;
                     });
}

/// The worst value of @p metric and the element holding it, READ from P16's
/// summary. The direction is asked, never assumed.
struct WorstOfMetric {
    double value = 0.0;
    meshing::ElementId element{};
    bool higherIsBetter = true;
    bool present = false;
};

[[nodiscard]] WorstOfMetric worstOf(const meshing::MeshQualityReport& quality,
                                    meshing::QualityMetric metric) {
    const auto found = quality.summaries.find(metric);
    if (found == quality.summaries.end() || found->second.count == 0) {
        return WorstOfMetric{};
    }
    const meshing::QualityDirection whichWay = meshing::direction(metric);
    if (whichWay == meshing::QualityDirection::ContextOnly) {
        // A DIMENSIONED SIZE HAS NO WORST. P16 refuses a bound on one and so
        // does this: "is this volume good?" has no scale-free answer.
        return WorstOfMetric{};
    }
    const bool higherIsBetter = whichWay == meshing::QualityDirection::HigherIsBetter;
    return WorstOfMetric{.value = higherIsBetter ? found->second.minimum : found->second.maximum,
                         .element = found->second.worst,
                         .higherIsBetter = higherIsBetter,
                         .present = true};
}

/// Whether @p value is past @p bound, in @p metric's own direction.
///
/// ONE COMPARISON, used for every metric and every severity, and STRICT in the
/// same direction P16's per-element classification is -- so a value exactly
/// equal to a bound is on the good side of it. A test runs the same meshes
/// through `evaluateMeshQuality` under these thresholds and requires the
/// per-element verdict to agree with this one, because two code paths reaching
/// one answer is acceptable and two reaching different ones is the defect.
[[nodiscard]] bool past(double value, double bound, bool higherIsBetter) noexcept {
    return higherIsBetter ? value < bound : value > bound;
}

} // namespace

StructuralValidationReport
validateStructuralMeshQuality(const meshing::MeshQualityReport& quality) {
    StructuralValidationReport report{};
    report.reachedStage = StructuralValidationReport::Stage::Quality;
    report.tetCount = quality.tetCount;

    // STRUCTURE FIRST, exactly as P16 evaluates it. An inverted, degenerate or
    // non-finite element is not a bad score, it is not a tetrahedron -- and no
    // threshold can produce the `Invalid` classification, so this refusal can
    // never be reached by a policy choice.
    if (!quality.structurallyValid) {
        report.findings.push_back(finding(
            ValidationCode::MeshStructurallyInvalid,
            std::format("the mesh violates a structural invariant and cannot be analysed: {}",
                        quality.structural.issues.empty()
                            ? std::string{"no issue recorded"}
                            : quality.structural.issues.front().message)));
    }
    if (quality.invalidElements > 0) {
        report.findings.push_back(
            finding(ValidationCode::MeshHasInvalidElements,
                    std::format("{} of {} elements are structurally invalid or have a metric "
                                "that is not finite",
                                quality.invalidElements,
                                quality.tetCount + quality.triangleCount)));
    }
    if (quality.thresholdPolicyError.has_value()) {
        // A WARNING, not a refusal. P16 still measured every metric and
        // reported it as under the report-only default; only ITS
        // classification was skipped. This module reads `summaries`, so it
        // has what it needs, and refusing here would refuse a sound mesh over
        // an unrelated error in the caller's reporting preferences.
        report.findings.push_back(
            finding(ValidationCode::QualityPolicyUnusable,
                    std::format("the quality policy this report was produced under is "
                                "self-contradictory, so P16 classified nothing: {}. The "
                                "structural verdict below is unaffected, because it is read "
                                "from the measured summaries.",
                                quality.thresholdPolicyError->message)));
    }
    if (report.status() == ValidationStatus::Rejected) {
        // SHORT-CIRCUIT ON A REFUSAL ONLY. Quality metrics of a mesh that is
        // not a valid mesh are numbers describing something that is not a
        // tetrahedron, and P16 does not compute them for an invalid element
        // anyway. A WARNING must not stop the metrics being read.
        order(report.findings);
        return report;
    }

    const meshing::QualityThresholds policy = structuralQualityThresholds();
    for (const auto& [metric, bounds] : policy.limits) {
        const WorstOfMetric worst = worstOf(quality, metric);
        if (!worst.present) {
            continue;
        }

        // RECORDED FOR EVERY METRIC, pass or fail, because a report that only
        // says what went wrong cannot be compared against the next one. A
        // MEASUREMENT and not a finding: it carries no code and no severity,
        // so nothing reading it can be told a metric warned when it did not.
        report.worstByMetric.push_back(MetricObservation{
            .metric = metric, .element = worst.element, .value = worst.value});

        if (bounds.failure.has_value() && past(worst.value, *bounds.failure, worst.higherIsBetter)) {
            ValidationFinding refused = finding(
                ValidationCode::ElementAccuracyBelowFloor,
                std::format(
                    "element {} has {} {}, past the accuracy floor of {}: a strain recovered "
                    "on it is not guaranteed to {} relative. The floor is derived from the "
                    "measured kernel accuracy law, not chosen -- see ADR-042.",
                    worst.element.value(), meshing::toString(metric), worst.value,
                    *bounds.failure, kStructuralStrainAccuracy));
            refused.metric = metric;
            refused.element = worst.element;
            refused.value = worst.value;
            refused.threshold = *bounds.failure;
            report.findings.push_back(std::move(refused));
            // NOT `continue`: an element past the failure bound is also past
            // the warning bound, and reporting both would be reporting one
            // problem twice.
            continue;
        }

        if (bounds.warning.has_value() && past(worst.value, *bounds.warning, worst.higherIsBetter)) {
            ValidationFinding warned = finding(
                ValidationCode::ElementOutsideQualifiedEnvelope,
                std::format("element {} has {} {}, outside the range of every qualified "
                            "BetterCAD reference mesh ({}). Results near it carry larger "
                            "discretisation error. This is an envelope, not a rejection.",
                            worst.element.value(), meshing::toString(metric), worst.value,
                            *bounds.warning));
            warned.metric = metric;
            warned.element = worst.element;
            warned.value = worst.value;
            warned.threshold = *bounds.warning;
            report.findings.push_back(std::move(warned));
        }
    }

    order(report.findings);
    return report;
}

StructuralValidationReport validateStructuralAnalysisForSolve(
    const Document& document, const features::Regenerator& regenerator,
    const meshing::Mesher& mesher, AnalysisId analysis) {
    StructuralValidationReport report{};

    // --- 1. the analysis -----------------------------------------------------
    report.reachedStage = StructuralValidationReport::Stage::Analysis;
    const StructuralAnalysis* object = findStructuralAnalysis(document, analysis);
    if (object == nullptr) {
        report.findings.push_back(
            finding(ValidationCode::AnalysisNotFound,
                    std::format("the document has no structural analysis {}", analysis.value())));
        return report;
    }
    const StructuralAnalysisDefinition& definition = object->definition();
    if (Result<void> valid = validate(definition); !valid.has_value()) {
        report.findings.push_back(finding(
            ValidationCode::AnalysisDefinitionInvalid,
            std::format("analysis {} does not validate on its own terms: {}", analysis.value(),
                        valid.error().message)));
        return report;
    }

    // --- 2. the inputs -------------------------------------------------------
    //
    // EVERY GATE ADR-036 ESTABLISHED, INHERITED RATHER THAN RE-ASKED: the
    // control exists, the body is eligible and not behind a configuration
    // override, a mesh is held, ITS CURRENCY IS Current, the last generation
    // did not fail, and the material resolves to complete linear-elastic
    // constants. P16 recorded that nothing forces a mesh holder to ask whether
    // it is stale. This is the somebody that asks.
    report.reachedStage = StructuralValidationReport::Stage::Inputs;
    Result<StructuralModel> model =
        requireStructuralModel(document, regenerator, mesher, definition.mesh);
    if (!model.has_value()) {
        report.inputProblem = structuralInputProblem(document, regenerator, mesher, definition.mesh);
        report.findings.push_back(
            finding(ValidationCode::InputsUnavailable,
                    std::format("analysis {} cannot be prepared: {}", analysis.value(),
                                model.error().message)));
        return report;
    }
    report.nodeCount = model->mesh().mesh().nodes().size();

    // --- 3. the mesh quality -------------------------------------------------
    //
    // The quality report is the MESHER'S, computed under whatever policy the
    // mesh control asked for, and its metrics are the same numbers under any
    // policy. The structural thresholds are applied to its summaries here.
    StructuralValidationReport meshReport = validateStructuralMeshQuality(model->quality());
    report.tetCount = meshReport.tetCount;
    report.worstByMetric = std::move(meshReport.worstByMetric);
    for (ValidationFinding& found : meshReport.findings) {
        report.findings.push_back(std::move(found));
    }
    // The measurements come across whole; `status()` never consults them.
    report.reachedStage = StructuralValidationReport::Stage::Quality;
    if (report.status() == ValidationStatus::Rejected) {
        order(report.findings);
        return report;
    }

    // --- 4. the material, for THIS mode -------------------------------------
    //
    // NOT A REPEAT OF STAGE 2. `requireStructuralModel` asks for complete
    // linear-elastic constants; `LinearStaticWithGravity` additionally needs a
    // density, so a model can pass the input boundary and fail here. P15
    // decides what a mode requires, not this file.
    report.reachedStage = StructuralValidationReport::Stage::Material;
    Result<StructuralMaterial> material =
        resolveStructuralMaterial(document, model->body(), definition.mode);
    if (!material.has_value()) {
        report.findings.push_back(finding(
            ValidationCode::MaterialUnusable,
            std::format("the material does not supply what this analysis mode requires: {}",
                        material.error().message)));
        order(report.findings);
        return report;
    }

    // --- 5. the restraints ---------------------------------------------------
    report.reachedStage = StructuralValidationReport::Stage::Restraints;
    Result<MeshDofMap> numbering = buildMeshDofMap(model->mesh().mesh());
    if (!numbering.has_value()) {
        report.findings.push_back(
            finding(ValidationCode::RestraintsUnresolvable,
                    std::format("the degree-of-freedom numbering could not be built: {}",
                                numbering.error().message)));
        order(report.findings);
        return report;
    }
    Result<PreparedRestraints> restraints =
        prepareStructuralRestraints(*model, *numbering, definition.restraints);
    if (!restraints.has_value()) {
        report.findings.push_back(
            finding(ValidationCode::RestraintsUnresolvable,
                    std::format("a restraint does not resolve against the current mesh: {}",
                                restraints.error().message)));
        order(report.findings);
        return report;
    }
    report.constrainedDegreesOfFreedom = restraints->constraints().size();

    // THE NECESSARY CONDITION, AND IT IS NOT THE SUFFICIENT ONE. See
    // `kSufficientRestraintIsTheSolversToJudge`. Fewer than six constrained
    // degrees of freedom cannot remove a 3D body's six rigid-body modes,
    // because the rank of a constraint set cannot exceed its size. Six or more
    // says NOTHING, and the factorisation's pivot ratio is what decides.
    if (report.constrainedDegreesOfFreedom < kMinimumConstrainedDofs) {
        report.findings.push_back(finding(
            ValidationCode::InsufficientRestraint,
            std::format(
                "only {} degree(s) of freedom are constrained, and a three-dimensional body "
                "has six rigid-body modes, so at least {} are necessary. This is a necessary "
                "condition and not a sufficient one: more than {} does not mean the model is "
                "adequately supported, and the factorisation's pivot ratio decides that.",
                report.constrainedDegreesOfFreedom, kMinimumConstrainedDofs,
                kMinimumConstrainedDofs)));
        order(report.findings);
        return report;
    }

    // --- 6. the loads --------------------------------------------------------
    report.reachedStage = StructuralValidationReport::Stage::Loads;
    Result<PreparedLoads> loads = prepareStructuralLoads(*model, *material, definition.loads);
    if (!loads.has_value()) {
        report.findings.push_back(
            finding(ValidationCode::LoadsUnresolvable,
                    std::format("a load does not resolve against the current mesh: {}",
                                loads.error().message)));
        order(report.findings);
        return report;
    }
    report.loadedNodes = loads->nodal().size();
    if (definition.loads.empty()) {
        // A WARNING AND NOT A REFUSAL. With `F = 0` the solution is `u = 0`
        // exactly, every strain and stress is zero, and equilibrium holds
        // trivially. That is a correct answer to a question probably not
        // meant, which is what a warning is for.
        report.findings.push_back(
            finding(ValidationCode::NoLoadsApplied,
                    "the analysis applies no load, so the solution is identically zero"));
    }

    report.reachedStage = StructuralValidationReport::Stage::Complete;
    order(report.findings);
    return report;
}

Result<StructuralSolveOutcome>
solveStructuralAnalysis(const Document& document, const features::Regenerator& regenerator,
                        const meshing::Mesher& mesher, AnalysisId analysis,
                        const SolverSettings& settings, const Point3D& origin,
                        const EquilibriumTolerance& tolerance) {
    // --- the gate ------------------------------------------------------------
    //
    // ONE IMPLEMENTATION OF THE ORDER. This calls the validator rather than
    // repeating its checks, so there is no way for the two to disagree about
    // what a solve requires. The cost of re-preparing afterwards is a few
    // lookups and two O(nodes) passes -- the price ADR-036 already chose to
    // pay for re-preparing rather than holding.
    StructuralValidationReport validation =
        validateStructuralAnalysisForSolve(document, regenerator, mesher, analysis);
    if (!validation.acceptable()) {
        const std::string why = validation.findings.empty()
                                    ? std::string{"no finding recorded"}
                                    : validation.findings.front().message;
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("analysis {} is not acceptable for a solve (stage {}): {}",
                                     analysis.value(), toString(validation.reachedStage), why));
    }

    const StructuralAnalysis* object = findStructuralAnalysis(document, analysis);
    if (object == nullptr) {
        // UNREACHABLE THROUGH THE VALIDATOR, which refuses a missing analysis
        // at stage 1. Checked rather than asserted because the pointer is
        // dereferenced below and a null dereference is a worse failure than a
        // diagnostic.
        return makeError(ErrorCode::Internal,
                         std::format("analysis {} vanished after validation", analysis.value()));
    }
    const StructuralAnalysisDefinition& definition = object->definition();

    Result<StructuralModel> model =
        requireStructuralModel(document, regenerator, mesher, definition.mesh);
    if (!model.has_value()) {
        return makeError(model.error().code, model.error().message);
    }
    Result<StructuralMaterial> material =
        resolveStructuralMaterial(document, model->body(), definition.mode);
    if (!material.has_value()) {
        return makeError(material.error().code, material.error().message);
    }
    Result<MeshDofMap> numbering = buildMeshDofMap(model->mesh().mesh());
    if (!numbering.has_value()) {
        return makeError(numbering.error().code, numbering.error().message);
    }
    Result<PreparedRestraints> restraints =
        prepareStructuralRestraints(*model, *numbering, definition.restraints);
    if (!restraints.has_value()) {
        return makeError(restraints.error().code, restraints.error().message);
    }
    Result<PreparedLoads> loads = prepareStructuralLoads(*model, *material, definition.loads);
    if (!loads.has_value()) {
        return makeError(loads.error().code, loads.error().message);
    }

    // --- assemble, solve, recover -------------------------------------------
    //
    // EVERY ONE OF THESE CARRIES ITS OWN GATES AND THIS FILE ADDS NONE. The
    // solve applies the settings check, the source agreement, finiteness, the
    // pivot ratio and an independent residual. The reaction recovery applies
    // force AND moment equilibrium at the caller's tolerance. The result
    // refuses a non-finite value. Nothing is published unless all of them pass.
    Result<GlobalStructuralSystem> system =
        assembleStructuralSystem(*model, *material, *loads);
    if (!system.has_value()) {
        return makeError(system.error().code, system.error().message);
    }
    Result<SolvedSystem> solution =
        solveStructuralSystem(*system, restraints->constraints(), settings);
    if (!solution.has_value()) {
        return makeError(solution.error().code, solution.error().message);
    }
    Result<RecoveredFields> fields = recoverFields(*model, *material, *system, *solution);
    if (!fields.has_value()) {
        return makeError(fields.error().code, fields.error().message);
    }
    Result<SupportReactions> reactions = recoverSupportReactions(
        *model, *system, *restraints, *loads, *solution, origin, tolerance);
    if (!reactions.has_value()) {
        return makeError(reactions.error().code, reactions.error().message);
    }

    // --- publish -------------------------------------------------------------
    //
    // THE PROVENANCE COMES FROM P17-DATA, not from this file. `currentResultSource`
    // calls `requireStructuralModel` itself and adds the analysis and material
    // revisions, so a result's identity is built by the one place that owns
    // currency and cannot be assembled field by field here.
    Result<StructuralResultSource> source =
        currentResultSource(document, regenerator, mesher, analysis);
    if (!source.has_value()) {
        return makeError(source.error().code, source.error().message);
    }

    // The arrays are DENSE and parallel to the mesh's own enumeration, which is
    // what `StructuralResult` documents and checks. They are built by walking
    // the recovered fields in the order they were produced -- which is that
    // same enumeration -- never by indexing on a raw NodeId.
    std::vector<Translation3D> displacements;
    displacements.reserve(fields->displacements().size());
    for (const NodalDisplacement& nodal : fields->displacements()) {
        displacements.push_back(nodal.displacement);
    }
    std::vector<Strain6> strains;
    std::vector<Stress6> stresses;
    strains.reserve(fields->elements().size());
    stresses.reserve(fields->elements().size());
    for (const ElementFields& element : fields->elements()) {
        strains.push_back(element.strain);
        stresses.push_back(element.stress);
    }
    std::vector<NodalReaction> nodalReactions;
    nodalReactions.reserve(reactions->nodal().size());
    for (const SupportReaction& reaction : reactions->nodal()) {
        nodalReactions.push_back(NodalReaction{.node = reaction.node, .force = reaction.force});
    }

    Result<StructuralResult> result =
        StructuralResult::create(*source, model->mesh(), std::move(displacements),
                                 std::move(nodalReactions), std::move(strains),
                                 std::move(stresses));
    if (!result.has_value()) {
        return makeError(result.error().code, result.error().message);
    }

    return StructuralSolveOutcome{.validation = std::move(validation),
                                  .result = std::move(*result),
                                  .residual = solution->residual(),
                                  .pivotRatio = solution->pivotRatio(),
                                  .strainEnergy = solution->strainEnergy(),
                                  .freeEquations = solution->freeEquations(),
                                  .forceBalance = reactions->forceBalance(),
                                  .momentBalance = reactions->momentBalance(),
                                  .largestDisplacement = fields->largestDisplacementMagnitude(),
                                  .largestVonMises = fields->largestVonMises()};
}

} // namespace bettercad::structural
