# BetterCAD — TODO

> `[x]` = implemented + tested + independently validated + adversarially reviewed + regression clean + evidence recorded.  
> Qualification milestones require the final qualified tree to match the committed tree.  
> Work top-to-bottom. Stop at failed gates. Never fake evidence.

---

# Status

```text
Current:   P15 — Materials / Engineering Data
Current milestone:
           P15-UNITS-001 — Engineering Quantity / Property Contracts
           (NOT STARTED. P15-ARCH-001 is complete; see ADR-025..028.)

Qualified:
           P11
           P12
           P13
           P14 — Technical Drawings

Next:
           P15-UNITS-001, then P15-MAT-001. P15-ARCH-001 decided the contracts
           they must implement; do not re-litigate them there.

Carried:
           Hole POSITION dimensions remain unsupported.
           GD&T symbols are not fully embedded in PDF/DXF.
           Cross-preset export byte identity is not guaranteed.

Infrastructure:
           Moving build output outside the source tree remains blocked by
           location-dependent windeployqt behaviour.
```

---

# Completed Phases

```text
P0–P10   BetterCAD v0.1.0
P11      Advanced Part Modelling             QUALIFIED
P12      Production Part Modelling           QUALIFIED
P13      Assemblies                          QUALIFIED
P14      Technical Drawings                  QUALIFIED 2026-09-26
```

Detailed completed milestone history and evidence belongs in:

```text
ROADMAP.md
docs/verification/
docs/architecture/decisions/
```

Do not keep expanding completed-phase implementation diaries in `TODO.md`.

---

# P15 — Materials / Engineering Data

## Goal

Give BetterCAD a canonical, unit-safe engineering-data system that can describe the physical and engineering behaviour of parts without coupling the data model to FEA, thermal analysis, CFD, or manufacturing solvers.

```text
Material definition
→ engineering properties
→ material assignment
→ part/body association
→ mass properties
→ downstream engineering consumers
```

P15 provides trustworthy engineering data.

Later phases consume it:

```text
P17 Structural FEA
→ E
→ ν
→ yield strength
→ ultimate strength
→ density

P18 Thermal
→ thermal conductivity
→ specific heat
→ density
→ thermal expansion

P19 CFD
→ density
→ viscosity
→ thermal properties where applicable

P25 Manufacturing
→ material designation
→ manufacturing metadata
```

P15 does **not** implement those solvers.

---

# P15 Core Invariants

* Engineering data uses strong/unit-safe quantities.
* Material identity is stable and independent of display name.
* Material properties are canonical engineering intent.
* Derived mass/inertia values are not stored as authoritative state.
* Missing property data is explicit; never invent values.
* Unknown is different from zero.
* Material assignment uses stable document/object identity.
* Model geometry remains authoritative for volume and shape.
* Density + geometry determine derived mass.
* Engineering-property provenance must be representable.
* User-defined materials must not silently mutate library materials.
* Save/load must preserve material identity and engineering intent.
* Configuration changes must not silently change material unless explicitly modelled.
* Downstream solvers must consume P15 data rather than maintain competing material databases.

---

# P15 Sequence

```text
P15-ARCH-001      Materials / engineering-data architecture
P15-UNITS-001     Engineering quantity and property contracts
P15-MAT-001       Material definition / identity / library
P15-MECH-001      Mechanical properties
P15-THERM-001     Thermal / physical properties
P15-ASSIGN-001    Material assignment to model objects
P15-MASS-001      Mass / centre of mass / inertia
P15-CUSTOM-001    Custom materials / controlled overrides
P15-PROV-001      Property provenance / completeness / validation
P15-CMD-001       Commands / undo / redo
P15-PERSIST-001   Save / load material intent
P15-CLI-001       Headless engineering-data workflows
P15-REFMOD-001    Engineering-data reference models
P15-QUAL-001      Full P15 qualification
```

Do not implement a milestone until its predecessor passes.

---

# DONE — P15-ARCH-001

## Materials / Engineering Data Architecture

**PASS 2026-09-26.** Evidence:
[docs/verification/P15-ARCH-001/](docs/verification/P15-ARCH-001/README.md).
Decisions: ADR-025, ADR-026, ADR-027, ADR-028.

