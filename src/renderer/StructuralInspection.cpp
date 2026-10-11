// Result currentness and inspection, both derived from core (P17-VIZ-001).
//
// NO ENGINEERING VALUE IS COMPUTED IN THIS FILE. Every field of every
// inspection is read from `RecoveredFields`, `SupportReactions` or the
// canonical mesh, and every currentness answer is read from
// `meshing::Mesher::currency` or `structural::analysisState`. A grep here for
// a stress formula, a norm, a cross product or a comparison of two revision
// numbers finds nothing.
//
// AND THERE IS NO GUI STALE FLAG. `structuralDisplayState` asks the two
// authorities and reports what they said. The one thing it adds is keeping
// them SEPARATE, because a material edit leaves the mesh current and the
// result stale, and a UI told only "stale" would send the user to remesh.

#include <bettercad/renderer/StructuralInspection.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace bettercad::renderer {

StructuralDisplayState structuralDisplayState(const Document& document,
                                              const features::Regenerator& regenerator,
                                              const meshing::Mesher& mesher,
                                              MeshControlId control, AnalysisId analysis,
                                              const structural::StructuralResult* lastResult,
                                              const Error* lastFailure) {
    StructuralDisplayState out;

    // P16'S AUTHORITY, asked rather than re-derived. `Mesher::currency` holds
    // the `VolumeMeshControls` its mesh was built from, so it distinguishes
    // StaleIntent from StaleGeometry -- which `MeshInspection::statusOf`
    // records that it CANNOT do, because a bare mesh carries no record of the
    // sizing it came from.
    out.mesh = mesher.currency(document, control);

    // P17-DATA'S AUTHORITY, likewise. It is the only place in P17 that answers
    // "does this result still describe the model", and it takes the held
    // result and failure as parameters precisely so a caller can pass what it
    // holds.
    out.analysis = structural::analysisState(document, regenerator, mesher, analysis, lastResult,
                                             lastFailure);

    if (lastFailure != nullptr) {
        out.failure = *lastFailure;
    }

    // WHICH DEPENDENCIES MOVED. Only askable when there is a result to compare
    // and the inputs still resolve: `currentResultSource` fails for exactly
    // the reasons `requireStructuralModel` fails, and a stale-reason list
    // derived from an unavailable input would be noise rather than an answer.
    if (lastResult != nullptr) {
        const Result<structural::StructuralResultSource> current =
            structural::currentResultSource(document, regenerator, mesher, analysis);
        if (current.has_value()) {
            out.staleReasons = structural::staleReasons(lastResult->source(), *current);
        }
    }

    return out;
}

Result<NodeResultInspection> inspectNode(const meshing::Mesh& mesh,
                                   const structural::RecoveredFields& fields,
                                   meshing::NodeId node,
                                   const structural::PreparedRestraints* restraints) {
    // THE IDENTITY-SAFE LOOKUP, which takes the mesh and refuses a foreign
    // one. This is the guard that stops an old selection binding to a
    // remeshed result because the numeric id repeated.
    const Result<structural::NodalDisplacement> nodal = fields.displacementOf(mesh, node);
    if (!nodal.has_value()) {
        return makeError(nodal.error().code, nodal.error().message);
    }
    const meshing::Node* canonical = mesh.findNode(node);
    if (canonical == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("node {} is not a node of this mesh", node.value()));
    }

    NodeResultInspection out;
    out.node = node;
    // THE CANONICAL POSITION, as a Point3D whose unit is part of its type.
    // Not read out of a render buffer, which holds bare doubles.
    out.undeformedPosition = canonical->position;
    out.displacement = nodal->displacement;
    // STORED, not re-normed.
    out.magnitude = nodal->magnitude;

    // RESTRAINT FROM THE PREPARED RESTRAINTS, NEVER FROM THE DISPLACEMENT. A
    // node can have exactly zero displacement without being restrained, and
    // reading restraint from a zero is a guess that is usually right and
    // sometimes badly wrong.
    //
    // Absent restraints mean UNKNOWN, not unrestrained. `restrained` stays
    // false and the header says a UI must not draw a support symbol from it.
    if (restraints != nullptr) {
        if (!restraints->describes(mesh)) {
            return makeError(ErrorCode::FailedPrecondition,
                             "these prepared restraints were resolved against a different mesh");
        }
        const std::span<const meshing::NodeId> held = restraints->nodes();
        out.restrained = std::ranges::binary_search(held, node);
        if (out.restrained) {
            // WHICH components, accumulated over every restraint that names
            // this node -- a union, which is what a constrained set is. Read
            // from the resolutions' own masks; nothing is inferred.
            for (const structural::RestraintResolution& resolution : restraints->resolutions()) {
                out.restrainedComponents =
                    out.restrainedComponents.unionWith(resolution.components);
            }
        }
    }
    return out;
}

