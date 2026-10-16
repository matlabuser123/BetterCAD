#pragma once

// P18-DATA-001 -- the derived thermal result, the one place that decides
// whether it still describes the model, and the lifecycle of a held result.
//
// A result is DERIVED STATE in exactly the sense ADR-030 gave the word for a
// mesh and P17-DATA-001 gave it for a structural result: reproducible from
// canonical intent, never persisted as authority, regenerated rather than
// restored on reload. What makes that safe is that a result carries the
// identity of everything it was computed from, so it can say "I am not about
// this document any more" instead of being quietly read as though it were.
//
// CURRENTNESS COMPARES SOURCE IDENTITY, NEVER ARRAYS. Two results with
// identical temperatures on two generations of a mesh are a current one and a
// stale one; two results with different temperatures on one generation are
// both current. Comparing values would make a remesh that happened to
// reproduce every number look current, and comparing sizes would make every
// remesh of a block look current. Neither is consulted.
//
// ONE PLACE DECIDES. `resultCurrency` and `analysisState` are the only
// currentness answers in P18. A viewer that compared node counts and a CLI
// that compared an analysis ID would be two answers that drift, and the one
// that drifts towards "current" ships a wrong temperature.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>
#include <bettercad/thermal/Export.hpp>
#include <bettercad/thermal/ThermalAnalysisObject.hpp>
#include <bettercad/thermal/ThermalData.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace bettercad::features {
class Regenerator;
} // namespace bettercad::features

namespace bettercad::thermal {

/// The solution of one thermal analysis on one mesh, at one time level.
///
/// IMMUTABLE, AND CONSTRUCTED ONLY BY A VALIDATING FACTORY. Every accessor is
/// const and returns a view or a value; there is no setter, and there is no
/// way to reach the source and rewrite it. A result whose provenance could be
/// edited would let stale output be made to look current without recomputing
/// anything, which is the one failure mode this design is built against.
///
/// HOW THE ARRAYS ARE KEYED:
///
/// ```text
/// temperatures    DENSE, parallel to mesh.nodes(). Entry i is the
///                 temperature of mesh.nodes()[i], in K. Every node has one.
/// gradients       DENSE, parallel to mesh.tetrahedra(). Constant within a
///                 Tet4, so one per element. In K/m.
/// heatFluxes      DENSE, likewise. In W/m^2, pointing where heat flows.
/// heatBalance     ONE, for the whole field.
/// time            NONE for a steady result; the sample time, in s, for a
///                 transient one.
/// ```
///
/// NOT INDEXED BY RAW NodeId. P16 allocates node IDs from 1 and a builder may
/// choose them, so nothing guarantees 0..N-1. The mesh's own ascending
/// enumeration is the access model, and `temperatureOf` does the lookup so no
/// caller has to assume anything about the numbering.
///
/// STORAGE IS O(nodes + elements) AND THE OBJECT IS A FIXED SIZE. The mesh is
/// not copied in; a result records the `MeshStamp` of the mesh it belongs to.
///
/// A TRANSIENT HISTORY IS A SEQUENCE OF THESE, one per kept time level, all
/// sharing one source. The container for that sequence is P18-TRANSIENT-001's;
/// what this type fixes is that every sample is a complete, validated field
/// bound to its mesh and its time.
class BETTERCAD_THERMAL_EXPORT ThermalResult {
public:
    /// Builds a result, or fails saying which invariant it broke.
    ///
    /// Checks, all of them against @p mesh and @p source rather than against
    /// a promise:
    ///
    /// ```text
    /// the source names this exact mesh        source.mesh == mesh stamp
    /// the mesh is not empty
    /// one temperature per node                size == nodeCount
    /// one gradient and one flux per Tet4      size == tetrahedronCount
    /// every temperature finite and >= 0 K     the one absolute-temperature rule
    /// every gradient and flux finite
    /// every balance term finite
    /// steady:    no time, no stored-energy rate
    /// transient: a finite time >= 0 s, and a stored-energy rate present
    /// ```
    ///
    /// A failed solve must not call this with half its channels filled: a
    /// partially populated result is not a worse result, it is a result that
    /// claims to be a solution and is not. The solve publishes a result only
    /// on success and records the failure otherwise; see `ThermalResultSlot`.
    [[nodiscard]] static Result<ThermalResult> create(ThermalResultSource source,
                                                      const meshing::VolumeMesh& mesh,
                                                      std::vector<Temperature> temperatures,
                                                      std::vector<TemperatureGradient3> gradients,
                                                      std::vector<HeatFlux3> heatFluxes,
                                                      HeatBalance heatBalance,
                                                      std::optional<Time> time);

