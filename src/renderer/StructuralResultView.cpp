// The read-only visualisation adapter for a structural result (P17-VIZ-001).
//
// NO SCALAR IS COMPUTED IN THIS FILE. Every value returned by `field()` is a
// read of a stored member of `NodalDisplacement` or `ElementFields`. A grep
// here for a von Mises formula, an eigensolver, a deviatoric invariant, a
// `sqrt` of a stress, or a nodal average of an element value finds nothing,
// and that absence is the milestone's central claim.
//
// NO UNIT CONVERSION EITHER. Positions and displacements stay in SI metres,
// exactly as `MeshView::positions()` does. The single metres-to-millimetres
// conversion is `toModelLength` in `occt/OcctMeshPresentation.cpp`, which is
// `geometry::occt::toModel`. A grep of the CODE here for `1000`, comments
// stripped, finds nothing.
//
// THE DEFORMED BUFFER IS ALWAYS BUILT FROM THE UNDEFORMED POSITIONS. There is
// no in-place update and no accumulator, which is what makes repeated toggles
// and repeated scale changes idempotent by construction rather than by a
// caller remembering to reset.

#include <bettercad/renderer/StructuralResultView.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>

namespace bettercad::renderer {
namespace {

using structural::ElementFields;
using structural::NodalDisplacement;

/// Every channel, in enumeration order. One list, shared by the menu, the
/// test sweep and the evidence table.
constexpr std::array<ResultChannel, 15> kChannels{
    ResultChannel::DisplacementMagnitude, ResultChannel::DisplacementX,
    ResultChannel::DisplacementY,         ResultChannel::DisplacementZ,
    ResultChannel::VonMises,              ResultChannel::StressXX,
    ResultChannel::StressYY,              ResultChannel::StressZZ,
    ResultChannel::StressXY,              ResultChannel::StressYZ,
    ResultChannel::StressZX,              ResultChannel::PrincipalStress1,
    ResultChannel::PrincipalStress2,      ResultChannel::PrincipalStress3,
    ResultChannel::HydrostaticStress,
};

/// A written-out switch rather than a chain of ternaries, so a `FieldProblem`
/// added to the enum and forgotten here is visible at the point of the
/// omission.
[[nodiscard]] constexpr ErrorCode codeFor(FieldProblem problem) noexcept {
    switch (problem) {
    case FieldProblem::NonFiniteValue:
        return ErrorCode::InvalidArgument;
    // THIS LIBRARY'S OWN BUG, not the caller's: the descriptor table named a
    // domain whose reader does not serve the channel. Internal rather than
    // InvalidArgument, because no argument the caller could pass would fix it.
    case FieldProblem::ChannelUnavailable:
        return ErrorCode::Internal;
    case FieldProblem::ResultSourceMismatch:
    case FieldProblem::EmptyClassificationDomain:
    case FieldProblem::IncompleteClassification:
        break;
    }
    return ErrorCode::FailedPrecondition;
}

[[nodiscard]] std::unexpected<Error> fail(FieldProblem problem, std::string message) {
    return makeError(codeFor(problem), std::format("{}: {}", toString(problem), message));
}

} // namespace

std::optional<double> channelValue(ResultChannel channel,
                                   const structural::NodalDisplacement& nodal) noexcept {
    switch (channel) {
    case ResultChannel::DisplacementMagnitude:
        // STORED, NOT RE-NORMED. P17-POST-001 stores `magnitude` precisely "so
        // that a consumer cannot compute it a second, different way", and
        // taking the norm again here would be that second way.
        return nodal.magnitude.si();
    case ResultChannel::DisplacementX:
        return nodal.displacement.x.si();
    case ResultChannel::DisplacementY:
        return nodal.displacement.y.si();
    case ResultChannel::DisplacementZ:
        return nodal.displacement.z.si();
    // EVERY ELEMENT CHANNEL IS ABSENT ON A NODE, written out rather than
    // defaulted so that a channel added to the enum fails to compile here
    // instead of silently joining this arm.
    case ResultChannel::VonMises:
    case ResultChannel::StressXX:
    case ResultChannel::StressYY:
    case ResultChannel::StressZZ:
    case ResultChannel::StressXY:
    case ResultChannel::StressYZ:
    case ResultChannel::StressZX:
    case ResultChannel::PrincipalStress1:
    case ResultChannel::PrincipalStress2:
    case ResultChannel::PrincipalStress3:
    case ResultChannel::HydrostaticStress:
        break;
    }
    return std::nullopt;
}

std::optional<double> channelValue(ResultChannel channel,
                                   const structural::ElementFields& element) noexcept {
    switch (channel) {
    case ResultChannel::VonMises:
        return element.vonMises.si();
    case ResultChannel::StressXX:
        return element.stress.xx.si();
    case ResultChannel::StressYY:
        return element.stress.yy.si();
    case ResultChannel::StressZZ:
        return element.stress.zz.si();
    case ResultChannel::StressXY:
        return element.stress.xy.si();
    // `yz` AND `zx` ARE WRITTEN OUT SEPARATELY AND IN THIS ORDER, following
    // `Stress6`'s own field names, which are P17-POST's `TensorComponent`
    // order. Transposing them is a defect that passes every test using a field
    // with equal shears, so the test that holds this uses unequal ones.
    case ResultChannel::StressYZ:
        return element.stress.yz.si();
    case ResultChannel::StressZX:
        return element.stress.zx.si();
    case ResultChannel::PrincipalStress1:
        return element.principalStress.sigma1.si();
    case ResultChannel::PrincipalStress2:
        return element.principalStress.sigma2.si();
    case ResultChannel::PrincipalStress3:
        return element.principalStress.sigma3.si();
    case ResultChannel::HydrostaticStress:
        return element.hydrostatic.si();
    // AND EVERY NODAL CHANNEL IS ABSENT ON AN ELEMENT. P17-VIZ does not
    // average an element value to the nodes and does not extrapolate a nodal
    // value to an element: either would be a second, quieter result.
    case ResultChannel::DisplacementMagnitude:
    case ResultChannel::DisplacementX:
    case ResultChannel::DisplacementY:
    case ResultChannel::DisplacementZ:
        break;
    }
    return std::nullopt;
}


std::string_view toString(ResultChannel channel) noexcept {
    switch (channel) {
    case ResultChannel::DisplacementMagnitude:
        return "displacement_magnitude";
    case ResultChannel::DisplacementX:
        return "displacement_x";
    case ResultChannel::DisplacementY:
        return "displacement_y";
    case ResultChannel::DisplacementZ:
        return "displacement_z";
    case ResultChannel::VonMises:
        return "von_mises";
    case ResultChannel::StressXX:
        return "stress_xx";
    case ResultChannel::StressYY:
        return "stress_yy";
    case ResultChannel::StressZZ:
        return "stress_zz";
    case ResultChannel::StressXY:
        return "stress_xy";
    case ResultChannel::StressYZ:
        return "stress_yz";
    case ResultChannel::StressZX:
        return "stress_zx";
    case ResultChannel::PrincipalStress1:
        return "principal_stress_1";
    case ResultChannel::PrincipalStress2:
        return "principal_stress_2";
    case ResultChannel::PrincipalStress3:
        return "principal_stress_3";
    case ResultChannel::HydrostaticStress:
        return "hydrostatic_stress";
    }
    return "unknown";
}

std::string_view toString(ExtremeKind kind) noexcept {
    switch (kind) {
    case ExtremeKind::Maximum:
        return "maximum";
    case ExtremeKind::Minimum:
        return "minimum";
    case ExtremeKind::MaximumMagnitude:
        return "maximum_magnitude";
    }
    return "unknown";
}

std::string_view toString(ChannelDomain domain) noexcept {
    switch (domain) {
    case ChannelDomain::Node:
        return "node";
    case ChannelDomain::Element:
        return "element";
    }
    return "unknown";
}

std::string_view toString(FieldProblem problem) noexcept {
    switch (problem) {
    case FieldProblem::ResultSourceMismatch:
        return "result_source_mismatch";
    case FieldProblem::EmptyClassificationDomain:
        return "empty_classification_domain";
    case FieldProblem::IncompleteClassification:
        return "incomplete_classification";
    case FieldProblem::NonFiniteValue:
        return "non_finite_value";
    case FieldProblem::ChannelUnavailable:
        return "channel_unavailable";
    }
    return "unknown";
}

ChannelDescriptor descriptorOf(ResultChannel channel) noexcept {
    // THE ONE TABLE. A channel's domain, unit and sign expectation are stated
    // here and nowhere else, so a reader checking "is tau_yz an element
    // channel in pascals" has one place to look.
    //
    // `mayBeNegative` is false for exactly two channels: the displacement
    // MAGNITUDE and von Mises, both of which are norms. Every stress
    // component, every principal stress, the hydrostatic stress and every
    // signed displacement component may be negative, and a range that clamped
    // them at zero would hide compression entirely.
    switch (channel) {
    case ResultChannel::DisplacementMagnitude:
        return {channel, ChannelDomain::Node, "Displacement magnitude", "m", false};
    case ResultChannel::DisplacementX:
        return {channel, ChannelDomain::Node, "Displacement X", "m", true};
    case ResultChannel::DisplacementY:
        return {channel, ChannelDomain::Node, "Displacement Y", "m", true};
    case ResultChannel::DisplacementZ:
        return {channel, ChannelDomain::Node, "Displacement Z", "m", true};
    case ResultChannel::VonMises:
        return {channel, ChannelDomain::Element, "von Mises stress", "Pa", false};
    case ResultChannel::StressXX:
        return {channel, ChannelDomain::Element, "Stress XX", "Pa", true};
    case ResultChannel::StressYY:
        return {channel, ChannelDomain::Element, "Stress YY", "Pa", true};
    case ResultChannel::StressZZ:
        return {channel, ChannelDomain::Element, "Stress ZZ", "Pa", true};
    case ResultChannel::StressXY:
        return {channel, ChannelDomain::Element, "Shear XY", "Pa", true};
    case ResultChannel::StressYZ:
        return {channel, ChannelDomain::Element, "Shear YZ", "Pa", true};
    case ResultChannel::StressZX:
        return {channel, ChannelDomain::Element, "Shear ZX", "Pa", true};
    case ResultChannel::PrincipalStress1:
        return {channel, ChannelDomain::Element, "Principal stress 1", "Pa", true};
    case ResultChannel::PrincipalStress2:
        return {channel, ChannelDomain::Element, "Principal stress 2", "Pa", true};
    case ResultChannel::PrincipalStress3:
        return {channel, ChannelDomain::Element, "Principal stress 3", "Pa", true};
    case ResultChannel::HydrostaticStress:
        return {channel, ChannelDomain::Element, "Hydrostatic stress", "Pa", true};
    }
    return {channel, ChannelDomain::Node, "unknown", "", true};
}

std::span<const ResultChannel> allResultChannels() noexcept {
    return kChannels;
}

bool isUsableDeformationScale(double scale) noexcept {
    return std::isfinite(scale) && scale >= 0.0;
}

Result<StructuralResultView> StructuralResultView::of(const meshing::Mesh& mesh,
                                                      const MeshView& view,
                                                      const structural::RecoveredFields& fields) {
    // BOTH GUARDS, AND THE RESULT'S FIRST. A view built from a mesh the result
    // does not describe would translate render indices into the wrong physics
    // while looking entirely successful.
    if (!fields.describes(mesh)) {
        return fail(FieldProblem::ResultSourceMismatch,
                    "these recovered fields were computed on a different mesh, so none of its "
                    "node or element handles names the same material");
    }
    if (!view.describes(mesh)) {
        return fail(FieldProblem::ResultSourceMismatch,
                    "this mesh view was built from a different mesh generation");
    }
    if (fields.displacements().empty() || fields.elements().empty()) {
        return fail(FieldProblem::EmptyClassificationDomain,
                    std::format("the result has {} nodal displacements and {} element fields, so "
                                "there is nothing to classify",
                                fields.displacements().size(), fields.elements().size()));
    }

    StructuralResultView out;
    out.mesh_ = fields.mesh();
    out.source_ = fields.source();
    out.nodeCount_ = fields.displacements().size();
    out.elementCount_ = fields.elements().size();
    return out;
}

bool StructuralResultView::describes(const meshing::Mesh& mesh,
                                     const structural::RecoveredFields& fields) const noexcept {
    return mesh.owns(mesh_) && fields.describes(mesh) && fields.mesh() == mesh_ &&
           fields.source() == source_;
}

std::size_t StructuralResultView::domainSize(ChannelDomain domain) const noexcept {
    switch (domain) {
    case ChannelDomain::Node:
        return nodeCount_;
    case ChannelDomain::Element:
        return elementCount_;
    }
    return 0;
}

Result<ScalarField> StructuralResultView::field(const meshing::Mesh& mesh,
                                                const structural::RecoveredFields& fields,
                                                ResultChannel channel) const {
    if (!describes(mesh, fields)) {
        return fail(FieldProblem::ResultSourceMismatch,
                    "this result view does not describe the mesh and fields it was given");
    }

    const ChannelDescriptor descriptor = descriptorOf(channel);
    ScalarField out;
    out.channel = channel;
    out.domain = descriptor.domain;
    out.expected = domainSize(descriptor.domain);

    // AN UNREACHABLE GUARD, KEPT. `of()` refuses a result whose nodal OR
    // element domain is empty, so holding a `StructuralResultView` already
    // proves both counts are positive and `expected` cannot be zero here.
    // Probe V11 records that by surviving.
    //
    // WHERE THE P16 TRAP IS ACTUALLY CLOSED, AND CHECKED: `complete()`
    // requires `expected > 0` and `extremeOf` gates on `complete()`, and both
    // are reachable -- `ScalarField` is a public aggregate, so a test can hand
    // them an empty domain directly. Probes V14 and V18 are killed there.
    //
    // This one is defence in depth against a future caller that builds a view
    // some other way, and it is NOT the evidence for the trap.
    if (out.expected == 0) {
        return fail(FieldProblem::EmptyClassificationDomain,
                    std::format("channel {} is defined on {}s and this result has none",
                                toString(channel), toString(descriptor.domain)));
    }

    out.values.reserve(out.expected);
    if (descriptor.domain == ChannelDomain::Node) {
        out.nodes.reserve(out.expected);
    } else {
        out.elements.reserve(out.expected);
    }

    // SEEDED FROM THE FIRST VALUE, NOT FROM ZERO. An extremum accumulator
    // seeded at 0.0 reports 0.0 as the maximum of an all-negative field, which
    // this project has been caught by: a peak principal stress under inward
    // pressure came back as its own initialiser.
    bool seeded = false;

    const auto consider = [&out, &seeded](std::optional<double> maybe, std::size_t index,
                                          bool nodal, meshing::NodeId node,
                                          meshing::ElementId element) {
        // ABSENT IS A SKIP, NOT A ZERO. Counted rather than substituted, so
        // that the field can be refused below instead of drawing silence as a
        // uniform result.
        if (!maybe.has_value()) {
            ++out.skipped;
            return;
        }
        const double value = *maybe;
        if (!std::isfinite(value)) {
            ++out.nonFinite;
            return;
        }
        out.values.push_back(value);
        if (nodal) {
            out.nodes.push_back(node);
        } else {
            out.elements.push_back(element);
        }
        ++out.classified;
        if (!seeded) {
            out.minimum = value;
            out.maximum = value;
            if (nodal) {
                out.minimumNode = node;
                out.maximumNode = node;
            } else {
                out.minimumElement = element;
                out.maximumElement = element;
            }
            seeded = true;
            return;
        }
        // STRICT comparisons, so the FIRST entity in the domain's ascending
        // order wins a tie. That is the deterministic tie-break the brief
        // requires, and it is a property of the traversal rather than of a
        // sort nobody can see.
        if (value < out.minimum) {
            out.minimum = value;
            if (nodal) {
                out.minimumNode = node;
            } else {
                out.minimumElement = element;
            }
        }
        if (value > out.maximum) {
            out.maximum = value;
            if (nodal) {
                out.maximumNode = node;
            } else {
                out.maximumElement = element;
            }
        }
        (void)index;
    };

    if (descriptor.domain == ChannelDomain::Node) {
        std::size_t index = 0;
        for (const NodalDisplacement& nodal : fields.displacements()) {
            consider(channelValue(channel, nodal), index++, true, nodal.node,
                     meshing::ElementId{});
        }
    } else {
        std::size_t index = 0;
        for (const ElementFields& element : fields.elements()) {
            consider(channelValue(channel, element), index++, false, meshing::NodeId{},
                     element.element);
        }
    }

    // A NON-FINITE VALUE WOULD BE REFUSED, NOT COLOURED -- and it cannot
    // arrive. A qualified current result cannot contain one, because
    // P17-VALID-001 and `StructuralResult::create` both refuse it, and
    // `RecoveredFields` cannot be synthesised past them: private constructor,
    // exactly one friend (ADR-036). So this is unreachable through the public
    // API and probe V13 records that by surviving.
    //
    // Kept because mapping NaN into a colour bucket would present it as an
    // ordinary number, and the possession invariant is a property of today's
    // callers rather than of this function's signature.
    if (out.nonFinite > 0) {
        return fail(FieldProblem::NonFiniteValue,
                    std::format("{} of {} {} values of channel {} are not finite", out.nonFinite,
                                out.expected, toString(descriptor.domain), toString(channel)));
    }
    // A SKIPPED ENTITY IS REFUSED. Unreachable while the one descriptor table
    // and the two readers agree -- `field()` chooses its reader from the
    // descriptor's own domain -- and kept because the disagreement it guards
    // against would render as a COMPLETE, UNIFORMLY ZERO field rather than as
    // an error, which is the P16 trap one layer down. `channelValue` is public
    // so that the absent arm is checked directly rather than argued about:
    // see StructuralViz_AChannelWithNoValueIsAbsentNotZero.
    if (out.skipped > 0) {
        return fail(FieldProblem::ChannelUnavailable,
                    std::format("channel {} has no value on {} of {} {}s", toString(channel),
                                out.skipped, out.expected, toString(descriptor.domain)));
    }
    // AND AN INCOMPLETE CLASSIFICATION WOULD BE REFUSED TOO -- unreachable by
    // ARITHMETIC rather than by an invariant, which is a stronger statement
    // than the two guards above can make. Every entity goes down exactly one
    // of three paths, so `classified + skipped + nonFinite == expected`
    // always; both of the other two counters have already returned non-zero
    // above; therefore `classified == expected` here, necessarily. Probe V12
    // records that by surviving.
    //
    // Kept as the accounting check it is: if it ever did differ, the three
    // counters would no longer describe the domain and `complete()` would be
    // answering about a field that does not exist. Rendering the values
    // that did classify, as though the field were whole, is the "silently skip
    // and render the rest as valid" defect.
    if (out.classified != out.expected) {
        return fail(FieldProblem::IncompleteClassification,
                    std::format("channel {} classified {} of {} {}s", toString(channel),
                                out.classified, out.expected, toString(descriptor.domain)));
    }

    return out;
}

Result<ExtremeEntity> extremeOf(const ScalarField& field, ExtremeKind kind) {
    // AN EXTREME OVER A PARTIAL CLASSIFICATION IS THE P16 TRAP AGAIN. A
    // maximum of the entities that happened to classify, presented as the
    // maximum, is exactly the shape of "processed zero entities, therefore all
    // valid" -- so the completeness gate comes first.
    if (!field.complete()) {
        return fail(FieldProblem::IncompleteClassification,
                    std::format("channel {} classified {} of {} {}s, so it has no extreme",
                                toString(field.channel), field.classified, field.expected,
                                toString(field.domain)));
    }

    const bool nodal = field.domain == ChannelDomain::Node;
    const std::size_t count = field.values.size();
    if ((nodal ? field.nodes.size() : field.elements.size()) != count) {
        return makeError(ErrorCode::Internal,
                         "the field\x27s identity array is not parallel to its values");
    }

    std::size_t best = 0;
    for (std::size_t i = 1; i < count; ++i) {
        // STRICTLY GREATER / LESS, so the FIRST entity in ascending order
        // wins a tie. Deterministic without a sort.
        const bool better = kind == ExtremeKind::Maximum ? field.values[i] > field.values[best]
                            : kind == ExtremeKind::Minimum
                                ? field.values[i] < field.values[best]
                                : std::abs(field.values[i]) > std::abs(field.values[best]);
        if (better) {
            best = i;
        }
    }

    ExtremeEntity out;
    out.kind = kind;
    out.domain = field.domain;
    out.index = best;
    // THE SIGNED VALUE, even for MaximumMagnitude: the magnitude was the
    // search key and not the answer.
    out.value = field.values[best];
    if (nodal) {
        out.node = field.nodes[best];
    } else {
        out.element = field.elements[best];
    }
    return out;
}

Result<std::vector<double>> StructuralResultView::deformedPositions(
    const meshing::Mesh& mesh, const MeshView& view, const structural::RecoveredFields& fields,
    double scale) const {
    if (!describes(mesh, fields)) {
        return fail(FieldProblem::ResultSourceMismatch,
                    "this result view does not describe the mesh and fields it was given");
    }
    if (!view.describes(mesh)) {
        return fail(FieldProblem::ResultSourceMismatch,
                    "this mesh view was built from a different mesh generation");
    }
    if (!isUsableDeformationScale(scale)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("a deformation display scale must be finite and "
                                     "non-negative, and {} is not",
                                     scale));
    }

