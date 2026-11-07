#include <bettercad/cfd/CfdConvection.hpp>

// For `cfdFaceInterpolationWeight`, the EXPORTED face interpolation weight
// (ADR-069 Decision 7). NOT a transcription of it: ADR-071 Decision 6 claimed
// the weight had "exactly one definition" when there were three, and the
// amendment that corrected it is the reason this file calls rather than copies.
// The producer needs it at every internal face and the central scheme needs it
// at every internal face it interpolates across.
#include <bettercad/cfd/CfdDiffusion.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <ranges>
#include <string>
#include <utility>

namespace bettercad::cfd {

// Type aliases have NO LINKAGE, so this may sit at namespace scope in a
// translation unit without exporting anything -- the arrangement
// `CfdGradient.cpp` and `CfdDivergence.cpp` both use.
using Vec = std::array<double, 3>;

namespace {

[[nodiscard]] Vec siOf(const AreaVector3D& v) noexcept {
    return {v.x.si(), v.y.si(), v.z.si()};
}

[[nodiscard]] Vec siOf(const Velocity3& v) noexcept {
    return {v.x.si(), v.y.si(), v.z.si()};
}

[[nodiscard]] double dotOf(const Vec& a, const Vec& b) noexcept {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/// The compressed-sparse-row pattern, and a slot lookup within it.
///
/// THE SAME SHAPE `CfdDiffusion.cpp` USES, and deliberately not shared with it:
/// extracting it would modify a qualified operator's translation unit to serve
/// a new consumer, which ADR-072 Decision 9 refused for the reason ADR-063
/// Decision 7 gave. It is eleven lines of std::ranges and it has no numerical
/// content -- the thing that MUST agree between operators is the interpolation
/// weight, and that is called, not copied.
struct Layout {
    std::vector<CfdSparseIndex> rowStart{};
    std::vector<CfdSparseIndex> inner{};
};

[[nodiscard]] Layout layoutOf(std::vector<std::vector<CfdSparseIndex>>& pattern) {
    Layout out;
    out.rowStart.reserve(pattern.size() + 1);
    out.rowStart.push_back(0);
    for (std::vector<CfdSparseIndex>& row : pattern) {
        std::ranges::sort(row);
        row.erase(std::ranges::unique(row).begin(), row.end());
        out.inner.insert(out.inner.end(), row.begin(), row.end());
        out.rowStart.push_back(static_cast<CfdSparseIndex>(out.inner.size()));
    }
    return out;
}

[[nodiscard]] std::optional<std::size_t> slotOf(const Layout& layout, std::size_t row,
                                                std::size_t column) noexcept {
    if (row + 1 >= layout.rowStart.size()) {
        return std::nullopt;
    }
    const auto first = static_cast<std::ptrdiff_t>(layout.rowStart[row]);
    const auto last = static_cast<std::ptrdiff_t>(layout.rowStart[row + 1]);
    const auto begin = std::next(layout.inner.begin(), first);
    const auto end = std::next(layout.inner.begin(), last);
    const auto found = std::lower_bound(begin, end, static_cast<CfdSparseIndex>(column));
    if (found == end || *found != static_cast<CfdSparseIndex>(column)) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(layout.inner.begin(), found));
}

template <typename Describe>
[[nodiscard]] std::optional<CfdConvectionProblem> refuse(CfdConvectionProblem problem,
                                                         std::string* message, Describe describe) {
    if (message != nullptr) {
        *message = describe();
    }
    return problem;
}

/// Which entry point is asking.
enum class Path : std::uint8_t { Assembly, Producer };

/// One reading of the caller's array, so that ONE gate serves both entry
/// points.
///
/// THE PUBLIC ASYMMETRY IS DELIBERATE AND THIS DOES NOT SOFTEN IT, which is the
/// distinction `CfdDivergence.cpp` draws of its own shared gate: the two public
/// functions stay distinct -- one takes a face array, the other a cell array and
/// a density -- and only the checks they have in common have one
/// implementation, so there is no second copy to drift.
struct View {
    Path path = Path::Assembly;
    std::span<const double> values{};
    std::size_t cells = 0;
    std::size_t faces = 0;
    /// PRODUCER ONLY. Absent on the assembly path, which takes no density
    /// because `phi` already carries it.
    std::optional<Density> density{};
    /// ASSEMBLY ONLY. Carried so the gate can validate it before anything
    /// subscripts a three-element array with it.
    std::optional<VectorComponent> component{};

    /// `faces` for a face-located `phi`, `3 * cells` for a component-blocked
    /// velocity (ADR-066).
    [[nodiscard]] std::size_t expectedLength() const noexcept {
        return path == Path::Assembly ? faces : 3 * cells;
    }

    [[nodiscard]] std::string_view name() const noexcept {
        return path == Path::Assembly ? "mass flow rate" : "velocity";
    }