Result<ElementResultInspection> inspectElement(const meshing::Mesh& mesh,
                                         const structural::RecoveredFields& fields,
                                         meshing::ElementId element) {
    const Result<structural::ElementFields> found = fields.fieldsOf(mesh, element);
    if (!found.has_value()) {
        return makeError(found.error().code, found.error().message);
    }

    // A COPY OF STORED MEMBERS. Every one of these was computed by
    // P17-POST-001 and validated there; this function's whole content is the
    // assignment below.
    ElementResultInspection out;
    out.element = found->element;
    out.strain = found->strain;
    out.stress = found->stress;
    out.principalStress = found->principalStress;
    out.principalStrain = found->principalStrain;
    out.vonMises = found->vonMises;
    out.hydrostatic = found->hydrostatic;
    return out;
}

Result<ReactionInspection> inspectReaction(const meshing::Mesh& mesh,
                                           const structural::SupportReactions& reactions,
                                           meshing::NodeId node) {
    const Result<structural::SupportReaction> found = reactions.at(mesh, node);
    if (!found.has_value()) {
        return makeError(found.error().code, found.error().message);
    }

    ReactionInspection out;
    out.node = found->node;
    // THE QUALIFIED SIGN CONVENTION, UNCHANGED. P17-REACTION-001 froze it
    // against the one-degree-of-freedom case: this is the force the support
    // applies TO the structure. Negating it here because an arrow looked
    // better would invert every reaction in the application.
    out.force = found->force;
    out.constrained = found->constrained;
    return out;
}

std::vector<meshing::NodeId> reactionNodes(const structural::SupportReactions& reactions) {
    std::vector<meshing::NodeId> out;
    out.reserve(reactions.nodal().size());
    for (const structural::SupportReaction& reaction : reactions.nodal()) {
        out.push_back(reaction.node);
    }
    // ALREADY ASCENDING AND WITHOUT REPEATS, because `SupportReactions::nodal()`
    // is. Not sorted again here: re-sorting would hide a change in that
    // guarantee instead of breaking on it.
    return out;
}

// ---------------------------------------------------------------------------
// Overlays
// ---------------------------------------------------------------------------

namespace {

/// Whether a force vector has a direction to draw.
///
/// A ZERO VECTOR IS NOT GIVEN ONE. Every component exactly zero means no
/// arrow; anything else, including a tiny force, has a direction. There is no
/// epsilon here on purpose -- a threshold would decide that a small real load
/// is "no load", which is the presentation layer's call about pixels and not
/// this layer's about physics.
[[nodiscard]] bool pointsSomewhere(const Force3D& force) noexcept {
    return force.x.si() != 0.0 || force.y.si() != 0.0 || force.z.si() != 0.0;
}

} // namespace

std::vector<LoadGlyph> loadGlyphs(const structural::PreparedLoads& loads) {
    std::vector<LoadGlyph> out;
    out.reserve(loads.contributions().size());
    for (const structural::LoadContribution& contribution : loads.contributions()) {
        // EVERY FIELD IS A READ. The resultant was formed by P17-LOAD-001,
        // which is also where the inward-positive pressure convention lives,
        // so this layer never sees a surface normal.
        out.push_back(LoadGlyph{.load = contribution.load,
                                .kind = contribution.kind,
                                .resultant = contribution.resultant,
                                .area = contribution.area,
                                .elements = contribution.elements,
                                .hasDirection = pointsSomewhere(contribution.resultant)});
    }
    return out;
}

std::vector<NodalLoadArrow> nodalLoadArrows(const structural::PreparedLoads& loads) {
    std::vector<NodalLoadArrow> out;
    out.reserve(loads.nodal().size());
    for (const structural::NodalLoad& nodal : loads.nodal()) {
        out.push_back(NodalLoadArrow{.node = nodal.node,
                                     .force = nodal.force,
                                     .hasDirection = pointsSomewhere(nodal.force)});
    }
    // ALREADY ASCENDING AND WITHOUT REPEATS, because the prepared nodal field
    // is accumulated into an ascending map. Not re-sorted: re-sorting would
    // hide a change in that guarantee instead of breaking on it.
    return out;
}

