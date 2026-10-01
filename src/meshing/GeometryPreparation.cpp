#include <bettercad/meshing/GeometryPreparation.hpp>

#include <bettercad/core/document/Configurations.hpp>
#include <bettercad/features/Validation.hpp>

#include <format>
#include <optional>
#include <set>
#include <vector>

namespace bettercad::meshing {
namespace {

using features::NodeState;
using features::Regenerator;

/// Mixes one 64-bit value into a running stamp.
///
/// splitmix64's finalizer, written out rather than taken from std::hash: the
/// standard does not require std::hash to agree between builds, and this value is
/// compared across Debug, Release and Debug-shared. A fixed arithmetic mix is the
/// same number everywhere.
[[nodiscard]] constexpr std::uint64_t mix(std::uint64_t state, std::uint64_t value) noexcept {
    std::uint64_t x = state ^ (value + 0x9e3779b97f4a7c15ULL);
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

/// Every object @p feature is transitively built from, plus itself, in ascending
/// order.
///
/// Ascending rather than topological, because two different topological orders of
/// the same graph are both valid and would give two different stamps for one
/// model. Ascending ObjectId is total and unique.
[[nodiscard]] std::set<ObjectId> sourcesOf(const DependencyGraph& graph, ObjectId feature) {
    std::set<ObjectId> reached;
    std::vector<ObjectId> queue{feature};
    reached.insert(feature);
    while (!queue.empty()) {
        const ObjectId node = queue.back();
        queue.pop_back();
        for (const ObjectId dependency : graph.dependenciesOf(node)) {
            if (reached.insert(dependency).second) {
                queue.push_back(dependency);
            }
        }
    }
    return reached;
}

/// The configuration guard, carried unchanged in substance from P15-MASS-001 as
/// ADR-030 requires.
///
/// A configuration override changes a parameter's EFFECTIVE value without
/// changing the parameter object, so the regenerator does not mark the features
/// that read it dirty and their bodies are still the base configuration's. For
/// mass that produced a correct density times the wrong volume; for meshing it
/// would produce a mesh of a shape the document does not currently describe, and
/// every result computed on it afterwards.
///
/// Checked before anything else, so that no amount of valid, current, closed,
/// positive-volume geometry can talk this into answering.
[[nodiscard]] bool configurationOverridesActive(const Document& document) {
    if (const std::optional<ConfigurationId> active = document.activeConfiguration()) {
        const Configuration* configuration = document.configurations().find(*active);
        return configuration != nullptr && !configuration->overrides().empty();
    }
    return false;
}

[[nodiscard]] std::string describeConfiguration(const Document& document) {
    if (const std::optional<ConfigurationId> active = document.activeConfiguration()) {
        if (const Configuration* configuration = document.configurations().find(*active)) {
            return std::format("'{}' overrides {} parameter(s)", configuration->name(),
                               configuration->overrides().size());
        }
    }
    return "a configuration overrides parameters";
}

/// The classification both entry points share, so the reason and the message can
/// never disagree about which check fired.
[[nodiscard]] std::optional<GeometryIneligibility> classify(const Document& document,
                                                           const Regenerator& regenerator, ObjectId feature) {
    if (configurationOverridesActive(document)) {
        return GeometryIneligibility::ConfigurationOverrideActive;
    }
    if (document.findObject(feature) == nullptr) {
        return GeometryIneligibility::ObjectNotFound;
    }

    const std::optional<NodeState> state = regenerator.state(feature);
    if (!state && !regenerator.builtRevision(feature)) {
        return GeometryIneligibility::NeverRegenerated;
    }
    if (state == NodeState::Failed) {
        return GeometryIneligibility::RegenerationFailed;
    }
    if (state == NodeState::Blocked) {
        return GeometryIneligibility::RegenerationBlocked;
    }
    // Currency BEFORE the body is looked at. A stale body must not be asked
    // whether it is closed or what it weighs: the answer would be true of a model
    // the user has already changed, and reporting ZeroVolume for it would name the
    // wrong problem.
    if (!regenerator.isCurrent(document, feature)) {
        return GeometryIneligibility::GeometryStale;
    }

    const geometry::Body* body = regenerator.body(feature);
    if (body == nullptr) {
        // Up to date and makes no body: a sketch, a parameter, a datum. Not a
        // broken model, and told apart from one.
        return GeometryIneligibility::NoBody;
    }
    // ONE definition of body eligibility, not a second one written here.
    // features::bodyDefect is what validateDocument() reports to a user, and
    // reusing it is why a shape this layer would mesh can never be one the rest of
    // BetterCAD calls invalid. It carries its own ordering -- topology before
    // measurement -- and its own freedom from epsilons: "positive and finite"
    // needs no threshold, so it cannot reject legitimate micro-scale geometry or
    // accept a near-flat solid the size of a building.
    if (const std::optional<features::BodyDefect> defect = features::bodyDefect(*body)) {
        switch (*defect) {
        case features::BodyDefect::Empty:
            return GeometryIneligibility::EmptyBody;
        case features::BodyDefect::NoSolid:
            return GeometryIneligibility::NotASolid;
        case features::BodyDefect::InvalidShape:
            return GeometryIneligibility::InvalidBRep;
        case features::BodyDefect::VolumeUnavailable:
        case features::BodyDefect::NonPositiveVolume:
            return GeometryIneligibility::ZeroVolume;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::string explain(const Document& document, const Regenerator& regenerator, ObjectId feature,
                                 GeometryIneligibility reason) {
    const DocumentObject* object = document.findObject(feature);
    const std::string name = object != nullptr ? object->name() : std::string{"(deleted)"};
    switch (reason) {
    case GeometryIneligibility::ConfigurationOverrideActive:
        return std::format("geometry is not available for meshing while a configuration with overrides is "
                           "active ({}): an override does not yet rebuild the geometry it changes, so the "
                           "bodies in hand are the base configuration's. Activate the base configuration",
                           describeConfiguration(document));
    case GeometryIneligibility::ObjectNotFound:
        return std::format("there is no object {} to mesh", feature);
    case GeometryIneligibility::NeverRegenerated:
        return std::format("'{}' ({}) has never been regenerated, so there is no geometry to mesh", name,
                           feature);
    case GeometryIneligibility::RegenerationFailed:
        return std::format("'{}' ({}) failed to regenerate, so it has no current geometry to mesh{}", name,
                           feature,
                           regenerator.error(feature) != nullptr
                               ? std::format(": {}", regenerator.error(feature)->message)
                               : std::string{});
    case GeometryIneligibility::RegenerationBlocked:
        return std::format("'{}' ({}) is blocked by a problem upstream, so it has no current geometry to mesh",
                           name, feature);
    case GeometryIneligibility::GeometryStale:
        return std::format("the geometry of '{}' ({}) is stale: it or something it is built from has changed "
                           "since it was last built. Regenerate before meshing",
                           name, feature);
    case GeometryIneligibility::NoBody:
        return std::format("'{}' ({}, {}) produces no body, so there is nothing to mesh", name,
                           object != nullptr ? object->typeName() : "unknown", feature);
    case GeometryIneligibility::EmptyBody:
        return std::format("the body of '{}' ({}) is empty", name, feature);
    case GeometryIneligibility::InvalidBRep:
        return std::format("the body of '{}' ({}) is not geometrically valid, and meshing an invalid shape "
                           "would mesh whatever the kernel made of it rather than the part",
                           name, feature);
    case GeometryIneligibility::NotASolid:
        return std::format("the body of '{}' ({}) contains no solid, so it encloses no volume to fill. A "
                           "volume mesh needs a closed solid, and an open shell is not promoted to one here",
                           name, feature);
    case GeometryIneligibility::ZeroVolume:
        return std::format("the body of '{}' ({}) encloses no usable volume", name, feature);
    }
    return std::format("the geometry of '{}' ({}) cannot be meshed", name, feature);
}

} // namespace

std::string_view toString(GeometryIneligibility reason) noexcept {
    switch (reason) {
    case GeometryIneligibility::ConfigurationOverrideActive:
        return "configuration_override_active";
    case GeometryIneligibility::ObjectNotFound:
        return "object_not_found";
    case GeometryIneligibility::NeverRegenerated:
        return "never_regenerated";
    case GeometryIneligibility::RegenerationFailed:
        return "regeneration_failed";
    case GeometryIneligibility::RegenerationBlocked:
        return "regeneration_blocked";
    case GeometryIneligibility::GeometryStale:
        return "geometry_stale";
    case GeometryIneligibility::NoBody:
        return "no_body";
    case GeometryIneligibility::EmptyBody:
        return "empty_body";
    case GeometryIneligibility::InvalidBRep:
        return "invalid_brep";
    case GeometryIneligibility::NotASolid:
        return "not_a_solid";
    case GeometryIneligibility::ZeroVolume:
        return "zero_volume";
    }
    return "unknown";
}

GeometryRevision geometryRevision(const Document& document, ObjectId feature) {
    const DocumentGraph documentGraph = buildDependencyGraph(document);
    // Seeded with a non-zero constant so that a valid stamp is never 0, which is
    // the "no stamp" value.
    std::uint64_t value = mix(0x6d657368696e6700ULL, feature.value());
    for (const ObjectId source : sourcesOf(documentGraph.graph, feature)) {
        value = mix(value, source.value());
        value = mix(value, document.revisionOf(source).value_or(0));
    }
    // The active configuration participates because it changes effective
    // parameter values. Its per-override granularity is deliberately absent while
    // overrides are refused outright: there is no accepted geometry for an
    // override to have changed.
    const std::optional<ConfigurationId> active = document.activeConfiguration();
    value = mix(value, active ? active->value() : 0);
    return GeometryRevision{value};
}

std::optional<GeometryIneligibility> geometryIneligibility(const Document& document,
                                                           const Regenerator& regenerator, ObjectId feature) {
    return classify(document, regenerator, feature);
}

Result<MeshableGeometry> requireMeshableGeometry(const Document& document, const Regenerator& regenerator,
                                                ObjectId feature) {
    if (const std::optional<GeometryIneligibility> reason = classify(document, regenerator, feature)) {
        const ErrorCode code = *reason == GeometryIneligibility::ObjectNotFound ? ErrorCode::NotFound
                                                                               : ErrorCode::FailedPrecondition;
        return makeError(code, explain(document, regenerator, feature, *reason));
    }

    // classify() established every one of these, so nothing here can fail.
    const geometry::Body* body = regenerator.body(feature);
    const Result<geometry::MassProperties> properties = body->massProperties();

    MeshableGeometry prepared;
    prepared.source = feature;
    // A Body copy shares the kernel shape, so this neither rebuilds nor
    // reconstructs: locations are kept and subshape identity is untouched, which
    // is what P16-MAP-001 will need.
    prepared.body = *body;
    prepared.solidCount = body->topology().solids;
    prepared.volume = properties->volume;
    prepared.revision = geometryRevision(document, feature);
    return prepared;
}

} // namespace bettercad::meshing