    /// What this was computed from. There is no non-const overload, by design.
    [[nodiscard]] const ThermalResultSource& source() const noexcept { return source_; }

    /// Temperature of `mesh.nodes()[i]`, in K. Length == node count.
    [[nodiscard]] std::span<const Temperature> temperatures() const noexcept { return temperatures_; }

    /// grad(T) of `mesh.tetrahedra()[i]`, in K/m.
    [[nodiscard]] std::span<const TemperatureGradient3> gradients() const noexcept {
        return gradients_;
    }

    /// Heat flux of `mesh.tetrahedra()[i]`, in W/m^2, in the direction of flow.
    [[nodiscard]] std::span<const HeatFlux3> heatFluxes() const noexcept { return heatFluxes_; }

    /// The global balance of this field, in W.
    [[nodiscard]] const HeatBalance& heatBalance() const noexcept { return heatBalance_; }

    /// The time this field is at, in s; nullopt for a steady result.
    [[nodiscard]] std::optional<Time> time() const noexcept { return time_; }

    /// Whether this result belongs to @p mesh. Compares stamps, never counts:
    /// two generations of one body's mesh can have identical node and element
    /// counts, identical NodeIds, and be different meshes.
    [[nodiscard]] bool describes(const meshing::VolumeMesh& mesh) const noexcept;

    /// The temperature of @p node, or an error if it is not @p mesh's node or
    /// @p mesh is not this result's mesh. Binary search over the mesh's
    /// ascending nodes; no assumption about dense IDs.
    [[nodiscard]] Result<Temperature> temperatureOf(const meshing::VolumeMesh& mesh,
                                                    meshing::NodeId node) const;

private:
    ThermalResult(ThermalResultSource source, std::vector<Temperature> temperatures,
                  std::vector<TemperatureGradient3> gradients, std::vector<HeatFlux3> heatFluxes,
                  HeatBalance heatBalance, std::optional<Time> time) noexcept;