std::vector<RestraintGlyph> restraintGlyphs(const structural::PreparedRestraints& restraints) {
    std::vector<RestraintGlyph> out;
    out.reserve(restraints.resolutions().size());
    for (const structural::RestraintResolution& resolution : restraints.resolutions()) {
        // THE COMPONENT MASK IS CARRIED, NOT COLLAPSED. A Ux-only restraint
        // and a fixed support are different supports, and a UI that drew both
        // the same would be lying about the model.
        out.push_back(RestraintGlyph{.restraint = resolution.restraint,
                                     .components = resolution.components,
                                     .nodes = resolution.nodes,
                                     .facets = resolution.facets,
                                     .degreesOfFreedom = resolution.degreesOfFreedom});
    }
    return out;
}

std::vector<ReactionArrow> reactionArrows(const structural::SupportReactions& reactions) {
    std::vector<ReactionArrow> out;
    out.reserve(reactions.nodal().size());
    for (const structural::SupportReaction& reaction : reactions.nodal()) {
        // THE SIGN IS NOT TOUCHED. P17-REACTION-001 froze it against the
        // one-degree-of-freedom case: this is the force the support applies to
        // the structure. Reversing it because an arrow "looks nicer pointing
        // the other way" would invert every reaction in the application.
        out.push_back(ReactionArrow{.node = reaction.node,
                                    .force = reaction.force,
                                    .constrained = reaction.constrained,
                                    .hasDirection = pointsSomewhere(reaction.force)});
    }
    return out;
}

bool isUsableArrowScale(double scale) noexcept {
    return std::isfinite(scale) && scale > 0.0;
}

Result<double> displayArrowLength(double magnitude, double reference, double scale) {
    if (!std::isfinite(magnitude)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("an arrow magnitude must be finite, and {} is not",
                                     magnitude));
    }
    if (!isUsableArrowScale(scale)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("an arrow display scale must be finite and positive, and "
                                     "{} is not",
                                     scale));
    }
    if (!std::isfinite(reference) || reference <= 0.0) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("an arrow reference magnitude must be finite and positive, "
                                     "and {} is not",
                                     reference));
    }
    // A DECLARED MAPPING, NOT A CONVERSION. Newtons do not become metres;
    // this is a proportion of a chosen display length, and the engineering
    // magnitude is reported separately and unchanged.
    return scale * (std::abs(magnitude) / reference);
}


// ---------------------------------------------------------------------------
// Boundary highlighting for a load or a restraint
// ---------------------------------------------------------------------------

std::optional<FaceName> loadTarget(const structural::StructuralLoad& load) {
    // READ FROM THE CANONICAL PAYLOAD. A pressure and a traction each carry a
    // FaceName; a nodal force carries a NodeId and gravity carries only an
    // acceleration, so neither has a face and neither is given one.
    if (const structural::PressureLoad* pressure = load.pressure(); pressure != nullptr) {
        return pressure->face;
    }
    if (const structural::SurfaceTractionLoad* traction = load.traction(); traction != nullptr) {
        return traction->face;
    }
    return std::nullopt;
}

FaceName restraintTarget(const structural::StructuralRestraint& restraint) {
    return restraint.face();
}

Result<MeshHighlight> highlightForLoad(const MeshView& view, const meshing::Mesh& mesh,
                                       const meshing::GeometryMeshMap& map,
                                       const structural::StructuralLoad& load) {
    const std::optional<FaceName> target = loadTarget(load);
    if (!target.has_value()) {
        // REFUSED, NOT EMPTY. "This load has no face" and "this face resolved
        // to nothing" are different facts and a UI must not conflate them.
        return makeError(ErrorCode::InvalidArgument,
                         std::format("load {} is a {} and has no CAD face to highlight",
                                     load.id().value(), structural::toString(load.kind())));
    }
    // P16's PATH, UNCHANGED. Every step from here is the mapping's.
    return highlightFor(view, mesh, map, *target);
}

Result<MeshHighlight> highlightForRestraint(const MeshView& view, const meshing::Mesh& mesh,
                                            const meshing::GeometryMeshMap& map,
                                            const structural::StructuralRestraint& restraint) {
    return highlightFor(view, mesh, map, restraintTarget(restraint));
}

} // namespace bettercad::renderer