    // FROM THE UNDEFORMED POSITIONS, EVERY TIME. `MeshView::positions()` is
    // the view's own immutable buffer, so there is no previous output to add
    // to and the cumulative-deformation bug is unrepresentable rather than
    // merely avoided.
    const std::span<const double> undeformed = view.positions();
    std::vector<double> out(undeformed.begin(), undeformed.end());

    for (std::size_t vertex = 0; vertex < view.vertexCount(); ++vertex) {
        const Result<meshing::NodeId> node = view.nodeOfVertex(vertex);
        if (!node.has_value()) {
            return makeError(node.error().code, node.error().message);
        }
        // THE IDENTITY-SAFE LOOKUP, which takes the mesh and refuses a foreign
        // one. A positional index into `displacements()` would be correct for
        // a view that draws every node and wrong for a boundary view, which
        // draws only some -- and wrong silently.
        const Result<NodalDisplacement> nodal = fields.displacementOf(mesh, *node);
        if (!nodal.has_value()) {
            return makeError(nodal.error().code, nodal.error().message);
        }
        // SI METRES THROUGHOUT. No conversion here: the adapter converts once.
        out[(vertex * 3U) + 0U] += scale * nodal->displacement.x.si();
        out[(vertex * 3U) + 1U] += scale * nodal->displacement.y.si();
        out[(vertex * 3U) + 2U] += scale * nodal->displacement.z.si();
    }

    return out;
}

} // namespace bettercad::renderer
