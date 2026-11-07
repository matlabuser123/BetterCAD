#include <bettercad/cfd/CfdDivergence.hpp>

// For `cfdFaceInterpolationWeight`, the EXPORTED face interpolation weight
// (ADR-069 Decision 7). ADR-071 Decision 6 makes reusing it a correctness
// requirement, and an independent review found this translation unit had copied
// it instead. Same module, same layer (cfd, 42), so this crosses no boundary.
#include <bettercad/cfd/CfdDiffusion.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <utility>

namespace bettercad::cfd {

// Type aliases have NO LINKAGE, so this may sit at namespace scope in a
// translation unit without exporting anything. The TU-local *types* the
// builder's signatures mention are nested inside the builder, so that no
// externally-linked member function names an internal-linkage type -- the same
// arrangement `CfdGradient.cpp` uses, and for the same reason.
using Vec = std::array<double, 3>;

namespace {

[[nodiscard]] Vec siOf(const AreaVector3D& v) noexcept {
    return {v.x.si(), v.y.si(), v.z.si()};
}

// A `siOf(const Translation3D&)` overload stood here and is DELETED, not kept:
// its only caller was this file's own transcription of the interpolation weight,
// which now calls the exported `cfdFaceInterpolationWeight` instead. Dead code
// kept to satisfy a compiler is worse than dead code removed (P19-GRAD-001's
// r3 made the same call about an unreachable reset).

[[nodiscard]] double dotOf(const Vec& a, const Vec& b) noexcept {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

template <typename Describe>
[[nodiscard]] std::optional<CfdDivergenceProblem> refuse(CfdDivergenceProblem problem,
                                                         std::string* message, Describe describe) {
    if (message != nullptr) {
        *message = describe();
    }
    return problem;
}

} // namespace

// ---------------------------------------------------------------------------
// The builder: the only thing that may construct a field, and the one place the
// arithmetic lives.
// ---------------------------------------------------------------------------

struct CfdDivergenceBuilder {
    /// Which flux is being summed.
    ///
    /// ADR-067's table is consulted down the `U` column and nowhere else, so the
    /// mass-flux path never reaches a policy: `phi` is a `Face` variable the
    /// caller owns, and this tag is what keeps the two readings apart inside one
    /// gate (ADR-071 Decision 5).
    enum class Path : std::uint8_t { MassFlux, Velocity };

    /// One reading of the caller's array, so that ONE gate serves both entry
    /// points.
    ///
    /// THE PUBLIC ASYMMETRY IS DELIBERATE AND THIS DOES NOT SOFTEN IT. ADR-071
    /// Decision 4 rejected a single public function taking an optional boundary,
    /// because that would hide behind a null check the fact that one path reads a
    /// face field and the other reads cell values and a frozen table. Sharing
    /// the gate is the opposite move: the two public functions stay distinct, and
    /// the checks they have in common have one implementation that cannot drift.
    struct View {
        Path path = Path::MassFlux;
        std::span<const double> values{};
        std::size_t cells = 0;
        std::size_t faces = 0;

        /// `faces` for a face-located `phi`, `3 * cells` for a component-blocked
        /// velocity (ADR-066).
        [[nodiscard]] std::size_t expectedLength() const noexcept {
            return path == Path::MassFlux ? faces : 3 * cells;
        }

        [[nodiscard]] std::string_view name() const noexcept {
            return path == Path::MassFlux ? "mass flow rate" : "velocity";
        }

        /// The whole cell velocity, component-blocked. VELOCITY PATH ONLY.
        [[nodiscard]] Vec velocityAt(std::size_t cell) const noexcept {
            return {values[cell], values[cells + cell], values[2 * cells + cell]};
        }
    };

    // -----------------------------------------------------------------------
    // Geometry reads
    // -----------------------------------------------------------------------

