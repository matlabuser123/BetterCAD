# BetterCAD — TODO

> `[x]` = implemented + tested + independently validated + adversarially reviewed + regression clean + evidence recorded.  
> Qualification milestones require the final qualified tree to match the committed tree.  
> Work top-to-bottom. Stop at failed gates. Never fake evidence.

---

# Status

```text
Current:   P15 — Materials / Engineering Data
Current milestone:
           NONE. P15-PROV-001 is complete.
           The next milestone is a scope decision, not Claude's to make.

Qualified:
           P11
           P12
           P13
           P14 — Technical Drawings
           P15-ARCH-001, P15-UNITS-001, INFRA-QT-DEPLOY-001, P15-MAT-001,
           P15-MECH-001, P15-THERM-001, P15-ASSIGN-001, P15-MASS-001,
           P15-CUSTOM-001, P15-PROV-001

Next:
           P15-CMD-001 — commands / undo / redo for materials.
           Awaiting explicit scope decision.

           A material cannot be SAVED until P15-PERSIST-001. Saving a document
           that holds one fails loudly and is tested to; nothing is dropped. But
           materials are not usable end to end until then, which may be reason
           to bring that milestone forward.

           Also awaiting a decision, and arguably ahead of it: the carried
           FileIo replace defect below. A user saving a document into a
           synchronised folder can still lose the save.

Carried:
           A document save can still lose to a file synchroniser — see below.
           Hole POSITION dimensions remain unsupported.
           GD&T symbols are not fully embedded in PDF/DXF.
           Cross-preset export byte identity is not guaranteed.

Infrastructure:
           RESOLVED by INFRA-QT-DEPLOY-001. Build output can now be put outside
           OneDrive with the -ext presets, and the determinism gate that had
           failed twice passed from there at 11500 = 2300 x 5.
           The recorded reason the move was blocked was WRONG: windeployqt does
           not resolve Qt relative to the executable it is deploying. It
           resolves the Qt directory through its 8.3 SHORT NAME and reaches the
           first same-basis sibling in name order; the build root chosen for the
           first attempt, %LOCALAPPDATA%\BetterCAD-build, was that sibling for
           %LOCALAPPDATA%\bettercad-deps. The build's location was never the
           cause -- the same failure reproduces from any location with that
           name, and none occurs from any location without it. Corrected in
           docs/verification/INFRA-QT-DEPLOY-001/README.md, which supersedes
           the diagnosis carried here and in P14-STREF-001's harness comments.
           The replace fault itself is AVOIDED, not fixed: see the carried
           FileIo item.
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

**NOT COMPLETE — 19 of 20 items pass; the determinism gate is BLOCKED by the
machine.** Evidence:
[docs/verification/P15-UNITS-001/](docs/verification/P15-UNITS-001/README.md).

The work itself is done and verified: 7 dimensions, 7 quantity types, 13 units,
26 literals, a distinct `PoissonRatio`, 27 new tests, **2286/2286 in all three
presets** with 0 warnings and fresh binaries, and **release determinism clean at
11430 = 2286 x 5**. Density and pressure already existed and were reused
untouched; `UnitScale` was already an exact rational, so the exactness
requirement was met by audit rather than by code.

**One production defect found and fixed:** quantity formatting printed `-0`,
against a convention the project states in five other places — `PdfWriter.cpp`
says it follows "the same rules as everywhere else". Fixed in `Format.hpp`
through the existing formatter, so no second formatting rule exists.

**One pre-existing test was stale:** `catalog.size() == 37` became 50. It failed
the first regression across all five stages. Worth noting that it is a test this
milestone did not write — a repeat filter narrowed to the new tests would have
missed it, which is the argument for keeping it total.

**THE BLOCKER IS THE ONEDRIVE BUILD LOCATION, and it has stopped being an
annoyance.** It failed a determinism repeat once per milestone through P14; here
it failed twice in a row on an unchanged tree, in two different tests, with zero
test-logic assertions failing. OneDrive's accumulated CPU is **99,264 s (27.6 h),
726 MB resident** — five times the 20,633 s recorded in P14-REFMOD-001. The
decision below is no longer deferrable: it is now preventing a gate from passing.


* [x] Audit existing strong quantity types
* [x] Reuse existing unit infrastructure where possible
* [x] Define density quantity
* [x] Define stress / pressure quantity
* [x] Define elastic modulus quantity
* [x] Define thermal conductivity quantity
* [x] Define specific heat capacity quantity
* [x] Define thermal-expansion quantity
* [x] Define viscosity quantities only if required by P15 scope
* [x] Define dimensionless Poisson ratio
* [x] Reject incompatible dimensions at compile time where practical
* [x] Define canonical internal units
* [x] Define display-unit conversion
* [x] Validate numeric round trips
* [x] Validate NaN / infinity rejection
* [x] Compile-fail unit-safety tests
* [x] Determinism PASS — from a build tree OUTSIDE the synchronised folder
      (preset `debug-ext`): **11500 = 2300 x 5, counted, exit 0**. Release had
      already passed at 11430 = 2286 x 5.
      The two DEBUG failures are NOT retracted -- `cli.refmod.build` then
      `cli.drawing.batch`, each passing twice and failing on an atomic replace
      of a file it had just written, 0 test-logic assertions in either. This is
      not a third attempt at the same thing: the cause was removed
      (INFRA-QT-DEPLOY-001), not the dice re-rolled. The evidence that it was
      the right cause is 48 consecutive runs of the four file-replacing CLI
      tests with 0 failures, not this single pass.
      NOT CLAIMED: that the gate passes from inside the synchronised folder, or
      that the replace fault is fixed. It is AVOIDED, for build output only --
      see the carried `FileIo` item
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded

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

Evidence:
[docs/verification/P15-MAT-001/](docs/verification/P15-MAT-001/README.md).

Built to ADR-025: a material is a **document object**, so it gets an ObjectId, a
name, a revision, the dependency graph and undo from machinery that already
exists; the built-in library is **reference data** in `core/materials/` on the
`core/standards/` pattern, with no ObjectId, named by a structured key. Using a
library entry **imports** it -- the values become the document's own and the key
is kept as provenance -- so no library lookup happens at load or solve time and a
library change cannot alter what a saved document says.

A material carries TWO names, because the repository forces it: the object name
is an identifier (letters, digits, `_`), unique per document; the designation is
free engineering text that may duplicate. Neither is identity.

* [x] Implement stable `MaterialId` — `Id<MaterialIdTag>`, widens to ObjectId
      (ADR-025); the document's one allocator, monotonic, never reused
* [x] Implement material definition object — `features::Material`, a
      DocumentObject with `kTypeName == "material"`
* [x] Implement material name — the existing identifier rule, unchanged
* [x] Implement optional designation / standard — free text, may duplicate
* [x] Implement category/family foundation — free text, extensible; a family no
      built-in entry uses is accepted
* [x] Implement material description / notes
* [x] Implement built-in material-library foundation — `core/materials/`, the
      `core/standards/` pattern, 4 METADATA-ONLY entries
* [x] Implement document-local material definitions
* [x] Allow duplicate display names with distinct identity — duplicate
      DESIGNATIONS; object names stay unique, which is the document's rule
* [x] Prevent identity from depending on name — rename and every metadata edit
      tested separately
* [x] Define immutable vs editable library behaviour — entries are `constexpr`,
      so there is nothing to mutate; editing means editing the document's copy
* [x] Validate material lookup — by ID, and by designation returning ALL matches
* [x] Validate deletion rules — plus the invariant a later assignment needs: a
      deleted ID never rebinds, tested against an otherwise identical material
* [x] Validate duplicate/collision behaviour — a taken ID and a taken name are
      both rejected, nothing overwritten; a rejected create consumes no ID
* [x] Determinism PASS — 11765 = 2353 x 5 in release-ext AND in debug-ext, 0
      failures, from the external build tree, no controlled rerun
* [x] Adversarial review PASS — 4 findings, 4 fixed; one was found by the
      debug-shared gate and voided a complete qualification run
* [x] Regression PASS — 3 presets from an external build root, 2353/2353 each,
      0 warnings, fresh binaries, 0 stages failed
* [x] Evidence recorded

### Example

As built. Note that "Aluminium 6061-T6" is the DESIGNATION, not the object name:
an object name is an identifier and may not contain a space or a hyphen, which is
the reason a material has two names (ADR-025).

```text
MaterialId:   42
Object name:  Al6061T6
Designation:  Aluminium 6061-T6
Standard:     ASTM B221
Family:       Aluminium Alloy
Origin:       bettercad/al-6061-t6 rev 1   (provenance of an import)
```

Identity must remain:

```text
MaterialId = 42
```

after any of:

```text
object name  → renamed
designation  → reworded
standard, family, notes, origin → edited
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

