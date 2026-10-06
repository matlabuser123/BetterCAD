// The derived structural result and the one place that decides whether it
// still describes the model (P17-DATA-001).

#include <bettercad/structural/StructuralResult.hpp>

#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/structural/StructuralAnalysis.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace bettercad::structural {
namespace {

/// Where @p node sits in @p mesh's enumeration, or npos.
///
/// Binary search, and it is correct only because `Mesh` guarantees nodes are
/// stored in ascending NodeId. That guarantee is documented on the class and is
/// what this milestone relies on instead of assuming the IDs are dense.
[[nodiscard]] std::size_t positionOf(const meshing::Mesh& mesh, meshing::NodeId node) noexcept {
    const std::span<const meshing::Node> nodes = mesh.nodes();
    const auto found = std::ranges::lower_bound(
        nodes, node, {}, [](const meshing::Node& candidate) { return candidate.id; });
    if (found == nodes.end() || found->id != node) {
        return static_cast<std::size_t>(-1);
    }
    return static_cast<std::size_t>(found - nodes.begin());
}

} // namespace

StructuralResult::StructuralResult(StructuralResultSource source,
                                   std::vector<Translation3D> displacements,
                                   std::vector<NodalReaction> reactions,
                                   std::vector<Strain6> strains,
                                   std::vector<Stress6> stresses) noexcept
    : source_(source)
    , displacements_(std::move(displacements))
    , reactions_(std::move(reactions))
    , strains_(std::move(strains))
    , stresses_(std::move(stresses)) {}

Result<StructuralResult> StructuralResult::create(StructuralResultSource source,
                                                  const meshing::VolumeMesh& mesh,
                                                  std::vector<Translation3D> displacements,
                                                  std::vector<NodalReaction> reactions,
                                                  std::vector<Strain6> strains,
                                                  std::vector<Stress6> stresses) {
    // THE SOURCE MUST NAME THIS MESH. Checked first, because every other check
    // below is against `mesh` and would otherwise be validating the result
    // against a mesh it does not claim to belong to.
    if (!(source.mesh == mesh.mesh().stamp())) {
        return makeError(ErrorCode::InvalidArgument,
                         "structural result: the source stamp does not name this mesh");
    }

    const std::size_t nodes = mesh.nodeCount();
    const std::size_t tetrahedra = mesh.tetrahedronCount();
    if (nodes == 0 || tetrahedra == 0) {
        // An empty mesh is not a solved model. P16 already refuses to publish a
        // zero-element mesh, so this is defence at the next boundary rather
        // than a case that can arrive through the validated path.
        return makeError(ErrorCode::FailedPrecondition,
                         "structural result: a mesh with no nodes or no elements cannot carry a "
                         "solution");
    }
    if (displacements.size() != nodes) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("structural result: {} displacements for {} nodes",
                                     displacements.size(), nodes));
    }
    if (strains.size() != tetrahedra || stresses.size() != tetrahedra) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("structural result: {} strains and {} stresses for {} "
                                     "tetrahedra",
                                     strains.size(), stresses.size(), tetrahedra));
    }

    for (std::size_t i = 0; i < displacements.size(); ++i) {
        if (!isFinite(displacements[i].x) || !isFinite(displacements[i].y) ||
            !isFinite(displacements[i].z)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("structural result: the displacement of node {} is not "
                                         "finite",
                                         mesh.mesh().nodes()[i].id.value()));
        }
    }
    for (std::size_t i = 0; i < strains.size(); ++i) {
        if (!isFinite(strains[i])) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("structural result: the strain of element {} is not "
                                         "finite",
                                         i));
        }
        if (!isFinite(stresses[i])) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("structural result: the stress of element {} is not "
                                         "finite",
                                         i));
        }
    }

    // REACTIONS: ascending, unique, and every node really in this mesh.
    // Ascending because the order is part of the contract -- a caller summing
    // reactions for an equilibrium check must get the same floating-point
    // answer every run, and addition is not associative.
    meshing::NodeId previous;
    for (const NodalReaction& reaction : reactions) {
        if (!reaction.node.isValid()) {
            return makeError(ErrorCode::InvalidArgument,
                             "structural result: a reaction names the invalid node handle");
        }
        if (previous.isValid() && !(previous < reaction.node)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("structural result: reactions are not in ascending node "
                                         "order at node {}",
                                         reaction.node.value()));
        }
        if (positionOf(mesh.mesh(), reaction.node) == static_cast<std::size_t>(-1)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("structural result: node {} carries a reaction but is "
                                         "not in this mesh",
                                         reaction.node.value()));
        }
        if (!isFinite(reaction.force)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("structural result: the reaction at node {} is not "
                                         "finite",
                                         reaction.node.value()));
        }
        previous = reaction.node;
    }

    return StructuralResult(source, std::move(displacements), std::move(reactions),
                            std::move(strains), std::move(stresses));
}

