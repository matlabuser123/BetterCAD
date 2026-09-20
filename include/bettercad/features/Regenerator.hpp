#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/math/RigidTransform.hpp>
#include <bettercad/features/Export.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::features {

enum class NodeState {
    UpToDate,    ///< Inputs unchanged since the item was last built.
    Regenerated, ///< Rebuilt in the last pass.
    Failed,      ///< Rebuilding failed, or the item is part of a dependency cycle.
    Blocked,     ///< Not rebuilt because something upstream failed or is missing.
};

[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::string_view toString(NodeState state) noexcept;

struct RegenerationReport {
    /// Items whose own revision changed since the previous pass: the sources
    /// of dirtiness (e.g. an edited parameter, or a driven parameter whose
    /// expression gave a new value).
    std::vector<ObjectId> changed{};
    /// Driven parameters whose expression gave a new value in this pass, in
    /// evaluation order.
    std::vector<ParameterId> updatedParameters{};
    /// Objects rebuilt in this pass, in evaluation (dependency) order.
    std::vector<ObjectId> regenerated{};
    std::vector<ObjectId> failed{};
    std::vector<ObjectId> blocked{};
    /// Groups of items that depend on each other.
    std::vector<std::vector<ObjectId>> cycles{};
    /// Errors of the failed items.
    std::map<ObjectId, Error> errors{};

    [[nodiscard]] bool succeeded() const noexcept { return failed.empty() && blocked.empty(); }
};

class Regenerator;

/// Rebuilds one object; returns the object's body if it produces one.
using RegenerationHandler = std::function<Result<std::optional<geometry::Body>>(
    Document& document, ObjectId object, const Regenerator& regenerator)>;

/// Computes a derived result that belongs to the document rather than to any
/// one object, after every object has been built (ADR-008).
///
/// It exists because the assembly solve does not fit the per-object shape: it
/// spans every active component and mate at once and produces transforms
/// keyed by ComponentId, so there is no object whose handler it is. The
/// assembly module registers one; `features` never learns what a component
/// is, and stores what the pass returns exactly as it stores a body.
///
/// The pass may record diagnostics in the report -- marking the objects
/// responsible as failed, say. Returning an empty map publishes no
/// transforms, which is what a broken assembly must do.
using FinalPass = std::function<Result<std::map<ComponentId, RigidTransform3D>>(
    Document& document, const Regenerator& regenerator, RegenerationReport& report)>;

/// Keeps a document's derived results up to date: driven parameter values,
/// solved sketches and feature bodies.
///
/// Each pass first evaluates the parameter expressions
/// (evaluateParameterExpressions()), which changes the revision of every
/// driven parameter whose value changes. It then builds the dependency graph
/// from the document and finds the items whose revision changed since they
/// were last built. It marks those items and everything downstream dirty,
/// and rebuilds only the dirty items, dependencies first. Unaffected items
/// keep their results. Failures are reported per item: a parameter whose
/// expression fails is failed (and keeps its last value). Items downstream
/// of a failure, a missing reference or a dependency cycle are blocked.
///
/// After the objects, it runs the registered final passes, in name order.
/// The structure is symmetric, and deliberately so (ADR-008):
///
///     parameters -> a document-level phase, before the objects
///     objects    -> one handler each, in dependency order
///     final      -> a document-level phase, after the objects
///
/// Built-in handlers: "sketch" (apply driving parameters, solve) and one per
/// feature kind. Objects of other kinds are treated as plain data.
class BETTERCAD_FEATURES_EXPORT Regenerator {
public:
    Regenerator();

    /// Adds or replaces the handler for objects of @p typeName.
    void registerHandler(std::string typeName, RegenerationHandler handler);
    /// Adds or replaces the final pass called @p name. Passes run after the
    /// objects, in name order, so that two of them cannot depend on
    /// registration order.
    void registerFinalPass(std::string name, FinalPass pass);

    /// Regenerates what changed since the previous pass. The regenerator
    /// binds to the first document it is used with.
    Result<RegenerationReport> regenerate(Document& document);
    /// Forgets all previous results and regenerates everything.
    Result<RegenerationReport> regenerateAll(Document& document);

    /// Body produced by @p object in the latest successful build, if any.
    [[nodiscard]] const geometry::Body* body(ObjectId object) const noexcept;
    /// The transform a final pass derived for @p component in the latest
    /// pass, if any (ADR-005: derived, never persisted).
    ///
    /// Absent means there is none to have: no assembly, or a pass that
    /// published none because something it needed was broken. It is never a
    /// stale one -- a transform that is one edit out of date renders, which
    /// makes it worse than nothing.
    [[nodiscard]] const RigidTransform3D* transform(ComponentId component) const noexcept;
    /// Every transform the last pass published, in ascending component order.
    [[nodiscard]] const std::map<ComponentId, RigidTransform3D>& transforms() const noexcept {
        return transforms_;
    }
    [[nodiscard]] std::optional<NodeState> state(ObjectId item) const noexcept;
    [[nodiscard]] const Error* error(ObjectId item) const noexcept;

private:
    std::map<std::string, RegenerationHandler, std::less<>> handlers_;
    std::optional<DocumentId> documentId_;
    std::map<ObjectId, std::uint64_t> builtRevisions_;
    std::map<ObjectId, NodeState> states_;
    std::map<ObjectId, Error> errors_;
    std::map<ObjectId, geometry::Body> bodies_;
    std::map<std::string, FinalPass, std::less<>> finalPasses_;
    std::map<ComponentId, RigidTransform3D> transforms_;
};

} // namespace bettercad::features
