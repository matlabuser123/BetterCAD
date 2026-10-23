#pragma once

// P18-VIZ-001 -- the read-only visualisation adapter for a qualified P18 thermal result.
//
//     canonical thermal analysis       owned by the document
//             |
//     qualified derived result         ThermalPostResult (P18-POST-001), owned elsewhere
//             |
//             v
//     read-only visualisation adapter  THIS FILE
//             |
//             v
//     viewer/render representation     src/renderer/occt/
//
// THERMAL VISUALISATION IS A READ-ONLY VIEW OF DERIVED P18 DATA. It is not a thermal-analysis authority, and
// this header exposes nothing that could become one: it computes no temperature, no gradient, no heat flux, no
// balance and no verdict. Every scalar it reports is READ from a stored member of `ThermalPostResult`, with ONE
// documented exception, the display norm of a stored gradient vector, below.
//
// IT CONTAINS NO Qt AND NO OCCT, so every gate of the milestone -- the source guards, the cardinalities, the
// extremes, the units -- is assertable headlessly. A GPU is not an engineering gate.
//
// THE UNITS, AND WHERE THEY CHANGE. Everything leaving this layer is the qualified SI value: a temperature in K,
// a gradient magnitude in K/m, a heat flux in W/m^2. Positions are SI METRES, exactly as `MeshView::positions()`
// gives them. The single metres-to-millimetres conversion is `toModelLength`, which already exists in the OCCT
// presentation adapter and is `geometry::occt::toModel`, the project's only one; there is no `* 1000.0` in this
// file or its implementation, and a K/m gradient is NEVER re-expressed as K/mm because the display happens to be
// in millimetres.
//
// THE ONE ARITHMETIC EXCEPTION. POST stores `grad(T)` as a vector and stores NO gradient magnitude, so the
// "temperature gradient magnitude" channel is the Euclidean norm of the stored vector, taken for display (a
// contour needs a scalar). It is NOT a recovery of grad(T): the vector is the qualified value, the norm is a
// presentation scalar, and `ThermalViz_TheGradientMagnitudeIsTheNormOfTheStoredVector` compares it with an
// independently typed expected value. The heat-flux magnitude is NOT computed here: it is POST's stored
// `|q|`, because POST owns that value and a second norm could disagree with it in the last bit.
//
// WHERE A VALUE LIVES, AND WHAT THAT MEANS FOR THE PICTURE.
//
//     Temperature                   NODE      a nodal scalar; a facet may interpolate it (its shape function)
//     TemperatureGradientMagnitude  ELEMENT   constant within a Tet4 -- one value per tetrahedron, UNSMOOTHED
//     HeatFluxMagnitude             ELEMENT   likewise
//
// An element value is drawn FLAT over the facets its tetrahedron owns. It is never averaged onto nodes in either
// direction and presented as the qualified value.

#include <bettercad/core/Error.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/renderer/Export.hpp>
#include <bettercad/renderer/MeshView.hpp>
#include <bettercad/thermal/ThermalData.hpp>
#include <bettercad/thermal/ThermalDof.hpp>
#include <bettercad/thermal/ThermalPost.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace bettercad::renderer {

// ---------------------------------------------------------------------------
// Channels
// ---------------------------------------------------------------------------

/// Which scalar a contour or an inspection reads. ONE ENUMERATION AND ONE DESCRIPTOR TABLE, so a channel's
/// location, unit and name are stated once.
enum class ThermalChannel : std::uint8_t {
    /// The nodal absolute temperature, K: POST's stored value, bit for bit.
    Temperature,
    /// The display norm of POST's stored `grad(T)`, K/m.
    TemperatureGradientMagnitude,
    /// POST's stored `|q|`, W/m^2. Not recomputed.
    HeatFluxMagnitude,
};

[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::string_view toString(ThermalChannel channel) noexcept;

/// What a channel is, stated once. The location and the unit are POST's own descriptor values, read rather than
/// restated, so a unit cannot be changed here without changing it in the core.
struct ThermalChannelDescriptor {
    ThermalChannel channel = ThermalChannel::Temperature;
    /// The POST field the channel is read from.
    thermal::ThermalPostField source = thermal::ThermalPostField::Temperature;
    thermal::FieldLocation location = thermal::FieldLocation::Node;
    /// For a legend or a menu.
    std::string_view name;
    /// The SI unit the VALUES are in (POST's descriptor text: "K", "K/m", "W/m^2"). Display conversion, if any
    /// is ever added, is a formatter's and happens outside this module.
    std::string_view unit;
    /// Whether a negative value is physically expected. A magnitude never is; a temperature never is (it is
    /// absolute, and POST refuses one below 0 K).
    bool mayBeNegative = false;

    friend bool operator==(const ThermalChannelDescriptor&, const ThermalChannelDescriptor&) = default;
};

[[nodiscard]] BETTERCAD_RENDERER_EXPORT ThermalChannelDescriptor descriptorOf(ThermalChannel channel) noexcept;

/// Every channel, in enumeration order, so a menu, a test sweep and the evidence table iterate one list.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::span<const ThermalChannel> allThermalChannels() noexcept;

/// The display scalar of a stored gradient vector: `sqrt(gx^2 + gy^2 + gz^2)`, in K/m.
///
/// A PRESENTATION SCALAR, NOT A RECOVERY. The vector is POST's; this is the one place its norm is taken, so the
/// "gradient magnitude" channel, the inspector and the legend cannot disagree with each other.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT double gradientDisplayNorm(const thermal::TemperatureGradient3& gradient) noexcept;

/// The value of a NODAL channel on one node, ABSENT for an element channel. A read of the stored temperature.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::optional<double> channelValue(ThermalChannel channel,
                                                                           Temperature temperature) noexcept;