bool StructuralResult::describes(const meshing::VolumeMesh& mesh) const noexcept {
    // STAMPS, NEVER COUNTS. Two meshes of one body can have identical node and
    // element counts and be different meshes; a result read across that gap
    // would map every value onto the wrong material.
    return source_.mesh.isValid() && source_.mesh == mesh.mesh().stamp();
}

Result<Translation3D> StructuralResult::displacementOf(const meshing::VolumeMesh& mesh,
                                                       meshing::NodeId node) const {
    if (!describes(mesh)) {
        return makeError(ErrorCode::FailedPrecondition,
                         "structural result: this result was not computed on that mesh");
    }
    const std::size_t position = positionOf(mesh.mesh(), node);
    if (position == static_cast<std::size_t>(-1) || position >= displacements_.size()) {
        return makeError(ErrorCode::NotFound,
                         std::format("structural result: node {} is not in this mesh",
                                     node.value()));
    }
    return displacements_[position];
}

std::string_view toString(ResultCurrency currency) noexcept {
    switch (currency) {
    case ResultCurrency::NoResult:
        return "no result";
    case ResultCurrency::Current:
        return "current";
    case ResultCurrency::Stale:
        return "stale";
    }
    return "unknown";
}

std::string_view toString(AnalysisState state) noexcept {
    switch (state) {
    case AnalysisState::NoAnalysis:
        return "no analysis";
    case AnalysisState::InputsUnavailable:
        return "inputs unavailable";
    case AnalysisState::Ready:
        return "ready";
    case AnalysisState::SolvedCurrent:
        return "solved: current";
    case AnalysisState::SolvedStale:
        return "solved: stale";
    case AnalysisState::SolveFailed:
        return "solve failed";
    }
    return "unknown";
}

Result<StructuralResultSource> currentResultSource(const Document& document,
                                                   const features::Regenerator& regenerator,
                                                   const meshing::Mesher& mesher,
                                                   AnalysisId analysis) {
    const StructuralAnalysis* intent = findStructuralAnalysis(document, analysis);
    if (intent == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("there is no structural analysis {}", analysis.value()));
    }

    // EVERY INPUT GATE, INHERITED RATHER THAN RE-ASKED. This is the one call
    // that establishes geometry eligibility, mesh currency and a usable
    // material, and ADR-036 made it the only way to establish them.
    const Result<StructuralModel> model =
        requireStructuralModel(document, regenerator, mesher, intent->definition().mesh);
    if (!model.has_value()) {
        return std::unexpected(model.error());
    }

    // The material's identity and revision, which the input boundary has no
    // reason to carry: it needs the CONSTANTS, and currentness needs to know
    // whether the material they came from has since been edited.
    const Result<const features::Material*> material = features::requireEffectiveMaterial(document);
    if (!material.has_value()) {
        return std::unexpected(material.error());
    }

    return StructuralResultSource{
        .body = model->body(),
        .control = model->control(),
        .geometry = model->geometryRevision(),
        .mesh = model->mesh().mesh().stamp(),
        .material = (*material)->materialId(),
        .materialRevision = (*material)->revision(),
        .analysis = analysis,
        .analysisRevision = intent->revision(),
    };
}

ResultCurrency resultCurrency(const StructuralResult* result,
                              const StructuralResultSource& current) {
    if (result == nullptr) {
        return ResultCurrency::NoResult;
    }
    return result->source() == current ? ResultCurrency::Current : ResultCurrency::Stale;
}

AnalysisState analysisState(const Document& document, const features::Regenerator& regenerator,
                            const meshing::Mesher& mesher, AnalysisId analysis,
                            const StructuralResult* lastResult, const Error* lastFailure) {
    if (findStructuralAnalysis(document, analysis) == nullptr) {
        return AnalysisState::NoAnalysis;
    }
    if (lastFailure != nullptr) {
        // A FAILED ATTEMPT IS ITS OWN STATE, whether or not an older result
        // survives it. Reporting such a result as merely stale would hide that
        // the attempt to replace it failed; reporting it current would be a
        // lie. The distinction MeshCurrency draws between StaleIntent and
        // GenerationFailed, for the same reason.
        return AnalysisState::SolveFailed;
    }

    const Result<StructuralResultSource> current =
        currentResultSource(document, regenerator, mesher, analysis);
    if (!current.has_value()) {
        return AnalysisState::InputsUnavailable;
    }

    switch (resultCurrency(lastResult, *current)) {
    case ResultCurrency::NoResult:
        return AnalysisState::Ready;
    case ResultCurrency::Current:
        return AnalysisState::SolvedCurrent;
    case ResultCurrency::Stale:
        return AnalysisState::SolvedStale;
    }
    return AnalysisState::InputsUnavailable;
}

} // namespace bettercad::structural
