#include <bettercad/features/VariableFilletFeature.hpp>

#include <bettercad/core/geometry/VariableFillet.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace bettercad::features {

namespace {

bool hasDrivenRadius(const VariableFilletEdgeDefinition& edge) {
    return std::ranges::any_of(edge.stations, [](const VariableFilletStation& station) {
        return station.radiusParameter.has_value();
    });
}

/// The geometry request, with @p placeholder for the radii that parameters
/// drive; with @p wholeEdges, every radius of an edge that has a driven one.
geometry::VariableFilletRequest placeholderRequest(const VariableFilletDefinition& definition, bool wholeEdges) {
    const Length placeholder = Length::fromSi(1.0);
    geometry::VariableFilletRequest request;
    for (const VariableFilletEdgeDefinition& edge : definition.edges) {
        const bool deferred = wholeEdges && hasDrivenRadius(edge);
        geometry::VariableFilletEdge entry{.edge = edge.edge};
        for (const VariableFilletStation& station : edge.stations) {
            const bool driven = deferred || station.radiusParameter.has_value();
            entry.stations.push_back({.position = station.position, .radius = driven ? placeholder : station.radius});
        }
        request.edges.push_back(std::move(entry));
    }
    return request;
}

} // namespace

Result<void> validate(const VariableFilletDefinition& definition) {
    if (!definition.target.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "a variable-radius fillet needs a target feature");
    }
    for (std::size_t i = 0; i < definition.edges.size(); ++i) {
        const auto& stations = definition.edges[i].stations;
        for (std::size_t k = 0; k < stations.size(); ++k) {
            if (stations[k].radiusParameter && !stations[k].radiusParameter->isValid()) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("edge reference {}, station {}: the radius parameter ID must be valid",
                                             i + 1, k + 1));
            }
        }
    }
    // The geometry request's contract. Driven radii are known only at
    // regeneration: first everything but the law, with placeholders for
    // them; then the law of every edge whose radii are all literals.
    if (auto valid = geometry::validate(placeholderRequest(definition, false), geometry::StationCheck::WithoutLaw);
        !valid) {
        return valid;
    }
    return geometry::validate(placeholderRequest(definition, true), geometry::StationCheck::Complete);
}

VariableFilletFeature::VariableFilletFeature(std::string name, const VariableFilletDefinition& definition)
    : SolidFeature(std::move(name)), definition_(definition) {}

Result<std::unique_ptr<VariableFilletFeature>>
VariableFilletFeature::create(std::string name, const VariableFilletDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    return std::unique_ptr<VariableFilletFeature>(new VariableFilletFeature(std::move(name), definition));
}

std::unique_ptr<DocumentObject> VariableFilletFeature::clone() const {
    return std::unique_ptr<DocumentObject>(new VariableFilletFeature(*this));
}

bool VariableFilletFeature::contentEquals(const DocumentObject& other) const {
    return definition_ == static_cast<const VariableFilletFeature&>(other).definition_;
}

std::vector<ObjectId> VariableFilletFeature::dependencies() const {
    std::vector<ObjectId> result{ObjectId{definition_.target}};
    for (const VariableFilletEdgeDefinition& edge : definition_.edges) {
        for (const VariableFilletStation& station : edge.stations) {
            if (station.radiusParameter) {
                const ObjectId id{*station.radiusParameter};
                if (std::ranges::find(result, id) == result.end()) {
                    result.push_back(id);
                }
            }
        }
    }
    return result;
}

Result<bool> VariableFilletFeature::setDefinition(const VariableFilletDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    if (definition == definition_) {
        return false;
    }
    definition_ = definition;
    return true;
}

} // namespace bettercad::features
