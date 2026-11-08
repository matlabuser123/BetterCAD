/// \file
/// P19-MOM-001 — the momentum assembly. ADR-073.
///
/// THE SHAPE OF THIS FILE. A gate that is total over its inputs and reports the
/// earliest phase that fails; then an assembly that calls two qualified
/// operators, verifies their sparse patterns are identical, sums them slot-wise
/// and adds three cell-wise source terms. There is no face loop here at all:
/// every face-dependent coefficient was decided by `assembleCfdConvection` or
/// `assembleCfdDiffusion`, and re-deciding one would be the duplication
/// ADR-073 Decision 1 rejects.

#include <bettercad/cfd/CfdMomentum.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace bettercad::cfd {

namespace {

using Vec = std::array<double, 3>;

// THE ONLY FLATTENING LEFT, AND IT IS IN THE GATE ALONE. Every term of the
// assembly is now computed in TYPED quantities and reaches a `double` only at
// the point of storage, which is what makes the header's `static_assert` claim
// true rather than decorative -- see `assembleCfdMomentum`. This one overload
// survives because phase 7 compares a gravity vector's three components against
// zero, which is a test on the numbers and not arithmetic on them.
//
// There is no `siOf(Velocity3)`: the previous velocity arrives as a
// component-blocked `std::span<const double>` and is indexed as
// `component * cells + cell`, never as a triple.
[[nodiscard]] Vec siOf(const Acceleration3& v) { return {v.x.si(), v.y.si(), v.z.si()}; }

/// An input the caller got wrong.
[[nodiscard]] CfdMomentumFailure failureOf(CfdMomentumProblem problem, std::string message) {
    return CfdMomentumFailure{problem, Error{ErrorCode::InvalidArgument, std::move(message)}};
}

/// A fault in THIS MODULE rather than in the caller's input.
///
/// A SEPARATE CODE, AND THE DISTINCTION IS `assembleCfdDiffusion`'S, which uses
/// `FailedPrecondition` for an input and `Internal` for an internal
/// inconsistency. An earlier draft stamped every refusal `InvalidArgument`,
/// including `AssemblyIsInconsistent`, which judges this assembly's own
/// self-consistency and blames the caller for it -- the same category error as
/// a wrong tag, in the milestone whose whole error-channel argument is that a
/// tag must say what happened. A review found it.
[[nodiscard]] CfdMomentumFailure internalFailure(CfdMomentumProblem problem,
                                                 std::string message) {
    return CfdMomentumFailure{problem, Error{ErrorCode::Internal, std::move(message)}};
}

[[nodiscard]] bool isUsablePositive(double si) noexcept { return std::isfinite(si) && si > 0.0; }

/// THE MESSAGE IS BUILT ONLY WHEN A CALLER PASSES SOMEWHERE TO PUT IT, which is
/// what makes `cfdMomentumProblem`'s `noexcept` honest: it allocates nothing.
/// `assembleCfdConvection`'s gate is written this way for the same reason, and
/// taking a formatted `std::string` by value instead would format on every
/// predictor call -- the argument is evaluated before the function that would
/// discard it.
template <typename Describe>
[[nodiscard]] std::optional<CfdMomentumProblem> refuse(CfdMomentumProblem problem,
                                                       std::string* message, Describe describe) {
    if (message != nullptr) {
        *message = describe();
    }
    return problem;
}

/// One caller-supplied SCALAR array the gate checks, so the length phase and
/// the finiteness phase walk the same arrays in the same order and cannot
/// disagree about which exist.
///
/// A LIST RATHER THAN A SEQUENCE OF `if`s, because the two phases must cover
/// exactly the same set. An array checked for length and not for value is the
/// hole `P19-DIFF-001`'s review found in its own operator, where "the two
/// caller-supplied arrays were validated for LENGTH and never for VALUE, so a
/// single NaN flowed through the cross terms into the source".
///
/// THE TWO VECTOR ARRAYS ARE NOT IN THIS LIST, AND THAT IS A CORRECTION. An
/// earlier draft put all five arrays in one list by `reinterpret_cast`ing the
/// two spans of three-component quantities to `const double*` and walking
/// `entries * stride` doubles. Three things were wrong with it, and an
/// independent review and the author found it independently:
///
/// ```text
/// it was UB            reading a double through a pointer cast from a
///                      CartesianVector<Q>* is not a valid access path, and no
///                      array-of-double object spans the three members
/// it assumed a layout  nothing asserted sizeof(VelocityGradient3) ==
///                      3 * sizeof(double). Had Quantity ever gained a member,
///                      phase 11 would have walked a PREFIX of each array --
///                      in bounds, no crash, silently value-checking part of it
///                      while phase 10 still passed: the very hole this list
///                      exists to close
/// the typed check      CfdData.hpp already owns
/// already existed      isFinite(const CartesianVector<Q>&), which is what
///                      assembleCfdDiffusion's own gate uses. Reaching past it
///                      with a cast strips the type in order to inspect it, in
///                      a file whose thesis is that a proof beats prose
/// ```
///
/// The `stride` field went with it, and so did the bug it had already caused
/// once: a first draft derived the stride by inspecting two characters of the
/// array's NAME, and the test matched `"the face flux phi"` as well as
/// `"the face gradient"`, so the scalar flux array would have been walked three
/// doubles per entry and read three times past its end.
struct CheckedScalars {
    std::string_view name;
    std::span<const double> values;
    std::size_t expected;
};

/// Three is the most scalar arrays any one input carries.
using ScalarArrays = std::array<CheckedScalars, 3>;

/// The declared check order of the scalar arrays, written into @p out and
/// counted, so the gate allocates nothing on the predictor path. `faceFlux`
/// first because it is the only required one.
[[nodiscard]] std::size_t scalarArrays(const CfdTopology& topology, const CfdMomentumInput& input,
                                       ScalarArrays& out) noexcept {
    const std::size_t cells = topology.cells();
    std::size_t n = 0;
    out[n++] = CheckedScalars{"the face flux phi", input.faceFlux, topology.faces()};
    if (!input.currentVelocity.empty()) {
        out[n++] = CheckedScalars{"the current velocity", input.currentVelocity, 3 * cells};
    }
    if (!input.previousVelocity.empty()) {
        out[n++] = CheckedScalars{"the previous velocity", input.previousVelocity, 3 * cells};
    }
    return n;
}

/// THE ONE DECISION PATH. The assembly and both predictors reach the gate
/// through here, so there is no second copy to drift -- `assembleCfdConvection`
/// and `assembleCfdDiffusion` are both written this way and for this reason.
///
/// EACH PHASE IS ITS OWN PASS, which is what makes the declared enum order the
/// check order BETWEEN phases. Within one pass the first offending face or
/// array decides, and no claim is made about the enum order there --
/// `CfdBoundary.hpp`, `CfdGradient.hpp`, `CfdDivergence.hpp` and
/// `CfdConvection.hpp` all say the same of their own gates, because claiming an
/// order the code does not have would be a false claim about a diagnostic.
[[nodiscard]] std::optional<CfdMomentumProblem>
problemOf(const CfdTopology& topology, const CfdGeometry& geometry,
          const PreparedCfdBoundary& boundary, const CfdMomentumInput& input,
          std::string* message) {
    // ---- PHASE 1: the metrics describe this topology ---------------------
    if (!geometry.describes(topology)) {
        return refuse(CfdMomentumProblem::GeometryDescribesAnotherTopology, message, [&] {
            return std::format("the finite-volume metrics were built for a different mesh "
                               "generation than the topology, so its {} cells and {} faces do not "
                               "mean the same thing",
                               topology.cells(), topology.faces());
        });
    }

    // ---- PHASE 2: the boundary was prepared against this topology --------
    //
    // THE CONDITIONAL FORM, which is `P19-DIFF-001`'s correction and
    // `P19-CONV-001`'s inheritance of it, copied here in that operator's own
    // shape: a default-constructed source is compared against nothing, because
    // `bindCfdBoundary` deliberately returns one and an unconditional
    // comparison would refuse every seam-built boundary. `P19-CONV-001`'s first
    // draft dropped the comparison altogether and was strictly weaker than the
    // operator it cited as precedent; a review found it. The boundary-face
    // count is compared unconditionally, because that one always means
    // something.
    if (boundary.boundaryFaces() != topology.boundaryFaces()) {
        return refuse(CfdMomentumProblem::BoundaryDescribesAnotherTopology, message, [&] {
            return std::format("the prepared boundary holds {} boundary faces and the topology "
                               "holds {}",
                               boundary.boundaryFaces(), topology.boundaryFaces());
        });
    }
    const CfdBoundarySource& source = boundary.source();
    if (!(source == CfdBoundarySource{}) &&
        (!(source.mesh == topology.mesh()) || source.cells != topology.cells() ||
         source.boundaryFaces != topology.boundaryFaces())) {
        return refuse(CfdMomentumProblem::BoundaryDescribesAnotherTopology, message, [&] {
            return std::format("the prepared boundary was built for a mesh with {} cells and {} "
                               "boundary faces, or for a different mesh generation; the topology "
                               "holds {} and {}",
                               source.cells, source.boundaryFaces, topology.cells(),
                               topology.boundaryFaces());
        });
    }

    // ---- PHASE 3: every face names cells the topology holds --------------
    //
    // THIS OPERATOR DOES NOT ITSELF INDEX BY OWNER OR NEIGHBOUR -- it sums
    // slots and walks cells -- so the unguarded `SIZE_MAX` write that
    // `P19-DIV-001`'s review and then `P19-CONV-001`'s review each found has no
    // path here. The check is kept because the gate is declared total over its
    // inputs, because both sub-operators do index that way, and because
    // reporting it as `ConvectionRefusedTheInput` would name convection for a
    // fault that is about the topology.
    const std::size_t cells = topology.cells();
    for (std::size_t f = 0; f < topology.faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const CellIndex owner = topology.owner(face);
        if (!owner.isValid() || owner.value() >= cells) {
            return refuse(CfdMomentumProblem::TopologyNamesAnUnknownCell, message, [&] {
                return std::format("face {} names owner cell {} and the topology holds {} cells", f,
                                   owner.value(), cells);
            });
        }
        const std::optional<CellIndex> neighbour = topology.neighbour(face);
        if (neighbour.has_value() && (!neighbour->isValid() || neighbour->value() >= cells)) {
            return refuse(CfdMomentumProblem::TopologyNamesAnUnknownCell, message, [&] {
                return std::format("face {} names neighbour cell {} and the topology holds {} cells",
                                   f, neighbour->value(), cells);
            });
        }
    }

    // ---- PHASE 4: the component is one of the three ----------------------
    //
    // `VectorComponent` is an enum class with a fixed underlying type, so
    // `static_cast<VectorComponent>(7)` is a well-formed value and indexing a
    // three-element array with it is undefined behaviour.
    if (std::ranges::find(kVectorComponents, input.component) == kVectorComponents.end()) {
        return refuse(CfdMomentumProblem::ComponentOutOfRange, message, [&] {
            return std::format("the assembled component is {} and the only components are 0, 1 "
                               "and 2",
                               static_cast<unsigned>(input.component));
        });
    }

    // ---- PHASE 5: the scheme is one of the two ---------------------------
    //
    // THE SAME ARGUMENT AS THE COMPONENT, AND AN EARLIER DRAFT MADE IT FOR ONE
    // AND NOT THE OTHER. `CfdConvectionScheme` is an enum class with a fixed
    // underlying type, so `static_cast<CfdConvectionScheme>(7)` is a
    // well-formed value -- and `assembleCfdConvection` forms
    // `central = (scheme == Central)`, which is FALSE for 7, so the system
    // would be assembled with UPWIND: a scheme the caller did not ask for,
    // reported back as 7 by the summary and as an unknown name by `toString`.
    // Not undefined behaviour, which is why the component check is the more
    // urgent of the two, but a SILENT SUBSTITUTION is exactly the class of
    // fault this gate exists to refuse, and the boundedness statements in
    // `CfdMomentumSummary` are made per scheme. An independent review found the
    // asymmetry.
    if (std::ranges::find(kCfdConvectionSchemes, input.scheme) == kCfdConvectionSchemes.end()) {
        return refuse(CfdMomentumProblem::SchemeOutOfRange, message, [&] {
            return std::format("the convection scheme is {} and the only schemes are 0 and 1; it "
                               "is refused rather than defaulted, because a scheme nobody asked "
                               "for would be assembled silently",
                               static_cast<unsigned>(input.scheme));
        });
    }

    // ---- PHASE 6: the density --------------------------------------------
    //
    // REQUIRED EVEN WHERE IT IS UNUSED. It enters only the transient term and
    // the body force, so a steady gravity-free assembly reads it nowhere;
    // making the requirement conditional would make a fluid property's validity
    // depend on which terms were switched on.
    if (!isUsablePositive(input.density.si())) {
        return refuse(CfdMomentumProblem::DensityNotUsable, message, [&] {
            return std::format("the density is {} kg/m^3, which must be finite and strictly "
                               "positive; it is not repaired, because a negative density would "
                               "reverse the body force with no other symptom",
                               input.density.si());
        });
    }

    // ---- PHASE 7: the viscosity ------------------------------------------
    if (!isUsablePositive(input.viscosity.si())) {
        return refuse(CfdMomentumProblem::ViscosityNotUsable, message, [&] {
            return std::format("the dynamic viscosity is {} Pa s, which must be finite and "
                               "strictly positive",
                               input.viscosity.si());
        });
    }

    // ---- PHASE 8: gravity, when there is a record ------------------------
    if (input.gravity.has_value()) {
        const Vec g = siOf(input.gravity->acceleration);
        if (!std::isfinite(g[0]) || !std::isfinite(g[1]) || !std::isfinite(g[2])) {
            return refuse(CfdMomentumProblem::GravityNotUsable, message, [&] {
                return std::format("the gravity vector is ({}, {}, {}) m/s^2 and must be finite",
                                   g[0], g[1], g[2]);
            });
        }
        if (g[0] == 0.0 && g[1] == 0.0 && g[2] == 0.0) {
            // REFUSED, NOT TREATED AS ABSENCE. `CfdData.hpp` makes the absence
            // of the record the absence of gravity and `validate(CfdGravity)`
            // refuses a zero vector, so accepting one here would admit a state
            // the canonical type forbids.
            return refuse(CfdMomentumProblem::GravityNotUsable, message, [&] {
                return std::string{
                    "the gravity record holds a zero vector; no gravity is the ABSENCE of the "
                    "record and not a zero one, because a zero vector would be a second spelling "
                    "of none"};
            });
        }
    }

    // ---- PHASE 9: the time step, when there is one -----------------------
    if (input.timeStep.has_value() && !isUsablePositive(input.timeStep->si())) {
        return refuse(CfdMomentumProblem::TimeStepNotUsable, message, [&] {
            return std::format("the time step is {} s, which must be finite and strictly positive",
                               input.timeStep->si());
        });
    }

    // ---- PHASE 10: the transient inputs agree with each other -------------
    if (input.timeStep.has_value() && input.previousVelocity.empty()) {
        return refuse(CfdMomentumProblem::TransientInputsAreInconsistent, message, [&] {
            return std::string{"a time step was supplied, so this is the transient form, and it "
                               "needs the velocity at the previous time level"};
        });
    }
    if (!input.timeStep.has_value() && !input.previousVelocity.empty()) {
        // AN INERT INPUT IS REFUSED RATHER THAN IGNORED, which is P18's
        // treatment of an initial temperature on a steady analysis and
        // `CfdData.hpp`'s reason for making no gravity an absent record.
        return refuse(CfdMomentumProblem::TransientInputsAreInconsistent, message, [&] {
            return std::string{"a previous velocity was supplied with no time step, so it would "
                               "contribute nothing; the steady form takes no previous time level"};
        });
    }

    // ---- PHASE 11: every supplied array is the right length --------------
    //
    // THE ORDER IS: the three scalar arrays, then the face gradient, then the
    // pressure gradient. Phase 12 walks exactly the same arrays in exactly the
    // same order, which is the property that makes "length-checked implies
    // value-checked" structural rather than remembered.
    ScalarArrays scalars{};
    const std::size_t scalarCount = scalarArrays(topology, input, scalars);
    for (std::size_t a = 0; a < scalarCount; ++a) {
        if (scalars[a].values.size() != scalars[a].expected) {
            return refuse(CfdMomentumProblem::ValueLengthWrong, message, [&] {
                return std::format("{} holds {} entries and {} were expected", scalars[a].name,
                                   scalars[a].values.size(), scalars[a].expected);
            });
        }
    }
    if (!input.faceGradient.empty() && input.faceGradient.size() != topology.faces()) {
        return refuse(CfdMomentumProblem::ValueLengthWrong, message, [&] {
            return std::format("the face gradient holds {} entries and {} were expected, one per "
                               "face",
                               input.faceGradient.size(), topology.faces());
        });
    }
    if (!input.pressureGradient.empty() && input.pressureGradient.size() != cells) {
        return refuse(CfdMomentumProblem::ValueLengthWrong, message, [&] {
            return std::format("the pressure gradient holds {} entries and {} were expected, one "
                               "per cell",
                               input.pressureGradient.size(), cells);
        });
    }

    // ---- PHASE 12: every supplied value is finite ------------------------
    //
    // AFTER EVERY LENGTH, so a wrong length is reported as a length and not as
    // whatever happens to sit in it. THE SAME ARRAYS AS PHASE 10 IN THE SAME
    // ORDER -- which is the hole a review found in `assembleCfdDiffusion`,
    // where an array was validated for length and never for value.
    //
    // THE TWO VECTOR ARRAYS GO THROUGH `isFinite(const CartesianVector<Q>&)`,
    // the typed check `CfdData.hpp` owns and `assembleCfdDiffusion`'s own gate
    // uses, rather than through a flattened `const double*`. The diagnostic
    // names the ENTRY, which locates a face or a cell; the flattened form named
    // a double offset, which locates nothing a reader can act on.
    for (std::size_t a = 0; a < scalarCount; ++a) {
        for (std::size_t i = 0; i < scalars[a].values.size(); ++i) {
            if (!std::isfinite(scalars[a].values[i])) {
                return refuse(CfdMomentumProblem::ValueNotFinite, message, [&] {
                    return std::format("{} holds {} at entry {} of {}, which is not finite",
                                       scalars[a].name, scalars[a].values[i], i,
                                       scalars[a].values.size());
                });
            }
        }
    }
    for (std::size_t i = 0; i < input.faceGradient.size(); ++i) {
        if (!isFinite(input.faceGradient[i])) {
            return refuse(CfdMomentumProblem::ValueNotFinite, message, [&] {
                return std::format("the face gradient at face {} of {} is not finite", i,
                                   input.faceGradient.size());
            });
        }
    }
    for (std::size_t i = 0; i < input.pressureGradient.size(); ++i) {
        if (!isFinite(input.pressureGradient[i])) {
            return refuse(CfdMomentumProblem::ValueNotFinite, message, [&] {
                return std::format("the pressure gradient at cell {} of {} is not finite", i,
                                   input.pressureGradient.size());
            });
        }
    }

    // ---- PHASE 13: convection would accept this input --------------------
    //
    // PREDICTED, NOT RE-DERIVED (ADR-073 Decision 7). Re-deriving twelve checks
    // that would then have to agree with that operator forever is the
    // duplication this milestone exists to avoid. The consequence is that this
    // gate is exactly as complete as that predictor, which shares one decision
    // path with its own assembly -- an inherited limitation, recorded and not
    // pretended away.
    const CfdConvectionInput convection{input.faceFlux, input.scheme, input.component};
    if (const std::optional<CfdConvectionProblem> problem =
            cfdConvectionProblem(topology, geometry, boundary, convection)) {
        return refuse(CfdMomentumProblem::ConvectionRefusedTheInput, message, [&] {
            return std::format("the convection operator refuses this input as {}",
                               toString(*problem));
        });
    }

    // ---- PHASE 14: diffusion would accept this input ---------------------
    const CfdDiffusionInput diffusion{input.viscosity, input.component, input.currentVelocity,
                                      input.faceGradient};
    if (const std::optional<CfdDiffusionProblem> problem =
            cfdDiffusionProblem(topology, geometry, boundary, diffusion)) {
        return refuse(CfdMomentumProblem::DiffusionRefusedTheInput, message, [&] {
            return std::format("the diffusion operator refuses this input as {}",
                               toString(*problem));
        });
    }

    return std::nullopt;
}

} // namespace

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