Evidence:
[docs/verification/P15-MECH-001/](docs/verification/P15-MECH-001/README.md).

This milestone finally spells ADR-027's property type, which did not exist: the
ADR deferred it to P15-UNITS-001, and P15-UNITS-001 added the quantities and left
the property type alone. `MaterialProperty<Value>` carries Known / Unknown /
Derived, returns an optional and offers no conversion to its value, so an Unknown
property cannot become a number somewhere else.

**There is no slot for a supplied shear or bulk modulus, by ADR-027**, which says
"the schema does not offer a slot for a canonical G, so the conflict ... cannot be
represented". So G and K are derived on request and never stored, and there is no
supplied-versus-derived reconciliation to get wrong. `-1 < nu < 0.5` lives here
because `PoissonRatio` says so by name.

* [x] Density available to mechanical consumers — the qualified `Density`, reused;
      reachable through `requireDensity()`, separate from the elastic constants
* [x] Young's modulus — `ElasticModulus`, validated > 0 and finite when Known
* [x] Poisson ratio — the semantic type, with both range ends EXCLUDED because
      each one makes a derivation divide by zero
* [x] Shear modulus foundation — `derivedShearModulus()`, never stored
* [x] Bulk modulus foundation — `derivedBulkModulus()`, never stored
* [x] Yield strength
* [x] Ultimate tensile strength
* [x] Ultimate compressive strength foundation — independent of the tensile one
* [x] Shear strength foundation — never estimated from yield or ultimate
* [x] Elongation foundation — a FRACTION, 0.12 is 12 %; no upper limit
* [x] Hardness metadata foundation — value AND scale; 60 HRC != 60 HRB; no
      conversion between scales
