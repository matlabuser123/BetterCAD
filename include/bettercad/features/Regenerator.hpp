#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Body.hpp>
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
    /// of dirtiness (e.g. an edited parameter).
    std::vector<ObjectId> changed{};
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

/// Keeps a document's derived results up to date: solved sketches and feature
/// bodies.
///
/// Each pass builds the dependency graph from the document and finds the
/// items whose revision changed since they were last built. It marks those
/// items and everything downstream dirty, and rebuilds only the dirty items,
/// dependencies first. Unaffected items keep their results. Failures are
/// reported per item; items downstream of a failure, a missing reference or
/// a dependency cycle are blocked.
///
/// Built-in handlers: "sketch" (apply driving parameters, solve) and
/// "extrude". Objects of other kinds are treated as plain data.
class BETTERCAD_FEATURES_EXPORT Regenerator {
public:
    Regenerator();

    /// Adds or replaces the handler for objects of @p typeName.
    void registerHandler(std::string typeName, RegenerationHandler handler);

    /// Regenerates what changed since the previous pass. The regenerator
    /// binds to the first document it is used with.
    Result<RegenerationReport> regenerate(Document& document);
    /// Forgets all previous results and regenerates everything.
    Result<RegenerationReport> regenerateAll(Document& document);

    /// Body produced by @p object in the latest successful build, if any.
    [[nodiscard]] const geometry::Body* body(ObjectId object) const noexcept;
    [[nodiscard]] std::optional<NodeState> state(ObjectId item) const noexcept;
    [[nodiscard]] const Error* error(ObjectId item) const noexcept;

private:
    std::map<std::string, RegenerationHandler, std::less<>> handlers_;
    std::optional<DocumentId> documentId_;
    std::map<ObjectId, std::uint64_t> builtRevisions_;
    std::map<ObjectId, NodeState> states_;
    std::map<ObjectId, Error> errors_;
    std::map<ObjectId, geometry::Body> bodies_;
};

} // namespace bettercad::features