std::string_view toString(CfdMomentumProblem problem) noexcept {
    switch (problem) {
    case CfdMomentumProblem::GeometryDescribesAnotherTopology:
        return "geometry describes another topology";
    case CfdMomentumProblem::BoundaryDescribesAnotherTopology:
        return "boundary describes another topology";
    case CfdMomentumProblem::TopologyNamesAnUnknownCell:
        return "topology names an unknown cell";
    case CfdMomentumProblem::ComponentOutOfRange:
        return "component out of range";
    case CfdMomentumProblem::SchemeOutOfRange:
        return "scheme out of range";
    case CfdMomentumProblem::DensityNotUsable:
        return "density not usable";
    case CfdMomentumProblem::ViscosityNotUsable:
        return "viscosity not usable";
    case CfdMomentumProblem::GravityNotUsable:
        return "gravity not usable";
    case CfdMomentumProblem::TimeStepNotUsable:
        return "time step not usable";
    case CfdMomentumProblem::TransientInputsAreInconsistent:
        return "transient inputs are inconsistent";
    case CfdMomentumProblem::ValueLengthWrong:
        return "value length wrong";
    case CfdMomentumProblem::ValueNotFinite:
        return "value not finite";
    case CfdMomentumProblem::ConvectionRefusedTheInput:
        return "convection refused the input";
    case CfdMomentumProblem::DiffusionRefusedTheInput:
        return "diffusion refused the input";
    case CfdMomentumProblem::ResultIsNotFinite:
        return "result is not finite";
    case CfdMomentumProblem::AssemblyIsInconsistent:
        return "assembly is inconsistent";
    }
    return "unknown momentum problem";
}

