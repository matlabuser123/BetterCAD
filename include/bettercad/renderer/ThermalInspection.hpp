#pragma once

// P18-VIZ-001 -- result currentness, inspection and playback, all derived from the core.
//
// The sibling of `StructuralInspection.hpp`, which answers the same questions for a P17 result: what is on screen
// relative to the model, what does the user see when they click something, and which stored state is shown.
//
// THERE IS NO GUI STALE FLAG IN THIS FILE, and that is its central claim. `ThermalDisplayState` carries P16's
// `MeshCurrency`, the thermal result's own stale reasons (the solution's comparison, by identity) and the
// validation report's own verdict and currentness, unchanged: a second opinion about currentness is a second
// opinion that can drift, and the one that drifts toward "current" shows a wrong number as right. A
// `bool viewerResultIsStale` appears nowhere, and neither does a timestamp, a count comparison or a guess from
// how similar the geometry looks.
//
// NOTHING HERE COMPUTES AN ENGINEERING VALUE. Every field of every inspection is read from `ThermalPostResult`
// or from the canonical mesh. There is no gradient recovery, no Fourier's law, no balance and no validation rule:
// the validation badge is the core's `ValidationStatus`, shown, never re-derived.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/renderer/Export.hpp>
#include <bettercad/renderer/ThermalResultView.hpp>
#include <bettercad/thermal/ThermalAnalysis.hpp>
#include <bettercad/thermal/ThermalAnalysisObject.hpp>
#include <bettercad/thermal/ThermalData.hpp>
#include <bettercad/thermal/ThermalDof.hpp>
#include <bettercad/thermal/ThermalPost.hpp>
#include <bettercad/thermal/ThermalTransient.hpp>
#include <bettercad/thermal/ThermalValidation.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace bettercad::features {
class Regenerator;
} // namespace bettercad::features

