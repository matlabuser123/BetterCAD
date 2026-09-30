#pragma once

#include <bettercad/meshing/Export.hpp>

#include <compare>
#include <cstdint>
#include <format>
#include <functional>
#include <ostream>
#include <string_view>

// Mesh-local identity (P16-DATA-001, ADR-031).
//
// THESE ARE DELIBERATELY NOT bettercad::Id<Tag>, and that is the single most
// important thing about this header.
//
// core/Id.hpp states the contract of every Id<Tag>: "IDs are identities, never
// container indices: they stay stable when other objects are added or removed,
// and they are persisted with the document", and IdAllocator exists so that "a
// stale reference can never silently resolve to a newer item". A mesh handle is
// the opposite on every count:
//
//                         bettercad::Id<Tag>        a mesh handle
//   stable across rebuild yes, that is the point    NO -- remeshing invalidates it
//   persisted             yes                       NO -- never in a file
//   an index              never                     effectively yes: dense, ordered
//   allocated by          IdAllocator, never reused the mesh, reused every generation
//   uniqueness scope      the document              one generation of one mesh
//
// Using Id<Tag> would promise persistence and stability a mesh cannot give, and
// would silently LACK IdAllocator's never-reuse guarantee -- the mechanism that
// makes a stale CAD reference safe in BetterCAD -- so two meshes would hand out
// node 1 for different points and nothing would notice.
//
// "Stable typed IDs ... never indices" (ARCHITECTURE.md) is not weakened by
// this, because a node has no persistent identity to name: nodes and elements
// are derived state, destroyed and recreated wholesale by every remesh. There is
// nothing here for a stable ID to be stable about.
//
// No second generic ID template is introduced either. These are three concrete
// types, written out, so that each one's semantics are stated where it is
// declared and none of them can be instantiated for a fourth purpose by
// accident.
namespace bettercad::meshing {

/// One node of one generation of one mesh.
///
/// NOT a CAD identity and NOT persistable. A remesh invalidates every NodeId it
/// ever handed out. Valid only together with the MeshStamp it came from.
///
/// Zero is the invalid value and IDs start at 1, matching the convention of
/// every other identifier in BetterCAD: a default-constructed handle is invalid
/// rather than pointing at the first node.
class NodeId {
public:
    using ValueType = std::uint32_t;

    /// The invalid handle.
    constexpr NodeId() noexcept = default;

    [[nodiscard]] static constexpr NodeId fromValue(ValueType value) noexcept {
        NodeId id;
        id.value_ = value;
        return id;
    }

    [[nodiscard]] constexpr ValueType value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool isValid() const noexcept { return value_ != 0; }
    constexpr explicit operator bool() const noexcept { return isValid(); }

    friend constexpr bool operator==(const NodeId&, const NodeId&) = default;
    friend constexpr auto operator<=>(const NodeId&, const NodeId&) = default;

private:
    ValueType value_{};
};

/// One element of one generation of one mesh: a Triangle3 or a Tetrahedron4.
///
/// One identity space covers every element kind, so an ElementId names exactly
/// one element whatever its type. Same lifetime rules as NodeId, and it is NOT a
/// NodeId: an element is not a node.
class ElementId {
public:
    using ValueType = std::uint32_t;

    constexpr ElementId() noexcept = default;

    [[nodiscard]] static constexpr ElementId fromValue(ValueType value) noexcept {
        ElementId id;
        id.value_ = value;
        return id;
    }

    [[nodiscard]] constexpr ValueType value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool isValid() const noexcept { return value_ != 0; }
    constexpr explicit operator bool() const noexcept { return isValid(); }

    friend constexpr bool operator==(const ElementId&, const ElementId&) = default;
    friend constexpr auto operator<=>(const ElementId&, const ElementId&) = default;

private:
    ValueType value_{};
};

/// One region of a mesh. ADR-032: "A mesh has one region per solid, shares no
/// node between regions."
///
/// Required by the architecture rather than added speculatively: a Body is "zero
/// or more solids", so a multi-solid body meshes as several regions, and the
/// no-shared-node invariant cannot be checked unless an element says which
/// region it belongs to. It is a strong type and not an `int`, so a region can
/// never be confused with a count, an index, a node or an element.
///
/// A region carries NO material data. ADR-028 and ADR-032: "No mesh, region or
/// element holds a material property value."
class RegionId {
public:
    using ValueType = std::uint32_t;

    constexpr RegionId() noexcept = default;