// ---------------------------------------------------------------------------
// The assembly
// ---------------------------------------------------------------------------

std::expected<CfdMomentumSystem, CfdMomentumFailure>
assembleCfdMomentum(const CfdTopology& topology, const CfdGeometry& geometry,
                    const PreparedCfdBoundary& boundary, const CfdMomentumInput& input) {
    std::string message;
    if (const std::optional<CfdMomentumProblem> problem =
            problemOf(topology, geometry, boundary, input, &message)) {
        return std::unexpected(failureOf(*problem, std::move(message)));
    }

    const std::size_t cells = topology.cells();
    const auto componentIndex = static_cast<std::size_t>(input.component);

    // ---- THE TWO SUB-ASSEMBLIES -----------------------------------------
    //
    // Neither can fail for an input reason: phases 12 and 13 asked their own
    // predictors. A refusal here is therefore an INTERNAL inconsistency, and
    // it carries that tag rather than the sub-operator's -- the tag says where
    // the fault is, and a predictor disagreeing with its own assembly is a
    // fault in this module and not in the caller's input.
    const CfdConvectionInput convectionInput{input.faceFlux, input.scheme, input.component};
    std::expected<CfdConvectionSystem, CfdConvectionFailure> convection =
        assembleCfdConvection(topology, geometry, boundary, convectionInput);
    if (!convection.has_value()) {
        // THE SUB-TAG DECIDES, AND AN EARLIER DRAFT MAPPED EVERY SUB-REFUSAL TO
        // `AssemblyIsInconsistent`. An independent review found the
        // consequence, and it was a FALSE UNREACHABILITY CLAIM rather than a
        // cosmetic one: phase 13 asks `cfdConvectionProblem`, which judges
        // INPUTS only and says so in its own header -- "IT CANNOT SEE
        // `ResultIsNotFinite`, which judges an output this function never
        // computes". So a `phi` whose values are each finite but whose SUM
        // overflows passes momentum's phase 12, passes the convection
        // predictor, and is then refused by the convection assembly as
        // `ResultIsNotFinite`. That is a non-finite RESULT, which momentum's
        // enum already has the right tag for; calling it an inconsistency said
        // that a predictor disagreed with its own assembly, when the two
        // agreed perfectly.
        const CfdConvectionProblem sub = convection.error().problem;
        if (sub == CfdConvectionProblem::ResultIsNotFinite) {
            return std::unexpected(failureOf(
                CfdMomentumProblem::ResultIsNotFinite,
                std::format("the convection assembly produced a non-finite result from inputs "
                            "every phase accepted: {}",
                            convection.error().error.message)));
        }
        return std::unexpected(internalFailure(
            CfdMomentumProblem::AssemblyIsInconsistent,
            std::format("the convection predictor accepted this input and the convection "
                        "assembly then refused it as {}, which the predictor can see: {}",
                        toString(sub), convection.error().error.message)));
    }

    const CfdDiffusionInput diffusionInput{input.viscosity, input.component, input.currentVelocity,
                                           input.faceGradient};
    Result<CfdDiffusionSystem> diffusion =
        assembleCfdDiffusion(topology, geometry, boundary, diffusionInput);
    if (!diffusion.has_value()) {
        // THERE IS NO SUB-TAG TO READ HERE, AND THAT IS P19-DIFF-001'S RECORDED
        // LIMITATION INHERITED. `assembleCfdDiffusion` returns
        // `Result<CfdDiffusionSystem>`, whose channel carries no tag --
        // `CfdDiffusion.hpp` removed its own `AssembledMatrixInconsistent`
        // for exactly that reason -- so momentum cannot tell an output overflow
        // from a genuine inconsistency and must not guess. A viscosity of
        // `1.5e308` is finite and strictly positive, passes phase 7, passes
        // `cfdDiffusionProblem`, and then overflows `mu A / d`; it arrives here.
        //
        // SO THIS PATH IS REACHED, AND IS NO LONGER DOCUMENTED AS UNREACHABLE.
        // The tag is honest about what momentum actually knows: the predictor
        // accepted an input its own assembly refused. The message carries
        // diffusion's own sentence, so the diagnostic is not weaker than the
        // untagged form it wraps.
        return std::unexpected(internalFailure(
            CfdMomentumProblem::AssemblyIsInconsistent,
            std::format("the diffusion predictor accepted this input and the diffusion assembly "
                        "then refused it, and its channel carries no tag to distinguish an "
                        "output overflow from an inconsistency: {}",
                        diffusion.error().message)));
    }

    const CfdSparseMatrix& convectionMatrix = convection->matrix;
    const CfdSparseMatrix& diffusionMatrix = diffusion->matrix;

    // ---- THE PATTERNS ARE VERIFIED IDENTICAL, NOT ASSUMED SO ------------
    //
    // ADR-073 Decision 2. Both operators build a diagonal for every cell plus
    // both off-diagonals of every internal face, through the same `layoutOf`,
    // so the patterns are identical and a slot-wise sum is exact. Relying on
    // that by reading the code would be a coupling with no gate.
    //
    // THE ALTERNATIVE WAS `coeffSi`, WHICH WOULD SILENTLY DROP a coefficient
    // falling outside this function's pattern. A dropped coefficient is a
    // conservation error with no symptom.
    if (convectionMatrix.rows() != cells || convectionMatrix.columns() != cells ||
        diffusionMatrix.rows() != cells || diffusionMatrix.columns() != cells) {
        return std::unexpected(internalFailure(
            CfdMomentumProblem::AssemblyIsInconsistent,
            std::format("the sub-assemblies produced {}x{} and {}x{} matrices for {} cells",
                        convectionMatrix.rows(), convectionMatrix.columns(), diffusionMatrix.rows(),
                        diffusionMatrix.columns(), cells)));
    }
    const std::span<const CfdSparseIndex> convectionRowStart = convectionMatrix.rowStart();
    const std::span<const CfdSparseIndex> diffusionRowStart = diffusionMatrix.rowStart();
    const std::span<const CfdSparseIndex> convectionInner = convectionMatrix.inner();
    const std::span<const CfdSparseIndex> diffusionInner = diffusionMatrix.inner();
    if (convectionRowStart.size() != diffusionRowStart.size() ||
        convectionInner.size() != diffusionInner.size()) {
        return std::unexpected(internalFailure(
            CfdMomentumProblem::AssemblyIsInconsistent,
            std::format("the convection pattern holds {} rows and {} entries; the diffusion "
                        "pattern holds {} and {}",
                        convectionRowStart.size(), convectionInner.size(), diffusionRowStart.size(),
                        diffusionInner.size())));
    }
    for (std::size_t i = 0; i < convectionRowStart.size(); ++i) {
        if (convectionRowStart[i] != diffusionRowStart[i]) {
            return std::unexpected(internalFailure(
                CfdMomentumProblem::AssemblyIsInconsistent,
                std::format("the two sparse patterns first differ at rowStart[{}]: convection says "
                            "{} and diffusion says {}",
                            i, convectionRowStart[i], diffusionRowStart[i])));
        }
    }
    for (std::size_t i = 0; i < convectionInner.size(); ++i) {
        if (convectionInner[i] != diffusionInner[i]) {
            return std::unexpected(internalFailure(
                CfdMomentumProblem::AssemblyIsInconsistent,
                std::format("the two sparse patterns first differ at inner[{}]: convection says {} "
                            "and diffusion says {}",
                            i, convectionInner[i], diffusionInner[i])));
        }
    }

    // ---- THE MATRIX -----------------------------------------------------
    //
    // `(conv + diff)` SLOT-WISE, THEN THE TRANSIENT DIAGONAL. The order is part
    // of the contract: measured at 78.06% bit-exact against `conv + (diff +
    // trans)`, worst relative 4.034e-13, so a caller summing in another order
    // does not get this matrix back bit-for-bit (ADR-073 Q1).
    const std::span<const double> convectionValues = convectionMatrix.values();
    const std::span<const double> diffusionValues = diffusionMatrix.values();
    std::vector<double> values(convectionValues.size(), 0.0);
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = convectionValues[i] + diffusionValues[i];
    }

    // The transient coefficient per cell, kept so the source pass uses the
    // SAME double the diagonal received rather than recomputing the product.
    std::vector<double> transient(cells, 0.0);
    double smallestTransient = 0.0;
    double largestTransient = 0.0;
    const bool isTransient = input.timeStep.has_value();
    if (isTransient) {
        for (std::size_t c = 0; c < cells; ++c) {
            // TYPED, AND THAT IS WHAT MAKES THE HEADER'S `static_assert`S LOAD
            // BEARING RATHER THAN DECORATIVE. An earlier draft computed
            // `rho * volume / dt` in raw doubles, and an independent review
            // pointed out the consequence: `rho * volume * dt` would have
            // compiled, passed all seven assertions, passed every finiteness
            // gate, and been caught only by a runtime oracle -- so the asserts
            // proved the units framework's own algebra and nothing this
            // milestone wrote, which is the INVERSE of what ADR-073 Decision 3
            // claimed for them.
            //
            // Binding the product to a `MassFlowRate` makes the wrong formula a
            // COMPILE ERROR. The arithmetic is unchanged: `operator*` and
            // `operator/` are `fromSi(a.si() OP b.si())`, so the grouping and
            // every bit are identical to the raw-double form this replaces,
            // which is why the change costs nothing and the measured order in
            // ADR-073's Q1 still describes the code.
            const MassFlowRate coefficient =
                input.density * geometry.cellVolume(CellIndex::fromValue(c)) / *input.timeStep;
            transient[c] = coefficient.si();
        }
        if (cells > 0) {
            const auto [least, most] = std::ranges::minmax_element(transient);
            smallestTransient = *least;
            largestTransient = *most;
        }
        // Added to the diagonal AFTER the slot-wise sum, which is the declared
        // order. the diagonal slot is found by `std::lower_bound` within the row -- an
            // earlier draft of this comment named a `slotOfDiagonal` helper that does
            // not exist anywhere in the repository, which is the comment-names-a-
            // thing-that-was-never-written defect this project records against
            // earlier milestones. The
        // diagonal is always present because both operators reserve it for
        // every cell whether or not a face reaches it.
        for (std::size_t c = 0; c < cells; ++c) {
            const auto first = static_cast<std::size_t>(convectionRowStart[c]);
            const auto last = static_cast<std::size_t>(convectionRowStart[c + 1]);
            const auto target = static_cast<CfdSparseIndex>(c);
            const auto found =
                std::lower_bound(convectionInner.begin() + static_cast<std::ptrdiff_t>(first),
                                 convectionInner.begin() + static_cast<std::ptrdiff_t>(last), target);
            if (found == convectionInner.begin() + static_cast<std::ptrdiff_t>(last) ||
                *found != target) {
                // THE STEADY PATH HAS NO SUCH CHECK, AND THAT IS NOT A GAP.
                // An independent review noted the asymmetry; the reason is that
                // the steady path never LOOKS UP the diagonal -- it sums slots
                // by index -- so there is nothing for it to fail to find. The
                // guard exists because the transient term must WRITE to a
                // particular slot, and only a write needs to know the slot is
                // there. `CfdMomentumProblem::AssemblyIsInconsistent`'s
                // documentation states the reservation unconditionally, which
                // is true of both sub-operators; this check is about this
                // function's use of it.
                return std::unexpected(internalFailure(
                    CfdMomentumProblem::AssemblyIsInconsistent,
                    std::format("row {} holds no diagonal entry, so the transient term has "
                                "nowhere to go; both sub-operators reserve a diagonal for every "
                                "cell, so this cannot happen",
                                c)));
            }
            values[static_cast<std::size_t>(std::distance(convectionInner.begin(), found))] +=
                transient[c];
        }
    }

    // ---- THE SOURCE -----------------------------------------------------
    //
    // THE FIVE TERMS IN THE DECLARED ORDER, which `CfdMomentumSystem::source`
    // states and the suite reproduces. Measured at 57.45% bit-exact against
    // the reverse order (ADR-073 Q5), so the order is contract and not detail.
    std::vector<double> source(cells, 0.0);
    const std::vector<double>& convectionSource = convection->source;
    const std::vector<double>& diffusionSource = diffusion->source;
    if (convectionSource.size() != cells || diffusionSource.size() != cells) {
        return std::unexpected(internalFailure(
            CfdMomentumProblem::AssemblyIsInconsistent,
            std::format("the sub-assemblies produced sources of {} and {} entries for {} cells",
                        convectionSource.size(), diffusionSource.size(), cells)));
    }

    const bool hasPressure = !input.pressureGradient.empty();
    const bool hasGravity = input.gravity.has_value();

    for (std::size_t c = 0; c < cells; ++c) {
        const Volume volume = geometry.cellVolume(CellIndex::fromValue(c));
        double total = convectionSource[c];
        total += diffusionSource[c];
        if (isTransient) {
            // EVERY LOCALLY FORMED TERM IS BOUND TO A `Force` BEFORE IT REACHES
            // THE SOURCE, for the reason the transient coefficient is bound to a
            // `MassFlowRate`: a term that lost a density, a volume or a time
            // then fails to COMPILE instead of producing a plausible number.
            // Each expression's grouping is the one it had as raw doubles, so
            // ADR-073's Q5 measurement still describes this accumulation.
            const Force carried = MassFlowRate::fromSi(transient[c]) *
                                  Velocity::fromSi(input.previousVelocity[componentIndex * cells + c]);
            total += carried.si();
        }
        if (hasPressure) {
            const Force pressureForce =
                -(volume * input.pressureGradient[c].component(input.component));
            total += pressureForce.si();
        }
        if (hasGravity) {
            const Force bodyForce =
                input.density * volume * input.gravity->acceleration.component(input.component);
            total += bodyForce.si();
        }
        source[c] = total;
    }

    // ---- THE FINITENESS GATE --------------------------------------------
    //
    // REACHABLE BY A ROUTE THE SUB-OPERATORS DO NOT HAVE: both gate their own
    // output, so two finite matrices can still SUM to an infinity, and
    // `rho * V / dt` can overflow from three finite factors.
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (!std::isfinite(values[i])) {
            return std::unexpected(failureOf(
                CfdMomentumProblem::ResultIsNotFinite,
                std::format("the assembled coefficient at slot {} is {}, which is not finite; both "
                            "sub-assemblies gated their own output, so this is an overflow in "
                            "their sum or in the transient term",
                            i, values[i])));
        }
    }
    for (std::size_t c = 0; c < cells; ++c) {
        if (!std::isfinite(source[c])) {
            return std::unexpected(
                failureOf(CfdMomentumProblem::ResultIsNotFinite,
                          std::format("the assembled source at cell {} is {}, which is not finite",
                                      c, source[c])));
        }
    }
    // THIS GATE IS UNREACHABLE FOR THIS OPERATOR, AND IT IS RECORDED AS
    // UNREACHED RATHER THAN JUSTIFIED BY A BORROWED ARGUMENT. An earlier draft
    // carried `CfdConvection.cpp`'s sentence -- "every coefficient may still be
    // finite, which is why this is checked separately" -- which is true THERE
    // and false here, and an independent review traced the difference.
    // Convection's reported totals are sums over faces that never enter a
    // coefficient, so they can be non-finite while every coefficient is fine.
    // These two are `minmax_element` over `transient`, and EVERY element of
    // `transient` is added to a diagonal slot a few lines above, so a
    // non-finite extremum implies a non-finite coefficient and the coefficient
    // loop -- which runs first -- always fires instead.
    //
    // KEPT ANYWAY, FOR THE REASON `CfdGradient.hpp` KEEPS ITS OWN UNREACHABLE
    // REFUSALS: the implication holds only while every transient coefficient
    // reaches a diagonal, which is a property of the loop above and not of this
    // one. If that ever stops being true the gate is the thing that notices.
    // What is NOT claimed is that a test reaches it.
    for (const auto& [name, value] : std::initializer_list<std::pair<const char*, double>>{
             {"smallest transient coefficient", smallestTransient},
             {"largest transient coefficient", largestTransient}}) {
        if (!std::isfinite(value)) {
            return std::unexpected(failureOf(
                CfdMomentumProblem::ResultIsNotFinite,
                std::format("the reported {} is {}, which is not finite", name, value)));
        }
    }

    // ---- THE MATRIX VALUE -----------------------------------------------
    //
    // NO `requiredSymmetryTolerance`. Diffusion's part is symmetric to the last
    // bit and convection's is asymmetric by construction, so their sum is
    // asymmetric wherever `phi` is not zero. Demanding symmetry of a momentum
    // operator with convection in it would be wrong rather than merely slack.
    std::vector<CfdSparseIndex> outRowStart(convectionRowStart.begin(), convectionRowStart.end());
    std::vector<CfdSparseIndex> outInner(convectionInner.begin(), convectionInner.end());
    std::expected<CfdSparseMatrix, CfdSparseFailure> matrix = CfdSparseMatrix::fromCsr(
        cells, cells, std::move(outRowStart), std::move(outInner), std::move(values));
    if (!matrix.has_value()) {
        return std::unexpected(internalFailure(
            CfdMomentumProblem::AssemblyIsInconsistent,
            std::format("the assembled arrays were refused by CfdSparseMatrix::fromCsr as {}",
                        toString(matrix.error().problem))));
    }

    // ---- THE MEASURED DOMINANCE -----------------------------------------
    //
    // A MEASUREMENT, NOT AN ACHIEVEMENT. Nothing above makes a row dominant:
    // there is no diagonal floor, no clipping and no artificial viscosity. The
    // count is taken from the matrix this function returns, so it cannot
    // disagree with it.
    std::size_t notDominant = 0;
    for (std::size_t r = 0; r < cells; ++r) {
        const auto first = static_cast<std::size_t>(matrix->rowStart()[r]);
        const auto last = static_cast<std::size_t>(matrix->rowStart()[r + 1]);
        double diagonal = 0.0;
        double offDiagonal = 0.0;
        for (std::size_t s = first; s < last; ++s) {
            const double entry = matrix->values()[s];
            if (matrix->inner()[s] == static_cast<CfdSparseIndex>(r)) {
                diagonal = std::abs(entry);
            } else {
                offDiagonal += std::abs(entry);
            }
        }
        if (diagonal < offDiagonal) {
            ++notDominant;
        }
    }

    CfdMomentumSummary summary;
    summary.cells = cells;
    summary.component = input.component;
    summary.transientTermApplied = isTransient;
    summary.pressureGradientApplied = hasPressure;
    summary.gravityApplied = hasGravity;
    summary.smallestTransientCoefficient = MassFlowRate::fromSi(smallestTransient);
    summary.largestTransientCoefficient = MassFlowRate::fromSi(largestTransient);
    summary.rowsNotDiagonallyDominant = notDominant;
    summary.convection = convection->summary;
    summary.diffusion = diffusion->summary;

    return CfdMomentumSystem{std::move(*matrix), std::move(source), summary};
}

// ---------------------------------------------------------------------------
// The predictors
// ---------------------------------------------------------------------------

std::optional<CfdMomentumFailure> cfdMomentumFailure(const CfdTopology& topology,
                                                     const CfdGeometry& geometry,
                                                     const PreparedCfdBoundary& boundary,
                                                     const CfdMomentumInput& input) {
    std::string message;
    if (const std::optional<CfdMomentumProblem> problem =
            problemOf(topology, geometry, boundary, input, &message)) {
        return failureOf(*problem, std::move(message));
    }
    return std::nullopt;
}

std::optional<CfdMomentumProblem> cfdMomentumProblem(const CfdTopology& topology,
                                                     const CfdGeometry& geometry,
                                                     const PreparedCfdBoundary& boundary,
                                                     const CfdMomentumInput& input) noexcept {
    return problemOf(topology, geometry, boundary, input, nullptr);
}

} // namespace bettercad::cfd
