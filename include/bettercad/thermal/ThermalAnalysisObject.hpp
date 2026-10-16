#pragma once

// P18-DATA-001 -- the canonical thermal analysis as a document object, and the
// provenance record a result carries back to it.
//
// WHY A DOCUMENT OBJECT. The argument ADR-030 made for a meshing control and
// P17-DATA-001 repeated for a structural analysis: a revision, undo and redo,
// a place in the dependency graph and (later) a file representation all come
// from machinery every other document object already uses, and only a graph
// node can express "editing the body reaches this analysis".
//
// WHAT IT HOLDS, AND WHAT IT CANNOT. A `ThermalAnalysisDefinition` is intent
// and nothing else: which control, which mode, the heat loads, the boundary
// conditions, the initial condition, the solver and transient settings. There
// is no temperature in it, no gradient, no flux, no matrix, no DOF map and no
// mesh handle. `ThermalResult.hpp` is not included here, so a result cannot
// even be NAMED from the definition's header -- the property
// `ThermalData_TheDefinitionHoldsOnlyIntent` asserts by size and by
// non-constructibility, and the compile-fail group asserts by trying.
//
// MODE-COMPLETE BY VALIDATION. A steady definition must not carry an initial
// condition or transient settings, and a transient one must carry both. Not a
// convenience: an inert field in canonical intent is one whose edit moves the
// revision and stales a result for nothing, and a missing required field is a
// definition the solver would have to default. `setDefinition` replaces the
// whole definition, so switching mode is one edit either way.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/meshing/GeometryPreparation.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/thermal/Export.hpp>
#include <bettercad/thermal/ThermalData.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad {
class Document;
} // namespace bettercad

namespace bettercad::thermal {

/// Everything a user decides about one thermal analysis. Intent only.
///
/// `loads` and `boundaryConditions` keep the order the user gave them; that
/// order is canonical state like the rest, is compared by `operator==` and
/// will be persisted as given. Identities within each collection must be
/// unique, and `validate` refuses a duplicate.
struct ThermalAnalysisDefinition {
    /// The meshing control whose current mesh is analysed.
    MeshControlId mesh{};
    ThermalAnalysisMode mode = ThermalAnalysisMode::Steady;
    std::vector<ThermalLoad> loads{};
    std::vector<ThermalBoundaryCondition> boundaryConditions{};
    /// Required for a transient analysis; must be absent for a steady one.
    std::optional<UniformInitialTemperature> initialCondition{};
    ThermalSolverSettings solver{};
    /// Required for a transient analysis; must be absent for a steady one.
    std::optional<TransientSettings> transient{};

    friend bool operator==(const ThermalAnalysisDefinition&, const ThermalAnalysisDefinition&) = default;
};

/// Checks a definition on its own terms: a valid control handle, valid solver
/// settings, every load and boundary condition valid with unique identities,
/// and the mode's required fields present and its forbidden ones absent.
///
/// Does not consult the document. Whether the control exists, the faces
/// resolve or the material can carry the mode are questions about a document
/// and a mesh, answered by `requireThermalModel` and the later gates.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<void> validate(const ThermalAnalysisDefinition& definition);

/// The canonical thermal analysis: a document object holding a definition.
///
/// Its revision moves on every effective `setDefinition`, which is what lets
/// a result say "the analysis I was computed from has since been edited"
/// without comparing definitions field by field.
class BETTERCAD_THERMAL_EXPORT ThermalAnalysis final : public DocumentObject {
public:
    static constexpr std::string_view kTypeName = "thermal-analysis";

    /// The only way to make one, so an invalid definition never enters a
    /// document.
    [[nodiscard]] static Result<std::unique_ptr<ThermalAnalysis>>
    create(std::string name, ThermalAnalysisDefinition definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;

    /// This object's identity, narrowed to its own kind.
    [[nodiscard]] ThermalAnalysisId analysisId() const noexcept {
        return ThermalAnalysisId::fromValue(id().value());
    }

    [[nodiscard]] const ThermalAnalysisDefinition& definition() const noexcept { return definition_; }

    /// Replaces the definition. Returns false, and moves no revision, when
    /// the new definition equals the old one: re-setting the same value is not
    /// an edit and must not stale a result. Call through
    /// `Document::modifyObject` so the change is tracked.
    [[nodiscard]] Result<bool> setDefinition(ThermalAnalysisDefinition definition);

private:
    ThermalAnalysis(std::string name, ThermalAnalysisDefinition definition);
    ThermalAnalysis(const ThermalAnalysis&) = default;

    ThermalAnalysisDefinition definition_{};
};

/// The analysis with @p id, or nullptr if there is none or the object with
/// that number is of another kind.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT const ThermalAnalysis*
findThermalAnalysis(const Document& document, ThermalAnalysisId id) noexcept;

/// Every thermal analysis in @p document, ascending by identity. The one
/// canonical order, whatever order the objects were added in.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::vector<ThermalAnalysisId>
thermalAnalysisIds(const Document& document);

// ---------------------------------------------------------------------------
// Provenance
// ---------------------------------------------------------------------------

/// The identity of everything a thermal result was computed from.
///
/// A REVISION TUPLE, NOT A HASH. Separate fields so that a stale result can
/// say WHICH dependency moved, and so that nothing here is derived from bytes:
/// no pointer, no address, no padding, no timestamp, no iteration order. Two
/// solves of the same document give the same source in every preset.
///
/// Field-complete equality by `= default`, so a dependency added here without
/// being compared is not possible.
///
/// ```text
/// body, control        which geometry and which meshing intent, by identity
/// geometry             P16's geometry revision: moves on a CAD edit, NOT on a
///                      material edit, which is why material is its own field
/// mesh                 WHICH MESH AND WHICH GENERATION. A remesh of the same
///                      body with the same counts and the same NodeIds has a
///                      different stamp, and that alone makes a result stale
/// material + revision  which material, and whether k, rho or cp was edited
/// analysis + revision  which analysis, and whether its definition -- loads,
///                      boundary conditions, initial condition, solver or
///                      transient settings, mode -- was edited
/// mode                 what kind of field this is, so a consumer can tell a
///                      transient sample from a steady solution without the
///                      document. Redundant with analysisRevision for
///                      currentness, and reported separately when it differs
/// ```
struct ThermalResultSource {
    ObjectId body{};
    MeshControlId control{};
    meshing::GeometryRevision geometry{};
    meshing::MeshStamp mesh{};
    MaterialId material{};
    std::uint64_t materialRevision = 0;
    ThermalAnalysisId analysis{};
    std::uint64_t analysisRevision = 0;
    ThermalAnalysisMode mode = ThermalAnalysisMode::Steady;

    friend bool operator==(const ThermalResultSource&, const ThermalResultSource&) = default;
};

/// Which dependency of a result no longer matches the document.
enum class StaleReason : std::uint8_t {
    Body,
    Control,
    Geometry,
    Mesh,
    Material,
    Analysis,
    Mode,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(StaleReason reason) noexcept;

/// Every field of @p result's source that differs from @p current, in the
/// enum's order. Empty when the two are equal.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::vector<StaleReason>
staleReasons(const ThermalResultSource& result, const ThermalResultSource& current);

} // namespace bettercad::thermal