namespace bettercad::renderer {

// ---------------------------------------------------------------------------
// What is on screen, relative to the model
// ---------------------------------------------------------------------------

/// What a held thermal result is, relative to the analysis the document has now.
enum class ThermalResultState : std::uint8_t {
    /// The document has no thermal analysis with that identity.
    NoAnalysis,
    /// The analysis exists, its inputs do not resolve (a stale or missing mesh, an ineligible body) and there is
    /// nothing held to show.
    InputsUnavailable,
    /// The inputs resolve and no result has been computed.
    NoResult,
    /// The held result describes the document as it is now: its recovered source equals the source a result
    /// computed now would carry.
    Current,
    /// A result is held and does not describe the document: a dependency moved. It may be inspected against its
    /// own mesh generation; it is NEVER drawn as current, and it is never put onto a mesh it was not made on.
    Stale,
    /// The most recent solve failed. Any result still held is stale by definition.
    SolveFailed,
};

[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::string_view toString(ThermalResultState state) noexcept;

/// What the validation badge says. The core's verdict, never re-derived, and only while the report is current.
enum class ThermalValidationBadge : std::uint8_t {
    /// No report was presented.
    None,
    Pass,
    PassWithWarnings,
    Fail,
    /// A report is held and no longer describes the model (or was produced under another policy). A stale
    /// verdict is not shown as one: it is shown as stale.
    Stale,
};

[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::string_view toString(ThermalValidationBadge badge) noexcept;

/// The mesh state, the result state and the validation state, side by side and never merged.
struct ThermalDisplayState {
    /// P16's verdict on the mesh.
    meshing::MeshCurrency mesh = meshing::MeshCurrency::NoMesh;
    /// The result's currentness, from the core's own comparison.
    ThermalResultState result = ThermalResultState::NoAnalysis;
    /// The input problem `requireThermalModel` reports, when the inputs do not resolve.
    std::optional<thermal::InputProblem> inputProblem{};
    /// WHICH dependencies of the held result moved, in the core's order (a SET, not the first mismatch). Empty
    /// when the result is current, when there is none, and when the inputs do not resolve at all (then
    /// `inputProblem` is the reason).
    std::vector<thermal::TransientStaleReason> staleReasons{};
    /// The core's diagnostic for a failed solve, when there was one.
    std::optional<Error> failure{};
    ThermalValidationBadge badge = ThermalValidationBadge::None;
    /// The report's status while the report is current.
    std::optional<thermal::ValidationStatus> validation{};

    [[nodiscard]] bool meshDescribesModel() const noexcept { return meshing::describesTheModel(mesh); }

    /// Whether a result may be drawn WITHOUT a stale marker: ONLY `Current`. The predicate the hardest
    /// requirement rests on.
    [[nodiscard]] bool resultMayDrawAsCurrent() const noexcept { return result == ThermalResultState::Current; }

    /// Whether a result exists at all and can be looked at, stale included. Retaining a historical result and
    /// MARKING it is a legitimate choice; showing it as current is not.
    [[nodiscard]] bool resultInspectable() const noexcept {
        return result == ThermalResultState::Current || result == ThermalResultState::Stale ||
               result == ThermalResultState::SolveFailed;
    }

    /// Whether the canonical loads, conditions and boundary sets can be drawn: they are intent, and need only a
    /// model that resolves. They stay visible before any solve and beside a stale result.
    [[nodiscard]] bool intentMayDraw() const noexcept { return meshDescribesModel() && !inputProblem.has_value(); }

    friend bool operator==(const ThermalDisplayState&, const ThermalDisplayState&) = default;
};

/// The display state of @p analysis, derived from @p document.
///
/// @p post, @p lastFailure and @p report are what the application holds for this analysis; each may be null. The
/// currentness of @p post is `isCurrent(post, ...)` -- the same identity comparison POST's own entry points use --
/// and the report's is `thermal::isCurrent(report, ...)`.
///
/// DETERMINISTIC: no wall clock, no pointer identity, no unordered iteration.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT ThermalDisplayState
thermalDisplayState(const Document& document, const features::Regenerator& regenerator,
                    const meshing::Mesher& mesher, ThermalAnalysisId analysis,
                    const thermal::ThermalPostResult* post, const Error* lastFailure,
                    const thermal::ThermalValidationReport* report);

/// Whether the result fields can be shown, and if not why.
enum class ThermalFieldAvailability : std::uint8_t {
    Available,
    /// No result exists (and no solve failed).
    NoResult,
    /// A result is held and is stale.
    StaleResult,
    /// A current result whose validation FAILED is not presented as an accepted analysis.
    InvalidResult,
    /// The latest solve failed.
    SolveFailed,
};

[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::string_view toString(ThermalFieldAvailability availability) noexcept;

/// The availability of the contours, vectors, extrema and playback in @p state. A warning does NOT make fields
/// unavailable (`PassWithWarnings` is an accepted result); a stale one does.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT ThermalFieldAvailability
thermalFieldAvailability(const ThermalDisplayState& state) noexcept;

/// The banner text for @p availability: empty when available. NOT a tooltip: it is drawn.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::string_view thermalBanner(ThermalFieldAvailability availability) noexcept;

// ---------------------------------------------------------------------------
// Inspection
// ---------------------------------------------------------------------------

/// Everything the UI shows for one selected node. EVERY FIELD IS READ.
struct ThermalNodeInspection {
    meshing::NodeId node{};
    /// The compact index of the node in the result's arrays. Sparse `NodeId`s never size anything.
    thermal::ThermalDofIndex dof = thermal::ThermalDofIndex::fromValue(0);
    /// The canonical position, engineering, unit in the type.
    Point3D position{};
    /// POST's stored value, K, unrounded. Formatting is the UI's job.
    Temperature temperature{};
    std::size_t state = 0;
    /// The stored time of the state; nullopt for a steady result.
    std::optional<Time> time{};
    /// Whether the result was current when inspected. A stale inspection says so.
    ThermalResultState resultState = ThermalResultState::Current;

    friend bool operator==(const ThermalNodeInspection&, const ThermalNodeInspection&) = default;
};

/// Everything the UI shows for one selected tetrahedron. EVERY FIELD IS READ; the norm is the display scalar.
struct ThermalElementInspection {
    meshing::ElementId element{};
    /// The compact ordinal in the result's element arrays. A sparse `ElementId` never indexes anything.
    std::size_t ordinal = 0;
    std::array<meshing::NodeId, 4> nodes{};
    /// grad(T), K/m: POST's stored vector.
    thermal::TemperatureGradient3 gradient{};
    /// The display norm of that vector, K/m (`gradientDisplayNorm`).
    TemperatureGradient gradientMagnitude{};
    /// q = -k grad(T), W/m^2: POST's stored vector, NOT sign-flipped and not normalised.
    thermal::HeatFlux3 heatFlux{};
    /// POST's stored |q|, W/m^2.
    HeatFlux heatFluxMagnitude{};
    std::size_t state = 0;
    std::optional<Time> time{};
    ThermalResultState resultState = ThermalResultState::Current;

    friend bool operator==(const ThermalElementInspection&, const ThermalElementInspection&) = default;
};

/// The inspection of @p node at stored state @p state.
///
/// Refuses when @p post does not describe @p map, when @p map does not describe @p mesh, when @p node is not a node
/// of the mesh, or when @p state is out of range. The temperature is found THROUGH THE NUMBERING, never by
/// `NodeId::value()`. @p resultState is what the application knows about the result's currentness
/// (`ThermalDisplayState::result`); a result that is not inspectable is refused.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<ThermalNodeInspection>
inspectThermalNode(const meshing::Mesh& mesh, const thermal::ThermalDofMap& map,
                   const thermal::ThermalPostResult& post, std::size_t state, meshing::NodeId node,
                   ThermalResultState resultState = ThermalResultState::Current);

/// The inspection of @p element at stored state @p state, found through POST's `ordinalOf` -- never by the raw id.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<ThermalElementInspection>
inspectThermalElement(const meshing::Mesh& mesh, const thermal::ThermalPostResult& post, std::size_t state,
                      meshing::ElementId element, ThermalResultState resultState = ThermalResultState::Current);

/// Where the camera should look for @p extreme: the node's position, or the tetrahedron's centroid, in SI metres.
///
/// NAVIGATION IS A CAMERA AND A SELECTION AND NOTHING ELSE. The extreme was searched in the source values; this only
/// answers where that entity IS, by identity, from the canonical mesh. Refuses a node or an element the mesh does not hold --
/// an extreme from another mesh generation names nothing here.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<Point3D> thermalNavigationTarget(const meshing::Mesh& mesh,
                                                                            const ThermalExtreme& extreme);

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

/// The frame cursor of a transient result: VIEW STATE, nothing more.
///
/// A frame IS a stored state, and its time IS the stored time of that state -- never `frame * dt`, because the
/// last step of a run is a shortened remainder and an output event can fall between nominal steps. There is no
/// interpolation: a frame is an accepted state or it does not exist. The playback RATE is a wall-clock matter for
/// the GUI scheduler and does not appear here; what a test can hold is which frame is selected.
class BETTERCAD_RENDERER_EXPORT ThermalPlayback {
public:
    /// Binds a playback to @p post. Refuses a transient result whose stored times are not finite and STRICTLY
    /// increasing (the core guarantees it; the viewer re-sorts nothing and accepts nothing else), and a result
    /// with no state. A steady result has one frame and no time.
    [[nodiscard]] static Result<ThermalPlayback> of(const thermal::ThermalPostResult& post);

    [[nodiscard]] std::size_t frameCount() const noexcept { return times_.size(); }
    /// The selected frame -- an index of the result's stored states.
    [[nodiscard]] std::size_t frame() const noexcept { return frame_; }
    /// The stored time of the selected frame in seconds; nullopt for a steady result.
    [[nodiscard]] std::optional<double> time() const noexcept;
    /// The stored time of frame @p frame, or nullopt for a steady result or an out-of-range frame.
    [[nodiscard]] std::optional<double> timeOf(std::size_t frame) const noexcept;
    [[nodiscard]] bool isSteady() const noexcept { return !transient_; }
    [[nodiscard]] const thermal::ThermalPostSource& source() const noexcept { return source_; }

    /// Whether this playback was bound to @p post. By source, by value: a new result needs a new playback.
    [[nodiscard]] bool describes(const thermal::ThermalPostResult& post) const noexcept {
        return post.source() == source_ && post.stateCount() == times_.size();
    }

    /// Selects @p frame. Refuses an out-of-range frame.
    [[nodiscard]] Result<void> select(std::size_t frame);
    void first() noexcept { frame_ = 0U; }
    /// Advances; false (and no change) at the last frame unless @p loop, which wraps to the first.
    bool next(bool loop = false) noexcept;
    /// Steps back; false (and no change) at the first frame unless @p loop, which wraps to the last.
    bool previous(bool loop = false) noexcept;

private:
    ThermalPlayback() = default;

    thermal::ThermalPostSource source_{};
    bool transient_ = false;
    /// One per stored state. Seconds; for a steady result a single placeholder that `time()` never reports.
    std::vector<double> times_{};
    std::size_t frame_ = 0;
};

} // namespace bettercad::renderer