**The audit found no material concept at all** — every "material" in the tree is
geometric prose or `drawing::MaterialRemoval`, the ISO 1302 surface-finish
symbol. So nothing was reused and nothing was duplicated. The unit system, by
contrast, is strong: `Quantity<Dimension>` gives compile-time dimensional safety
and `Density` already exists.

**The central decision, and it went against the repository's own nearest
precedent.** `HoleClearance` stores an ISO 273 *designation* and resolves the
diameter at regeneration — "its diameter comes from ISO 273, so `diameter` stays
zero". A material library was deliberately NOT modelled that way: a clearance
diameter is a modelling input chosen by designation, whereas a density is a
measurement that gets *corrected*, so a library revision would silently change
the mass and stresses of every saved document naming it. Instead a document OWNS
its material values, imported from the library with the key and revision kept as
provenance. Consequence: **no library lookup happens at load or solve time, so a
missing or updated library cannot change what a saved document computes**, and
the "unresolved library material" failure mode is designed out rather than
guarded.

**Two answers were forced by the repository rather than chosen:**

* Object names are identifiers, unique per document — `"Aluminium 6061-T6"` is
  not a legal object name. So a material carries an identifier-style object name
  AND a free-text designation; identity is the `MaterialId` and is neither.
* A configuration overrides free *parameter values* only and "never changes the
  document's canonical state". A `MaterialId` is not a parameter value, so
  configuration cannot select a material variant. Material assignment is
  configuration-independent.

**One test was added**, `DocumentFile_NoDerivedGeometryIsPersisted`: ADR-026
rests on derived geometry never being persisted, which was true in practice and
guarded by nothing. It is not vacuous (the fixture is asserted to have derived
geometry, and the file to contain intent) and not over-strict (none of its ten
forbidden words appears in any of the 32 committed models).

**One adversarial finding, unresolved by design and recorded:** nothing
mechanical stops a future solver defining its own `kSteelE` constant — that is a
legal downward dependency on `core/units`. ADR-028 prohibits it in words; the
milestone that creates the first analysis module inherits the obligation to guard
it.

**Deferred, with mechanisms named:** per-body material (a body's persistent
handle is its producing feature's `ObjectId`); occurrence override (needs the
external references ADR-003 defers, so an assembly uses each part's own
material); electrical properties (`Dimension` has no electric current base
dimension — a base change, not an alias).


* [x] Audit existing material/property concepts in the repository
* [x] Audit existing unit/quantity infrastructure
* [x] Define material ownership model
* [x] Define stable material identity
* [x] Define material-definition vs material-assignment boundary
* [x] Define canonical vs derived engineering data
* [x] Define missing-property semantics
* [x] Define material-library vs document-local material semantics
* [x] Define downstream-consumer contract
* [x] Define engineering-property provenance model
* [x] Define configuration behaviour
* [x] Define persistence boundary
* [x] Define module/layer ownership
* [x] Record architecture decisions as ADRs
* [x] Architecture adversarial review PASS
* [x] Evidence recorded

### Questions that must be answered

```text
Does a material live globally, in a document, or both?

What gives a material stable identity?

Can two materials have the same display name?

Can a library material be edited directly?

Does assigning a material copy it or reference it?

What happens if a referenced library material changes?

How is "property unknown" represented?

How are temperature-dependent properties represented later?

Can one part/body have more than one material?

Can an assembly occurrence override the part material?

Does configuration affect material assignment?

What exactly is persisted?

Which quantities are canonical?

Which quantities are derived?

How do P17/P18/P19 consume the data without duplicating it?
```

### Gate

```text
material architecture coherent
+ stable identity defined
+ canonical/derived boundary clear
+ missing-data semantics explicit
+ assignment semantics clear
+ unit contract clear
+ provenance contract clear
+ downstream solver boundary clear
+ persistence boundary clear
+ ADRs complete
```

**MET.** Every required question answered, none TBD. 24 claims checked against
the tree; no ADR rests on infrastructure that does not exist.

```text
P15-ARCH-001 → [x]
Next → P15-UNITS-001. The evidence tells it exactly what to add: energy, power,
       specific heat, thermal conductivity, thermal expansion and dynamic
       viscosity as aliases over the existing base dimensions — and a separate
       decision on whether electric current becomes a sixth base dimension.
```