* [x] Isotropic-material model — E and nu are the coherent pair; completeness is
      per consumer, not one ambiguous `isComplete()`
* [x] Validate property ranges — every problem reported, not the first
* [x] Validate optional / unknown properties — Unknown is valid and is never zero
* [x] Validate derived modulus relationships — G bit-identical to the
      hand-computed 80.76923076923077 GPa; K 1 ULP below 175 GPa, explained
* [x] Adversarial review PASS — 23 questions, 3 findings, 3 fixed. One was a
      compile-fail case that had been failing on a missing include rather than on
      the immutability it claimed to test
* [x] Regression PASS — 3 presets from an external build root, 2400/2400 each,
      0 warnings, fresh binaries, 0 stages failed, first attempt
* [x] Evidence recorded

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

Evidence:
[docs/verification/P15-THERM-001/](docs/verification/P15-THERM-001/README.md).

Built on the SAME `MaterialProperty<Value>` P15-MECH-001 qualified, not a second
property system. **Density is NOT in `ThermalProperties`**: a material has one
density, it lives with the mechanical properties, and thermal consumers read that
one — two authoritative values for one physical quantity is the defect that
arrangement prevents.

**An absolute temperature and a temperature interval are the same type, and this
is recorded rather than papered over.** The relationships need `dT` to be
dimensionally a temperature, so they cannot be separated; the distinction lives in
validation (a melting point must be above absolute zero, an interval of -20 K is
ordinary cooling). There is one temperature unit, scale 1:1, so there is no
Celsius offset to get wrong, and a test fails the day one is added.