/// The value of an ELEMENT channel on one element, ABSENT for the nodal channel.
///
/// `magnitude` is POST's stored `|q|`. ABSENT IS NOT ZERO: a channel with nothing to say returns an absent
/// optional, because 0.0 is an ordinary temperature gradient and an ordinary heat flux, and a field of silence
/// must not look like a valid uniform result.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::optional<double>
channelValue(ThermalChannel channel, const thermal::TemperatureGradient3& gradient, HeatFlux magnitude) noexcept;

// ---------------------------------------------------------------------------
// A classified scalar field
// ---------------------------------------------------------------------------

/// Why a channel could not be prepared for display.
enum class ThermalFieldProblem : std::uint8_t {
    /// The result does not describe the mesh, the numbering or the view it was asked about -- by stamp, by node
    /// count and by element identity, never by counts alone.
    ResultSourceMismatch,
    /// The result's domain is empty. A range over zero entities is not a valid uniform field.
    EmptyClassificationDomain,
    /// The requested state is not one of the result's stored states.
    StateOutOfRange,
    /// An array of the state is not exactly as long as its domain. NEVER zero-filled, NEVER truncated.
    CardinalityMismatch,
    /// A source value is not finite. A qualified current result cannot contain one; this refuses rather than
    /// mapping NaN to a colour.
    NonFiniteValue,
    /// The channel has no value on the entity it was asked for (the descriptor table and the readers disagree).
    ChannelUnavailable,
};