    /// The weight on the OWNER's value at an internal face.
    ///
    /// THE EXPORTED DEFINITION IS CALLED, NOT COPIED, and that is a correction an
    /// independent review forced. This function first held its own transcription
    /// of `(d . n - (C_f - C_P) . n) / (d . n)` under a comment claiming it was
    /// "read from one definition and not re-derived" -- which was false, and
    /// which made ADR-071 Decision 6's "the weight therefore has exactly one
    /// definition" false with it. `cfdFaceInterpolationWeight` (ADR-069
    /// Decision 7, P19-DIFF-001) is public, exported and already the normative
    /// form; this operator reads it.
    ///
    /// REUSING IT IS A CORRECTNESS REQUIREMENT, NOT TIDINESS. A Green-Gauss
    /// gradient and this divergence on the same mesh must agree about what the
    /// field is at a face, or `trace(grad U)` and `div U` would be two different
    /// numbers for one quantity.
    ///
    /// AND IT REFUSES RATHER THAN DEFAULTING. The transcription returned `0.5` on
    /// a face with no positive orthogonal distance or no unit normal, so a
    /// degenerate face would have produced a SUCCESSFUL field interpolated at a
    /// midpoint nobody asked for -- the one place in this operator where a wrong
    /// answer could be returned as a success, and against this file's own
    /// "REFUSED, NOT SKIPPED" doctrine. `std::nullopt` here becomes a named
    /// refusal in the scatter. Unreachable while `buildCfdGeometry` holds, since
    /// it refuses a face with a non-positive orthogonal distance.
    [[nodiscard]] static std::optional<double> interpolationWeight(const CfdGeometry& geometry,
                                                                    FaceIndex face) noexcept {
        return cfdFaceInterpolationWeight(geometry, face);
    }

    /// The velocity a boundary policy gives at @p boundaryFace.
    ///
    /// `Symmetry` IS NOT HERE: ADR-071 Decision 7 gives a symmetry face a flux of
    /// exactly zero without forming a velocity at all, so `faceFlux` answers it
    /// before asking this. Returning `U_P - (U_P . n) n` and then dotting it
    /// would compute a quantity already known to vanish, and would get it only to
    /// rounding, because `U_b . n = U_P.n - (U_P.n)(n.n)` is exactly zero only
    /// when `n . n` is exactly 1.
    [[nodiscard]] static std::optional<Vec> boundaryVelocity(const PreparedCfdBoundary& boundary,
                                                             const View& view,
                                                             BoundaryFaceIndex boundaryFace,
                                                             CfdBoundaryPolicy policy,
                                                             std::size_t owner) {
        switch (policy) {
        case CfdBoundaryPolicy::FixedValue: {
            const std::optional<CfdBoundaryFace> facet = boundary.face(boundaryFace);
            if (!facet.has_value() || !facet->velocity.has_value()) {
                return std::nullopt;
            }
            const Velocity3& prescribed = *facet->velocity;
            return Vec{prescribed.x.si(), prescribed.y.si(), prescribed.z.si()};
        }
        case CfdBoundaryPolicy::ZeroGradient:
            // A zero NORMAL DERIVATIVE means the face value is the owner's own,
            // which is what `ZeroGradient` says about a face value.
            return view.velocityAt(owner);
        case CfdBoundaryPolicy::Symmetry:
            // Answered by faceFlux before this is called. Reaching here would be
            // an internal inconsistency, so it refuses rather than inventing a
            // value.
            return std::nullopt;
        case CfdBoundaryPolicy::Computed:
            // An outcome of a solve. The gate refuses this first; a fall-through
            // to the owner's own velocity would silently admit flow through a
            // boundary whose flux is an unknown.
            return std::nullopt;
        }
        return std::nullopt;
    }