Electrical resistivity needed an architecture decision and has one: **ADR-029** —
a scoped strong type, not a sixth base dimension, because ADR-027 had recorded
that adding an electric-current exponent is a base-dimension change and nothing in
P15 consumes the arithmetic. It carries NO dimensional checking, which the ADR
states at the definition.

* [x] Density — the qualified `Density`, reused; ONE store, read by both consumer
      paths, proved four ways
* [x] Thermal conductivity — `> 0`, finite; the Fourier-law shape proved by
      `static_assert`, without a solver
* [x] Specific heat capacity — `> 0`, finite; SPECIFIC, so a total heat capacity
      (J/K) is a compile error
* [x] Coefficient of thermal expansion — finite ONLY; **negative accepted**,
      because real materials contract when heated
* [x] Melting-temperature foundation — an ABSOLUTE temperature, above absolute
      zero; never inferred from a family, a strength or a name
* [x] Electrical resistivity foundation — ADR-029; a named type, never an untyped
      double, never confused with mass density
* [x] Temperature metadata foundation — a reference temperature on the one
      property wrapper, validated as the absolute temperature it is, and it does
      NOT turn a constant into k(T)
* [x] Define constant-property representation — one value, used as-is
* [x] Define future temperature-dependent property contract — `Constant` /
      `Table` / `AnalyticLaw` named; six table invariants fixed; three
      out-of-range behaviours named with NO default, so nothing extrapolates
      silently
* [x] Validate physical ranges — every problem reported, not the first
* [x] Validate missing-property behaviour — Unknown is valid, is never zero, and
      needs no placeholder
* [x] Adversarial review PASS — 23 questions, 2 findings, 2 fixed. The first was
      this change breaking four of P15-MECH-001's compile-fail assertions
* [x] Regression PASS — 3 presets from an external build root, 2446/2446 each,
      0 warnings, fresh binaries, 0 stages failed, first attempt
* [x] Evidence recorded

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

Evidence:
[docs/verification/P15-ASSIGN-001/](docs/verification/P15-ASSIGN-001/README.md).

ADR-026 decided the target by AUDIT, and the answer is that there is nothing
conventional to hang a material on: **there is no Part document object and no body
document object.** A part IS a document; a body is a regeneration result whose only
handle is its producing feature's ObjectId. So the assignment is one optional
`MaterialId` on the Document, exactly as `ConfigurationId` is document-level state.
No inheritance, so the effective material IS the direct assignment.

Four states, never collapsed: **Unassigned / Resolved / Unresolved / Invalid.**
Resolution is by identity alone -- no name, no designation, no position, no
similarity -- and a deleted material leaves the assignment Unresolved with its
intent intact, never repointed at a same-named or content-identical rival.

* [x] Define valid material-assignment targets — by audit; neither Part nor Body
      exists as an object, so the DOCUMENT owns it
* [x] Assign material to part/body according to architecture — document-level
      intent, one optional MaterialId (ADR-026)
* [x] Stable material reference — identity only; no name, index, position or
      pointer is stored, so a rename cannot move an assignment
* [x] Query effective material — `effectiveMaterial()`, the name ADR-027 promised
      a solver; plus `materialAssignment()` for the state and
      `requireEffectiveMaterial()` for a diagnostic
* [x] Replace material assignment explicitly — identity decides even when the two
      materials share a designation
* [x] Remove material assignment explicitly — and removing is not deleting
* [x] Missing material becomes unresolved — never erased, never defaulted
* [x] Deleted material never silently rebinds — proved against a same-designation
      rival AND a content-identical one
* [x] Validate duplicate-name materials — three coexist; renaming one moves no
      assignment
* [x] Validate model regeneration — and a FAILED regeneration keeps the intent
* [x] Define assembly occurrence behaviour — no override; `ComponentDefinition`
      has no material field, proved by two compile-fail cases
* [x] Define configuration behaviour — a configuration cannot change the material.
      **It also does not rebuild the geometry it changes — a pre-existing defect
      found here, carried below, and a precondition for P15-MASS-001**
