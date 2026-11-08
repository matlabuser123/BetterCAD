/// \file
/// P19-CONT-001 — the continuity evaluation. ADR-074.
///
/// THE SHAPE OF THIS FILE. A gate that is total over its inputs and reports the
/// earliest phase that fails; then an evaluation that calls one qualified
/// operator for the interior and walks BOUNDARY faces only for the attribution
/// that operator cannot do, because `divergeCfdMassFlux` takes no boundary.
/// There is no internal-face loop here at all: every per-cell residual was
/// decided by the divergence, and re-deciding one would be the duplication
/// ADR-074 Decision 1 rejects.
///
/// THE HEADLINE NUMBER COMES FROM THE QUALIFIED OPERATOR AND NOT FROM THIS
/// FILE'S OWN SUM. `netBoundaryFlux()` is the divergence's value verbatim. The
/// boundary walk below produces `inflow`, `outflow`, the per-patch breakdown and
/// the throughput, and its two reconstructions of the net are MEASURED against
/// the divergence's value rather than substituted for it -- which is what makes
/// `directionalSplitResidual` and `patchSumResidual` mean something. Two sums of
/// the same faces are two sources of truth; one of them has been qualified.

#include <bettercad/cfd/CfdContinuity.hpp>

#include <cmath>
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace bettercad::cfd {

namespace {

/// An input the caller got wrong.
[[nodiscard]] CfdContinuityFailure failureOf(CfdContinuityProblem problem, std::string message) {
    return CfdContinuityFailure{problem, Error{ErrorCode::InvalidArgument, std::move(message)}};
}

/// A fault in THIS MODULE rather than in the caller's input.
///
/// A SEPARATE CODE, as `assembleCfdDiffusion` and then `assembleCfdMomentum`
/// both have it: `InvalidArgument` for an input and `Internal` for an internal
/// inconsistency. `EvaluationIsInconsistent` judges this evaluator's agreement
/// with a sub-operator, so blaming the caller for it would be the same category
/// error as a wrong tag -- a review found exactly that in P19-MOM-001's first
/// draft, in the milestone whose whole error-channel argument is that a tag must
/// say what happened.
[[nodiscard]] CfdContinuityFailure internalFailure(CfdContinuityProblem problem,
                                                   std::string message) {
    return CfdContinuityFailure{problem, Error{ErrorCode::Internal, std::move(message)}};
}

/// THE MESSAGE IS BUILT ONLY WHEN A CALLER PASSES SOMEWHERE TO PUT IT, which is
/// what makes `cfdContinuityProblem`'s `noexcept` honest: it allocates nothing.
template <typename Describe>
[[nodiscard]] std::optional<CfdContinuityProblem>
refuse(CfdContinuityProblem problem, std::string* message, Describe describe) {
    if (message != nullptr) {
        *message = describe();
    }
    return problem;
}

/// Whether `ADR-067`'s table prescribes `phi = FixedValue 0` at @p kind.
///
/// A TOTAL SWITCH ON THE KIND, AND NOT `treatmentOf(kind).massFlux ==
/// FixedValue`, because that predicate does NOT separate a wall from a velocity
/// inlet: the table gives `phi` as `FixedValue` at both, and only the VALUE
/// differs -- `0` at the three walls and `rho (U_b . S_b)` at a velocity inlet.
/// Reading the policy would have counted every velocity inlet as a wall and
/// called an open domain closed, which is the kind of mistake that makes a
/// closed-domain check pass on a mesh with an inlet in it.
///
/// NO `default:` LABEL. The project builds with `-Wswitch` inside `-Werror`, so
/// a seventh `CfdBoundaryConditionKind` fails this translation unit rather than
/// silently falling through to "not a wall".
[[nodiscard]] bool isWallKind(CfdBoundaryConditionKind kind) noexcept {
    switch (kind) {
    case CfdBoundaryConditionKind::NoSlipWall:
    case CfdBoundaryConditionKind::MovingWall:
    case CfdBoundaryConditionKind::Slip:
        return true;
    case CfdBoundaryConditionKind::VelocityInlet:
    case CfdBoundaryConditionKind::PressureInlet:
    case CfdBoundaryConditionKind::PressureOutlet:
        return false;
    }
    return false;
}

/// `|a - b| / scale`, or `0.0` when nothing was accumulated.
///
/// THE DENOMINATOR IS THE MAGNITUDE ACCUMULATED, NOT THE ANSWER ARRIVED AT.
/// Stating a floating-point departure against the answer is `P19-GRAD-001`'s
/// finding r8, and `P19-DIV-001`'s own first experiment reported `9.992e+284`
/// for arithmetic correct to half an eps by doing it.
///
/// ZERO WHEN `scale` IS ZERO IS A STATEMENT ABOUT AN EMPTY OR EXACTLY
/// CANCELLING INPUT, not about perfect agreement, and the summary's comment says
/// so. The alternative -- dividing by zero to produce a NaN -- would then be
/// caught by the finiteness phase and reported as a non-finite result, blaming
/// the arithmetic for a mesh with no boundary flux.
[[nodiscard]] double relativeDeparture(double a, double b, double scale) noexcept {
    if (!(scale > 0.0)) {
        return 0.0;
    }
    return std::abs(a - b) / scale;
}

/// THE ONE DECISION PATH. The evaluation and both predictors reach the gate
/// through here, so there is no second copy to drift.
///
/// EACH PHASE IS ITS OWN PASS, which is what makes the declared enum order the
/// check order BETWEEN phases. Within one pass the first offending face decides,
/// and no claim is made about the enum order there.
[[nodiscard]] std::optional<CfdContinuityProblem>
problemOf(const CfdTopology& topology, const CfdGeometry& geometry,
          const PreparedCfdBoundary& boundary, const CfdContinuityInput& input,
          std::string* message) {
    // ---- PHASE 1: the metrics describe this topology ---------------------
    if (!geometry.describes(topology)) {
        return refuse(CfdContinuityProblem::GeometryDescribesAnotherTopology, message, [&] {
            return std::format("the finite-volume metrics were built for a different mesh "
                               "generation than the topology, so its {} cells and {} faces do not "
                               "mean the same thing",
                               topology.cells(), topology.faces());
        });
    }

    // ---- PHASE 2: the boundary was prepared against this topology --------
    //
    // THE CONDITIONAL FORM, which is `P19-DIFF-001`'s correction and every
    // operator since has inherited, copied here in their shape: a
    // default-constructed source is compared against nothing, because
    // `bindCfdBoundary` deliberately returns one and an unconditional comparison
    // would refuse every seam-built boundary. `P19-CONV-001`'s first draft
    // dropped the comparison altogether and was strictly weaker than the
    // operator it cited as precedent; a review found it. The boundary-face count
    // is compared unconditionally, because that one always means something.
    if (boundary.boundaryFaces() != topology.boundaryFaces()) {
        return refuse(CfdContinuityProblem::BoundaryDescribesAnotherTopology, message, [&] {
            return std::format(
                "the prepared boundary holds {} boundary faces and the topology holds {}",
                boundary.boundaryFaces(), topology.boundaryFaces());
        });
    }
    const CfdBoundarySource& source = boundary.source();
    if (!(source == CfdBoundarySource{}) &&
        (!(source.mesh == topology.mesh()) || source.cells != topology.cells() ||
         source.boundaryFaces != topology.boundaryFaces())) {
        return refuse(CfdContinuityProblem::BoundaryDescribesAnotherTopology, message, [&] {
            return std::format("the prepared boundary was built for a mesh with {} cells and {} "
                               "boundary faces, or for a different mesh generation; the topology "
                               "holds {} and {}",
                               source.cells, source.boundaryFaces, topology.cells(),
                               topology.boundaryFaces());
        });
    }

    // ---- PHASE 3: every face names cells the topology holds ---------------
    //
    // THIS EVALUATOR DOES NOT INDEX BY OWNER OR NEIGHBOUR -- it walks cells and
    // boundary faces -- but `divergeCfdMassFlux` does, and its own gate has NO
    // enumerator for an out-of-range cell, so the guard lives here. The
    // unguarded `SIZE_MAX` index reaching WRITES was found by `P19-DIV-001`'s
    // review, and found AGAIN by `P19-CONV-001`'s review one milestone later
    // because the correction had regressed. It is not regressing a third time.
    const std::size_t cells = topology.cells();
    const std::size_t faces = topology.faces();
    for (std::size_t f = 0; f < faces; ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const CellIndex owner = topology.owner(face);
        if (!owner.isValid() || owner.value() >= cells) {
            return refuse(CfdContinuityProblem::TopologyNamesAnUnknownCell, message, [&] {
                return std::format("face {} names owner cell {}, and the topology holds {} cells",
                                   f, owner.value(), cells);
            });
        }
        const std::optional<CellIndex> neighbour = topology.neighbour(face);
        if (neighbour.has_value() && (!neighbour->isValid() || neighbour->value() >= cells)) {
            return refuse(CfdContinuityProblem::TopologyNamesAnUnknownCell, message, [&] {
                return std::format(
                    "face {} names neighbour cell {}, and the topology holds {} cells", f,
                    neighbour->value(), cells);
            });
        }
        // AN INTERNAL FACE MUST NAME A NEIGHBOUR AT ALL, and this arm is a
        // review's correction. `divergeCfdMassFlux` refuses "internal face {}
        // has no usable neighbour" from its BODY, outside its own gate, so its
        // predictor cannot foresee it -- and this gate checked only the range of
        // a neighbour that EXISTED, so a `nullopt` on an internal face passed
        // here and was refused there. That made this file's claim that
        // "the only refusal its predictor cannot foresee is
        // DivergenceIsNotFinite" false, and would have reported a topology
        // fault under `ResultIsNotFinite` with `InvalidArgument`, blaming the
        // caller's phi for it. Closing the gap makes the claim true, which is
        // better than weakening the tag.
        if (!neighbour.has_value() && topology.isInternal(face)) {
            return refuse(CfdContinuityProblem::TopologyNamesAnUnknownCell, message, [&] {
                return std::format("internal face {} names no neighbour cell, so the divergence's "
                                   "scatter has nothing to subtract it from",
                                   f);
            });
        }
    }

    // ---- PHASE 3b: every cell has a usable volume -------------------------
    //
    // THE DIVERGENCE'S OTHER BODY REFUSAL, for the same reason as the arm
    // above. It refuses "cell {} has no volume in these metrics, so they do not
    // describe this topology after all" from its body, and its own comment
    // calls that "phase 1's currency failure surfacing late" -- so it is
    // reported here under phase 1's enumerator rather than a new one, because
    // that is what it is. Without this arm the refusal arrived as
    // `ResultIsNotFinite`, with a message about geometry inside an error about
    // a non-finite result.
    for (std::size_t c = 0; c < cells; ++c) {
        const CellIndex cell = CellIndex::fromValue(c);
        if (!(geometry.cellVolume(cell).si() > 0.0)) {
            return refuse(CfdContinuityProblem::GeometryDescribesAnotherTopology, message, [&] {
                return std::format("cell {} has no volume in these metrics, so they do not "
                                   "describe this topology after all",
                                   c);
            });
        }
    }

    // ---- PHASE 4: every boundary face names a patch the topology holds ----
    //
    // REACHED BY THIS EVALUATOR'S OWN INDEXING, unlike phase 3: the per-patch
    // breakdown subscripts a vector by `patchOf(...).value()`, so an invalid or
    // out-of-range patch is a write past the end rather than a wrong number.
    const std::size_t patches = topology.patches();
    const std::size_t boundaryFaces = topology.boundaryFaces();
    for (std::size_t b = 0; b < boundaryFaces; ++b) {
        const PatchIndex patch = topology.patchOf(BoundaryFaceIndex::fromValue(b));
        if (!patch.isValid() || patch.value() >= patches) {
            return refuse(CfdContinuityProblem::TopologyNamesAnUnknownPatch, message, [&] {
                return std::format(
                    "boundary face {} names patch {}, and the topology holds {} patches", b,
                    patch.value(), patches);
            });
        }
    }

    // ---- PHASE 5: the flux is one value per FACE -------------------------
    if (input.faceFlux.size() != faces) {
        return refuse(CfdContinuityProblem::ValueLengthWrong, message, [&] {
            return std::format("the face flux phi holds {} values and the topology has {} faces",
                               input.faceFlux.size(), faces);
        });
    }

    // ---- PHASE 6: every supplied value is finite -------------------------
    //
    // AFTER THE LENGTH, so a wrong length is reported as a length and not as
    // whatever happens to sit in it. The same array in the same order as phase
    // 5, so the two cannot disagree about what they cover -- the hole
    // `P19-DIFF-001`'s review found, where two arrays were validated for LENGTH
    // and never for VALUE and a single NaN flowed through into the source.
    for (std::size_t f = 0; f < input.faceFlux.size(); ++f) {
        if (!std::isfinite(input.faceFlux[f])) {
            return refuse(CfdContinuityProblem::ValueNotFinite, message, [&] {
                return std::format("the face flux phi is not finite at face {}", f);
            });
        }
    }

    return std::nullopt;
}

} // namespace

std::string_view toString(CfdContinuityProblem problem) noexcept {
    switch (problem) {
    case CfdContinuityProblem::GeometryDescribesAnotherTopology:
        return "geometry describes another topology";
    case CfdContinuityProblem::BoundaryDescribesAnotherTopology:
        return "boundary describes another topology";
    case CfdContinuityProblem::TopologyNamesAnUnknownCell:
        return "topology names an unknown cell";
    case CfdContinuityProblem::TopologyNamesAnUnknownPatch:
        return "topology names an unknown patch";
    case CfdContinuityProblem::ValueLengthWrong:
        return "value length wrong";
    case CfdContinuityProblem::ValueNotFinite:
        return "value not finite";
    case CfdContinuityProblem::ResultIsNotFinite:
        return "result is not finite";
    case CfdContinuityProblem::EvaluationIsInconsistent:
        return "evaluation is inconsistent";
    }
    return "unknown continuity problem";
}

// ---------------------------------------------------------------------------
// Report accessors
// ---------------------------------------------------------------------------

std::optional<MassFlowRate> CfdContinuityReport::cellResidualAt(CellIndex cell) const noexcept {
    return divergence_.at(cell);
}

MassFlowRate CfdContinuityReport::largestCellResidual() const noexcept {
    return MassFlowRate::fromSi(largestCellResidualSi_);
}

std::optional<CellIndex> CfdContinuityReport::worstCell() const noexcept { return worstCell_; }

MassFlowRate CfdContinuityReport::inflow() const noexcept {
    return MassFlowRate::fromSi(inflowSi_);
}

MassFlowRate CfdContinuityReport::outflow() const noexcept {
    return MassFlowRate::fromSi(outflowSi_);
}

MassFlowRate CfdContinuityReport::netBoundaryFlux() const noexcept {
    return MassFlowRate::fromSi(netBoundarySi_);
}

MassFlowRate CfdContinuityReport::throughput() const noexcept {
    return MassFlowRate::fromSi(throughputSi_);
}

std::optional<CfdMassBalance> CfdContinuityReport::massBalance() const noexcept {
    return massBalance_;
}

/// The only thing that may build a report.
struct CfdContinuityBuilder {
    [[nodiscard]] static std::expected<CfdContinuityReport, CfdContinuityFailure>
    build(const CfdTopology& topology, const CfdGeometry& geometry,
          const PreparedCfdBoundary& boundary, const CfdContinuityInput& input) {
        std::string message;
        if (const std::optional<CfdContinuityProblem> problem =
                problemOf(topology, geometry, boundary, input, &message)) {
            return std::unexpected(failureOf(*problem, std::move(message)));
        }

        // ---- the sub-operator's refusal is PREDICTED before it is called ---
        //
        // ADR-074 Decision 10, and ADR-073 Decision 7 before it. Of the seven
        // problems `CfdDivergenceProblem` declares, the mass-flux path can
        // report three -- `GeometryDescribesAnotherTopology`, `ValueLengthWrong`
        // and `ValueNotFinite` -- and phases 1, 5 and 6 above cover all three
        // with the same meaning. So once this gate has passed, the predictor
        // must agree; if it does not, the divergence's gate has moved underneath
        // this one.
        const CfdMassFluxDivergenceInput divergenceInput{input.faceFlux};
        if (const std::optional<CfdDivergenceProblem> predicted =
                cfdMassFluxDivergenceProblem(topology, geometry, divergenceInput)) {
            return std::unexpected(internalFailure(
                CfdContinuityProblem::EvaluationIsInconsistent,
                std::format("the continuity gate accepted an input that the divergence refuses "
                            "as \"{}\"; the two gates no longer agree",
                            toString(*predicted))));
        }

        Result<CfdMassFluxDivergenceField> divergence =
            divergeCfdMassFlux(topology, geometry, divergenceInput);
        if (!divergence.has_value()) {
            // THE ONLY REFUSAL ITS PREDICTOR CANNOT FORESEE is
            // `DivergenceIsNotFinite`, which judges an output the predictor
            // never computes -- its own header says so. Its entry points return
            // an UNTAGGED `Error`, which that milestone records as a limitation,
            // so the tag cannot be read and is inferred from the predictor's
            // agreement above. The inference is exactly as strong as the
            // divergence's documented enumeration and no stronger, and it is
            // written down here rather than presented as a reading.
            //
            // THAT CLAIM WAS FALSE WHEN FIRST WRITTEN, AND A REVIEW FOUND IT BY
            // READING THE OPERATOR RATHER THAN ITS HEADER. `divergeCfdMassFlux`
            // has TWO refusal sites in its BODY, outside its gate and so beyond
            // its predictor: a cell with no usable volume, and an internal face
            // with no usable neighbour. Either would have arrived here and been
            // reported as `ResultIsNotFinite` with `InvalidArgument` -- a tag
            // naming the wrong phase and a code blaming the caller's phi for a
            // geometry or topology fault. Phases 3 and 3b now cover both, which
            // makes the claim true rather than weakening it. Both remain
            // unreachable while `buildCfdGeometry` and `buildCfdTopology` hold.
            return std::unexpected(failureOf(
                CfdContinuityProblem::ResultIsNotFinite,
                std::format("the divergence of the supplied phi is not finite: {}",
                            divergence.error().message)));
        }

        // CONSTRUCTED WITH THE DIVERGENCE, not default-constructed and filled:
        // `CfdDivergenceField`'s default constructor is private, so a report
        // without a divergence does not compile. The sub-operator's "a field
        // that exists has been through the gate" invariant is inherited here
        // rather than restated.
        CfdContinuityReport report{std::move(*divergence)};
        const CfdDivergenceSummary& divergenceSummary = report.divergence_.summary();

        // ---- the per-cell residual, from the divergence's own values --------
        //
        // A WALK FOR THE WORST CELL, NOT A SECOND ACCUMULATION. Each value is
        // the divergence's; only the maximum and its location are computed here,
        // because `CfdDivergenceField` reports `largestMagnitude()` without
        // saying WHERE. The suite requires this maximum to equal that accessor
        // exactly, which it must: a maximum over the same finite values is
        // order-independent in a way a sum is not.
        const std::size_t cells = topology.cells();
        for (std::size_t c = 0; c < cells; ++c) {
            const CellIndex cell = CellIndex::fromValue(c);
            const std::optional<MassFlowRate> residual = report.divergence_.at(cell);
            if (!residual.has_value()) {
                return std::unexpected(internalFailure(
                    CfdContinuityProblem::EvaluationIsInconsistent,
                    std::format("the divergence holds no residual for cell {} of {}", c, cells)));
            }
            const double magnitude = std::abs(residual->si());
            // STRICTLY GREATER, so the FIRST cell in `CellIndex` order wins a
            // tie -- which the header promises and which a uniform field on a
            // symmetric mesh reaches.
            if (!report.worstCell_.has_value() || magnitude > report.largestCellResidualSi_) {
                report.largestCellResidualSi_ = magnitude;
                report.worstCell_ = cell;
            }
        }

        // ---- the boundary walk: attribution only ---------------------------
        //
        // `BoundaryFaceIndex` ORDER, ONCE EACH (ADR-074 Decision 7). Nothing
        // here iterates an unordered container, and the per-patch subtotals
        // accumulate in this same order within each patch.
        const std::size_t patchCount = topology.patches();
        report.patches_.resize(patchCount);
        for (std::size_t p = 0; p < patchCount; ++p) {
            report.patches_[p].patch = PatchIndex::fromValue(p);
        }

        // Per patch, whether any face has been seen yet. Needed because "no face
        // seen yet" and "faces of mixed kinds" are BOTH an empty optional on the
        // record, and the two must not be confused while building: without this
        // flag the first face of a patch would be indistinguishable from a patch
        // already found to be mixed, and would overwrite the reset.
        //
        // THERE WAS A SECOND VECTOR HERE, `patchMixed`, AND IT WAS DEAD. A
        // review proved it by deleting the write and finding the suite green,
        // which it had to be: `record.kind.reset()` on the next line already
        // does the work, and the loop that read `patchMixed` afterwards only
        // called `reset()` a second time. By this project's own standard --
        // `CfdDivergence.hpp`'s "a summary member that cannot vary is decoration
        // that reads as evidence" -- a guarded distinction that cannot change an
        // output is scaffolding, so it is gone rather than commented.
        std::vector<bool> patchSeen(patchCount, false);

        double absoluteSum = 0.0;
        const std::size_t boundaryFaces = topology.boundaryFaces();
        for (std::size_t b = 0; b < boundaryFaces; ++b) {
            const BoundaryFaceIndex boundaryFace = BoundaryFaceIndex::fromValue(b);
            const FaceIndex face = topology.faceOf(boundaryFace);
            if (!face.isValid() || face.value() >= input.faceFlux.size()) {
                return std::unexpected(internalFailure(
                    CfdContinuityProblem::EvaluationIsInconsistent,
                    std::format("boundary face {} maps to face {}, which is not one of the {} "
                                "faces the flux covers",
                                b, face.value(), input.faceFlux.size())));
            }
            const double phi = input.faceFlux[face.value()];

            const PatchIndex patch = topology.patchOf(boundaryFace);
            // Phase 4 proved this in range; the index is re-derived and not
            // trusted from there, because a gate that ran earlier is not the
            // same statement as a bound that holds here.
            if (!patch.isValid() || patch.value() >= patchCount) {
                return std::unexpected(internalFailure(
                    CfdContinuityProblem::EvaluationIsInconsistent,
                    std::format("boundary face {} names patch {} after the gate accepted it", b,
                                patch.value())));
            }
            CfdPatchMassFlow& record = report.patches_[patch.value()];
            record.faces += 1;

            // ---- DIRECTION FROM THE SIGN, NEVER FROM THE KIND --------------
            //
            // ADR-074 Decision 4. `phi` is signed out of the fluid at a
            // boundary face, so positive is outflow whatever the facet is
            // called. `PressureInlet` carries `phi` as `Computed` precisely
            // because the rate is an outcome, so a facet named an inlet may
            // carry outflow, and `P19-CONV-001` already reports
            // `backflowFaces`. Reading the kind here would be wrong exactly in
            // the regime the diagnostic exists for.
            if (phi > 0.0) {
                record.outflowFaces += 1;
                record.outflow = record.outflow + MassFlowRate::fromSi(phi);
                report.outflowSi_ += phi;
                report.summary_.outflowFaces += 1;
            } else if (phi < 0.0) {
                record.inflowFaces += 1;
                record.inflow = record.inflow + MassFlowRate::fromSi(-phi);
                report.inflowSi_ += -phi;
                report.summary_.inflowFaces += 1;
            } else {
                // `phi == 0.0`, and `-0.0` lands here too, which is correct: it
                // is neither inflow nor outflow and adding it to either would
                // make the face counts depend on a signed zero.
                record.zeroFluxFaces += 1;
                report.summary_.zeroFluxFaces += 1;
            }
            record.net = record.net + MassFlowRate::fromSi(phi);
            absoluteSum += std::abs(phi);

            const std::optional<CfdBoundaryFace> boundaryRecord = boundary.face(boundaryFace);
            if (!boundaryRecord.has_value()) {
                return std::unexpected(internalFailure(
                    CfdContinuityProblem::EvaluationIsInconsistent,
                    std::format("the prepared boundary holds no record for boundary face {} of {}",
                                b, boundaryFaces)));
            }
            if (isWallKind(boundaryRecord->kind)) {
                report.summary_.wallFaces += 1;
            } else {
                report.summary_.openFaces += 1;
            }
            if (!patchSeen[patch.value()]) {
                patchSeen[patch.value()] = true;
                record.kind = boundaryRecord->kind;
            } else if (record.kind.has_value() && *record.kind != boundaryRecord->kind) {
                record.kind.reset();
            }
        }

        // ---- the headline number is the DIVERGENCE'S ------------------------
        report.netBoundarySi_ = report.divergence_.netBoundaryFlux().si();
        report.throughputSi_ = 0.5 * absoluteSum;

        // ---- the measured reconstructions ----------------------------------
        //
        // NOT IDENTITIES, AND MEASURED BEFORE THIS CODE EXISTED.
        // `(outflow - inflow)` is bit-equal to the net on 1.02% of balanced
        // fields and the per-patch sum on 1.93%, so both are reported as
        // departures relative to the magnitude accumulated.
        report.summary_.directionalSplitResidual = relativeDeparture(
            report.outflowSi_ - report.inflowSi_, report.netBoundarySi_, absoluteSum);

        double patchNetSum = 0.0;
        for (std::size_t p = 0; p < patchCount; ++p) { // `PatchIndex` order
            patchNetSum += report.patches_[p].net.si();
        }
        report.summary_.patchSumResidual =
            relativeDeparture(patchNetSum, report.netBoundarySi_, absoluteSum);

        // ADR-071'S ACCUMULATION ARTIFACT, CARRIED VERBATIM FROM THE QUALIFIED
        // OPERATOR'S OWN SUMMARY. This is NOT the mass balance, and the two are
        // kept apart by the name as much as by the arithmetic -- see
        // CfdContinuity.hpp's opening note. `divergence_.globalImbalance()` is
        // still never read in this file.
        //
        // AN EARLIER VERSION RE-DERIVED THIS NUMBER HERE AND DIVIDED IT BY THE
        // WRONG SCALE. Two independent reviews found it and a separately written
        // model confirmed it. The numerator is a rounding artifact of a sum over
        // ALL CELLS, so the scale its error belongs to is the ALL-FACES flux
        // magnitude -- which is exactly what `relativeImbalance` divides by.
        // This file divided by the BOUNDARY-ONLY magnitude, and the two differ
        // by the ratio of interior to boundary flux, which nothing bounds.
        // Measured in the operator's own summation order, for CORRECT
        // arithmetic:
        //
        //     interior 1e6   boundary 1e-6    re-derived    7.614493e-06
        //                                     relativeImbalance 4.568696e-17
        //     interior 1e12  boundary 1e-12   re-derived    1.000000e+00
        //                                     relativeImbalance 6.000000e-24
        //
        // `1.0` is the value this file's own header declares to be the signature
        // of a broken scatter, reached by a correct one -- and the suite's
        // `< 1e-14` assertions passed only because every fixture opened the
        // whole boundary and so maximised the denominator. Worse, in a CLOSED
        // domain the boundary magnitude is bit-zero, so the re-derived figure
        // was identically `0.0` whatever the scatter did: a number that could
        // not read false, asserted as `== 0.0` on all eight reference bodies.
        //
        // Consuming the qualified value is what ADR-074 Decision 1 prescribes,
        // and re-deriving it was the one place in this file that ignored it.
        report.summary_.accumulationResidual = divergenceSummary.relativeImbalance;

        report.summary_.cells = cells;
        report.summary_.internalFaces = topology.internalFaces();
        report.summary_.boundaryFaces = boundaryFaces;
        report.summary_.patches = patchCount;
        report.summary_.closedDomain = report.summary_.openFaces == 0;
        report.summary_.worstClosure = divergenceSummary.worstClosure;

        // ---- the two measures ADR-058 fixed --------------------------------
        //
        // PRESENT EXACTLY WHEN THE THROUGHPUT IS STRICTLY POSITIVE (ADR-074
        // Decision 9). `CfdData.hpp` assigns the floor that would make these
        // total to `P19-MASSBAL-001`, so none is invented here, and reporting
        // `0.0` for a closed domain would claim perfect balance where the
        // measure is undefined.
        if (report.throughputSi_ > 0.0) {
            CfdMassBalance balance{};
            balance.maxCellContinuity = report.largestCellResidualSi_ / report.throughputSi_;
            balance.globalImbalance = std::abs(report.netBoundarySi_) / report.throughputSi_;
            report.massBalance_ = balance;
        }

        // ---- the only phase that judges the answer -------------------------
        //
        // AN UNREACHABLE DEFENCE, AND THE JUSTIFICATION THAT USED TO SIT HERE
        // WAS FALSE. It claimed that "every INPUT being finite admits 1.7e308,
        // from which a boundary sum can overflow", which `divergeCfdMassFlux`
        // forecloses: it gates its own two totals AND its accumulated magnitude
        // `sum_f |flux_f|`, and every quantity computed here is bounded by that
        // magnitude. The header's `ResultIsNotFinite` note has the inequalities
        // and `ADR-074` records this as `L8`.
        //
        // THE CORRECTION WAS MADE IN THE HEADER AND THE ADR AND NOT HERE, which
        // a review found: the same "a fix at one site is not a fix of a class"
        // failure that cost `P19-MOM-001` its first qualification, one
        // milestone later and in documentation rather than in code. The loop is
        // kept because it guards a promise ANOTHER module makes about its own
        // gate; it is no longer kept with a false story attached.
        for (const double value :
             {report.inflowSi_, report.outflowSi_, report.netBoundarySi_, report.throughputSi_,
              report.largestCellResidualSi_, report.summary_.directionalSplitResidual,
              report.summary_.patchSumResidual, report.summary_.accumulationResidual}) {
            if (!std::isfinite(value)) {
                return std::unexpected(
                    failureOf(CfdContinuityProblem::ResultIsNotFinite,
                              "a continuity aggregate is not finite, although every supplied phi "
                              "was finite and the divergence accepted them"));
            }
        }
        for (std::size_t p = 0; p < patchCount; ++p) {
            const CfdPatchMassFlow& record = report.patches_[p];
            if (!std::isfinite(record.inflow.si()) || !std::isfinite(record.outflow.si()) ||
                !std::isfinite(record.net.si())) {
                return std::unexpected(failureOf(
                    CfdContinuityProblem::ResultIsNotFinite,
                    std::format("the mass flow of patch {} is not finite, although every supplied "
                                "phi was finite and the divergence accepted them",
                                p)));
            }
        }
        if (report.massBalance_.has_value() && !isWellFormed(*report.massBalance_)) {
            return std::unexpected(
                failureOf(CfdContinuityProblem::ResultIsNotFinite,
                          "the mass balance is not well formed, although every supplied phi was "
                          "finite and the throughput is positive"));
        }

        return report;
    }
};

std::expected<CfdContinuityReport, CfdContinuityFailure>
evaluateCfdContinuity(const CfdTopology& topology, const CfdGeometry& geometry,
                      const PreparedCfdBoundary& boundary, const CfdContinuityInput& input) {
    return CfdContinuityBuilder::build(topology, geometry, boundary, input);
}

std::optional<CfdContinuityFailure> cfdContinuityFailure(const CfdTopology& topology,
                                                         const CfdGeometry& geometry,
                                                         const PreparedCfdBoundary& boundary,
                                                         const CfdContinuityInput& input) {
    std::string message;
    if (const std::optional<CfdContinuityProblem> problem =
            problemOf(topology, geometry, boundary, input, &message)) {
        return failureOf(*problem, std::move(message));
    }
    return std::nullopt;
}

std::optional<CfdContinuityProblem> cfdContinuityProblem(const CfdTopology& topology,
                                                         const CfdGeometry& geometry,
                                                         const PreparedCfdBoundary& boundary,
                                                         const CfdContinuityInput& input) noexcept {
    return problemOf(topology, geometry, boundary, input, nullptr);
}

} // namespace bettercad::cfd
