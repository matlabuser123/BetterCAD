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
struct SheetIdTag {
    static constexpr std::string_view name = "sheet";
};
struct ViewIdTag {
    static constexpr std::string_view name = "view";
};
struct DimensionIdTag {
    static constexpr std::string_view name = "dimension";
};
struct AnnotationIdTag {
    static constexpr std::string_view name = "annotation";
};
struct MaterialIdTag {
    static constexpr std::string_view name = "material";
};
struct MeshControlIdTag {
    static constexpr std::string_view name = "mesh control";
};
struct AnalysisIdTag {
    static constexpr std::string_view name = "structural analysis";
};
struct LoadIdTag {
    static constexpr std::string_view name = "load";
};
struct RestraintIdTag {
    static constexpr std::string_view name = "restraint";
};
struct EntityIdTag {
    static constexpr std::string_view name = "entity";
};
struct ConstraintIdTag {
    static constexpr std::string_view name = "constraint";
};
struct ChamferEdgeIdTag {
    static constexpr std::string_view name = "chamfer_edge";
};
struct BoundarySetIdTag {
    static constexpr std::string_view name = "boundary_set";
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
template <>
inline constexpr bool isDocumentObjectTag<SheetIdTag> = true;
template <>
inline constexpr bool isDocumentObjectTag<ViewIdTag> = true;
template <>
inline constexpr bool isDocumentObjectTag<DimensionIdTag> = true;
template <>
inline constexpr bool isDocumentObjectTag<AnnotationIdTag> = true;
template <>
inline constexpr bool isDocumentObjectTag<MaterialIdTag> = true;
template <>
inline constexpr bool isDocumentObjectTag<MeshControlIdTag> = true;
template <>
inline constexpr bool isDocumentObjectTag<AnalysisIdTag> = true;

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
/// One sheet of a drawing. A sheet is a document object and a node in the
/// dependency graph, so its ID widens to ObjectId (ADR-010, ADR-017). It is
/// the sheet's persistent identity: the number a sheet shows in its title
/// block is its position among the sheets, which changes when another is
/// deleted, and is never this.
using SheetId = Id<SheetIdTag>;
/// One view on a drawing sheet. A view is a document object and a node in
/// the dependency graph -- it depends on its sheet, its source and its
/// parent -- so its ID widens to ObjectId (ADR-017). It is not a SheetId: a
/// view sits on a sheet, it is not one.
using ViewId = Id<ViewIdTag>;
/// One dimension on a drawing view. A dimension is a document object and a
/// node in the dependency graph -- it depends on its view and on the objects
/// its references name -- so its ID widens to ObjectId (ADR-017). What it
/// stores is what to measure and how to write it; the NUMBER is derived from
/// the model every time it is asked for and is never this.
using DimensionId = Id<DimensionIdTag>;
/// One annotation on a drawing view: a note, a leader, a centreline, a centre
/// mark, a hole callout, a surface-finish symbol or a datum. A document
/// object and a node in the dependency graph, so its ID widens to ObjectId
/// (ADR-017). What it stores is what to say and what to point at; the lines
/// and the text of anything model-driven are derived every time.
using AnnotationId = Id<AnnotationIdTag>;
/// One material definition owned by a document. A material is a document
/// object and a node in the dependency graph -- changing a density must be able
/// to invalidate a derived mass -- so its ID widens to ObjectId (ADR-025).
///
/// This is a material's identity, and a material has nothing else that is.
/// Neither its object name nor its designation is identity: a material may be
/// renamed and redesignated and is still the same material, and two materials
/// may carry the designation "Steel", hold different values, and stay distinct
/// because their MaterialIds differ. A library entry is NOT one of these: it is
/// reference data with no ObjectId, named by a materials::MaterialLibraryKey,
/// so a library key and a MaterialId cannot be confused for one another.
using MaterialId = Id<MaterialIdTag>;
/// A meshing control: the canonical intent for meshing one body (ADR-030).
///
/// A DOCUMENT OBJECT ID, which is the whole point. ADR-030 chose "the Document
/// owns the controls as document objects; a Mesher service owns the generated
/// meshes", so that changing a body can invalidate a derived mesh -- "and only
/// a graph node can express that". Undo, redo, persistence and dependency
/// invalidation then come from the machinery every other document object
/// already uses.
///
/// It is NOT a mesh identity. A NodeId or an ElementId is mesh-local, reused
/// across generations and never persisted (ADR-031); this names the intent
/// that produced them and outlives every mesh built from it.
using MeshControlId = Id<MeshControlIdTag>;
/// One structural analysis: which mesh is analysed, under which loads,
/// restraints and solver settings (P17-DATA-001). A document object, so it
/// gets a revision, undo, a place in the dependency graph and a file
/// representation from machinery that already exists -- the argument ADR-030
/// made for a meshing control, whose sibling this is.
///
/// IT WIDENS TO ObjectId, AND THAT IS THE DOMAIN CLAIM. An analysis is
/// canonical document state that survives a remesh, a reload and a process.
/// The two IDs below deliberately do NOT widen, and that difference is the
/// whole of this milestone's identity model.
using AnalysisId = Id<AnalysisIdTag>;
/// One load: a force, a traction or a pressure the user asked for, naming CAD
/// geometry (P17-LOAD-001 defines the payload). Unique within the analysis
/// that owns it, as a boundary set is unique within whatever owns the sets.
///
/// CANONICAL INTENT, NOT A MESH ENTITY. A load means "this much force on that
/// CAD face". The nodes it resolves to, the rows of F it contributes and the
/// facets it covers are derived afresh for whatever mesh is current, which is
/// what makes a load survive a remesh (ADR-032). A LoadId never identifies a
/// node, a facet, an element or a row.
using LoadId = Id<LoadIdTag>;
/// One restraint: which displacement components are held at which CAD
/// geometry (P17-BC-001 defines the payload). Unique within its analysis.
///
/// The same rule as LoadId, for the same reason. A restraint's identity is the
/// intent; the constrained DOF indices are a derived consequence of the current
/// mesh and the current numbering, and are never its identity.
using RestraintId = Id<RestraintIdTag>;
/// Sketch entity (point, line, arc, ...); unique within its sketch.
using EntityId = Id<EntityIdTag>;
/// Sketch constraint; unique within its sketch.
using ConstraintId = Id<ConstraintIdTag>;
/// One edge selection of a chamfer feature; unique within that feature, and
/// its persistent identity. It is deliberately NOT the selection's position
/// in `ChamferDefinition::edges`: that vector is stored intent a user may
/// reorder, so a position names different material after a reorder while a
/// stored reference is untouched -- a silent rebind, which ADR-024 forbids.
/// Allocated by the chamfer's own IdAllocator, so a deleted selection's ID is
/// never handed out again and a stale reference to it stays unresolved.
using ChamferEdgeId = Id<ChamferEdgeIdTag>;
/// One named boundary set: a solver-facing region of a part's surface, meant
/// for a load or a restraint (P16-MAP-001). Unique within whatever owns the
/// sets, as a chamfer's edge selections are unique within their chamfer.
///
/// This is the set's identity, and the set has nothing else that is. Its
/// DISPLAY NAME is not identity: two sets may both be called "fixed_end" and
/// stay distinct, and a set may be renamed and remain the same set. Nor are
/// the mesh entities it resolves to: a boundary set means a CAD geometry
/// selection, and the facets, nodes and elements are derived afresh for
/// whatever mesh is current -- which is what makes a restraint survive a
/// remesh.
using BoundarySetId = Id<BoundarySetIdTag>;
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