### Evidence

```text
docs/verification/P15-ARCH-001/
```

### Stop condition

Do not start `P15-UNITS-001` until every architecture item above passes.

---

# P15-UNITS-001

## Engineering Quantity / Property Contracts

* [ ] Audit existing strong quantity types
* [ ] Reuse existing unit infrastructure where possible
* [ ] Define density quantity
* [ ] Define stress / pressure quantity
* [ ] Define elastic modulus quantity
* [ ] Define thermal conductivity quantity
* [ ] Define specific heat capacity quantity
* [ ] Define thermal-expansion quantity
* [ ] Define viscosity quantities only if required by P15 scope
* [ ] Define dimensionless Poisson ratio
* [ ] Reject incompatible dimensions at compile time where practical
* [ ] Define canonical internal units
* [ ] Define display-unit conversion
* [ ] Validate numeric round trips
* [ ] Validate NaN / infinity rejection
* [ ] Compile-fail unit-safety tests
* [ ] Determinism PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Core relationships

Density:

```text
ρ = m / V
```

Elastic behaviour foundation:

```text
σ = E ε
```

Shear modulus derivation where appropriate:

```text
G = E / [2(1 + ν)]
```

Bulk modulus derivation where appropriate:

```text
K = E / [3(1 - 2ν)]
```

Thermal expansion:

```text
ε_thermal = α ΔT
```

Thermal energy foundation:

```text
Q = m cp ΔT
```

### Gate

```text
engineering dimensions correct
+ incompatible quantities cannot mix silently
+ conversions correct
+ invalid numeric state rejected
+ deterministic representation
```

---

# P15-MAT-001

## Material Definition / Identity / Library

* [ ] Implement stable `MaterialId`
* [ ] Implement material definition object
* [ ] Implement material name
* [ ] Implement optional designation / standard
* [ ] Implement category/family foundation
* [ ] Implement material description / notes
* [ ] Implement built-in material-library foundation
* [ ] Implement document-local material definitions
* [ ] Allow duplicate display names with distinct identity
* [ ] Prevent identity from depending on name
* [ ] Define immutable vs editable library behaviour
* [ ] Validate material lookup
* [ ] Validate deletion rules
* [ ] Validate duplicate/collision behaviour
* [ ] Determinism PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Example

```text
MaterialId: 42
Name:       Aluminium 6061-T6
Standard:   ASTM / AMS designation where supplied
Family:     Aluminium Alloy
```

Identity must remain:

```text
MaterialId = 42
```

even if:

```text
"Aluminium 6061-T6"
→ renamed for display
```

### Gate

```text
material identity stable
+ names are not identity
+ library/document ownership correct
+ lookup deterministic
+ no silent replacement
```

---

# P15-MECH-001

## Mechanical Properties

* [ ] Density available to mechanical consumers
* [ ] Young's modulus
* [ ] Poisson ratio
* [ ] Shear modulus foundation
* [ ] Bulk modulus foundation
* [ ] Yield strength
* [ ] Ultimate tensile strength
* [ ] Ultimate compressive strength foundation
* [ ] Shear strength foundation
* [ ] Elongation foundation
* [ ] Hardness metadata foundation
* [ ] Isotropic-material model
* [ ] Validate property ranges
* [ ] Validate optional / unknown properties
* [ ] Validate derived modulus relationships
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Validation examples

```text
E > 0

-1 < ν < 0.5
for ordinary stable isotropic elasticity

G = E / [2(1 + ν)]

K = E / [3(1 - 2ν)]
```

Do not automatically invent `G` or `K` unless the contract explicitly declares them derived.

### Gate

```text
mechanical properties unit-safe
+ invalid states rejected
+ unknown != zero
+ isotropic relationships coherent
+ downstream FEA-ready
```

---

# P15-THERM-001

## Physical / Thermal Properties

* [ ] Density
* [ ] Thermal conductivity
* [ ] Specific heat capacity
* [ ] Coefficient of thermal expansion
* [ ] Melting-temperature foundation
* [ ] Electrical resistivity foundation
* [ ] Temperature metadata foundation
* [ ] Define constant-property representation
* [ ] Define future temperature-dependent property contract
* [ ] Validate physical ranges
* [ ] Validate missing-property behaviour
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Relationships