    /// THE ONE FLUX PER FACE, computed exactly once.
    ///
    /// This is the whole of ADR-071 Decision 2: the caller adds the returned
    /// value to the owner and its NEGATION to the neighbour, so an internal
    /// face's two contributions are exact negatives and cancel bit-for-bit.
    /// Nothing recomputes a face value from the other side, so there is no
    /// second reading to keep consistent.
    ///
    /// Raises @p worstSkewness for what an interpolation paid, on internal faces
    /// only, because that is the only place a value is interpolated: a
    /// `FixedValue` facet gives the exact velocity AT the face centroid, and a
    /// `phi` is already a face quantity.
    [[nodiscard]] static std::optional<double> faceFlux(const CfdTopology& topology,
                                                        const CfdGeometry& geometry,
                                                        const PreparedCfdBoundary* boundary,
                                                        const View& view, FaceIndex face,
                                                        double& worstSkewness,
                                                        CfdDivergenceSummary& summary) {
        if (view.path == Path::MassFlux) {
            // `phi` IS ALREADY THE INTEGRATED FLUX, kg/s, signed out of the
            // owner (ADR-066's table, and `CfdFields.hpp`'s own note that it is a
            // mass FLOW RATE and not a mass flux). There is no area factor, no
            // interpolation and no policy on this path.
            return view.values[face.value()];
        }

        const Vec stored = siOf(geometry.faceAreaVector(face));

        if (topology.isInternal(face)) {
            const CellIndex owner = topology.owner(face);
            const std::optional<CellIndex> neighbour = topology.neighbour(face);
            if (!neighbour.has_value()) {
                return std::nullopt;
            }
            if (const std::optional<double> skew = geometry.skewness(face); skew.has_value()) {
                worstSkewness = std::max(worstSkewness, std::abs(*skew));
            }
            const std::optional<double> weightOf = interpolationWeight(geometry, face);
            if (!weightOf.has_value()) {
                // REFUSED, NOT DEFAULTED. See `interpolationWeight`.
                return std::nullopt;
            }
            const double weight = *weightOf;
            const Vec ownerValue = view.velocityAt(owner.value());
            const Vec neighbourValue = view.velocityAt(neighbour->value());
            // `U_N + w (U_P - U_N)`, not `w U_P + (1 - w) U_N`: the first is
            // EXACTLY `U` when the two are equal and the second is not, which is
            // the defect P19-DIFF-001's test caught in its face viscosity and
            // P19-GRAD-001's mutation sweep caught again in a test that could not
            // see it.
            const Vec interpolated{
                neighbourValue[0] + weight * (ownerValue[0] - neighbourValue[0]),
                neighbourValue[1] + weight * (ownerValue[1] - neighbourValue[1]),
                neighbourValue[2] + weight * (ownerValue[2] - neighbourValue[2])};
            return dotOf(stored, interpolated);
        }

        if (boundary == nullptr) {
            return std::nullopt;
        }
        const std::optional<BoundaryFaceIndex> boundaryFace = topology.boundaryFaceOf(face);
        if (!boundaryFace.has_value()) {
            return std::nullopt;
        }
        const std::optional<CfdBoundaryTreatment> treatment = boundary->treatment(*boundaryFace);
        if (!treatment.has_value()) {
            return std::nullopt;
        }
        switch (treatment->velocity) {
        case CfdBoundaryPolicy::FixedValue:
            ++summary.fixedValueFaces;
            break;
        case CfdBoundaryPolicy::ZeroGradient:
            ++summary.zeroGradientFaces;
            break;
        case CfdBoundaryPolicy::Symmetry:
            ++summary.symmetryFaces;
            break;
        case CfdBoundaryPolicy::Computed:
            break;
        }

        if (treatment->velocity == CfdBoundaryPolicy::Symmetry) {
            // ADR-071 DECISION 7. The normal component is zero BY DEFINITION of
            // the policy, so the volumetric flow through the face is exactly
            // zero. Reporting `3e-17` here would be reporting rounding as
            // physics.
            return 0.0;
        }

        const CellIndex owner = topology.owner(face);
        const std::optional<Vec> value =
            boundaryVelocity(*boundary, view, *boundaryFace, treatment->velocity, owner.value());
        if (!value.has_value()) {
            return std::nullopt;
        }
        return dotOf(stored, *value);
    }

    // -----------------------------------------------------------------------
    // The gate
    // -----------------------------------------------------------------------