* [x] Validate undo-ready mutation semantics — canonical state is one optional
      MaterialId; A→B→A restores exactly and allocates no ID
* [x] Adversarial review PASS — 22 questions, 3 findings: 1 pre-existing product
      defect (not fixed, out of scope) and 2 test defects
* [x] Regression PASS — 3 presets from an external build root, 2479/2479 each,
      0 warnings, fresh binaries, 0 stages failed, first attempt
* [x] Evidence recorded

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

**PASS 2026-09-27.** Evidence:
[docs/verification/P15-MASS-001/](../docs/verification/P15-MASS-001/README.md).

* [x] Compute solid volume from authoritative geometry — the regenerator's body for
      the named feature, refused when it is absent, stale or encloses no volume
* [x] Compute material density lookup — composes requireEffectiveMaterial and
      requireDensity; no material logic is duplicated here
* [x] Compute mass — density x volume, dimensionally checked, never zero for an
      unanswered question
* [x] Compute centre of mass — the geometric centroid for a uniform part (ADR-026)
* [x] Compute moments of inertia — about the centroid AND about the origin
* [x] Compute products of inertia — the inertia TENSOR convention (negated
      products), **measured on a three-box staircase, not assumed**: no symmetric
      body can distinguish the two conventions
* [x] Define reference coordinate frame — the document's own axes; each tensor
      carries the point it is taken about, so a tensor cannot be paired with the
      wrong reference point
* [x] Validate transformed bodies — integrating the moved body and moving the
      integrated tensor agree to 1e-12, on a rotation about a non-principal axis
      through a non-centroidal point at 37 degrees; plus reflection and a pure
      translation
* [x] Validate assemblies where in scope — **scope decision recorded: aggregation is
      OUT of scope, and NOT implemented.** ADR-026 forces every part in one document
      to share one material, so an assembly mass would be ρ x ΣV — useless for the
      case that matters. See KNOWN LIMITATIONS 2 for the reason and what it needs
* [x] Missing density fails explicitly — three material faults with three diagnostics,
      asserted pairwise distinct in one test
* [x] Invalid/open geometry fails explicitly where required — empty body, no volume,
      no body, failed or blocked regeneration, swept-curve face
* [x] Derived values never persisted as authority — 0 hits in src/io/, and four
      compile-fail cases prove there is no setter to persist from
* [x] Independently validate analytical solids — every expected value a hand-evaluated
      closed form; box, cylinder, sphere, fused staircase, two-solid body, drilled
      plate, rotation, translation; plus four reference-free invariants
* [x] Determinism PASS — bit-for-bit over ten runs at the tightened tolerance on five
      shapes including a torus; 12655 = 2531 x 5 in each of two presets
* [x] Adversarial review PASS — 24 questions, 4 findings: 3 defects in this
      milestone's own work (a wrong API contract, an untrue comment, a test that
      proved nothing), 1 recorded limitation
* [x] Regression PASS — 3 presets from an external build root, 2531/2531 each,
      0 warnings, fresh binaries, 0 stages failed, first attempt
* [x] Evidence recorded

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

**PASS 2026-09-28.** Evidence:
[docs/verification/P15-CUSTOM-001/](../docs/verification/P15-CUSTOM-001/README.md).

**Mostly a proof that the architecture already got this right.** Three functions were
missing and were added; everything else was already built or already structurally
impossible to get wrong, and the work there was turning "impossible" into 12
compile-fail cases and an audit.

* [x] Create user-defined material — `createMaterial` already existed; a material
      with nothing but a name is valid and every property starts Unknown, not zero
* [x] Clone library material into editable document material — `importLibraryMaterial`
      already existed and already copies (ADR-025). **NEW: `cloneMaterial`**, the
      document-to-document clone, same-document and cross-document
* [x] Edit custom property — the three existing setters, tested for non-clobbering:
      a mechanical edit cannot reach the thermal half, and an unrelated edit does not
      disturb a hardness scale or a reference temperature
* [x] Remove custom property — **NEW: `removeMaterialProperty`**. Known → Unknown,
      never zero; refuses the derived kinds, saying to remove E or nu instead
