#pragma once

// P17-VIZ-001 -- the read-only visualisation adapter for a qualified P17
// structural result.
//
//     canonical P17 result            RecoveredFields, SupportReactions,
//                                     owned elsewhere
//             |
//             v
//     read-only visualisation adapter THIS FILE
//             |
//             v
//     viewer/render representation    src/renderer/occt/
//             |
//             v
//     inspection UI                   apps/bettercad/
//
// The sibling of `MeshView`, built the same way for the same reasons, and it
// reuses `MeshView` rather than rendering a second mesh: the undeformed
// geometry, the triangle and edge buffers and the render-index-to-identity
// bridge are all P16-VIZ-001's and are not rebuilt here.
//
// WHAT THIS IS NOT. It is not a result. It computes no strain, no stress, no
// von Mises, no principal value, no reaction and no equilibrium. Every scalar
// it reports is READ from a stored field of `RecoveredFields`, which is why
// this header exposes no arithmetic on a `Stress6` and why a grep of the
// implementation for a von Mises formula, an eigensolver or a `sqrt` of a
// stress invariant finds nothing.
//
// IT CONTAINS NO Qt AND NO OCCT. The deformed positions, the channel
// extraction, the classification cardinality, the ranges and the extremes are
// all assertable headlessly, and that is where this milestone is verified --
// pixel comparison is supplemental, because a GPU and a driver are not an
// engineering gate.
//
// THE TWO CARRIED TRAPS THIS FILE IS WRITTEN AGAINST:
//
//   1. UNITS. A `meshing::Node` position and a P17 displacement are both SI
//      METRES. OCCT model space is MILLIMETRES. Everything here stays in
//      metres, exactly as `MeshView::positions()` does, and the single
//      conversion lives where P16-VIZ-001 put it -- `toModelLength` in
//      `src/renderer/occt/OcctMeshPresentation.cpp`, which is
//      `geometry::occt::toModel`, the project's only one. There is no
//      `* 1000.0` in this file or its implementation.
//
//   2. A CLASSIFIER THAT CLASSIFIED NOTHING IS NOT AN ALL-VALID RESULT.
//      P16-QUALITY-001 recorded that `reportOnlyThresholds()` classifies
//      NOTHING, so every element came back Valid and the report looked clean.
//      So `ScalarField` carries `expected`, `classified`, `skipped` and
//      `nonFinite` counts, `complete()` requires a non-empty domain fully
//      classified, and there is no path that produces a usable field from
//      zero entities.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/renderer/Export.hpp>
#include <bettercad/renderer/MeshView.hpp>
#include <bettercad/structural/StructuralPost.hpp>
#include <bettercad/structural/StructuralReaction.hpp>

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

/// Which scalar a contour or an inspection reads.
///
/// ONE ENUMERATION AND ONE DESCRIPTOR TABLE, so a channel's domain, unit and
/// name are stated once. The alternative -- a switch per question, repeated at
/// every call site -- is how `tauYZ` ends up reading `tauZX` in one of five
/// places and nowhere else.
///
/// EVERY VALUE IS BACKED BY A STORED FIELD of `RecoveredFields`. None is
/// derived here.
enum class ResultChannel : std::uint8_t {
    // --- nodal, from NodalDisplacement -------------------------------------
    /// `|u|`, in metres. STORED, not recomputed: `NodalDisplacement::magnitude`
    /// exists precisely "so that a consumer cannot compute it a second,
    /// different way".
    DisplacementMagnitude,
    /// Signed components, in metres. SIGNED, and a contour of them must
    /// preserve the sign -- plotting `abs(Ux)` for Ux is a defect the brief
    /// names explicitly.
    DisplacementX,
    DisplacementY,
    DisplacementZ,