    /// THE ONE DECISION PATH. Both entry points and both predictors reach the
    /// gate through here, so there is no second copy that could drift. The
    /// message is built only when a caller passes somewhere to put it, which is
    /// what makes the predictors' `noexcept` honest: they allocate nothing.
    ///
    /// @p boundary is null on the mass-flux path, which has no boundary to check
    /// -- so phases 2, 5 and 6 are velocity-only by construction rather than by
    /// a flag.
    [[nodiscard]] static std::optional<CfdDivergenceProblem>
    problemOf(const CfdTopology& topology, const CfdGeometry& geometry,
              const PreparedCfdBoundary* boundary, const View& view, std::string* message) {
        // PHASE 1 -- the metrics must describe this topology. First, because
        // every later phase reads both.
        if (!geometry.describes(topology)) {
            return refuse(CfdDivergenceProblem::GeometryDescribesAnotherTopology, message, [&] {
                return std::format("the geometric metrics were built for a different mesh generation than "
                                   "the topology, so its {} cells and {} faces do not mean the same thing",
                                   topology.cells(), topology.faces());
            });
        }

        // PHASE 2 -- and so must the prepared boundary, where there is one.
        // STRUCTURAL ONLY, by face count: `bindCfdBoundary` deliberately returns
        // a default-constructed source, so comparing a stamp unconditionally
        // would refuse every seam-built boundary -- the defect P19-DIFF-001 found
        // in its own gate before any test ran.
        if (boundary != nullptr && boundary->boundaryFaces() != topology.boundaryFaces()) {
            return refuse(CfdDivergenceProblem::BoundaryDescribesAnotherTopology, message, [&] {
                return std::format("the prepared boundary covers {} boundary faces and the topology has {}",
                                   boundary->boundaryFaces(), topology.boundaryFaces());
            });
        }
        // AND TOTALITY IS PART OF PHASE 2, not of phase 5. An independent review
        // found this walk sitting inside the prescribed-value phase while
        // reporting `BoundaryDescribesAnotherTopology`, so a caller reading the
        // tag was told "phase 2" for a condition detected in phase 5 -- and the
        // header's claim that the enum is declared in PHASE ORDER was then not
        // what the code did. A boundary that does not answer for every face of
        // this topology describes another one, whatever its count says.
        if (boundary != nullptr) {
            for (std::size_t b = 0; b < topology.boundaryFaces(); ++b) {
                const BoundaryFaceIndex boundaryFace = BoundaryFaceIndex::fromValue(b);
                if (!boundary->treatment(boundaryFace).has_value()) {
                    return refuse(CfdDivergenceProblem::BoundaryDescribesAnotherTopology, message, [&] {
                        return std::format("the prepared boundary holds no treatment at boundary face {}",
                                           b);
                    });
                }
            }
        }

        // PHASE 3 -- the values, at the length this entry point needs.
        if (view.values.size() != view.expectedLength()) {
            return refuse(CfdDivergenceProblem::ValueLengthWrong, message, [&] {
                if (view.path == Path::MassFlux) {
                    return std::format("a face-located mass flow rate over {} faces needs {} values, "
                                       "not {}",
                                       view.faces, view.expectedLength(), view.values.size());
                }
                return std::format("a component-blocked velocity over {} cells needs {} values, not {}",
                                   view.cells, view.expectedLength(), view.values.size());
            });
        }

        // PHASE 4 -- and finite. AFTER the length, so a wrong length is reported
        // as a length and not as whatever happens to sit in it.
        for (std::size_t i = 0; i < view.values.size(); ++i) {
            if (!std::isfinite(view.values[i])) {
                return refuse(CfdDivergenceProblem::ValueNotFinite, message, [&] {
                    return std::format("the {} holds {} at index {}, which is not finite", view.name(),
                                       view.values[i], i);
                });
            }
        }

        if (boundary == nullptr) {
            return std::nullopt;
        }

        // PHASE 5 -- a prescribed velocity must be there when the policy says it
        // is.
        for (std::size_t b = 0; b < topology.boundaryFaces(); ++b) {
            const BoundaryFaceIndex boundaryFace = BoundaryFaceIndex::fromValue(b);
            const std::optional<CfdBoundaryTreatment> treatment = boundary->treatment(boundaryFace);
            if (!treatment.has_value()) {
                // UNREACHABLE WHILE PHASE 2 HOLDS, which now walks every boundary
                // face for exactly this. Kept so the optional is answered for,
                // and tagged as phase 2 because that is the phase that owns the
                // condition.
                return refuse(CfdDivergenceProblem::BoundaryDescribesAnotherTopology, message, [&] {
                    return std::format("the prepared boundary holds no treatment at boundary face {}", b);
                });
            }
            if (treatment->velocity != CfdBoundaryPolicy::FixedValue) {
                continue;
            }
            const std::optional<CfdBoundaryFace> facet = boundary->face(boundaryFace);
            if (!facet.has_value() || !facet->velocity.has_value()) {
                return refuse(CfdDivergenceProblem::PrescribedValueMissing, message, [&] {
                    return std::format("boundary face {} gives the velocity the FixedValue policy and "
                                       "the prepared boundary holds no value for it",
                                       b);
                });
            }
        }

        // PHASE 6 -- an outcome policy. Reported AFTER a missing value, so a
        // boundary with both faults reports the earlier phase.
        for (std::size_t b = 0; b < topology.boundaryFaces(); ++b) {
            const std::optional<CfdBoundaryTreatment> treatment =
                boundary->treatment(BoundaryFaceIndex::fromValue(b));
            if (treatment.has_value() && treatment->velocity == CfdBoundaryPolicy::Computed) {
                return refuse(CfdDivergenceProblem::VelocityPolicyIsAnOutcome, message, [&] {
                    return std::format("boundary face {} gives the velocity the Computed policy, which "
                                       "is an outcome of a solve and not a boundary condition",
                                       b);
                });
            }
        }

        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // The divergence
    // -----------------------------------------------------------------------

    template <typename Q>
    [[nodiscard]] static Result<CfdDivergenceField<Q>> build(const CfdTopology& topology,
                                                             const CfdGeometry& geometry,
                                                             const PreparedCfdBoundary* boundary,
                                                             const View& view) {
        std::string message;
        if (const std::optional<CfdDivergenceProblem> problem =
                problemOf(topology, geometry, boundary, view, &message);
            problem.has_value()) {
            return makeError(ErrorCode::FailedPrecondition, std::move(message));
        }

        const std::size_t cells = topology.cells();
        const std::size_t faces = topology.faces();

        CfdDivergenceField<Q> field;
        field.si_.assign(cells, 0.0);
        field.volumeSi_.assign(cells, 0.0);
        field.summary_.cells = cells;
        field.summary_.internalFaces = topology.internalFaces();
        field.summary_.boundaryFaces = topology.boundaryFaces();

        // The volumes, and the closure, in CellIndex order. The volume is STORED
        // rather than re-read, so `divergenceAt` cannot be given a different
        // geometry than the divergence was taken against.
        double worstClosure = 0.0;
        for (std::size_t c = 0; c < cells; ++c) {
            const CellIndex cell = CellIndex::fromValue(c);
            const double volume = geometry.cellVolume(cell).si();
            if (!(volume > 0.0)) {
                // A zero volume means this geometry does not hold the cell, by
                // `CfdGeometry`'s own documented contract -- so this is phase 1's
                // currency failure surfacing late. Refused rather than left as a
                // silent division. Unreachable while `describes` holds, and
                // recorded as such.
                return makeError(ErrorCode::FailedPrecondition,
                                 std::format("cell {} has no volume in these metrics, so they do not "
                                             "describe this topology after all",
                                             c));
            }
            field.volumeSi_[c] = volume;
            if (const std::optional<double> closure = geometry.cellClosure(cell); closure.has_value()) {
                worstClosure = std::max(worstClosure, *closure);
            }
        }
        field.summary_.worstClosure = worstClosure;

        // THE SCATTER, in FaceIndex order (ADR-071 Decisions 2 and 9). One flux
        // per face, added to the owner and SUBTRACTED from the neighbour.
        double worstSkewness = 0.0;
        double netBoundary = 0.0;
        double accumulated = 0.0;
        for (std::size_t f = 0; f < faces; ++f) {
            const FaceIndex face = FaceIndex::fromValue(f);

            // THE RANGE GUARDS COME FIRST, BEFORE ANY CELL IS INDEXED, and that
            // ordering is a correction an independent review forced. They used to
            // sit after `faceFlux`, which on the velocity path had ALREADY indexed
            // the caller's span with both values -- `velocityAt` reads
            // `values[cell]`, `values[cells + cell]` and `values[2 * cells + cell]`
            // unchecked -- so the guard ran after the read it exists to prevent.
            // `geometry.describes` compares the mesh stamp and the counts, never
            // the owner and neighbour ranges, so nothing upstream establishes
            // this. Unreachable while `buildCfdTopology` holds, and recorded as
            // such, exactly like the two refusals below.
            const CellIndex owner = topology.owner(face);
            if (owner.value() >= cells) {
                return makeError(ErrorCode::FailedPrecondition,
                                 std::format("face {} names cell {} as its owner and the topology holds "
                                             "{} cells",
                                             f, owner.value(), cells));
            }
            std::optional<CellIndex> neighbour;
            if (topology.isInternal(face)) {
                neighbour = topology.neighbour(face);
                if (!neighbour.has_value() || neighbour->value() >= cells) {
                    return makeError(ErrorCode::FailedPrecondition,
                                     std::format("internal face {} has no usable neighbour", f));
                }
            }

            const std::optional<double> flux =
                faceFlux(topology, geometry, boundary, view, face, worstSkewness, field.summary_);
            if (!flux.has_value()) {
                // REFUSED, NOT SKIPPED. Skipping would leave a face out of
                // `sum_f F_f . S_f` while still dividing by the whole volume --
                // a wrong divergence returned as a success, which is the defect
                // P19-GRAD-001's review found in the Green-Gauss loop.
                // Unreachable while the gate holds, and recorded as such.
                return makeError(ErrorCode::FailedPrecondition,
                                 std::format("face {} has no usable {} flux, so the surface sum would be "
                                             "taken over fewer faces than the volumes it divides",
                                             f, view.name()));
            }
            accumulated += std::abs(*flux);

            field.si_[owner.value()] += *flux;

            if (neighbour.has_value()) {
                // THE NEGATION, NOT A RECOMPUTATION. `(-S) . U` is bit-for-bit
                // `-(S . U)`, so this is exact and the face's two contributions
                // cancel to exactly zero.
                field.si_[neighbour->value()] += -*flux;
            } else {
                netBoundary += *flux;
            }
        }
        field.summary_.worstSkewness = worstSkewness;

        // The global sum, in CellIndex order.
        double globalNet = 0.0;
        for (std::size_t c = 0; c < cells; ++c) {
            globalNet += field.si_[c];
        }

        // THE ONLY PHASE THAT JUDGES THE ANSWER. Requiring every INPUT to be
        // finite admits 1.8e308, from which a surface sum can overflow to
        // infinities of each sign and cancel to a NaN -- which would pass every
        // earlier phase and reach a caller through `at()` with no error at all.
        for (std::size_t c = 0; c < cells; ++c) {
            if (!std::isfinite(field.si_[c])) {
                return makeError(ErrorCode::FailedPrecondition,
                                 std::format("the {} divergence at cell {} is {}, which is not finite",
                                             view.name(), c, field.si_[c]));
            }
        }
        if (!std::isfinite(netBoundary) || !std::isfinite(globalNet) || !std::isfinite(accumulated)) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("the {} divergence totals are not finite: boundary {}, global "
                                         "{}, accumulated magnitude {}",
                                         view.name(), netBoundary, globalNet, accumulated));
        }

