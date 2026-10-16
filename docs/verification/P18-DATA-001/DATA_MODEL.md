# P18-DATA-001 — the data model

```text
SUBJECT:  every type this milestone adds, what it is for, and what it refuses
FILES:    include/bettercad/core/Id.hpp                     3 tags, 3 aliases
          include/bettercad/core/units/Dimension.hpp        4 dimensions
          include/bettercad/core/units/Units.hpp            4 quantities, 4 units
          include/bettercad/core/units/Literals.hpp         8 literals
          src/core/units/UnitCatalog.cpp                    4 catalog entries
          include/bettercad/thermal/ThermalData.hpp         canonical records,
                                                            settings, derived
                                                            value types
          include/bettercad/thermal/ThermalAnalysisObject.hpp  definition,
                                                            document object,
                                                            provenance
          include/bettercad/thermal/ThermalResult.hpp       result, currentness,
                                                            state, slot
          src/thermal/ThermalData.cpp, ThermalAnalysisObject.cpp,
            ThermalResult.cpp
```

## The audit that preceded it

The brief required the existing data architecture to be audited before any
type was defined. The identity audit is in IDENTITIES.md; the rest:

| Concern | Existing mechanism | Reusable? | P18 decision | Reason |
| --- | --- | --- | --- | --- |
| Source fingerprint | `structural::StructuralResultSource`: a revision tuple of eight fields, `= default` equality | Pattern yes, type no (layer 45 cannot include 50, and a thermal result is not about a structural analysis) | `thermal::ThermalResultSource`, the same eight fields plus `mode` | Separate fields say WHICH dependency moved; a hash cannot. Sharing the type would couple two phases through one struct whose fields differ |
| Currentness | `resultCurrency(result*, current)`, `analysisState(...)`, `staleReasons` | Pattern yes | The same three functions in `thermal`, with `StaleReason::Mode` added | One place decides, and it decides by source equality |
| Diagnostics | `Result<T>` / `makeError(ErrorCode, message)`; `toString` per enum; `std::formatter<Id>` | Yes, unchanged | Every refusal is a `Result` with a message naming the record by its ID and the offending value | The project's one error mechanism |
| Deterministic ordering | `Mesh` ascending `NodeId`; `structuralAnalysisIds` sorted; `staleReasons` in enum order | Yes | Same three rules; definition collections keep user order | See DETERMINISM.md |
| Document object | `DocumentObject` with `revision()`, `clone()`, `contentEquals()`, `dependencies()`; `Document::modifyObject` | Yes | `ThermalAnalysis final : DocumentObject`, `kTypeName "thermal-analysis"`, `setDefinition -> Result<bool>` with no-op detection | The machinery every object uses: revision, undo, graph, later a file representation |
| Input gates | `requireThermalModel` (ADR-051) | Yes | `currentResultSource` calls it | Every gate inherited, none re-asked |
| Material revision | `features::requireEffectiveMaterial`, `Material::revision()` | Yes | Read into the source | A k edit must stale a result and not the mesh |
| Result container | `StructuralResult`: immutable, validating factory, dense arrays parallel to the mesh, stamp binding, binary search by `NodeId` | Pattern yes | `ThermalResult`, same shape, scalar field | Node IDs are not assumed dense; counts are not identity |
| Solver settings | `structural::SolverSettings` {algorithm, two tolerances} (ADR-047: canonical) | Pattern yes; the enum no | `ThermalSolverSettings` {two tolerances}; **no algorithm selector** | P18-SOLVE-001 decides how P17's solver is reached from below layer 50; a selector with one legal value would be a false capability |
| Face targets | `FaceName` + `validate(FaceSelector)` in core | Yes | Every face load and BC stores a `FaceName`; `validate` asks core | One classification of geometry |
| Load/BC record shape | `StructuralLoad` (variant of typed payloads), `StructuralRestraint` (one payload) | Pattern yes | `ThermalLoad` variant of 2, `ThermalBoundaryCondition` variant of 2 | Different units per payload: a shared `double value` would let one be read as the other |
| Command history | `CommandHistory` stores commands; `AddObjectCommand` clones | Yes, unchanged | Nothing added; pinned by test | No snapshot exists to capture a result |
| Persistence | `io::documentToJson` refuses an unknown type by name | Unchanged until P18-PERSIST-001 | Nothing added; refusal pinned by test | A document with a thermal analysis cannot be saved, and says so |
| Units | `Quantity<Dimension>`, `Unit`, literals, `unitCatalog()` | Yes | 4 dimensions, 4 quantities, 4 units, 8 literals, 4 catalog entries | See UNITS.md |

