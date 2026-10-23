// The read-only visualisation adapter for a thermal result (P18-VIZ-001).
//
// NO ENGINEERING VALUE IS COMPUTED IN THIS FILE. Every value `field()` returns is a read of a stored member of
// `ThermalPostState`, with the one documented exception of the display norm of a stored gradient vector
// (`gradientDisplayNorm`). A grep here for a gradient recovery, Fourier's law, a heat balance or a nodal average
// of an element value finds nothing.
//
// NO UNIT CONVERSION EITHER. Positions stay in SI metres as `MeshView::positions()` gives them; the single
// metres-to-millimetres conversion is `toModelLength` in the OCCT presentation adapter. A grep of the CODE here,
// comments stripped, for the literal conversion factor finds nothing.

#include <bettercad/renderer/ThermalResultView.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <optional>

namespace bettercad::renderer {
namespace {

constexpr std::array<ThermalChannel, 3> kChannels{ThermalChannel::Temperature,
                                                  ThermalChannel::TemperatureGradientMagnitude,
                                                  ThermalChannel::HeatFluxMagnitude};

/// A written-out switch, so a problem added to the enum and forgotten here is visible at the omission.
[[nodiscard]] constexpr ErrorCode codeFor(ThermalFieldProblem problem) noexcept {
    switch (problem) {
    case ThermalFieldProblem::NonFiniteValue:
    case ThermalFieldProblem::StateOutOfRange:
        return ErrorCode::InvalidArgument;
    // THIS LIBRARY'S OWN BUG, not the caller's: the descriptor table named a location whose reader does not
    // serve the channel. No argument the caller could pass would fix it.
    case ThermalFieldProblem::ChannelUnavailable:
        return ErrorCode::Internal;
    case ThermalFieldProblem::ResultSourceMismatch:
    case ThermalFieldProblem::EmptyClassificationDomain:
    case ThermalFieldProblem::CardinalityMismatch:
        break;
    }
    return ErrorCode::FailedPrecondition;
}

[[nodiscard]] std::unexpected<Error> fail(ThermalFieldProblem problem, std::string message) {
    return makeError(codeFor(problem), std::format("{}: {}", toString(problem), message));
}

} // namespace

std::string_view toString(ThermalChannel channel) noexcept {
    switch (channel) {
    case ThermalChannel::Temperature:
        return "temperature";
    case ThermalChannel::TemperatureGradientMagnitude:
        return "temperature_gradient_magnitude";
    case ThermalChannel::HeatFluxMagnitude:
        return "heat_flux_magnitude";
    }
    return "unknown";
}

std::string_view toString(ThermalFieldProblem problem) noexcept {
    switch (problem) {
    case ThermalFieldProblem::ResultSourceMismatch:
        return "result_source_mismatch";
    case ThermalFieldProblem::EmptyClassificationDomain:
        return "empty_classification_domain";
    case ThermalFieldProblem::StateOutOfRange:
        return "state_out_of_range";
    case ThermalFieldProblem::CardinalityMismatch:
        return "cardinality_mismatch";
    case ThermalFieldProblem::NonFiniteValue:
        return "non_finite_value";
    case ThermalFieldProblem::ChannelUnavailable:
        return "channel_unavailable";
    }
    return "unknown";
}

std::string_view toString(ThermalExtremeKind kind) noexcept {
    switch (kind) {
    case ThermalExtremeKind::Maximum:
        return "maximum";
    case ThermalExtremeKind::Minimum:
        return "minimum";
    }
    return "unknown";
}

ThermalChannelDescriptor descriptorOf(ThermalChannel channel) noexcept {
    ThermalChannelDescriptor out;
    out.channel = channel;
    switch (channel) {
    case ThermalChannel::Temperature:
        out.source = thermal::ThermalPostField::Temperature;
        out.name = "Temperature";
        break;
    case ThermalChannel::TemperatureGradientMagnitude:
        out.source = thermal::ThermalPostField::TemperatureGradient;
        out.name = "Temperature Gradient Magnitude";
        break;
    case ThermalChannel::HeatFluxMagnitude:
        out.source = thermal::ThermalPostField::HeatFluxMagnitude;
        out.name = "Heat Flux Magnitude";
        break;
    }
    // THE LOCATION AND THE UNIT ARE THE CORE'S, read from POST's own descriptor. They are not restated here.
    const thermal::ThermalFieldDescriptor core = thermal::descriptorOf(out.source);
    out.location = core.location;
    out.unit = core.unit;
    out.mayBeNegative = false;
    return out;
}

std::span<const ThermalChannel> allThermalChannels() noexcept { return kChannels; }

double gradientDisplayNorm(const thermal::TemperatureGradient3& gradient) noexcept {
    // A COMPONENT THAT IS NOT A NUMBER MAKES THE NORM NOT A NUMBER. This is not a nicety: libstdc++'s three-argument
    // hypot takes the largest component with std::max, which returns its FIRST argument when the comparison
    // involves a NaN, so hypot(0, NaN, 0) is 0 -- a corrupt gradient would read as a perfectly quiet one, and the
    // finiteness check downstream would never see it. Found by RejectsANonFiniteValueInsteadOfMappingItToAColour.
    if (!std::isfinite(gradient.x.si()) || !std::isfinite(gradient.y.si()) || !std::isfinite(gradient.z.si())) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return std::hypot(gradient.x.si(), gradient.y.si(), gradient.z.si());
}

std::optional<double> channelValue(ThermalChannel channel, Temperature temperature) noexcept {
    switch (channel) {
    case ThermalChannel::Temperature:
        // THE STORED KELVIN VALUE, bit for bit. No offset, no conversion.
        return temperature.si();
    // EVERY ELEMENT CHANNEL IS ABSENT ON A NODE, written out rather than defaulted so that a channel added to the
    // enum fails to compile here instead of silently joining this arm.
    case ThermalChannel::TemperatureGradientMagnitude:
    case ThermalChannel::HeatFluxMagnitude:
        break;
    }
    return std::nullopt;
}

std::optional<double> channelValue(ThermalChannel channel, const thermal::TemperatureGradient3& gradient,
                                   HeatFlux magnitude) noexcept {
    switch (channel) {
    case ThermalChannel::TemperatureGradientMagnitude:
        return gradientDisplayNorm(gradient);
    case ThermalChannel::HeatFluxMagnitude:
        // POST'S STORED |q|. Not a norm of the flux vector taken here: POST owns that value.
        return magnitude.si();
    case ThermalChannel::Temperature:
        break;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// The view
// ---------------------------------------------------------------------------

Result<ThermalResultView> ThermalResultView::of(const meshing::Mesh& mesh, const MeshView& view,
                                                const thermal::ThermalDofMap& map,
                                                const thermal::ThermalPostResult& post) {
    if (!view.describes(mesh)) {
        return fail(ThermalFieldProblem::ResultSourceMismatch,
                    "the mesh view was built from another mesh generation than the mesh presented");
    }
    if (!map.describes(mesh)) {
        return fail(ThermalFieldProblem::ResultSourceMismatch,
                    "the numbering does not describe the mesh presented (another generation, or another count)");
    }
    if (!post.describes(map)) {
        return fail(ThermalFieldProblem::ResultSourceMismatch,
                    "the result was recovered on another numbering than the one presented");
    }
    if (post.stateCount() == 0U) {
        return fail(ThermalFieldProblem::EmptyClassificationDomain, "the result holds no state");
    }
    if (map.nodeCount() == 0U) {
        return fail(ThermalFieldProblem::EmptyClassificationDomain, "the numbering has no node");
    }
    const std::span<const meshing::Tetrahedron> tets = mesh.tetrahedra();
    if (tets.empty()) {
        return fail(ThermalFieldProblem::EmptyClassificationDomain, "the mesh has no tetrahedron");
    }
    // THE ELEMENTS, ID FOR ID AND IN ORDER. A result with as many elements as the mesh but not these elements
    // would map every value onto a different tetrahedron, and the count says nothing about it.
    const std::span<const meshing::ElementId> ids = post.elementIds();
    bool same = ids.size() == tets.size();
    for (std::size_t i = 0U; same && i < tets.size(); ++i) {
        same = ids[i] == tets[i].id;
    }
    if (!same) {
        return fail(ThermalFieldProblem::ResultSourceMismatch,
                    std::format("the result holds {} element ids and they are not the {} tetrahedra of the mesh, "
                                "id for id and in order",
                                ids.size(), tets.size()));
    }

    ThermalResultView out;
    out.mesh_ = mesh.stamp();
    out.source_ = post.source();
    out.nodeCount_ = map.nodeCount();
    out.elementCount_ = tets.size();
    out.stateCount_ = post.stateCount();
    return out;
}

bool ThermalResultView::describes(const meshing::Mesh& mesh, const thermal::ThermalDofMap& map,
                                  const thermal::ThermalPostResult& post) const noexcept {
    return mesh.owns(mesh_) && map.describes(mesh) && post.describes(map) && post.source() == source_ &&
           map.nodeCount() == nodeCount_ && mesh.tetrahedra().size() == elementCount_ &&
           post.stateCount() == stateCount_;
}

std::size_t ThermalResultView::domainSize(thermal::FieldLocation location) const noexcept {
    return location == thermal::FieldLocation::Node ? nodeCount_ : elementCount_;
}

Result<ThermalScalarField> classifyThermalState(ThermalChannel channel, const thermal::ThermalPostState& stored,
                                                std::span<const meshing::NodeId> nodes,
                                                std::span<const meshing::ElementId> elements, std::size_t state) {
    const ThermalChannelDescriptor descriptor = descriptorOf(channel);

    ThermalScalarField out;
    out.channel = channel;
    out.location = descriptor.location;
    out.state = state;
    if (stored.time.has_value()) {
        out.time = stored.time->si();
    }
    out.expected = descriptor.location == thermal::FieldLocation::Node ? nodes.size() : elements.size();
    if (out.expected == 0U) {
        return fail(ThermalFieldProblem::EmptyClassificationDomain,
                    std::format("channel {} has no {} to classify", toString(channel), toString(descriptor.location)));
    }

    if (descriptor.location == thermal::FieldLocation::Node) {
        // THE ARRAY IS PARALLEL TO ThermalDofIndex AND TO the numbering's node list, and a short or long one is
        // refused -- not padded with zeros (a field of silence reads as a valid uniform result) and not truncated.
        if (stored.temperatures.size() != out.expected) {
            return fail(ThermalFieldProblem::CardinalityMismatch,
                        std::format("state {} holds {} temperatures for {} nodes", state,
                                    stored.temperatures.size(), out.expected));
        }
        out.nodes.assign(nodes.begin(), nodes.end());
        out.values.reserve(out.expected);
        for (const Temperature t : stored.temperatures) {
            const std::optional<double> value = channelValue(channel, t);
            if (!value.has_value()) {
                ++out.skipped;
                continue;
            }
            if (!std::isfinite(*value)) {
                ++out.nonFinite;
                continue;
            }
            out.values.push_back(*value);
        }
    } else {
        const std::size_t n = out.expected;
        if (stored.fields.gradients.size() != n || stored.fields.heatFluxes.size() != n ||
            stored.fields.heatFluxMagnitudes.size() != n) {
            return fail(ThermalFieldProblem::CardinalityMismatch,
                        std::format("state {} holds {} gradients, {} fluxes and {} flux magnitudes for {} "
                                    "tetrahedra",
                                    state, stored.fields.gradients.size(), stored.fields.heatFluxes.size(),
                                    stored.fields.heatFluxMagnitudes.size(), n));
        }
        out.elements.assign(elements.begin(), elements.end());
        out.values.reserve(n);
        for (std::size_t i = 0U; i < n; ++i) {
            const std::optional<double> value =
                channelValue(channel, stored.fields.gradients[i], stored.fields.heatFluxMagnitudes[i]);
            if (!value.has_value()) {
                ++out.skipped;
                continue;
            }
            if (!std::isfinite(*value)) {
                ++out.nonFinite;
                continue;
            }
            out.values.push_back(*value);
        }
    }

    if (out.skipped > 0U) {
        return fail(ThermalFieldProblem::ChannelUnavailable,
                    std::format("channel {} has no value on {} of {} {} entities", toString(channel), out.skipped,
                                out.expected, toString(descriptor.location)));
    }
    if (out.nonFinite > 0U) {
        return fail(ThermalFieldProblem::NonFiniteValue,
                    std::format("channel {} of state {} holds {} value(s) that are not finite", toString(channel),
                                state, out.nonFinite));
    }
    out.classified = out.values.size();
    // The expected count is positive (the view refused an empty domain), so a complete field here has entities.

    // SEEDED FROM THE FIRST VALUE, never from zero. Ties keep the first entity: the comparison is strict.
    out.minimum = out.values.front();
    out.maximum = out.values.front();
    std::size_t lowest = 0U;
    std::size_t highest = 0U;
    for (std::size_t i = 1U; i < out.values.size(); ++i) {
        if (out.values[i] < out.minimum) {
            out.minimum = out.values[i];
            lowest = i;
        }
        if (out.values[i] > out.maximum) {
            out.maximum = out.values[i];
            highest = i;
        }
    }
    if (descriptor.location == thermal::FieldLocation::Node) {
        out.minimumNode = out.nodes[lowest];
        out.maximumNode = out.nodes[highest];
    } else {
        out.minimumElement = out.elements[lowest];
        out.maximumElement = out.elements[highest];
    }
    return out;
}

Result<ThermalScalarField> ThermalResultView::field(const meshing::Mesh& mesh,
                                                    const thermal::ThermalDofMap& map,
                                                    const thermal::ThermalPostResult& post, std::size_t state,
                                                    ThermalChannel channel) const {
    if (!describes(mesh, map, post)) {
        return fail(ThermalFieldProblem::ResultSourceMismatch,
                    std::format("this view was built for another result or mesh than the one presented for "
                                "channel {}",
                                toString(channel)));
    }
    if (state >= post.stateCount()) {
        return fail(ThermalFieldProblem::StateOutOfRange,
                    std::format("state {} requested of a result holding {}", state, post.stateCount()));
    }
    // THE SAME KERNEL a hand-built state is judged by, after the source checks above.
    return classifyThermalState(channel, post.states()[state], map.nodes(), post.elementIds(), state);
}

Result<ThermalExtreme> thermalExtremeOf(const ThermalScalarField& field, ThermalExtremeKind kind) {
    // THE P16 TRAP, AGAIN: an extreme over a partial classification, or over zero entities, is not an extreme.
    if (!field.complete() || field.values.empty() || field.values.size() != field.classified) {
        return fail(ThermalFieldProblem::EmptyClassificationDomain,
                    std::format("channel {} classified {} of {} entities, so it has no extreme",
                                toString(field.channel), field.classified, field.expected));
    }
    std::size_t at = 0U;
    for (std::size_t i = 1U; i < field.values.size(); ++i) {
        const bool better = kind == ThermalExtremeKind::Maximum ? field.values[i] > field.values[at]
                                                                : field.values[i] < field.values[at];
        if (better) {
            at = i;
        }
    }
    ThermalExtreme out;
    out.kind = kind;
    out.location = field.location;
    out.value = field.values[at];
    out.index = at;
    if (field.location == thermal::FieldLocation::Node) {
        if (at >= field.nodes.size()) {
            return fail(ThermalFieldProblem::CardinalityMismatch, "the field has no node for its extreme");
        }
        out.node = field.nodes[at];
    } else {
        if (at >= field.elements.size()) {
            return fail(ThermalFieldProblem::CardinalityMismatch, "the field has no element for its extreme");
        }
        out.element = field.elements[at];
    }
    return out;
}

} // namespace bettercad::renderer