[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::string_view toString(ThermalFieldProblem problem) noexcept;

/// One channel of one stored state, extracted and classified.
///
/// THE CARDINALITY IS PART OF THE ANSWER. `complete()` requires a non-empty domain fully classified with nothing
/// skipped and nothing non-finite -- the P16 trap, closed at the type.
struct ThermalScalarField {
    ThermalChannel channel = ThermalChannel::Temperature;
    thermal::FieldLocation location = thermal::FieldLocation::Node;
    /// Which stored state, and its time in seconds (nullopt for a steady result).
    std::size_t state = 0;
    std::optional<double> time{};

    std::size_t expected = 0;
    std::size_t classified = 0;
    std::size_t skipped = 0;
    std::size_t nonFinite = 0;

    /// The values, in the domain's own deterministic order: ascending `NodeId` (which is `ThermalDofIndex`
    /// order) for the nodal channel, ascending `ElementId` (POST's tetrahedron order) for an element channel.
    /// Never indexed by a raw id.
    std::vector<double> values{};
    /// The entity each value belongs to, parallel to `values`; exactly one of the two is populated.
    std::vector<meshing::NodeId> nodes{};
    std::vector<meshing::ElementId> elements{};

    /// The range over the entities actually classified, seeded from the first value (never from 0.0), and where
    /// the extremes are. The FIRST entity in the domain's order holds a tie.
    double minimum = 0.0;
    double maximum = 0.0;
    meshing::NodeId minimumNode{};
    meshing::NodeId maximumNode{};
    meshing::ElementId minimumElement{};
    meshing::ElementId maximumElement{};

    [[nodiscard]] bool complete() const noexcept {
        return expected > 0 && classified == expected && skipped == 0 && nonFinite == 0;
    }
    /// Whether every classified value is the same -- a valid field, not an error.
    [[nodiscard]] bool uniform() const noexcept { return classified > 0 && minimum == maximum; }

    friend bool operator==(const ThermalScalarField&, const ThermalScalarField&) = default;
};

/// Extracts and classifies @p channel of one stored @p state.
///
/// THE PUBLIC KERNEL OF THE VIEW, exposed for the reason P17-VIZ-001 exposed its own: ThermalPostResult cannot
/// be corrupted (that is its guarantee), so a refusal of a short, long or non-finite array is reachable only
/// through a seam that takes the plain ThermalPostState. ThermalResultView::field calls exactly this after its
/// source checks, so the two cannot disagree.
///
/// @p nodes is the numbering's node list (ThermalDofMap::nodes, ascending) and @p elements the result's element
/// ids (POST's tetrahedron order). Refuses an array that is not exactly as long as its domain -- NEVER padded with
/// zeros, NEVER truncated -- a non-finite value, and an empty domain. @p stateIndex only labels the field.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<ThermalScalarField>
classifyThermalState(ThermalChannel channel, const thermal::ThermalPostState& state,
                     std::span<const meshing::NodeId> nodes, std::span<const meshing::ElementId> elements,
                     std::size_t stateIndex);

// ---------------------------------------------------------------------------
// The view
// ---------------------------------------------------------------------------

/// A derived, disposable, source-keyed render cache for one thermal result.
///
/// KEYED ON BOTH the mesh generation and the result's source, because either moving invalidates it and they move
/// for different reasons. `describes()` asks both, by value: two results of one mesh can have identical counts and
/// come from different materials, and a view read across that gap would put every value on the wrong physics.
///
/// There is NO API to update one in place. A new result means a new view; the old one answers
/// `describes(...) == false` rather than reinterpreting its buffers against different numbers.
class BETTERCAD_RENDERER_EXPORT ThermalResultView {
public:
    /// Binds @p post to @p mesh, @p map and @p view.
    ///
    /// Refuses when the numbering does not describe the mesh, the result does not describe the numbering, the
    /// mesh view does not describe the mesh, the result's element ids are not the mesh's tetrahedra id for id and
    /// in order, the result holds no state, or a domain is empty. Nothing is extracted here.
    [[nodiscard]] static Result<ThermalResultView> of(const meshing::Mesh& mesh, const MeshView& view,
                                                      const thermal::ThermalDofMap& map,
                                                      const thermal::ThermalPostResult& post);

    [[nodiscard]] const meshing::MeshStamp& mesh() const noexcept { return mesh_; }
    [[nodiscard]] const thermal::ThermalPostSource& source() const noexcept { return source_; }

    /// Whether this view may be used against @p mesh, @p map and @p post. THE REVISION GUARD: the sources are
    /// compared by value, not the counts.
    [[nodiscard]] bool describes(const meshing::Mesh& mesh, const thermal::ThermalDofMap& map,
                                 const thermal::ThermalPostResult& post) const noexcept;

    [[nodiscard]] std::size_t nodeCount() const noexcept { return nodeCount_; }
    [[nodiscard]] std::size_t elementCount() const noexcept { return elementCount_; }
    [[nodiscard]] std::size_t stateCount() const noexcept { return stateCount_; }
    /// How many entities @p location holds. The `expected` of any field on it.
    [[nodiscard]] std::size_t domainSize(thermal::FieldLocation location) const noexcept;

    /// Extracts and classifies @p channel of stored state @p state.
    ///
    /// Reads a STORED value for every entity. Refuses rather than producing a partial field.
    [[nodiscard]] Result<ThermalScalarField> field(const meshing::Mesh& mesh, const thermal::ThermalDofMap& map,
                                                   const thermal::ThermalPostResult& post, std::size_t state,
                                                   ThermalChannel channel) const;

private:
    ThermalResultView() = default;

    meshing::MeshStamp mesh_{};
    thermal::ThermalPostSource source_{};
    std::size_t nodeCount_ = 0;
    std::size_t elementCount_ = 0;
    std::size_t stateCount_ = 0;
};

// ---------------------------------------------------------------------------
// Extremes
// ---------------------------------------------------------------------------

/// Which extreme a navigation step asks for. The thermal channels are never negative, so the signed maximum and
/// minimum are the whole of it (there is no "largest magnitude" to disambiguate).
enum class ThermalExtremeKind : std::uint8_t {
    Maximum,
    Minimum,
};

[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::string_view toString(ThermalExtremeKind kind) noexcept;

/// One extreme of one channel, and the entity holding it.
struct ThermalExtreme {
    ThermalExtremeKind kind = ThermalExtremeKind::Maximum;
    thermal::FieldLocation location = thermal::FieldLocation::Node;
    /// Set for a nodal channel.
    meshing::NodeId node{};
    /// Set for an element channel.
    meshing::ElementId element{};
    double value = 0.0;
    /// Where it is in the field's arrays.
    std::size_t index = 0;

    friend bool operator==(const ThermalExtreme&, const ThermalExtreme&) = default;
};

/// The @p kind extreme of @p field, over the result's whole domain (the field is built over the result, not over
/// the render buffer, so it is global by construction).
///
/// THE EXTREMUM IS SEARCHED IN THE SOURCE VALUES, never in rendered colours. TIES GO TO THE FIRST ENTITY in the
/// domain's ascending order -- the lowest `ThermalDofIndex` for temperature, the lowest element ordinal for an
/// element channel -- which is a property of the traversal rather than of a sort nobody can see. Refuses an
/// incomplete field.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<ThermalExtreme> thermalExtremeOf(const ThermalScalarField& field,
                                                                                ThermalExtremeKind kind);

} // namespace bettercad::renderer