    [[nodiscard]] static constexpr RegionId fromValue(ValueType value) noexcept {
        RegionId id;
        id.value_ = value;
        return id;
    }

    [[nodiscard]] constexpr ValueType value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool isValid() const noexcept { return value_ != 0; }
    constexpr explicit operator bool() const noexcept { return isValid(); }

    friend constexpr bool operator==(const RegionId&, const RegionId&) = default;
    friend constexpr auto operator<=>(const RegionId&, const RegionId&) = default;

private:
    ValueType value_{};
};

/// Which mesh a handle belongs to, within this process.
///
/// Not persisted and not a document identity: it exists so that a handle kept
/// across a possible remesh can be REFUSED rather than reinterpreted. Values are
/// handed out by MeshBuilder from a process-local counter, so two meshes never
/// share one.
class MeshId {
public:
    using ValueType = std::uint64_t;

    constexpr MeshId() noexcept = default;

    [[nodiscard]] static constexpr MeshId fromValue(ValueType value) noexcept {
        MeshId id;
        id.value_ = value;
        return id;
    }

    [[nodiscard]] constexpr ValueType value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool isValid() const noexcept { return value_ != 0; }
    constexpr explicit operator bool() const noexcept { return isValid(); }

    friend constexpr bool operator==(const MeshId&, const MeshId&) = default;
    friend constexpr auto operator<=>(const MeshId&, const MeshId&) = default;

private:
    ValueType value_{};
};

/// What a NodeId, ElementId or RegionId is only meaningful against (ADR-031: "A
/// handle is valid only with the MeshId and generation it came from. A mismatch
/// is refused with a diagnostic and is never reinterpreted.").
///
/// The stamp lives on the MESH, not on every handle: a tetrahedron stores four
/// node handles, and a million-element mesh stores four million, so carrying a
/// stamp in each would multiply connectivity memory for a check that belongs at
/// the boundary. A caller that keeps a handle across a possible remesh keeps the
/// stamp with it and asks the mesh (Mesh::owns) before using it.
struct MeshStamp {
    MeshId mesh{};
    /// Increments for every mesh built from the same source, so a handle from an
    /// earlier generation is detectably stale even in the unreachable case of a
    /// repeated MeshId.
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool isValid() const noexcept { return mesh.isValid(); }

    friend constexpr bool operator==(const MeshStamp&, const MeshStamp&) = default;
};

} // namespace bettercad::meshing

template <>
struct std::hash<bettercad::meshing::NodeId> {
    std::size_t operator()(const bettercad::meshing::NodeId& id) const noexcept {
        return std::hash<std::uint32_t>{}(id.value());
    }
};

template <>
struct std::hash<bettercad::meshing::ElementId> {
    std::size_t operator()(const bettercad::meshing::ElementId& id) const noexcept {
        return std::hash<std::uint32_t>{}(id.value());
    }
};

template <>
struct std::hash<bettercad::meshing::RegionId> {
    std::size_t operator()(const bettercad::meshing::RegionId& id) const noexcept {
        return std::hash<std::uint32_t>{}(id.value());
    }
};

/// Diagnostic form "node:12". Deliberately distinct from the CAD kinds' names,
/// so a mesh handle can never be read as a document identity in a log.
template <>
struct std::formatter<bettercad::meshing::NodeId> {
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    template <typename FormatContext>
    auto format(const bettercad::meshing::NodeId& id, FormatContext& ctx) const {
        return std::format_to(ctx.out(), "node:{}", id.value());
    }
};

template <>
struct std::formatter<bettercad::meshing::ElementId> {
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    template <typename FormatContext>
    auto format(const bettercad::meshing::ElementId& id, FormatContext& ctx) const {
        return std::format_to(ctx.out(), "element:{}", id.value());
    }
};

template <>
struct std::formatter<bettercad::meshing::RegionId> {
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    template <typename FormatContext>
    auto format(const bettercad::meshing::RegionId& id, FormatContext& ctx) const {
        return std::format_to(ctx.out(), "region:{}", id.value());
    }
};

namespace bettercad::meshing {

inline std::ostream& operator<<(std::ostream& os, const NodeId& id) {
    return os << std::format("{}", id);
}

inline std::ostream& operator<<(std::ostream& os, const ElementId& id) {
    return os << std::format("{}", id);
}

inline std::ostream& operator<<(std::ostream& os, const RegionId& id) {
    return os << std::format("{}", id);
}

} // namespace bettercad::meshing