* [x] Preserve original library material — the entries are `static constexpr` with no
      setters, so it is structural. Verified field by field, by key, by `operator==`
      and by table order after every local edit, plus 6 compile-fail cases
* [x] Explicit property override semantics — **the model is a SNAPSHOT and ADR-025
      chose it.** No base reference, no override map, no `Inherited` state; 4
      compile-fail cases prove the absence rather than asserting it
* [x] No accidental partial override — an edit touches exactly one property, compared
      member by member afterwards including the whole other half
* [x] Validate duplicate names — **the brief's premise does not hold here: names are
      UNIQUE.** A duplicate name is refused, consumes no ID and changes nothing; the
      duplicate that is legitimate is the DESIGNATION, and a three-way import +
      two-custom fixture shows all three resolve by identity with no fallback
* [x] Validate identity preservation — rename, metadata, mechanical, thermal and
      removal all leave the MaterialId untouched; a deleted ID never rebinds to an
      identical replacement
* [x] Validate copy/clone semantics — `Material::clone()` keeps the ID for undo/redo,
      which is why it is not the clone route: `addObject` refuses an ID-bearing object
      and `insertObject` refuses a duplicate ID, neither merging nor overwriting
* [x] Adversarial review PASS — 26 attacks, 5 findings: 2 defects in this milestone's
      own work, 3 places where the brief's premises do not hold. **No production
      defect survived.** Two gates mutation-tested
* [x] Regression PASS — 3 presets from an external build root, 2589/2589 each,
      0 warnings, fresh binaries, 0 stages failed, first attempt
* [x] Evidence recorded

**Known limitation, recorded not hidden:** a clone of an imported material carries the
same `origin` as the import, so the two are indistinguishable. Both hold values that
came from that library entry, so neither is lying — but "which document material did
this come from" needs a provenance model that separates a library source from a
document one, which is P15-PROV-001's subject. A `clonedFrom` field can be added
beside `origin` without migrating anything.

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

**PASS 2026-09-28.** Evidence:
[docs/verification/P15-PROV-001/](../docs/verification/P15-PROV-001/README.md).
Qualified on the SECOND attempt; the first failed on `debug-shared-ext build` and is
recorded in
[qualification-void/](../docs/verification/P15-PROV-001/qualification-void/README.md).

* [x] Define property source metadata — `PropertyProvenance`: kind, source, standard,
      reference, revision, date, condition, notes. Per property, with a material-level
      default that a property's own record overrides (ADR-028's two-level model, and
      the only inheritance in the material model — values never inherit)
* [x] Define optional standard/reference field — both, plus `condition` for temper.
      Every field optional; sparse metadata is the normal case and never a reason to
      refuse data
* [x] Define source revision/date foundation — `revision` is metadata and is NEVER
      parsed, ordered or compared for precedence. `materials::Date` is the
      repository's FIRST date type: validated including leap years, ISO 8601 from
      three integers so it cannot pick up a locale, a zone or a clock
* [x] Define measured vs reference-data distinction if needed — needed, and resolved.
      `isMeasured` / `isReferenceData` predicates, and NOT inferred from whether a
      source string exists
* [x] Define completeness report — `CompletenessReport` with `Ready | Incomplete |
      Invalid`, present/missing lists by semantic kind, and structured
      `MaterialIssue`s. **No single completeness flag**: 4 compile-fail cases prove a
      report is not a bool and neither it nor a Material has `isComplete()`
* [x] Query properties required by a consumer — `requiredProperties(ConsumerKind)`,
      seven consumers, asserted BY VALUE. **Cross-checked against the six `require*()`
      functions consumers actually call**, by removing each required property in turn
      so the table cannot drift from them
* [x] Report missing FEA properties — exact: E known, ν unknown → missing
      `{PoissonRatio}`. `FeaLinearStatic` requires NO density; the gravity variant is
      where a mass enters
* [x] Report missing thermal properties — exact: density + cp known, k unknown →
      missing `{ThermalConductivity}`. `ThermalSteady` requires a conductivity and
      NOTHING else