Thermal expansion:

```text
ΔL = α L0 ΔT
```

Thermal energy:

```text
Q = m cp ΔT
```

Fourier-law consumer contract:

```text
q = -k ∇T
```

P15 stores `k`.

P18 implements the thermal PDE.

### Gate

```text
thermal properties coherent
+ quantities unit-safe
+ constant-property model valid
+ future variable-property path preserved
+ no solver logic introduced
```

---

# P15-ASSIGN-001

## Material Assignment

* [ ] Define valid material-assignment targets
* [ ] Assign material to part/body according to architecture
* [ ] Stable material reference
* [ ] Query effective material
* [ ] Replace material assignment explicitly
* [ ] Remove material assignment explicitly
* [ ] Missing material becomes unresolved
* [ ] Deleted material never silently rebinds
* [ ] Validate duplicate-name materials
* [ ] Validate model regeneration
* [ ] Define assembly occurrence behaviour
* [ ] Define configuration behaviour
* [ ] Validate undo-ready mutation semantics
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Critical no-rebinding fixture

```text
Material A:
"Steel"

Material B:
"Steel"

Part → Material A

delete Material A

Required:
Part material = unresolved

Forbidden:
Part silently adopts Material B
```

### Gate

```text
assignment stable
+ identity-based
+ no name-based rebinding
+ missing material explicit
+ configuration semantics correct
```

---

# P15-MASS-001

## Mass Properties

* [ ] Compute solid volume from authoritative geometry
* [ ] Compute material density lookup
* [ ] Compute mass
* [ ] Compute centre of mass
* [ ] Compute moments of inertia
* [ ] Compute products of inertia
* [ ] Define reference coordinate frame
* [ ] Validate transformed bodies
* [ ] Validate assemblies where in scope
* [ ] Missing density fails explicitly
* [ ] Invalid/open geometry fails explicitly where required
* [ ] Derived values never persisted as authority
* [ ] Independently validate analytical solids
* [ ] Determinism PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Core equations

```text
m = ρV
```

Centre of mass:

```text
r_cm = (1/m) ∫ r dm
```

Inertia tensor:

```text
I = ∫ (||r||² 1 - r rᵀ) dm
```

### Independent fixtures

Cube:

```text
V = abc
m = ρabc
```

Centroid:

```text
(a/2, b/2, c/2)
```

Cuboid centroidal inertia:

```text
Ixx = m(b² + c²)/12
Iyy = m(a² + c²)/12
Izz = m(a² + b²)/12
```

Cylinder:

```text
V = πr²h

Iaxis = 1/2 mr²

Itransverse = m(3r² + h²)/12
```

Do not use the production mass-property routine to generate expected test values.

### Gate

```text
volume correct
+ density resolution correct
+ mass correct
+ centroid correct
+ inertia correct
+ units correct
+ independent analytical validation PASS
```

---

# P15-CUSTOM-001

## Custom Materials / Controlled Overrides

* [ ] Create user-defined material
* [ ] Clone library material into editable document material
* [ ] Edit custom property
* [ ] Remove custom property
* [ ] Preserve original library material
* [ ] Explicit property override semantics
* [ ] No accidental partial override
* [ ] Validate duplicate names
* [ ] Validate identity preservation
* [ ] Validate copy/clone semantics
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Rule

```text
library material
≠
document-local editable clone
```

Editing a local material must never mutate the built-in library definition.

---

# P15-PROV-001

## Provenance / Completeness / Validation

* [ ] Define property source metadata
* [ ] Define optional standard/reference field
* [ ] Define source revision/date foundation
* [ ] Define measured vs reference-data distinction if needed
* [ ] Define completeness report
* [ ] Query properties required by a consumer
* [ ] Report missing FEA properties
* [ ] Report missing thermal properties
* [ ] Never fabricate unavailable properties
* [ ] Validate inconsistent data
* [ ] Validate provenance persistence
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Example

```text
Material:
Aluminium 6061-T6

density:
  value: ...
  source: ...

Young's modulus:
  value: ...
  source: ...

thermal conductivity:
  UNKNOWN
```

Consumer:

```text
P18 Thermal
→ requires density + cp + k

Result:
cannot run
missing thermal conductivity
```

Not:

```text
assume some default k
```

### Gate

```text
property provenance preserved
+ missing-data report correct
+ consumer requirements explicit
+ no fabricated engineering data
```

---

# P15-CMD-001

## Commands / Undo / Redo

* [ ] Create material command
* [ ] Delete material command
* [ ] Edit material command
* [ ] Assign material command
* [ ] Remove assignment command
* [ ] Undo restores exact engineering intent
* [ ] Redo restores exact post-command state
* [ ] Failed commands atomic
* [ ] Redo invalidation correct
* [ ] Material identity preserved
* [ ] No derived mass state in history
* [ ] Determinism PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Gate

```text
commands mutate canonical material intent
+ undo exact
+ redo exact
+ failed mutations atomic
+ derived engineering results excluded
```

---

# P15-PERSIST-001

## Materials / Engineering Data Persistence

* [ ] Define canonical persisted schema
* [ ] Persist material identity
* [ ] Persist material metadata
* [ ] Persist mechanical properties
* [ ] Persist thermal/physical properties
* [ ] Persist material assignments
* [ ] Persist provenance
* [ ] Persist custom materials
* [ ] Do not persist derived mass properties as authority
* [ ] Validate stable references
* [ ] Validate malformed-file rejection
* [ ] Validate deterministic serialization
* [ ] Validate backward compatibility
* [ ] Validate full round trip
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Gate

```text
material intent preserved
+ assignments preserved
+ units preserved
+ provenance preserved
+ derived state excluded
+ malformed files rejected
+ deterministic serialization PASS
```

---

# P15-CLI-001

## Headless Engineering Data Workflows

* [ ] CLI list materials
* [ ] CLI inspect material
* [ ] CLI create custom material
* [ ] CLI edit custom material
* [ ] CLI assign material
* [ ] CLI remove assignment
* [ ] CLI query effective material
* [ ] CLI query engineering properties
* [ ] CLI mass-properties command
* [ ] CLI completeness/requirements report
* [ ] Structured diagnostics
* [ ] Correct process exit codes
* [ ] Validate CLI/core equivalence
* [ ] End-to-end scripted workflow PASS
* [ ] Adversarial review PASS
* [ ] Regression PASS
* [ ] Evidence recorded

### Gate

```text
CLI uses core APIs
+ engineering values identical to core
+ diagnostics structured
+ failures propagate
+ no CLI-only material semantics
```

---

# P15-REFMOD-001

## Materials / Engineering Data Reference Models

* [ ] Define reference suite
* [ ] Aluminium block
* [ ] Steel shaft
* [ ] Hollow steel tube
* [ ] Multi-material assembly
* [ ] Custom-material part
* [ ] Incomplete-property material
* [ ] Configuration/assignment case if architecture supports it
* [ ] Validate density → mass
* [ ] Validate centroid
* [ ] Validate inertia
* [ ] Validate mechanical-property lookup
* [ ] Validate thermal-property lookup
* [ ] Validate missing-property diagnostics
* [ ] Validate model-change recomputation
* [ ] Validate save/load
* [ ] Validate CLI
* [ ] Independent analytical validation
* [ ] Adversarial review PASS
* [ ] Three-preset regression PASS
* [ ] Evidence recorded

### Reference examples

```text
RM-MAT-01  Aluminium rectangular block
RM-MAT-02  Steel cylindrical shaft
RM-MAT-03  Hollow tube
RM-MAT-04  Multi-part assembly
RM-MAT-05  Custom engineering material
RM-MAT-06  Missing-property failure model
```

### Gate

```text
realistic material data PASS
+ assignment PASS
+ mass properties PASS
+ engineering properties PASS
+ missing-data behaviour PASS
+ persistence PASS
+ CLI PASS
+ independent validation PASS
```

---

# P15-QUAL-001

## Full P15 Qualification