        field.netBoundarySi_ = netBoundary;
        field.globalNetSi_ = globalNet;
        // THE DENOMINATOR IS THE SCALE THE SUM ACCUMULATED, not the answer it
        // arrived at. Stating it against the answer is P19-GRAD-001's finding r8,
        // and this milestone's own first experiment reported 9.992e+284 for
        // arithmetic correct to half an eps by making exactly that mistake.
        field.summary_.relativeImbalance =
            accumulated > 0.0 ? std::abs(globalNet - netBoundary) / accumulated : 0.0;

        return field;
    }

    [[nodiscard]] static View massFluxView(const CfdTopology& topology,
                                           const CfdMassFluxDivergenceInput& input) noexcept {
        return View{Path::MassFlux, input.faceFlux, topology.cells(), topology.faces()};
    }

    [[nodiscard]] static View velocityView(const CfdTopology& topology,
                                           const CfdVelocityDivergenceInput& input) noexcept {
        return View{Path::Velocity, input.currentVelocity, topology.cells(), topology.faces()};
    }
};

// ---------------------------------------------------------------------------
// CfdDivergenceField
// ---------------------------------------------------------------------------

template <typename Q>
std::optional<Q> CfdDivergenceField<Q>::at(CellIndex cell) const noexcept {
    if (!cell.isValid() || cell.value() >= summary_.cells) {
        return std::nullopt;
    }
    return Q::fromSi(si_[cell.value()]);
}

template <typename Q>
std::optional<typename CfdDivergenceField<Q>::Divergence>
CfdDivergenceField<Q>::divergenceAt(CellIndex cell) const noexcept {
    if (!cell.isValid() || cell.value() >= summary_.cells) {
        return std::nullopt;
    }
    const double volume = volumeSi_[cell.value()];
    if (!(volume > 0.0)) {
        return std::nullopt;
    }
    return Divergence::fromSi(si_[cell.value()] / volume);
}

template <typename Q>
Q CfdDivergenceField<Q>::largestMagnitude() const noexcept {
    double worst = 0.0;
    for (std::size_t c = 0; c < summary_.cells; ++c) {
        worst = std::max(worst, std::abs(si_[c]));
    }
    return Q::fromSi(worst);
}

template <typename Q>
Q CfdDivergenceField<Q>::netBoundaryFlux() const noexcept {
    return Q::fromSi(netBoundarySi_);
}

template <typename Q>
Q CfdDivergenceField<Q>::globalNetFlux() const noexcept {
    return Q::fromSi(globalNetSi_);
}

template <typename Q>
Q CfdDivergenceField<Q>::globalImbalance() const noexcept {
    return Q::fromSi(globalNetSi_ - netBoundarySi_);
}

template class BETTERCAD_CFD_EXPORT CfdDivergenceField<VolumetricFlowRate>;
template class BETTERCAD_CFD_EXPORT CfdDivergenceField<MassFlowRate>;

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

std::string_view toString(CfdDivergenceProblem problem) noexcept {
    switch (problem) {
    case CfdDivergenceProblem::GeometryDescribesAnotherTopology:
        return "geometry_describes_another_topology";
    case CfdDivergenceProblem::BoundaryDescribesAnotherTopology:
        return "boundary_describes_another_topology";
    case CfdDivergenceProblem::ValueLengthWrong:
        return "value_length_wrong";
    case CfdDivergenceProblem::ValueNotFinite:
        return "value_not_finite";
    case CfdDivergenceProblem::PrescribedValueMissing:
        return "prescribed_value_missing";
    case CfdDivergenceProblem::VelocityPolicyIsAnOutcome:
        return "velocity_policy_is_an_outcome";
    case CfdDivergenceProblem::DivergenceIsNotFinite:
        return "divergence_is_not_finite";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// The four public entry points
// ---------------------------------------------------------------------------

Result<CfdMassFluxDivergenceField> divergeCfdMassFlux(const CfdTopology& topology,
                                                      const CfdGeometry& geometry,
                                                      const CfdMassFluxDivergenceInput& input) {
    return CfdDivergenceBuilder::build<MassFlowRate>(
        topology, geometry, nullptr, CfdDivergenceBuilder::massFluxView(topology, input));
}

Result<CfdVelocityDivergenceField> divergeCfdVelocity(const CfdTopology& topology,
                                                       const CfdGeometry& geometry,
                                                       const PreparedCfdBoundary& boundary,
                                                       const CfdVelocityDivergenceInput& input) {
    return CfdDivergenceBuilder::build<VolumetricFlowRate>(
        topology, geometry, &boundary, CfdDivergenceBuilder::velocityView(topology, input));
}

std::optional<CfdDivergenceProblem>
cfdMassFluxDivergenceProblem(const CfdTopology& topology, const CfdGeometry& geometry,
                             const CfdMassFluxDivergenceInput& input) noexcept {
    // `nullptr` for the message, so this path allocates nothing.
    return CfdDivergenceBuilder::problemOf(topology, geometry, nullptr,
                                           CfdDivergenceBuilder::massFluxView(topology, input), nullptr);
}

std::optional<CfdDivergenceProblem>
cfdVelocityDivergenceProblem(const CfdTopology& topology, const CfdGeometry& geometry,
                             const PreparedCfdBoundary& boundary,
                             const CfdVelocityDivergenceInput& input) noexcept {
    return CfdDivergenceBuilder::problemOf(topology, geometry, &boundary,
                                           CfdDivergenceBuilder::velocityView(topology, input), nullptr);
}

} // namespace bettercad::cfd
