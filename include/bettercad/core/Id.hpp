#pragma once

#include <bettercad/core/Uuid.hpp>

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string_view>

namespace bettercad {

// ---------------------------------------------------------------------------
// ID tags. Each tag defines a distinct ID type and its diagnostic name.
// ---------------------------------------------------------------------------
struct DocumentIdTag {
    static constexpr std::string_view name = "document";
};
struct ObjectIdTag {
    static constexpr std::string_view name = "object";
};
struct SketchIdTag {
    static constexpr std::string_view name = "sketch";
};
struct FeatureIdTag {
    static constexpr std::string_view name = "feature";
};
struct ParameterIdTag {
    static constexpr std::string_view name = "parameter";
};
struct ConfigurationIdTag {
    static constexpr std::string_view name = "configuration";
};
struct BodyIdTag {
    static constexpr std::string_view name = "body";
};
struct ComponentIdTag {
    static constexpr std::string_view name = "component";
};
struct MateIdTag {
    static constexpr std::string_view name = "mate";
};
struct EntityIdTag {
    static constexpr std::string_view name = "entity";
};
struct ConstraintIdTag {
    static constexpr std::string_view name = "constraint";
};
struct FaceIdTag {
    static constexpr std::string_view name = "face";
};
struct EdgeIdTag {
    static constexpr std::string_view name = "edge";
};
struct VertexIdTag {
    static constexpr std::string_view name = "vertex";
};

/// Tags of document objects. Their IDs share the document's object ID space
/// and widen implicitly to ObjectId.
template <typename Tag>
inline constexpr bool isDocumentObjectTag = false;
template <>
inline constexpr bool isDocumentObjectTag<SketchIdTag> = true;
template <>
inline constexpr bool isDocumentObjectTag<FeatureIdTag> = true;
template <>
inline constexpr bool isDocumentObjectTag<ParameterIdTag> = true;
template <>
inline constexpr bool isDocumentObjectTag<BodyIdTag> = true;
template <>
inline constexpr bool isDocumentObjectTag<ComponentIdTag> = true;
template <>
inline constexpr bool isDocumentObjectTag<MateIdTag> = true;

template <typename Tag, typename Value = std::uint64_t>
class Id;

using ObjectId = Id<ObjectIdTag>;

/// Strongly typed identifier.
///
/// IDs of different kinds are distinct types: they do not convert to or from
/// integers or to each other (except the widening of document object IDs to
/// ObjectId). A default-constructed ID is invalid. IDs are identities, never
/// container indices: they stay stable when other objects are added or
/// removed, and they are persisted with the document.
template <typename Tag, typename Value>
class Id {
public:
    using TagType = Tag;
    using ValueType = Value;

    /// The invalid ID.
    constexpr Id() noexcept = default;

    [[nodiscard]] static constexpr Id fromValue(const Value& value) noexcept {
        Id id;
        id.value_ = value;
        return id;
    }

    [[nodiscard]] constexpr const Value& value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool isValid() const noexcept { return value_ != Value{}; }
    constexpr explicit operator bool() const noexcept { return isValid(); }

    /// A sketch, feature, parameter or body is also a document object, so its
    /// ID widens implicitly. The reverse needs a checked lookup.
    constexpr operator ObjectId() const noexcept
        requires(isDocumentObjectTag<Tag>)
    {
        return ObjectId::fromValue(value_);
    }

    friend constexpr bool operator==(const Id&, const Id&) = default;
    friend constexpr auto operator<=>(const Id&, const Id&) = default;

private:
    Value value_{};
};

using DocumentId = Id<DocumentIdTag, Uuid>;
using SketchId = Id<SketchIdTag>;
using FeatureId = Id<FeatureIdTag>;
using ParameterId = Id<ParameterIdTag>;
/// A configuration is document-level state, not a document object, so its ID
/// does not widen to ObjectId; it comes from the document's one allocator so
/// that no ID is ever reused (P12-PARAM-002).
using ConfigurationId = Id<ConfigurationIdTag>;
using BodyId = Id<BodyIdTag>;
/// One placement of a part in an assembly. A component is a document object
/// and a node in the dependency graph, so its ID widens to ObjectId
/// (ADR-002). Two components of the same part have different ComponentIds:
/// this identifies the instance, never the part it instances.
using ComponentId = Id<ComponentIdTag>;
/// One assembly constraint. A mate is a document object and a node in the
/// dependency graph, so its ID widens to ObjectId (ADR-002). It is not a
/// ComponentId: a mate relates components, it is not one of them.
using MateId = Id<MateIdTag>;
/// Sketch entity (point, line, arc, ...); unique within its sketch.
using EntityId = Id<EntityIdTag>;
/// Sketch constraint; unique within its sketch.
using ConstraintId = Id<ConstraintIdTag>;
/// Topology IDs; unique within their body. Persistent naming across
/// regenerations is future work (semantic topology naming).
using FaceId = Id<FaceIdTag>;
using EdgeId = Id<EdgeIdTag>;
using VertexId = Id<VertexIdTag>;

/// Hands out integer IDs from one ID space. Values start at 1 and are never
/// reused, even after the identified item is deleted, so a stale reference
/// can never silently resolve to a newer item. Not thread-safe.
class IdAllocator {
public:
    /// Next ID of the given kind.
    /// @throws std::overflow_error if the ID space is exhausted.
    template <typename IdType>
        requires std::same_as<typename IdType::ValueType, std::uint64_t>
    [[nodiscard]] IdType allocate() {
        if (last_ == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("IdAllocator: ID space exhausted");
        }
        return IdType::fromValue(++last_);
    }

    /// Makes later allocations return values above @p value. Used when
    /// loading items that already carry IDs.
    constexpr void reserveThrough(std::uint64_t value) noexcept {
        if (value > last_) {
            last_ = value;
        }
    }

    /// Highest value allocated or reserved so far (0 if none).
    [[nodiscard]] constexpr std::uint64_t lastValue() const noexcept { return last_; }

private:
    std::uint64_t last_ = 0;
};

} // namespace bettercad

template <typename Tag, typename Value>
struct std::hash<bettercad::Id<Tag, Value>> {
    std::size_t operator()(const bettercad::Id<Tag, Value>& id) const noexcept {
        return std::hash<Value>{}(id.value());
    }
};

/// Diagnostic form "<kind>:<value>", e.g. "sketch:12".
template <typename Tag, typename Value>
struct std::formatter<bettercad::Id<Tag, Value>> {
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    template <typename FormatContext>
    auto format(const bettercad::Id<Tag, Value>& id, FormatContext& ctx) const {
        return std::format_to(ctx.out(), "{}:{}", Tag::name, id.value());
    }
};

namespace bettercad {

/// Streams the diagnostic form, e.g. "sketch:12" (also used by test output).
template <typename Tag, typename Value>
std::ostream& operator<<(std::ostream& os, const Id<Tag, Value>& id) {
    return os << std::format("{}", id);
}

} // namespace bettercad
