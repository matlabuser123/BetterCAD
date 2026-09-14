#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/features/Export.hpp>

#include <optional>
#include <string_view>

// What all solid-producing features share: how their tool solid combines
// with an existing body, and which feature's body they consume.
namespace bettercad::features {

/// How a feature's tool solid combines with an existing body.
enum class FeatureOperation {
    NewBody,   ///< the tool solid on its own
    Join,      ///< union with the target body
    Cut,       ///< target body minus the tool solid
    Intersect, ///< intersection with the target body
};

/// "new body", "join", "cut" or "intersect".
[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::string_view toString(FeatureOperation operation) noexcept;

/// The pairing rule of every solid feature: NewBody takes no target;
/// Join, Cut and Intersect need a valid one. Fails with InvalidArgument.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> validateOperation(FeatureOperation operation,
                                                                       const std::optional<FeatureId>& target);

/// Base class of features that produce a body (extrude, revolve, ...).
/// A Join, Cut or Intersect feature consumes the body of its target
/// feature, which then becomes an intermediate result.
class BETTERCAD_FEATURES_EXPORT SolidFeature : public DocumentObject {
public:
    [[nodiscard]] virtual FeatureOperation operation() const noexcept = 0;
    /// The feature whose body this one combines with; empty for NewBody.
    [[nodiscard]] virtual std::optional<FeatureId> target() const noexcept = 0;

    /// Typed view of id(); invalid until the feature is in a document.
    [[nodiscard]] FeatureId featureId() const noexcept { return FeatureId::fromValue(id().value()); }

protected:
    using DocumentObject::DocumentObject;
    SolidFeature(const SolidFeature&) = default;
};

/// Shared body combination: the feature's @p tool solid on its own
/// (NewBody), or combined with @p target (Join/Cut/Intersect), which must
/// then be a non-empty body. Errors are prefixed with @p featureName.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::Body> combineWithTarget(FeatureOperation operation,
                                                                                const geometry::Body& tool,
                                                                                const geometry::Body* target,
                                                                                std::string_view featureName);

} // namespace bettercad::features