* [x] Never fabricate unavailable properties — full tree audit: 12 `value_or` hits,
      exactly one in the material path and it is a change-flag; `valueOr` on
      `MaterialProperty` does not exist. 0 hits for `getOrDefault`, `defaultMaterial`,
      "generic steel", "default density", "default nu". **Mutation-tested**
* [x] Validate inconsistent data — `inconsistencies()` WRAPS
      `mechanicalInconsistencies()` rather than restating its rules. A supplied G that
      disagrees with E and ν cannot arise: ADR-027 gave it no slot, so it was designed
      out rather than validated
* [x] Validate provenance persistence — **BLOCKED on P15-PERSIST-001**, recorded as
      the brief instructs. No material of any kind can be saved, so no round trip
      exists. Verified instead: provenance is canonical state, a cited material is NOT
      content-equal to an uncited one, and the save gap stays LOUD with a full record
      present
* [x] Adversarial review PASS — 30 attacks, 5 findings: 1 test defect found by
      mutation testing, 1 implementation defect, 3 recorded conflicts. No production
      defect survived
* [x] Regression PASS — 3 presets from an external build root, 2639/2639 each,
      0 warnings, fresh binaries, 0 stages failed on the second attempt
* [x] Evidence recorded

**Known limitations, recorded not hidden:** provenance is not persisted (1);
`FeaYieldStrength` has no runtime counterpart to cross-check against, because no yield
consumer exists (2); a clone of an import is still indistinguishable from a direct
import (3); a future CFD consumer will need a dynamic-viscosity property, which no
material carries — the unit exists, the slot does not (4); provenance and value are
separable, so a hand-built definition can still create an orphan citation and will be
told about it rather than prevented (5); `revision` is not ordered, so BetterCAD cannot
say a citation is older than the library's (6); traceability completeness is reported
but required by no consumer (7).

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

# INFRA-QT-DEPLOY-001

## Build output outside OneDrive

Evidence:
[docs/verification/INFRA-QT-DEPLOY-001/](docs/verification/INFRA-QT-DEPLOY-001/README.md).

The problem recorded against this decision was:

```text
out-of-source-tree GUI build
→ windeployqt searches for Qt relative to target executable
→ deployment fails
```

**The second line is wrong, and it is the reason this sat blocked for two
milestones.** windeployqt resolves the Qt binary directory through that
directory's 8.3 short name, not from the executable. Copying the same executable
to three unrelated directories produced the *same* reported Qt path, twice with
no relation to where the executable was. The build root used for the first
attempt, `%LOCALAPPDATA%\BetterCAD-build`, shared an 8.3 short name with
`%LOCALAPPDATA%\bettercad-deps` and sorted before it, so the short name resolved
to the build root. Creating one empty directory beside the dependency prefix
reproduces it from any build location; removing it fixes it; a name that sorts
after the dependency prefix, or shares no short name with it, never fails.

The fix does not pretend to repair windeployqt. It refuses the collision at
configure time with a diagnostic that names both directories, refuses a
deployment that reported success without producing a Qt runtime, and adds
`-ext` presets that build into `BETTERCAD_BUILD_ROOT` and fail rather than
quietly building back inside the synchronised folder.

* [x] Root cause established, and the recorded diagnosis corrected
* [x] Qt deployment works from a build tree in any location
* [x] `-ext` presets: configure, build and test resolve to one external tree
* [x] `BETTERCAD_BUILD_ROOT` unset or pointing inside the source tree is refused
* [x] Regression tests, each made to fail on the real defect — 14 tests
* [x] Three-preset qualification from an external build root — 2300/2300 each,
      0 warnings, fresh binaries, 0 stages failed
* [x] File-replacement stress: 48 runs (4 tests x 12), 0 failures
* [x] Evidence recorded

## Carried: a configuration override does not rebuild the geometry it changes

**Found by P15-ASSIGN-001, pre-existing, and it contradicts an ADR-026
consequence.** ADR-026 reasons that "a configuration changes a part's
*dimensions*, so it changes volume and therefore mass -- through geometry, which
is exactly the existing derivation chain and requires nothing new". That chain
does not currently run.