    // --- element, from ElementFields ---------------------------------------
    /// Full-3D von Mises, in pascals. P17-POST-001 validated it against a
    /// deviatoric oracle and against a plane-stress formula it is 11.2% away
    /// from; this reads the stored value.
    VonMises,
    /// The six stress components in `TensorComponent` order, in pascals.
    /// SIGNED -- compression is negative and a range must keep it.
    StressXX,
    StressYY,
    StressZZ,
    StressXY,
    StressYZ,
    StressZX,
    /// Principal stresses, in pascals, DESCENDING as P17-POST produced them:
    /// `sigma1 >= sigma2 >= sigma3`. Not re-sorted here.
    PrincipalStress1,
    PrincipalStress2,
    PrincipalStress3,
    /// `tr(S)/3`, in pascals, normal-stress sign convention.
    HydrostaticStress,
};

[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::string_view toString(ResultChannel channel) noexcept;

/// Which entity a channel is defined on.
///
/// THE DISTINCTION IS NOT COSMETIC. A Tet4's stress state is ELEMENT-CONSTANT
/// -- P17-POST-001 produces one per element, by construction -- and pretending
/// it is nodal would require an averaging rule nobody chose. There is no
/// smoothing anywhere in this module.
enum class ChannelDomain : std::uint8_t {
    /// One value per mesh node, parallel to `RecoveredFields::displacements()`.
    Node,
    /// One value per Tet4, parallel to `RecoveredFields::elements()`.
    Element,
};

[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::string_view toString(ChannelDomain domain) noexcept;

/// What a channel is, stated once.
struct ChannelDescriptor {
    ResultChannel channel = ResultChannel::DisplacementMagnitude;
    ChannelDomain domain = ChannelDomain::Node;
    /// For a legend or a menu.
    std::string_view name;
    /// The SI unit the VALUES are in. Display conversion is the UI's and
    /// happens in a formatter, never here: this module reports metres and
    /// pascals.
    std::string_view unit;
    /// Whether negative values are physically expected.
    ///
    /// Carried so a range check can assert the right thing rather than
    /// guessing: a von Mises minimum below zero is a defect, and a `StressXX`
    /// minimum below zero is ordinary compression.
    bool mayBeNegative = true;

    friend bool operator==(const ChannelDescriptor&, const ChannelDescriptor&) = default;
};

[[nodiscard]] BETTERCAD_RENDERER_EXPORT ChannelDescriptor
descriptorOf(ResultChannel channel) noexcept;

/// Every channel, in enumeration order.
///
/// So a menu, a test sweep and the evidence table all iterate the same list
/// and a channel added to the enum cannot be forgotten by one of them.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::span<const ResultChannel> allResultChannels() noexcept;

/// The value of @p channel on one node, ABSENT if @p channel is not a nodal
/// channel.
///
/// THE PUBLIC NUMERICAL KERNEL of the view, exposed for the reason
/// P17-SOLVE-001 exposed its own: a refusal nothing can reach is a refusal
/// nothing has checked. `field()` chooses between these two overloads by the
/// descriptor's domain and so never takes the absent arm, which would leave
/// that arm, and the `skipped` count that depends on it, permanently
/// unexercised.
///
/// EVERY VALUE IS A READ of a stored member of @p nodal, `magnitude`
/// included -- P17-POST-001 stores that precisely so a consumer cannot norm it
/// a second, different way. No formula of any kind lives behind this call.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::optional<double>
channelValue(ResultChannel channel, const structural::NodalDisplacement& nodal) noexcept;

/// The value of @p channel on one element, ABSENT if @p channel is not an
/// element channel. The sibling of the overload above, with the same contract:
/// a read of a stored member, and no averaging of an element value to the
/// nodes in either direction.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::optional<double>
channelValue(ResultChannel channel, const structural::ElementFields& element) noexcept;

// ---------------------------------------------------------------------------
// A classified scalar field
// ---------------------------------------------------------------------------

/// Why a channel could not be prepared for display.
enum class FieldProblem : std::uint8_t {
    /// The fields do not describe the mesh they were asked about.
    ResultSourceMismatch,
    /// The result's domain is empty -- no nodes, or no elements.
    ///
    /// THE P16 TRAP, AS A REFUSAL. A range over zero entities is not a valid
    /// uniform field; it is a question that was never asked.
    EmptyClassificationDomain,
    /// Fewer entities were classified than the domain holds.
    IncompleteClassification,
    /// A source value is not finite. A qualified current result cannot contain
    /// one -- P17-VALID-001 and `StructuralResult::create` both refuse it --
    /// so this is the contract of a function whose input the library does not
    /// control, and it REFUSES rather than mapping NaN to a colour.
    NonFiniteValue,
    /// The channel has no value on the entity it was asked for.
    ///
    /// ABSENT IS NOT ZERO, and that is the whole point of the value: 0.0 is an
    /// ordinary displacement and an ordinary stress, so a channel answering
    /// 0.0 where it has nothing to say makes a field of silence look like a
    /// valid uniform result.
    ///
    /// Reachable through `channelValue`, by asking a nodal channel for an
    /// element or the reverse. `field()` picks its reader from
    /// `descriptorOf(channel).domain`, so while the one descriptor table and
    /// the two readers agree it cannot arise there -- and the guard is kept
    /// because a channel added to the enum, given a descriptor and forgotten
    /// by its reader would otherwise render as a COMPLETE, UNIFORMLY ZERO
    /// field, which is the P16 trap wearing a different hat.
    ChannelUnavailable,
};

[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::string_view toString(FieldProblem problem) noexcept;

/// One channel, extracted and classified over one result.
///
/// THE CARDINALITY IS PART OF THE ANSWER, not a diagnostic afterthought. A
/// field that classified nothing must be distinguishable from a field that
/// classified everything and found nothing wrong, and `complete()` is the
/// question a caller asks before drawing.
struct ScalarField {
    ResultChannel channel = ResultChannel::DisplacementMagnitude;
    ChannelDomain domain = ChannelDomain::Node;

    /// Entities the domain holds, from the mesh.
    std::size_t expected = 0;
    /// Entities a value was extracted for.
    std::size_t classified = 0;
    /// Entities the channel had no value for, counted by `channelValue`
    /// returning absent.
    ///
    /// `FieldProblem::ChannelUnavailable` says why this cannot arise while the
    /// descriptor table and the two readers agree, and why the count is kept
    /// regardless. It is not a diagnostic afterthought: `complete()` gates on
    /// it, so a field cannot claim to be whole while carrying a skip.
    std::size_t skipped = 0;
    /// Entities whose value was not finite.
    std::size_t nonFinite = 0;

    /// The values, in the domain's own ascending order -- ascending `NodeId`
    /// for a nodal channel, ascending `ElementId` for an element channel,
    /// which is the order `RecoveredFields` stores them in and the order the
    /// mesh enumerates. Never indexed by a raw id.
    std::vector<double> values{};

    /// The entity each value belongs to, parallel to `values`. Exactly one of
    /// the two is populated, according to `domain`.
    ///
    /// CARRIED RATHER THAN RE-DERIVED, so a field is self-contained: an
    /// extreme, a tooltip or a navigation step maps a value back to its
    /// identity without going to the source again and without arithmetic on an
    /// index. A positional index into `RecoveredFields` would be correct for a
    /// full-mesh view and wrong for a boundary view, silently.
    std::vector<meshing::NodeId> nodes{};
    std::vector<meshing::ElementId> elements{};

    /// The range over the ENTITIES ACTUALLY CLASSIFIED, in the channel's SI
    /// unit.
    ///
    /// SEEDED FROM THE FIRST VALUE, never from 0.0. An accumulator seeded at
    /// zero reports 0.0 as the maximum of an all-negative field, which this
    /// project has already been caught by once -- a peak principal stress
    /// under inward pressure came back as its own initialiser.
    double minimum = 0.0;
    double maximum = 0.0;

    /// Where the extremes are. Exactly one of the two id kinds is set,
    /// according to `domain`.
    meshing::NodeId minimumNode{};
    meshing::NodeId maximumNode{};
    meshing::ElementId minimumElement{};
    meshing::ElementId maximumElement{};

    /// Non-empty domain, every entity classified, nothing skipped and nothing
    /// non-finite.
    ///
    /// A FUNCTION OF THE COUNTS, so a field cannot claim to be complete while
    /// carrying a skip. `expected > 0` is the half that closes the P16 trap.
    [[nodiscard]] bool complete() const noexcept {
        return expected > 0 && classified == expected && skipped == 0 && nonFinite == 0;
    }

    /// Whether every classified value is the same.
    ///
    /// A VALID FIELD, not an error: a uniform stress state is ordinary. The
    /// caller must not divide by `maximum - minimum` without asking this.
    [[nodiscard]] bool uniform() const noexcept { return classified > 0 && minimum == maximum; }

    friend bool operator==(const ScalarField&, const ScalarField&) = default;
};

// ---------------------------------------------------------------------------
// The view
// ---------------------------------------------------------------------------

/// A derived, disposable, source-keyed render cache for one structural result.
///
/// KEYED ON BOTH the mesh generation and the assembly source, because either
/// moving invalidates it and they move for different reasons: a remesh changes
/// the `MeshStamp`, while a material or load edit leaves the mesh current and
/// changes what was assembled. `describes()` asks both.
///
/// There is no API to update one in place. A new result means a new view, and
/// the old one answers `describes(...) == false` rather than reinterpreting
/// its buffers against different numbers -- ADR-031's refusal applied to
/// result rendering, exactly as `MeshView` applies it to mesh rendering.
class BETTERCAD_RENDERER_EXPORT StructuralResultView {
public:
    /// Binds @p fields to @p mesh and @p view.
    ///
    /// Refuses when the fields do not describe the mesh, when the mesh view
    /// does not describe the mesh, or when the result's domains are empty.
    /// Nothing is extracted here: channels are prepared on demand, so holding
    /// a view costs the stamps and not a copy of every channel.
    [[nodiscard]] static Result<StructuralResultView> of(const meshing::Mesh& mesh,
                                                         const MeshView& view,
                                                         const structural::RecoveredFields& fields);

    [[nodiscard]] const meshing::MeshStamp& mesh() const noexcept { return mesh_; }
    [[nodiscard]] const structural::AssemblySource& source() const noexcept { return source_; }

    /// Whether this view may be used against @p mesh and @p fields.
    ///
    /// THE REVISION GUARD, and it compares SOURCES rather than counts: two
    /// results of the same mesh can have identical node and element counts and
    /// come from different materials, and a view read across that gap would
    /// map every value onto the wrong physics.
    [[nodiscard]] bool describes(const meshing::Mesh& mesh,
                                 const structural::RecoveredFields& fields) const noexcept;

    [[nodiscard]] std::size_t nodeCount() const noexcept { return nodeCount_; }
    [[nodiscard]] std::size_t elementCount() const noexcept { return elementCount_; }

    /// How many entities @p domain holds. The `expected` of any field on it.
    [[nodiscard]] std::size_t domainSize(ChannelDomain domain) const noexcept;

    /// Extracts and classifies @p channel.
    ///
    /// Reads a STORED field of `RecoveredFields` for every value. Refuses
    /// rather than producing a partial field: an incomplete classification of a
    /// qualified current result is a defect in this layer, not a condition to
    /// render around.
    [[nodiscard]] Result<ScalarField> field(const meshing::Mesh& mesh,
                                            const structural::RecoveredFields& fields,
                                            ResultChannel channel) const;

    // --- the deformed display ----------------------------------------------

    /// Display positions with the displacement applied, in SI METRES.
    ///
    /// ```text
    /// x_display = x_undeformed + scale * u
    /// ```
    ///
    /// ALWAYS FROM THE UNDEFORMED POSITIONS, never from the previous output.
    /// That is what makes repeated toggling and repeated scale changes
    /// idempotent rather than cumulative, and it is why this returns a fresh
    /// buffer instead of mutating one -- there is no accumulator to corrupt.
    ///
    /// STILL METRES. The conversion to OCCT millimetres happens once, in the
    /// presentation adapter, through the project's only conversion. A buffer
    /// of millimetres leaving this header would be a unit the type does not
    /// state.
    ///
    /// Parallel to `MeshView::positions()`: three doubles per vertex, in the
    /// view's vertex order. A boundary view draws only boundary nodes, so this
    /// is as long as that buffer and not as long as the node list.
    ///
    /// Refuses a non-finite or negative @p scale. Negative is refused rather
    /// than silently inverting the deformation, which looks plausible and is
    /// meaningless; zero is allowed and reproduces the undeformed positions
    /// exactly.
    [[nodiscard]] Result<std::vector<double>> deformedPositions(
        const meshing::Mesh& mesh, const MeshView& view,
        const structural::RecoveredFields& fields, double scale) const;

private:
    StructuralResultView() = default;

    meshing::MeshStamp mesh_{};
    structural::AssemblySource source_{};
    std::size_t nodeCount_ = 0;
    std::size_t elementCount_ = 0;
};

/// Whether @p scale is usable as a deformation display factor.
///
/// Finite and non-negative. Exposed so a UI can refuse an entry before it
/// reaches a render path, and so the rule is stated once.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT bool isUsableDeformationScale(double scale) noexcept;

// ---------------------------------------------------------------------------
// Extremes, and what "highest" means
// ---------------------------------------------------------------------------

/// Which extreme a navigation step is asking for.
///
/// THREE DISTINCT CONCEPTS, NAMED, because "highest stress" is ambiguous the
/// moment a channel can be negative: a compressive `StressZZ` of -300 MPa is
/// larger in magnitude than a tensile +200 MPa and smaller as a number. A
/// single "go to highest" would silently pick one meaning, and which one would
/// depend on the channel.
///
/// For `DisplacementMagnitude` and `VonMises` -- the two norms -- all three
/// agree on the maximum, which is why the brief calls those two unambiguous.
enum class ExtremeKind : std::uint8_t {
    /// The largest signed value.
    Maximum,
    /// The smallest signed value. For a compressive field this is the most
    /// severe one.
    Minimum,
    /// The largest `|value|`, whichever sign it has.
    MaximumMagnitude,
};

[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::string_view toString(ExtremeKind kind) noexcept;

/// One extreme of one channel, and the entity holding it.
struct ExtremeEntity {
    ExtremeKind kind = ExtremeKind::Maximum;
    ChannelDomain domain = ChannelDomain::Node;
    /// Set for a nodal channel.
    meshing::NodeId node{};
    /// Set for an element channel.
    meshing::ElementId element{};
    /// The SIGNED source value, even when `kind` is `MaximumMagnitude`: the
    /// magnitude was the search key, not the answer, and reporting `|v|` as
    /// the value would lose the sign a user needs.
    double value = 0.0;
    /// Where it is in the field's arrays.
    std::size_t index = 0;

    friend bool operator==(const ExtremeEntity&, const ExtremeEntity&) = default;
};

/// The @p kind extreme of @p field.
///
/// OPERATES OVER THE WHOLE RESULT DOMAIN, which is the brief's requirement:
/// a maximum computed only over the elements a boundary view happens to draw,
/// and labelled the global maximum, would be wrong for every interior
/// element. `ScalarField` is built over the result's own domain, not over the
/// render buffer, so this is global by construction.
///
/// TIES GO TO THE FIRST ENTITY in the domain's ascending order, which is a
/// property of the traversal rather than of a sort nobody can see. Refuses an
/// incomplete field -- an extreme over a partial classification is the P16
/// trap wearing a different hat.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<ExtremeEntity> extremeOf(const ScalarField& field,
                                                                        ExtremeKind kind);

} // namespace bettercad::renderer