* [ ] Freeze final P15 tree
* [ ] Audit all P15 milestone evidence
* [ ] Verify all P15 TODO items complete
* [ ] Verify all new P15 ADRs
* [ ] Audit engineering units
* [ ] Audit material identity
* [ ] Audit property provenance
* [ ] Validate material assignments
* [ ] Validate no silent material rebinding
* [ ] Validate mechanical properties
* [ ] Validate thermal properties
* [ ] Validate mass properties
* [ ] Validate analytical reference cases
* [ ] Validate missing-property behaviour
* [ ] Validate custom materials
* [ ] Validate undo / redo
* [ ] Validate persistence
* [ ] Validate CLI workflows
* [ ] Clean Debug qualification
* [ ] Clean Release qualification
* [ ] Clean Debug-shared qualification
* [ ] Repeated determinism qualification
* [ ] Final adversarial review
* [ ] Confirm 0 unexpected warnings
* [ ] Confirm qualified tree == committed tree
* [ ] Evidence in `docs/verification/P15-QUAL-001/`
* [ ] Mark P15 qualified

### Final Gate

```text
all P15 milestones PASS
+ material architecture PASS
+ units PASS
+ material identity PASS
+ mechanical properties PASS
+ thermal properties PASS
+ assignments PASS
+ mass properties PASS
+ provenance PASS
+ missing-data handling PASS
+ custom materials PASS
+ undo/redo PASS
+ persistence PASS
+ CLI PASS
+ reference models PASS
+ independent analytical validation PASS
+ Debug PASS
+ Release PASS
+ Debug-shared PASS
+ determinism PASS
+ adversarial review PASS
+ 0 unexpected warnings
+ qualified tree == committed tree
```

---

# P15 Scope Boundaries

P15 **does** implement:

```text
materials
engineering property storage
units
material assignment
density
mechanical property data
thermal property data
mass properties
property provenance
engineering-data persistence
engineering-data CLI
```

P15 does **not** implement:

```text
mesh generation               → P16
stress/strain solution         → P17
thermal PDE solution           → P18
CFD solution                   → P19
optimization                   → P20
full semantic topology         → P21
CAM/toolpaths                  → P25
```

Do not let P15 become a solver phase.

---

# Carried Infrastructure Decision

## Build output outside OneDrive

Still open.

Observed problem:

```text
out-of-source-tree GUI build
→ windeployqt searches for Qt relative to target executable
→ deployment fails
```

Do not mix this fix into a P15 material milestone unless it blocks P15 qualification.

Treat it as a separate infrastructure task with its own regression evidence.

---

# Future Phases

```text
P15  Materials / Engineering Data       CURRENT
P16  Meshing
P17  Structural FEA
P18  Thermal Analysis
P19  CFD Integration
P20  Design Optimization
P21  Semantic Topology
P22  Versioning / Collaboration
P23  Python / Automation
P24  AI Engineering Agent
P25  Manufacturing / CAM
P26  Performance / GPU / Scale
P27  Production Hardening
P28  BetterCAD 1.0
```

---

# Workflow

```text
UNDERSTAND
→ ARCHITECT
→ BLAST RADIUS
→ IMPLEMENT
→ TARGETED TESTS
→ INDEPENDENT VALIDATION
→ FAILURE PATHS
→ PERSISTENCE
→ DETERMINISM
→ ADVERSARIAL REVIEW
→ FULL REGRESSION
→ EVIDENCE
→ [x]
→ COMMIT
→ PUSH
```

If a gate fails:

```text
STOP
→ reproduce
→ regression test
→ root cause
→ fix
→ revalidate
```

Never mark work complete because it merely compiles.

---

# Project Authority

```text
TODO.md
→ current authorized work

ROADMAP.md
→ long-term direction + completed phases

ARCHITECTURE.md
→ architecture / invariants

CLAUDE.md
→ engineering process / Definition of Done

docs/architecture/decisions/
→ durable ADRs

docs/verification/<milestone>/
→ qualification evidence

docs/engineering/
→ reusable engineering templates
```

---

# CURRENT NEXT STEP

```text
P15-ARCH-001
Materials / Engineering Data Architecture
```

Start here.

Do **not** implement material classes or databases yet.

First audit the existing repository and answer:

```text
Where does material data belong?
What is its stable identity?
What is canonical?
What is derived?
What units already exist?
How is missing data represented?
How are materials assigned?
How do P17/P18/P19 consume it?
How is provenance represented?
What must be persisted?
```

Only after those decisions are recorded and qualified should implementation begin.