Measured on `BracketModel`, whose `Pad` extrudes a sketch with
`.depthParameter = depth`:

```text
setConfigurationOverride(wide, depth, 40 mm)
setActiveConfiguration(wide)
regenerate()

effectiveParameterValue(depth)  =  0.040   -- the override IS in force
report.regenerated              =  1       -- and it is the SLOT, not the pad
report.updatedParameters        =  1       -- slot_depth = depth * 0.6
volume of Pad                   =  unchanged from the base configuration
```

Root cause, as far as this milestone established it: a configuration override
changes a parameter's EFFECTIVE value and never the parameter object, so the
parameter's own revision does not change and nothing marks a feature that reads
it dirty. `features::Regenerator` contains no mention of configurations at all.
A feature that depends on a DRIVEN parameter is rebuilt, because that parameter's
stored value and revision do change -- which is why the slot rebuilt and the pad
did not.

The assembly layer does not have this problem: `SolveTrigger::ConfigurationChanged`
exists and placements follow a configuration switch.

Why it matters beyond a wrong volume: **P15-MASS-001 computes mass from density
and volume.** Under any non-base configuration it would multiply a correct density
by a stale volume and report a mass that is wrong without saying so.

**P15-MASS-001 shipped a GUARD for this rather than a fix, because the fix is not
authorized here.** `features::partMassProperties` refuses to answer while a
configuration with parameter overrides is active, naming the configuration and the
reason, and it checks that before anything else. So a wrong mass is not reported --
but mass properties are unavailable under a configuration, which is a real
capability gap and the reason this defect is now worth fixing on its own.

When it is fixed, delete the guard and
`MassProperties_RefuseToAnswerUnderAConfigurationThatOverridesAParameter` together,
and replace them with a test that the mass FOLLOWS a configuration switch.

Scope, when authorized:

```text
mark a feature dirty when the EFFECTIVE value of a parameter it reads changes,
which a configuration switch or an override edit can do without touching the
parameter object -- the assembly solver already distinguishes exactly this case
```

A regression test must assert the volume of a feature reading a FREE parameter
changes across a configuration switch. Neither P15-ASSIGN-001 nor P15-MASS-001
asserted the present behaviour, because pinning it would record a defect as the
contract.

**Awaiting explicit scope decision.** It is a regeneration concern, not a
materials one, and it was outside the authorized scope of both P15-ASSIGN-001 and
P15-MASS-001.

## Carried: a document save can still lose to a file synchroniser

**Not fixed by INFRA-QT-DEPLOY-001, and not a test problem.** That milestone
moved BetterCAD's *build output* out of the synchronised folder. It did nothing
for a user whose *documents* are in one, and the fault they would hit is the same
one that cost six milestones a determinism rerun.

`writeFileAtomically` in `src/io/FileIo.cpp` writes a temporary file and then:

```text
std::filesystem::rename(temporary, path, error);
if (error) { remove(temporary); return makeError(IoError, "cannot replace ..."); }
```

One attempt, no retry, and the temporary is discarded. On Windows a rename over
a file another process holds open -- a synchroniser, an indexer, antivirus, a
backup agent -- fails with a sharing violation from that single attempt, and the
holder releases it milliseconds later. So an ordinary save into a OneDrive or
Dropbox folder can fail with `cannot replace '...': Permission denied` and throw
the new bytes away. The old file survives, so nothing is corrupted, but the save
did not happen.

Scope, when it is authorized:

```text
bounded retry with backoff on a TRANSIENT replace failure, distinguished from a
permanent one (no such directory, read-only volume, access denied on the
directory itself) -- which must still fail immediately rather than retry
```

Tests must include a regression that holds a handle open on the target and
proves the retry succeeds, and one proving a permanent failure still fails
rather than retrying to a timeout. Do not widen this into a general I/O layer
rewrite.

**Awaiting explicit scope decision.** It is a product defect, not infrastructure,
and it is not part of any authorized P15 milestone.

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