## Canonical records

```cpp
enum class ThermalAnalysisMode : uint8_t { Steady, Transient };

struct SurfaceHeatFluxLoad      { FaceName face; HeatFlux flux; };            // W/m^2, into body +
struct VolumetricHeatSourceLoad { VolumetricHeatSource generation; };         // W/m^3
class  ThermalLoad              { ThermalLoadId id; variant<...> payload; };  // kind(), target()

struct PrescribedTemperature    { FaceName face; Temperature value; };        // K
struct ConvectionBoundary       { FaceName face; HeatTransferCoefficient coefficient; Temperature ambient; };
class  ThermalBoundaryCondition { ThermalBoundaryConditionId id; variant<...> payload; };  // kind(), face()

struct UniformInitialTemperature { Temperature value; };                      // K

struct ThermalSolverSettings { double relativeResidualTolerance = 1e-9; double pivotFloor = 1e-12; };
enum class TransientOutputPolicy : uint8_t { FinalOnly, EveryStep };
struct TransientSettings { Time timeStep; Time endTime; TransientOutputPolicy output; };

struct ThermalAnalysisDefinition {
    MeshControlId mesh;  ThermalAnalysisMode mode;
    std::vector<ThermalLoad> loads;  std::vector<ThermalBoundaryCondition> boundaryConditions;
    std::optional<UniformInitialTemperature> initialCondition;
    ThermalSolverSettings solver;  std::optional<TransientSettings> transient;
};
```

### Validation, each record on its own terms

`validate(ThermalLoad)`, `validate(ThermalBoundaryCondition)`,
`validate(UniformInitialTemperature)`, `validate(ThermalSolverSettings)`,
`validate(TransientSettings)` and `validate(ThermalAnalysisDefinition)`,
which calls all of the above. None consults a mesh or a document: whether a
face RESOLVES is P18-LOAD-001's and P18-BC-001's question; whether a record
is well formed is this one's. `ThermalAnalysis::create` and
`ThermalAnalysis::setDefinition` call `validate(definition)`, so an invalid
definition never enters a document.

| Rule | Refused value | Diagnostic names |
| --- | --- | --- |
| a load or BC needs a valid identity | the default `Id` | the kind |
| a face target needs a valid selector | e.g. a `Side` with no entity | core's message |
| a flux, generation or balance term is finite | NaN, +-inf | the record's ID |
| a temperature is absolute | NaN, +-inf, below 0 K | the ID and the value in K |
| a convection coefficient is finite and positive | 0, negative, NaN, inf | the ID and the value |
| solver tolerances are finite and positive | 0, negative, NaN, inf | the value |
| `0 < timeStep <= endTime`, both finite | 0, negative, NaN, inf, step > span | the values in s |
| identities unique within a collection | a repeated `ThermalLoadId` or `ThermalBoundaryConditionId` | the repeated ID |
| **steady**: no initial condition, no transient settings | either present | "inert intent whose edit stales a result for nothing" |
| **transient**: initial condition and transient settings present and valid | either absent | which is missing |

### Why mode-completeness is strict

An inert field in canonical intent is one whose edit moves the revision and
stales a result for nothing; a missing required field is a definition the
solver would have to default. `setDefinition` replaces the whole definition,
so switching mode is one edit either way, and refusing the inert half costs
the user nothing. Measured: `ThermalData_TheDefinitionIsValidatedPerMode`.