    /// The whole cell velocity, component-blocked. PRODUCER ONLY.
    [[nodiscard]] Vec velocityAt(std::size_t cell) const noexcept {
        return {values[cell], values[cells + cell], values[2 * cells + cell]};
    }
};

/// THE ONE DECISION PATH. Both entry points and both predictors reach the gate
/// through here. The message is built only when a caller passes somewhere to put
/// it, which is what makes the predictors' `noexcept` honest: they allocate
/// nothing.
///
/// @p interpolates says whether an internal face's value will be interpolated,
/// so phase 8 is asked exactly where a weight is actually needed: always on the
/// producer path, and on the assembly path only under `Central`.
///
/// EACH PHASE IS ITS OWN WALK over the boundary, which is what makes the
/// declared enum order the check order between phases -- the arrangement
/// `CfdDivergence.cpp` arrived at. Within one walk the first offending facet
/// decides, and no claim is made about the enum order there.
[[nodiscard]] std::optional<CfdConvectionProblem>
problemOf(const CfdTopology& topology, const CfdGeometry& geometry,
          const PreparedCfdBoundary& boundary, const View& view, bool interpolates,
          std::string* message) {
    // PHASE 1 -- the metrics must describe this topology. First, because every
    // later phase reads both.
    if (!geometry.describes(topology)) {
        return refuse(CfdConvectionProblem::GeometryDescribesAnotherTopology, message, [&] {
            return std::format("the geometric metrics were built for a different mesh generation than "
                               "the topology, so its {} cells and {} faces do not mean the same thing",
                               topology.cells(), topology.faces());
        });
    }

    // PHASE 2 -- and so must the prepared boundary.
    //
    // THE STAMP COMPARISON IS CONDITIONAL, NOT ABSENT, and the difference is a
    // review finding. `bindCfdBoundary` deliberately returns a
    // default-constructed source, so comparing unconditionally would refuse
    // every seam-built boundary -- the defect P19-DIFF-001 found in its own
    // gate before any test ran. But that milestone's correction was to make the
    // comparison CONDITIONAL, and the first draft of this gate dropped it
    // altogether, which made this operator strictly weaker than the one it
    // cited as precedent: a boundary prepared against a DIFFERENT mesh with the
    // same boundary-face count was silently accepted, and `cfdMassFlux` would
    // then read that boundary's kinds and prescribed velocities and return a
    // successful `phi` built from the wrong conditions. This is
    // `assembleCfdDiffusion`'s form, restored.
    const CfdBoundarySource& source = boundary.source();
    if (!(source == CfdBoundarySource{}) &&
        (!(source.mesh == topology.mesh()) || source.cells != topology.cells() ||
         source.boundaryFaces != topology.boundaryFaces())) {
        return refuse(CfdConvectionProblem::BoundaryDescribesAnotherTopology, message, [&] {
            return std::format("the prepared boundary was built for a mesh with {} cells and {} "
                               "boundary faces, and this topology has {} and {}",
                               source.cells, source.boundaryFaces, topology.cells(),
                               topology.boundaryFaces());
        });
    }
    if (boundary.boundaryFaces() != topology.boundaryFaces()) {
        return refuse(CfdConvectionProblem::BoundaryDescribesAnotherTopology, message, [&] {
            return std::format("the prepared boundary covers {} boundary faces and the topology has {}",
                               boundary.boundaryFaces(), topology.boundaryFaces());
        });
    }
    // AND THE COVERAGE WALK BELOW CANNOT FIRE, WHICH IS NOW SAID RATHER THAN
    // IMPLIED. An earlier comment presented it as a second independent check
    // ("by face count AND by coverage"); a review pointed out that
    // `PreparedCfdBoundary::treatment` returns an empty optional only for an
    // index it does not hold, and the count equality above already puts every
    // index in range. It is kept as a defence against that accessor's contract
    // moving, and recorded as UNREACHABLE rather than counted as evidence.
    for (std::size_t b = 0; b < topology.boundaryFaces(); ++b) {
        const BoundaryFaceIndex boundaryFace = BoundaryFaceIndex::fromValue(b);
        if (!boundary.treatment(boundaryFace).has_value()) {
            return refuse(CfdConvectionProblem::BoundaryDescribesAnotherTopology, message, [&] {
                return std::format("the prepared boundary holds no treatment at boundary face {}", b);
            });
        }
    }

    // PHASE 3 -- EVERY FACE MUST NAME CELLS THIS TOPOLOGY HOLDS, and this comes
    // before any cell is indexed anywhere.
    //
    // `CellIndex{}` is `kInvalid`, which is `static_cast<size_t>(-1)`, and
    // `CfdTopology::owner` returns exactly that for a face it does not hold. In
    // this operator an unguarded index reaches the symbolic pass's
    // `pattern[owner].push_back(...)` and `source[owner] -= ...`, which are
    // WRITES into vectors of length `cells`, and the producer's
    // `velocityAt(cell)`, which reads three unchecked slots of the caller's
    // span. `geometry.describes` compares the mesh stamp and the counts, never
    // the owner and neighbour ranges.
    //
    // P19-DIV-001 CARRIES THIS GUARD AND ITS COMMENT RECORDS THAT A REVIEW
    // FORCED IT THERE. The first draft of this operator had none, which is that
    // correction regressed one milestone later; a review found it again.
    // Unreachable while `buildCfdTopology` holds, and recorded as such.
    for (std::size_t f = 0; f < topology.faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const CellIndex owner = topology.owner(face);
        if (owner.value() >= topology.cells()) {
            return refuse(CfdConvectionProblem::TopologyNamesAnUnknownCell, message, [&] {
                return std::format("face {} names cell {} as its owner and the topology holds {} cells",
                                   f, owner.value(), topology.cells());
            });
        }
        const std::optional<CellIndex> neighbour = topology.neighbour(face);
        if (neighbour.has_value() && neighbour->value() >= topology.cells()) {
            return refuse(CfdConvectionProblem::TopologyNamesAnUnknownCell, message, [&] {
                return std::format("face {} names cell {} as its neighbour and the topology holds {} "
                                   "cells",
                                   f, neighbour->value(), topology.cells());
            });
        }
    }

    // PHASE 4 -- the transported component must be one of the three.
    //
    // `VectorComponent` has a fixed underlying type, so
    // `static_cast<VectorComponent>(7)` is a well-formed value and
    // `prescribed[7]` on a three-element array is undefined behaviour. The gate
    // claims to be total over its inputs, so it answers for this rather than
    // trusting a cast it cannot prevent. ASSEMBLY ONLY.
    if (view.component.has_value() &&
        static_cast<std::size_t>(*view.component) >= kVectorComponents.size()) {
        return refuse(CfdConvectionProblem::ComponentOutOfRange, message, [&] {
            return std::format("the transported component is {} and there are {}",
                               static_cast<std::size_t>(*view.component), kVectorComponents.size());
        });
    }

    // PHASE 5 -- the density, PRODUCER ONLY, and before any array is read for
    // the reason thermal gives: a malformed scalar is malformed whether or not
    // it multiplies something small.
    if (view.path == Path::Producer) {
        const double rho = view.density.has_value() ? view.density->si() : 0.0;
        if (!std::isfinite(rho) || !(rho > 0.0)) {
            return refuse(CfdConvectionProblem::DensityNotUsable, message, [&] {
                return std::format("the density is {} kg/m^3, which is not finite and strictly "
                                   "positive; no abs and no max(rho, eps) is applied, because a "
                                   "negative density would reverse every flux in the mesh with no "
                                   "other symptom",
                                   rho);
            });
        }
    }

    // PHASE 6 -- the length the entry point requires.
    if (view.values.size() != view.expectedLength()) {
        return refuse(CfdConvectionProblem::ValueLengthWrong, message, [&] {
            return std::format("the {} holds {} values and this entry point requires {}", view.name(),
                               view.values.size(), view.expectedLength());
        });
    }

    // PHASE 7 -- and every value must be finite. AFTER the length, so a wrong
    // length is reported as a length and not as whatever happens to sit in it.
    for (std::size_t i = 0; i < view.values.size(); ++i) {
        if (!std::isfinite(view.values[i])) {
            return refuse(CfdConvectionProblem::ValueNotFinite, message, [&] {
                return std::format("the {} holds {} at index {}, which is not finite", view.name(),
                                   view.values[i], i);
            });
        }
    }

    // PHASE 8 -- a `FixedValue` velocity must actually have a value.
    for (std::size_t b = 0; b < topology.boundaryFaces(); ++b) {
        const BoundaryFaceIndex boundaryFace = BoundaryFaceIndex::fromValue(b);
        const std::optional<CfdBoundaryTreatment> treatment = boundary.treatment(boundaryFace);
        if (!treatment.has_value() || treatment->velocity != CfdBoundaryPolicy::FixedValue) {
            continue;
        }
        const std::optional<CfdBoundaryFace> facet = boundary.face(boundaryFace);
        if (!facet.has_value() || !facet->velocity.has_value()) {
            return refuse(CfdConvectionProblem::PrescribedValueMissing, message, [&] {
                return std::format("boundary face {} gives the velocity the FixedValue policy and the "
                                   "prepared boundary holds no velocity for it; this is an "
                                   "inconsistency in that value and not a zero, because defaulting "
                                   "to zero would invent a wall condition nobody asked for",
                                   b);
            });
        }
    }

    // PHASE 9 -- and `Computed` is not a velocity boundary condition.
    //
    // UNREACHABLE THROUGH THE DOCUMENT PATH: read down the `U` column of
    // ADR-067's frozen table and the only values are `FixedValue`,
    // `ZeroGradient` and `Symmetry`. Kept because the switch must be total over
    // four policies and the only correct answer for the fourth is a refusal.
    //
    // AND `phi`'S OWN `Computed` IS NOT CHECKED HERE, ON EITHER PATH, WHICH IS
    // THE POINT. Neither path consults the `phi` column: the assembly takes
    // `phi` as a face array, and the producer WRITES that column. ADR-071
    // Decision 5, relied on unchanged.
    for (std::size_t b = 0; b < topology.boundaryFaces(); ++b) {
        const std::optional<CfdBoundaryTreatment> treatment =
            boundary.treatment(BoundaryFaceIndex::fromValue(b));
        if (treatment.has_value() && treatment->velocity == CfdBoundaryPolicy::Computed) {
            return refuse(CfdConvectionProblem::PolicyIsAnOutcome, message, [&] {
                return std::format("boundary face {} gives the velocity the Computed policy, which is "
                                   "an outcome of a solve and not a boundary condition; falling "
                                   "through to the owner's own value would silently admit transport "
                                   "of an unknown",
                                   b);
            });
        }
    }

    // PHASE 10 -- a face that will be interpolated must have a weight.
    //
    // REFUSED, NOT DEFAULTED, which is P19-DIV-001's review finding applied in
    // advance: its transcription returned `0.5` on a degenerate face, so a
    // successful field could be interpolated at a midpoint nobody asked for.
    // UNREACHABLE while `buildCfdGeometry` holds, since it refuses a face with a
    // non-positive orthogonal distance.
    if (interpolates) {
        for (std::size_t f = 0; f < topology.internalFaces(); ++f) {
            const FaceIndex face = FaceIndex::fromValue(f);
            if (!cfdFaceInterpolationWeight(geometry, face).has_value()) {
                return refuse(CfdConvectionProblem::FaceWeightUnavailable, message, [&] {
                    return std::format("internal face {} has no interpolation weight: its orthogonal "
                                       "distance is not positive or it has no unit normal, and a "
                                       "midpoint is not substituted",
                                       f);
                });
            }
        }
    }

    return std::nullopt;
}

/// The failure a gate answer becomes.
[[nodiscard]] CfdConvectionFailure failureOf(CfdConvectionProblem problem, std::string message) {
    return CfdConvectionFailure{problem, Error{ErrorCode::InvalidArgument, std::move(message)}};
}

/// `U_f = U_N + w (U_P - U_N)`.
///
/// THE NUMERICALLY STABLE FORM OF THE CONVEX COMBINATION, and the same form
/// `CfdDivergence.cpp` and `cfdFaceViscosity` both use. Written
/// `w U_P + (1-w) U_N` it rounds twice and can leave the interval the two values
/// span; written this way it is an increment from one endpoint.
///
/// THIS IS THE DUPLICATION ADR-072 DECISION 9 DECLARES RATHER THAN HIDES. The
/// WEIGHT has exactly one definition and is called above; this three-line
/// APPLICATION of it also exists privately in `CfdDivergence.cpp`, and the two
/// must agree. They are not unified, because unifying them means editing a
/// qualified operator for a convenience; the agreement is pinned by a test that
/// requires `div(cfdMassFlux(U, rho))` to equal `rho` times `div(U)` per cell,
/// so a drift between them fails rather than producing two slightly different
/// answers.
[[nodiscard]] Vec interpolated(const Vec& owner, const Vec& neighbour, double weight) noexcept {
    return {neighbour[0] + weight * (owner[0] - neighbour[0]),
            neighbour[1] + weight * (owner[1] - neighbour[1]),
            neighbour[2] + weight * (owner[2] - neighbour[2])};
}

} // namespace

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

std::string_view toString(CfdConvectionScheme scheme) noexcept {
    switch (scheme) {
    case CfdConvectionScheme::Upwind:
        return "Upwind";
    case CfdConvectionScheme::Central:
        return "Central";
    }
    return "<unknown>";
}

std::string_view toString(CfdConvectionProblem problem) noexcept {
    switch (problem) {
    case CfdConvectionProblem::GeometryDescribesAnotherTopology:
        return "GeometryDescribesAnotherTopology";
    case CfdConvectionProblem::BoundaryDescribesAnotherTopology:
        return "BoundaryDescribesAnotherTopology";
    case CfdConvectionProblem::DensityNotUsable:
        return "DensityNotUsable";
    case CfdConvectionProblem::ValueLengthWrong:
        return "ValueLengthWrong";
    case CfdConvectionProblem::ValueNotFinite:
        return "ValueNotFinite";
    case CfdConvectionProblem::PrescribedValueMissing:
        return "PrescribedValueMissing";
    case CfdConvectionProblem::PolicyIsAnOutcome:
        return "PolicyIsAnOutcome";
    case CfdConvectionProblem::FaceWeightUnavailable:
        return "FaceWeightUnavailable";
    case CfdConvectionProblem::ResultIsNotFinite:
        return "ResultIsNotFinite";
    case CfdConvectionProblem::TopologyNamesAnUnknownCell:
        return "TopologyNamesAnUnknownCell";
    case CfdConvectionProblem::ComponentOutOfRange:
        return "ComponentOutOfRange";
    case CfdConvectionProblem::AssemblyIsInconsistent:
        return "AssemblyIsInconsistent";
    }
    return "<unknown>";
}

// ---------------------------------------------------------------------------
// The producer
// ---------------------------------------------------------------------------

std::expected<std::vector<double>, CfdConvectionFailure>
cfdMassFlux(const CfdTopology& topology, const CfdGeometry& geometry,
            const PreparedCfdBoundary& boundary, const CfdMassFluxInput& input) {
    const View view{Path::Producer, input.currentVelocity, topology.cells(), topology.faces(),
                    input.density};
    std::string message;
    if (const std::optional<CfdConvectionProblem> problem =
            problemOf(topology, geometry, boundary, view, /*interpolates=*/true, &message)) {
        return std::unexpected(failureOf(*problem, std::move(message)));
    }

    const double rho = input.density.si();
    std::vector<double> flux(topology.faces(), 0.0);

    for (std::size_t f = 0; f < topology.faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const Vec stored = siOf(geometry.faceAreaVector(face));
        double value = 0.0;

        if (topology.isInternal(face)) {
            // The gate proved the weight and the neighbour are both there.
            const std::optional<double> weight = cfdFaceInterpolationWeight(geometry, face);
            const std::optional<CellIndex> neighbour = topology.neighbour(face);
            // SPLIT, BECAUSE ONE MESSAGE SAYING "weight OR neighbour" CANNOT
            // DISTINGUISH THE TWO CAUSES IT REPORTS AS ONE. A review found the
            // combined form; they are different faults with different tags.
            if (!neighbour.has_value()) {
                return std::unexpected(failureOf(
                    CfdConvectionProblem::AssemblyIsInconsistent,
                    std::format("internal face {} lost its neighbour between the gate and the "
                                "traversal",
                                f)));
            }
            if (!weight.has_value()) {
                return std::unexpected(failureOf(
                    CfdConvectionProblem::FaceWeightUnavailable,
                    std::format("internal face {} lost its interpolation weight between the gate and "
                                "the traversal",
                                f)));
            }
            const Vec owner = view.velocityAt(topology.owner(face).value());
            const Vec other = view.velocityAt(neighbour->value());
            value = rho * dotOf(stored, interpolated(owner, other, *weight));
        } else {
            const std::optional<BoundaryFaceIndex> boundaryFace = topology.boundaryFaceOf(face);
            const std::optional<CfdBoundaryFace> facet =
                boundaryFace.has_value() ? boundary.face(*boundaryFace) : std::nullopt;
            if (!facet.has_value()) {
                return std::unexpected(
                    failureOf(CfdConvectionProblem::BoundaryDescribesAnotherTopology,
                              std::format("face {} is a boundary face the prepared boundary does not "
                                          "hold",
                                          f)));
            }

            // THE TABLE, BY ROW (ADR-067), AND THE WALLS' ZERO IS LITERAL.
            switch (facet->kind) {
            case CfdBoundaryConditionKind::VelocityInlet: {
                // `U` is FixedValue here; the gate proved the value is present.
                if (!facet->velocity.has_value()) {
                    return std::unexpected(failureOf(
                        CfdConvectionProblem::PrescribedValueMissing,
                        std::format("boundary face {} is a velocity inlet with no prescribed velocity",
                                    f)));
                }
                value = rho * dotOf(stored, siOf(*facet->velocity));
                break;
            }
            case CfdBoundaryConditionKind::PressureInlet:
            case CfdBoundaryConditionKind::PressureOutlet:
                // `phi` IS `Computed` HERE, AND THIS IS WHAT COMPUTES IT. `U` is
                // ZeroGradient down this row, so the face velocity is the
                // owner's own and the flow rate is an outcome of the interior
                // field -- which is exactly what ADR-058 says it is.
                value = rho * dotOf(stored, view.velocityAt(topology.owner(face).value()));
                break;
            // A SEVENTH KIND MUST NOT INHERIT THE WALL'S ZERO. `value` starts
            // at `0.0` and this switch has no `default`, so an added kind would
            // silently receive a wall's exact zero -- the inverse of the leak
            // ADR-072 Decision 2 exists to prevent, surfacing the way that
            // decision says a wall leak surfaces: as an imbalance somewhere
            // else. A review found it. The static_assert below is what makes
            // adding a kind a build failure here rather than a silent zero.
            static_assert(kCfdBoundaryConditionKinds.size() == 6,
                          "a CfdBoundaryConditionKind was added: give it a row in this switch, "
                          "because an unhandled kind would otherwise receive a wall's exact zero "
                          "flux and break conservation silently");
            case CfdBoundaryConditionKind::NoSlipWall:
            case CfdBoundaryConditionKind::MovingWall:
            case CfdBoundaryConditionKind::Slip:
                // EXACTLY ZERO, FROM THE TABLE, NEVER A COMPUTED DOT PRODUCT
                // (ADR-072 Decision 2). All three walls read `FixedValue 0` down
                // the `phi` column. A MovingWall prescribes a TANGENTIAL
                // velocity, so `w . S_b` is zero in exact arithmetic and is NOT
                // bit-zero once the centroid, the area vector and the velocity
                // have each been rounded -- and a computed wall flux leaks a
                // tiny mass flow through a solid boundary that surfaces as an
                // imbalance somewhere else.
                value = 0.0;
                break;
            }
        }

        if (!std::isfinite(value)) {
            return std::unexpected(failureOf(
                CfdConvectionProblem::ResultIsNotFinite,
                std::format("the mass flux at face {} is {}, which is not finite; every input was "
                            "finite, so this is an overflow in the product or the sum",
                            f, value)));
        }
        flux[f] = value;
    }

    return flux;
}

// ---------------------------------------------------------------------------
// The assembly
// ---------------------------------------------------------------------------

std::expected<CfdConvectionSystem, CfdConvectionFailure>
assembleCfdConvection(const CfdTopology& topology, const CfdGeometry& geometry,
                      const PreparedCfdBoundary& boundary, const CfdConvectionInput& input) {
    const bool central = input.scheme == CfdConvectionScheme::Central;
    const View view{Path::Assembly,      input.faceFlux, topology.cells(),
                    topology.faces(),    std::nullopt,   input.component};
    std::string message;
    if (const std::optional<CfdConvectionProblem> problem =
            problemOf(topology, geometry, boundary, view, central, &message)) {
        return std::unexpected(failureOf(*problem, std::move(message)));
    }

    const std::size_t cells = topology.cells();
    const auto componentIndex = static_cast<std::size_t>(input.component);

    // THE SYMBOLIC PASS. Every pair the numeric pass can ask for is reserved
    // here, which is what makes the numeric pass's `slotOf` miss an internal
    // inconsistency rather than a reachable refusal.
    std::vector<std::vector<CfdSparseIndex>> pattern(cells);
    for (std::size_t c = 0; c < cells; ++c) {
        pattern[c].push_back(static_cast<CfdSparseIndex>(c));
    }
    for (std::size_t f = 0; f < topology.internalFaces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const std::optional<CellIndex> neighbour = topology.neighbour(face);
        if (!neighbour.has_value()) {
            continue;
        }
        const std::size_t owner = topology.owner(face).value();
        const std::size_t other = neighbour->value();
        pattern[owner].push_back(static_cast<CfdSparseIndex>(other));
        pattern[other].push_back(static_cast<CfdSparseIndex>(owner));
    }
    const Layout layout = layoutOf(pattern);

    std::vector<double> values(layout.inner.size(), 0.0);
    std::vector<double> source(cells, 0.0);
    CfdConvectionSummary summary;
    summary.cells = cells;
    summary.internalFaces = topology.internalFaces();
    summary.boundaryFaces = topology.boundaryFaces();
    summary.scheme = input.scheme;

    double inflowSi = 0.0;
    double outflowSi = 0.0;
    double prescribedInflowSi = 0.0;
    double worstSkewness = 0.0;

    const auto add = [&](std::size_t row, std::size_t column,
                         double amount) -> std::optional<CfdConvectionFailure> {
        const std::optional<std::size_t> slot = slotOf(layout, row, column);
        if (!slot.has_value()) {
            // NOT `ResultIsNotFinite`: nothing here is non-finite. An earlier
            // draft used that tag for this, for a `fromCsr` refusal and for a
            // missing neighbour, so three unrelated faults reported the same
            // thing -- the "a message is not a tag" rule inverted. A review
            // found it.
            return failureOf(CfdConvectionProblem::AssemblyIsInconsistent,
                             std::format("the symbolic pass did not reserve ({}, {})", row, column));
        }
        values[*slot] += amount;
        return std::nullopt;
    };

    // THE SCATTER. One pass over faces, in `FaceIndex` order, each face visited
    // exactly once. ADR-072 Decision 3.
    for (std::size_t f = 0; f < topology.faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const double phi = input.faceFlux[f];
        const std::size_t owner = topology.owner(face).value();

        if (topology.isInternal(face)) {
            const std::optional<CellIndex> neighbour = topology.neighbour(face);
            if (!neighbour.has_value()) {
                // A TOPOLOGY inconsistency, not a boundary one. The earlier tag
                // named the boundary, which this fault has nothing to do with.
                return std::unexpected(
                    failureOf(CfdConvectionProblem::AssemblyIsInconsistent,
                              std::format("internal face {} is internal and has no neighbour", f)));
            }
            const std::size_t other = neighbour->value();

            // THE OWNER'S AND THE NEIGHBOUR'S COEFFICIENTS COME FROM ONE
            // DECISION, TAKEN ONCE. The owner's row receives `+phi x_f` and the
            // neighbour's `-phi x_f`, with `x_f` expressed in the same two
            // coefficients, so the two contributions are exact negations and
            // internal-face conservation is a property of the traversal.
            double ownerShare = 0.0;
            if (central) {
                const std::optional<double> weight = cfdFaceInterpolationWeight(geometry, face);
                if (!weight.has_value()) {
                    return std::unexpected(failureOf(
                        CfdConvectionProblem::FaceWeightUnavailable,
                        std::format("internal face {} lost its weight between the gate and the "
                                    "traversal",
                                    f)));
                }
                ownerShare = *weight;
                const std::optional<double> skew = geometry.skewness(face);
                if (skew.has_value()) {
                    worstSkewness = std::max(worstSkewness, std::abs(*skew));
                }
            } else {
                // UPWIND. `phi >= 0` means flow out of the owner, so the upwind
                // cell IS the owner and its share is one; otherwise zero. The
                // comparison is made ONCE, here, for both rows -- a gather would
                // make it twice and could disagree with itself.
                ownerShare = phi >= 0.0 ? 1.0 : 0.0;
            }
            const double otherShare = 1.0 - ownerShare;

            if (const auto bad = add(owner, owner, phi * ownerShare)) {
                return std::unexpected(*bad);
            }
            if (const auto bad = add(owner, other, phi * otherShare)) {
                return std::unexpected(*bad);
            }
            if (const auto bad = add(other, owner, -phi * ownerShare)) {
                return std::unexpected(*bad);
            }
            if (const auto bad = add(other, other, -phi * otherShare)) {
                return std::unexpected(*bad);
            }
            continue;
        }

        // A BOUNDARY FACE, TREATED BY ADR-072 DECISION 6'S RULE WHATEVER THE
        // SCHEME. `Central` changes how a value between TWO cells is formed, and
        // a boundary face has one cell and a boundary condition. This is also
        // why the weight is not consulted here: it returns `1.0` for a boundary
        // face, which would make the face value the owner's and silently discard
        // a velocity inlet's prescribed value.
        const std::optional<BoundaryFaceIndex> boundaryFace = topology.boundaryFaceOf(face);
        const std::optional<CfdBoundaryTreatment> treatment =
            boundaryFace.has_value() ? boundary.treatment(*boundaryFace) : std::nullopt;
        if (!treatment.has_value()) {
            return std::unexpected(
                failureOf(CfdConvectionProblem::BoundaryDescribesAnotherTopology,
                          std::format("face {} is a boundary face with no treatment", f)));
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
            // Phase 9 refused this already.
            return std::unexpected(
                failureOf(CfdConvectionProblem::PolicyIsAnOutcome,
                          std::format("boundary face {} reached the traversal with the Computed "
                                      "velocity policy",
                                      f)));
        }

        if (phi >= 0.0) {
            // OUTFLOW: the upwind value is the owner's, whatever the policy
            // says, because the flow is leaving and the condition describes what
            // arrives.
            ++summary.outflowFaces;
            outflowSi += phi;
            if (const auto bad = add(owner, owner, phi)) {
                return std::unexpected(*bad);
            }
            continue;
        }

        ++summary.inflowFaces;
        inflowSi += phi;

        if (treatment->velocity == CfdBoundaryPolicy::FixedValue) {
            // INFLOW WITH A PRESCRIBED VALUE: a SOURCE, not a coefficient. This
            // is the one case whose flux is ABSENT from the matrix, which is why
            // the row-sum identity needs `prescribedInflow` restored and not all
            // inflow.
            const std::optional<CfdBoundaryFace> facet = boundary.face(*boundaryFace);
            if (!facet.has_value() || !facet->velocity.has_value()) {
                return std::unexpected(failureOf(
                    CfdConvectionProblem::PrescribedValueMissing,
                    std::format("boundary face {} prescribes a velocity that is not there", f)));
            }
            const Vec prescribed = siOf(*facet->velocity);
            ++summary.prescribedInflowFaces;
            prescribedInflowSi += phi;
            source[owner] -= phi * prescribed[componentIndex];
            continue;
        }

        // INFLOW, ZERO-GRADIENT OR SYMMETRY: the face value IS the owner's, so
        // the coefficient lands on the diagonal -- and `phi` is NEGATIVE here,
        // so it SUBTRACTS from the diagonal and reduces dominance directly.
        //
        // THIS IS BACKFLOW, and it is the second way boundedness fails. It is a
        // real and known weakness of a zero-gradient inlet, and it is what
        // ADR-067's table prescribes, so it is implemented and COUNTED rather
        // than corrected.
        //
        // `Symmetry` IS TREATED AS ZERO-GRADIENT HERE, AND THAT IS NOT ADR-071
        // DECISION 7'S CHOICE. Its reason was that a symmetry plane's NORMAL
        // flux is zero by definition, so a divergence may skip the face. That
        // does not transfer: convection transports a TANGENTIAL component across
        // the face too, and symmetry prescribes the tangential components as
        // zero-gradient. No correct caller sees a difference -- ADR-067 gives
        // all three walls `phi = 0` -- but skipping the face would break the
        // row-sum identity for a hand-made `phi`, silently.
        ++summary.backflowFaces;
        if (const auto bad = add(owner, owner, phi)) {
            return std::unexpected(*bad);
        }
    }

    for (std::size_t i = 0; i < values.size(); ++i) {
        if (!std::isfinite(values[i])) {
            return std::unexpected(failureOf(
                CfdConvectionProblem::ResultIsNotFinite,
                std::format("the assembled coefficient at slot {} is {}, which is not finite; every "
                            "input was finite, so this is an overflow in the accumulation",
                            i, values[i])));
        }
    }
    for (std::size_t c = 0; c < source.size(); ++c) {
        if (!std::isfinite(source[c])) {
            return std::unexpected(
                failureOf(CfdConvectionProblem::ResultIsNotFinite,
                          std::format("the assembled source at cell {} is {}, which is not finite", c,
                                      source[c])));
        }
    }

    // F6: THE REPORTED TOTALS ARE INSIDE THE GATE TOO. An earlier draft
    // scanned `values` and `source` and then published these three unchecked,
    // so an assembly could succeed with `outflow == +inf` while every
    // coefficient was finite -- exactly the hole `ResultIsNotFinite` is
    // documented to close, left open for the reported quantities.
    // `CfdDivergence.cpp` checks its own totals; a review found that this one
    // did not.
    for (const auto& [name, value] : std::initializer_list<std::pair<const char*, double>>{
             {"inflow", inflowSi}, {"outflow", outflowSi},
             {"prescribed inflow", prescribedInflowSi}}) {
        if (!std::isfinite(value)) {
            return std::unexpected(failureOf(
                CfdConvectionProblem::ResultIsNotFinite,
                std::format("the reported {} total is {}, which is not finite; every coefficient may "
                            "still be finite, which is why this is checked separately",
                            name, value)));
        }
    }

    summary.worstSkewness = worstSkewness;
    summary.inflow = MassFlowRate::fromSi(inflowSi);
    summary.outflow = MassFlowRate::fromSi(outflowSi);
    summary.prescribedInflow = MassFlowRate::fromSi(prescribedInflowSi);

    // NO `requiredSymmetryTolerance`. ADR-072 Decision 3: a convection matrix is
    // asymmetric by nature, and demanding symmetry of it would be wrong rather
    // than merely slack.
    std::expected<CfdSparseMatrix, CfdSparseFailure> matrix =
        CfdSparseMatrix::fromCsr(cells, cells, layout.rowStart, layout.inner, std::move(values));
    if (!matrix.has_value()) {
        return std::unexpected(failureOf(
            CfdConvectionProblem::AssemblyIsInconsistent,
            std::format("the assembled arrays were refused by CfdSparseMatrix::fromCsr as {}",
                        toString(matrix.error().problem))));
    }

    return CfdConvectionSystem{std::move(*matrix), std::move(source), summary};
}

// ---------------------------------------------------------------------------
// The predictors
// ---------------------------------------------------------------------------

std::optional<CfdConvectionFailure>
cfdConvectionFailure(const CfdTopology& topology, const CfdGeometry& geometry,
                     const PreparedCfdBoundary& boundary, const CfdConvectionInput& input) {
    const View view{Path::Assembly,   input.faceFlux, topology.cells(),
                    topology.faces(), std::nullopt,   input.component};
    std::string message;
    const std::optional<CfdConvectionProblem> problem =
        problemOf(topology, geometry, boundary, view,
                  input.scheme == CfdConvectionScheme::Central, &message);
    if (!problem.has_value()) {
        return std::nullopt;
    }
    return failureOf(*problem, std::move(message));
}

std::optional<CfdConvectionProblem>
cfdConvectionProblem(const CfdTopology& topology, const CfdGeometry& geometry,
                     const PreparedCfdBoundary& boundary,
                     const CfdConvectionInput& input) noexcept {
    const View view{Path::Assembly,   input.faceFlux, topology.cells(),
                    topology.faces(), std::nullopt,   input.component};
    return problemOf(topology, geometry, boundary, view,
                     input.scheme == CfdConvectionScheme::Central, nullptr);
}

std::optional<CfdConvectionProblem>
cfdMassFluxProblem(const CfdTopology& topology, const CfdGeometry& geometry,
                   const PreparedCfdBoundary& boundary, const CfdMassFluxInput& input) noexcept {
    const View view{Path::Producer, input.currentVelocity, topology.cells(), topology.faces(),
                    input.density};
    return problemOf(topology, geometry, boundary, view, /*interpolates=*/true, nullptr);
}

} // namespace bettercad::cfd
