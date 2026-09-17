#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/features/Export.hpp>
#include <bettercad/features/Feature.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::features {

/// A radius at one point of an edge (see geometry::RadiusStation).
struct VariableFilletStation {
    /// From 0 to 1 along the edge, 0 at the end that comes first along the
    /// edge's canonical direction (geometry::EdgeSignature::direction).
    double position = 0.0;
    /// Used when no parameter drives the radius.
    Length radius{};
    /// Document parameter (a length) that drives the radius, if any.
    std::optional<ParameterId> radiusParameter{};

    friend bool operator==(const VariableFilletStation&, const VariableFilletStation&) = default;
};

/// One edge of a variable-radius fillet and its stations, in order.
struct VariableFilletEdgeDefinition {
    /// A straight edge of the target's body, referred to by its supporting
    /// line (see geometry::EdgeSignature; not a persistent topological name).
    geometry::EdgeSignature edge{};
    std::vector<VariableFilletStation> stations{};

    friend bool operator==(const VariableFilletEdgeDefinition&, const VariableFilletEdgeDefinition&) = default;
};

/// Inputs of a variable-radius fillet feature, e.g.
/// `VariableFilletDefinition{.target = padId, .edges = {{.edge = edge,
/// .stations = {{.position = 0, .radius = 3_mm}, {.position = 1, .radius = 8_mm}}}}}`.
struct VariableFilletDefinition {
    /// The feature whose body is rounded. The fillet consumes it.
    FeatureId target{};
    std::vector<VariableFilletEdgeDefinition> edges{};

    friend bool operator==(const VariableFilletDefinition&, const VariableFilletDefinition&) = default;
};

/// Checks that the definition is self-consistent: a valid target, valid
/// radius parameter IDs, and the geometry request's contract
/// (geometry::validate(VariableFilletRequest)). The radius law is checked
/// here when every radius is a literal, and at regeneration otherwise.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> validate(const VariableFilletDefinition& definition);

/// Rounds straight edges of another feature's body with radii that vary
/// along them (type name "variable_fillet"; see
/// geometry::variableFilletEdges() for what is rounded and how the result
/// is checked). Stores its inputs only; its body is computed by
/// regeneration from the target's body. It names no faces, and carries the
/// names of its target's.
class BETTERCAD_FEATURES_EXPORT VariableFilletFeature final : public SolidFeature {
public:
    using Definition = VariableFilletDefinition;
    static constexpr std::string_view kTypeName = "variable_fillet";

    [[nodiscard]] static Result<std::unique_ptr<VariableFilletFeature>>
    create(std::string name, const VariableFilletDefinition& definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    /// The target feature, then each radius parameter once, in station order.
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;
    [[nodiscard]] std::optional<FeatureId> target() const noexcept override { return definition_.target; }

    [[nodiscard]] const VariableFilletDefinition& definition() const noexcept { return definition_; }
    /// Replaces the definition after validating it.
    Result<bool> setDefinition(const VariableFilletDefinition& definition);

private:
    VariableFilletFeature(std::string name, const VariableFilletDefinition& definition);

    VariableFilletDefinition definition_;
};

} // namespace bettercad::features