### Why no algorithm selector, no method enum, no per-node initial field

Each would be a field with one legal value or a mesh-local field inside
canonical intent. ADR-049 fixes backward Euler, a fixed step and a uniform
initial temperature; P18-SOLVE-001 fixes how the linear system is solved. A
later authorization adds a field and extends the size mirror, which is where
its authority is stated.

## The document object

`ThermalAnalysis final : DocumentObject`, `kTypeName = "thermal-analysis"`.
`create` validates; `clone()` uses the copy constructor so the ID survives a
redo (P16-CMD-001's probe, inherited); `contentEquals` compares definitions;
`dependencies()` is the mesh control, so a body edit reaches the analysis
through the graph; `setDefinition` returns `false` and moves no revision for
a no-op (measured: `ThermalData_ANoOpEditDoesNotMoveTheRevision`, revision
unchanged on a re-set and +1 on a real edit). `findThermalAnalysis` answers
`nullptr` for an object of another kind carrying the number;
`thermalAnalysisIds` is ascending.

## Derived value types

```cpp
enum class VectorComponent : uint8_t { X = 0, Y = 1, Z = 2 };
struct TemperatureGradient3 { TemperatureGradient x, y, z; component(which); };   // K/m
struct HeatFlux3            { HeatFlux x, y, z; component(which); magnitude(); }; // W/m^2, direction of flow
struct HeatBalance {
    Power appliedSurfaceHeat, generatedHeat, convectionHeat, prescribedTemperatureReaction;  // W, into body +
    std::optional<Power> storedEnergyRate;                                                   // transient only
    Power netImbalance() const;  Power scale() const;  std::optional<double> normalizedImbalance() const;
};
```

`magnitude()`, `netImbalance()` and `normalizedImbalance()` are computed,
never stored, so none can disagree with the terms it summarises.
`normalizedImbalance()` is `|net| / sum|terms|` and is **empty** when every
term is zero: a balance with no heat in it is not a perfectly balanced one, it
is one the metric does not apply to.

## The result

```cpp
class ThermalResult {
    static Result<ThermalResult> create(ThermalResultSource, const VolumeMesh&,
        std::vector<Temperature>, std::vector<TemperatureGradient3>, std::vector<HeatFlux3>,
        HeatBalance, std::optional<Time>);
    const ThermalResultSource& source() const;
    span<const Temperature> temperatures() const;        // dense, parallel to mesh.nodes()
    span<const TemperatureGradient3> gradients() const;  // dense, parallel to mesh.tetrahedra()
    span<const HeatFlux3> heatFluxes() const;            // likewise
    const HeatBalance& heatBalance() const;
    std::optional<Time> time() const;                    // transient only
    bool describes(const VolumeMesh&) const;             // by stamp
    Result<Temperature> temperatureOf(const VolumeMesh&, NodeId) const;  // binary search
};
```

Immutable: every accessor is const and returns a view or a value; there is no
setter and no mutable path to the source. Assignable, deliberately, so a slot
can replace it. Not default-constructible. `sizeof(ThermalResult)` is fixed
whatever the mesh (three vectors, a source, a balance, an optional time) and
is asserted below `sizeof(VolumeMesh)`.

`create` refuses: a stamp that does not name the mesh; an empty mesh; wrong
cardinalities; any non-finite value; any temperature below 0 K; a steady
result with a time or a stored-energy rate; a transient result without a
finite non-negative time or without a stored-energy rate. Measured, one
section each: `ThermalData_AResultMustMatchItsMeshExactly`.

## The slot

`ThermalResultSlot`: `result()`, `failure()`, `recordSuccess(result)`,
`recordFailure(error)`, `reset()`. See STATE_MACHINE.md for the transitions
and why it is in this milestone.

## Layering

`thermal` (45) includes `core`, `features` and `meshing` only. Nothing here
includes `structural`, `io`, `renderer` or an app, and the P18-ARCH-001
fixtures that refuse those directions are unchanged and still pass. The three
new sources link no new library: no Eigen, no linear algebra, no matrix.