    ThermalResultSource source_{};
    std::vector<Temperature> temperatures_{};
    std::vector<TemperatureGradient3> gradients_{};
    std::vector<HeatFlux3> heatFluxes_{};
    HeatBalance heatBalance_{};
    std::optional<Time> time_{};
};

// ---------------------------------------------------------------------------
// Currentness
// ---------------------------------------------------------------------------

/// What a held result is, relative to the document as it is now.
enum class ResultCurrency : std::uint8_t {
    /// Nothing has been solved.
    NoResult,
    /// Every dependency matches what it was computed from.
    Current,
    /// At least one dependency moved. `staleReasons` says which.
    Stale,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(ResultCurrency currency) noexcept;

/// Whether @p currency means the result may be read as describing the model.
/// Only `Current` does. No export macro: defined here, so in a shared build
/// the macro would expand to dllimport on an inline function.
[[nodiscard]] constexpr bool describesTheModel(ResultCurrency currency) noexcept {
    return currency == ResultCurrency::Current;
}

/// The source a result computed NOW would carry.
///
/// Fails for exactly the reasons `requireThermalModel` fails, because it
/// calls it: every gate ADR-051 established is inherited rather than
/// re-asked. On top of that it reads the analysis, its revision and mode,
/// and the material's identity and revision, which the input boundary has no
/// reason to know about.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<ThermalResultSource>
currentResultSource(const Document& document, const features::Regenerator& regenerator,
                    const meshing::Mesher& mesher, ThermalAnalysisId analysis);

/// Compares @p result's provenance against @p current. Identity only; no
/// array, no count, no value is read.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT ResultCurrency
resultCurrency(const ThermalResult* result, const ThermalResultSource& current);

/// Where one thermal analysis stands.
///
/// DERIVED, NEVER STORED. Every value is a function of the document, the
/// current mesh state and what is held for the analysis, so there is no
/// cached flag to drift out of agreement with the thing it describes.
enum class AnalysisState : std::uint8_t {
    /// The document has no thermal analysis with that id.
    NoAnalysis,
    /// The analysis is defined but its inputs do not resolve: the geometry is
    /// ineligible, no mesh is current, or the material is missing or cannot
    /// conduct. `thermalInputProblem` says which. The brief's "defined" state:
    /// a definition exists and nothing can yet be computed from it.
    InputsUnavailable,
    /// Every input resolves and nothing has been solved yet.
    Ready,
    /// A result exists and every dependency matches.
    SolvedCurrent,
    /// A result exists and at least one dependency moved. It is historical:
    /// readable as what the model WAS, never as what it is.
    SolvedStale,
    /// The last solve attempt failed. Its own state whether or not an older
    /// result survives: that result is still stale, and reporting it as
    /// merely stale would hide that the attempt to replace it failed. A
    /// failure is never a current result and never makes a stale one current.
    SolveFailed,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(AnalysisState state) noexcept;

/// Evaluates the state of @p analysis.
///
/// @p lastResult and @p lastFailure are what a solver service holds for this
/// analysis; both may be null. `ThermalResultSlot` is the type that holds
/// them with the right lifecycle, and passing its two accessors here is the
/// intended use.
///
/// DETERMINISTIC: no wall clock, no pointer identity, no unordered iteration.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT AnalysisState
analysisState(const Document& document, const features::Regenerator& regenerator,
              const meshing::Mesher& mesher, ThermalAnalysisId analysis,
              const ThermalResult* lastResult, const Error* lastFailure);

// ---------------------------------------------------------------------------
// The result lifecycle
// ---------------------------------------------------------------------------

/// What a solver service holds for one analysis: at most one result and at
/// most one failure, with the transitions that keep them honest.
///
/// ```text
/// recordSuccess(result)   the result replaces whatever was held; the failure
///                         is cleared. The only way a result becomes held.
/// recordFailure(error)    the failure is held; the result, if any, is KEPT
///                         UNTOUCHED. A failed attempt does not discard the
///                         last good field -- a user may still want to see
///                         what the model was -- and it does not touch that
///                         field's source, so it cannot make a stale result
///                         current.
/// reset()                 nothing held.
/// ```
///
/// Nothing here consults the document: currentness is `analysisState`'s to
/// decide from the slot's two accessors, and the slot never caches it.
class BETTERCAD_THERMAL_EXPORT ThermalResultSlot {
public:
    ThermalResultSlot() = default;

    /// The last successful result, or nullptr. Stale or current is not the
    /// slot's to say.
    [[nodiscard]] const ThermalResult* result() const noexcept {
        return result_.has_value() ? &*result_ : nullptr;
    }

    /// The last attempt's failure, or nullptr if the last attempt succeeded
    /// or none was made.
    [[nodiscard]] const Error* failure() const noexcept {
        return failure_.has_value() ? &*failure_ : nullptr;
    }

    void recordSuccess(ThermalResult result);
    void recordFailure(Error failure);
    void reset() noexcept;

private:
    std::optional<ThermalResult> result_{};
    std::optional<Error> failure_{};
};

} // namespace bettercad::thermal
