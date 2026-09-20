# BetterCAD — TODO

> `[x]` = implemented + tested + independently validated + adversarially reviewed + regression clean + evidence recorded.
> Qualification milestones also require the final qualified tree to match the committed tree.
> Work top-to-bottom. Stop at failed gates. Never fake evidence.

---

## Status

```text
Current:   P13 — Assemblies
Next:      P13-REGEN-001 — Dependency / regeneration
Then:      P13-CMD-001 — Commands / undo / redo

Released:  v0.1.0 — P0–P10
Qualified: P11, P12

P13 architecture:
ADR-002 → ADR-006
```

---

# Qualified Baseline

## P12 — Parametric CAD Completion

* [x] `P12-PARAM-001` — Parameter expressions
* [x] `P12-SKETCH-001` — Advanced sketch constraints
* [x] `P12-SKETCH-002` — Ellipse and spline entities
* [x] `P12-DATUM-001` — Datum planes, axes and coordinate systems
* [x] `P12-STREF-001` — Stable feature-face references
* [x] `P12-SKETCH-003` — Sketches on feature faces
* [x] `P12-FEAT-001` — Through-all extrude
* [x] `P12-FEAT-002` — Split / combine
* [x] `P12-FEAT-003` — Shell
* [x] `P12-FEAT-004` — Draft
* [x] `P12-FEAT-005` — Rib
* [x] `P12-FEAT-006` — Variable-radius fillet
* [x] `P12-HOLE-001` — Advanced holes
* [x] `P12-PATTERN-001` — Advanced patterns
* [x] `P12-SWEEP-001` — Advanced sweep
* [x] `P12-LOFT-001` — Advanced loft
* [x] `P12-PARAM-002` — Design equations and configurations
* [x] `P12-REF-001` — Production reference models
* [x] `P12-QUAL-001` — P12 qualification

Evidence:

```text
docs/verification/P12-*/
```

P12 qualified on `15d7f75`.

---

# P13 — Assemblies

> Architecture is decided. Implement milestones sequentially.
> Only the current milestone is authorized.

## Completed

* [x] `P13-ARCH-001` — Assembly architecture and contracts
* [x] `P13-COMP-001` — Component definitions and instances
* [x] `P13-XFORM-001` — Component transforms
* [x] `P13-REF-001` — Internal / external reference infrastructure
* [x] `P13-MATE-001` — Basic assembly constraints

Decisions:

```text
ADR-002 — assembly/document model
ADR-003 — internal/external reference contract
ADR-004 — mate-reference semantics
ADR-005 — solve-state / transform policy
ADR-006 — module layering
```

Evidence:

```text
docs/verification/P13-ARCH-001/
docs/verification/P13-COMP-001/
docs/verification/P13-XFORM-001/
docs/verification/P13-REF-001/
docs/verification/P13-MATE-001/
```

---

# DONE — P13-COMP-001

## Component Definitions and Instances

The canonical component-instance model. Evidence in
[docs/verification/P13-COMP-001/](docs/verification/P13-COMP-001/README.md).

* [x] `ComponentInstance` participates as a `DocumentObject`
* [x] Strong `ComponentId` identity
* [x] Component identity distinct from referenced part identity
* [x] Multiple instances may reference one internal part
* [x] Internal part references validated
* [x] Foreign-document references rejected — a component's reference never
  resolves outside its own document, and an ID this document does not have is
  refused `NotFound`, changing nothing. A foreign ID that *collides* with a
  local object is undetectable and binds locally: inherent to `ObjectId`,
  tested, and the reason ADR-003 defers cross-document references to
  `P13-REF-001`.
* [x] Correct dependency-graph participation
* [x] Invalid creation is atomic and recoverable
* [x] Component deletion validated
* [x] Save/load preserves canonical component state
* [x] Pre-P13 files remain compatible
* [x] No unnecessary `.bcad` schema/version change
* [x] Deterministic behavior validated
* [x] Adversarial review PASS
* [x] Debug / Release / Debug-shared regression PASS
* [x] Evidence recorded in `docs/verification/P13-COMP-001/`

### Gate

```text
component model correct
+ identity stable
+ document ownership correct
+ internal references correct
+ persistence PASS
+ legacy compatibility PASS
+ determinism PASS
+ adversarial review PASS
+ full regression PASS
+ 0 unexpected warnings
```

Met: 1282/1282 on `debug`, `release` and `debug-shared` from clean, and
632/632 five times over in `release` and `debug`; 0 compiler warnings; the
adversarial review found and fixed 4 defects, each with a regression test;
the qualified tree IDs match the committed tree.

```text
P13-COMP-001 → [x]
Next → P13-XFORM-001
```

---

# DONE — P13-XFORM-001

## Component Transforms

A component's placement: canonical intent, derived transform. Evidence in
[docs/verification/P13-XFORM-001/](docs/verification/P13-XFORM-001/README.md).

* [x] Define canonical rigid-transform representation
* [x] Implement component local placement
* [x] Keep solved/world transforms derived, not canonical
* [x] Validate transform composition/order
* [x] Validate rotation convention and units
* [x] Multiple instances transform independently
* [x] Validate identity/default placement
* [x] Save/load preserves canonical placement
* [x] Invalid transforms fail atomically
* [x] Deterministic transform results
* [x] Adversarial review PASS
* [x] Debug / Release / Debug-shared regression PASS
* [x] Evidence in `docs/verification/P13-XFORM-001/`

### Gate

```text
rigid-transform model correct
+ canonical/derived state separation correct
+ composition mathematically correct
+ multiple-instance behavior correct
+ persistence PASS
+ failure atomicity PASS
+ determinism PASS
+ adversarial review PASS
+ full regression PASS
+ 0 unexpected warnings
```

Met: 1301/1301 on `debug`, `release` and `debug-shared` from clean, and
708/708 five times over in `release` and `debug`; 0 compiler warnings; the
adversarial review found 3 gaps and 0 production defects, closing two of them
with new regression tests; the qualified tree IDs match the committed tree.

```text
P13-XFORM-001 → [x]
Next → P13-REF-001
```

---

# DONE — P13-REF-001

## Internal / External Reference Infrastructure

Reference identity wider than `ObjectId`. Evidence in
[docs/verification/P13-REF-001/](docs/verification/P13-REF-001/README.md).

* [x] Define canonical reference identity model
* [x] Implement same-document internal references
* [x] Define unresolved-reference state explicitly
* [x] Implement injectable resolver interface for future external references
* [x] Use stable UUID/document identity, never filesystem-path identity
* [x] Keep external-reference resolution separate from `ObjectId`
* [x] Reject accidental cross-document `ObjectId` binding — for a reference
  that carries a document identity. A *bare* `ObjectId` is still defined as
  local and still binds locally; `P13-COMP-001`'s test of that is retained.
* [x] Validate reference type / target compatibility
* [x] Validate missing / deleted target behavior
* [x] Validate reference recovery after target becomes available again
* [x] Prevent silent rebinding to another object
* [x] Preserve references through save/load
* [x] Preserve pre-P13 file compatibility
* [x] Validate deterministic reference resolution
* [x] Validate failure atomicity
* [x] Adversarial review PASS
* [x] Debug / Release / Debug-shared regression PASS
* [x] Evidence in `docs/verification/P13-REF-001/`

### Gate

```text
reference model correct
+ internal references PASS
+ external-resolution contract correct
+ unresolved state explicit
+ no path-based identity
+ no silent rebinding
+ persistence PASS
+ determinism PASS
+ failure atomicity PASS
+ adversarial review PASS
+ full regression PASS
+ 0 unexpected warnings
```

Met: 1323/1323 on `debug`, `release` and `debug-shared` from clean, and
730/730 five times over in `release` and `debug`; 0 compiler warnings; the
adversarial review found 2 gaps and 0 production defects, closing both with
regression tests; the qualified tree IDs match the committed tree.

Carried forward: external references do not regenerate, because the
dependency graph speaks in `ObjectId` (ADR-003). Such a component fails
regeneration with the state that says why, rather than appearing to succeed.

```text
P13-REF-001 → [x]
Next → P13-MATE-001
```

---

# DONE — P13-MATE-001

## Basic Assembly Constraints

The constraint model: seven kinds, what they may point at, and what they
refuse. Nothing moves. Evidence in
[docs/verification/P13-MATE-001/](docs/verification/P13-MATE-001/README.md).

* [x] Implement strong `MateId`
* [x] Implement canonical `MateConstraint` as document state
* [x] Implement Fixed constraint
* [x] Implement Coincident constraint
* [x] Implement Concentric constraint
* [x] Implement Parallel constraint
* [x] Implement Perpendicular constraint
* [x] Implement Distance constraint
* [x] Implement Angle constraint
* [x] Mate targets obey ADR-004 reference rules — enforced by the type: a
  `FaceSignature` has no field it could occupy in a `MateTarget`
* [x] Reject invalid component/reference combinations
* [x] Validate dimensional values and units
* [x] Missing mate targets become unresolved, never silently rebound
* [x] Mate dependencies participate correctly in the document graph
* [x] Invalid mate creation/modification is atomic and recoverable
* [x] Save/load preserves mate engineering intent
* [x] Deterministic mate representation validated
* [x] Adversarial review PASS
* [x] Debug / Release / Debug-shared regression PASS
* [x] Evidence in `docs/verification/P13-MATE-001/`

### Gate

```text
mate model correct
+ all 7 basic constraints represented
+ reference semantics correct
+ units/dimensions correct
+ unresolved-state behavior correct
+ dependencies correct
+ persistence PASS
+ determinism PASS
+ failure atomicity PASS
+ adversarial review PASS
+ full regression PASS
+ 0 unexpected warnings
```

Met: 1349/1349 on `debug`, `release` and `debug-shared` from clean, and
771/771 five times over in `release` and `debug`; 0 compiler warnings; the
adversarial review found 2 gaps and 0 production defects, closing both with
regression tests; the qualified tree IDs match the committed tree.

Carried forward: nothing is solved. A mate is intent, and no mate is checked
for whether it can be satisfied — over-constraint, redundancy and degrees of
freedom are `P13-SOLVE-001`.

```text
P13-MATE-001 → [x]
Next → P13-SOLVE-001
```

---

# DONE — P13-SOLVE-001

## Assembly Constraint Solver

Solve mate intent into component transforms.

```text
P13-MATE-001   stores assembly intent
P13-SOLVE-001  solves that intent into derived component transforms
```

A solved transform is **derived state**: recomputed by regeneration, held
beside the bodies keyed by `ComponentId`, dropped when a component's
regeneration fails exactly as a body is, and **never written to `.bcad`**.

### What ADR-005 already binds

[ADR-005](docs/architecture/decisions/ADR-005-placement-is-intent-transforms-are-derived.md)
decided this milestone's hardest constraints while rejecting the easier
options, and says so in terms. They are not open questions:

* **No seed, no warm start.** ADR-005 considered persisting the last solved
  transform as a solver seed — what the sketch solver actually does — and
  rejected it. The solver must converge **from placement intent alone**.
  ADR-005 calls this "the honest cost" and "a real constraint on
  `P13-SOLVE-001`": intent must be a good enough starting point, and the
  solver must be robust from it.
* **Why the seed was rejected**, so it is not quietly reintroduced: a seeded
  solve depends on save history, so two documents with identical intent could
  solve differently. `P12-PARAM-002` measured warm-starting's path-dependence
  at **1.3e-15** per configuration cycle — bounded, but real enough to be a
  recorded limitation. Repeating a known wart in a subsystem designed from
  scratch would be choosing it deliberately.
* **Deterministic start and deterministic iteration order.** The dependency
  graph already breaks ties by ascending ID; ADR-005 requires "the assembly
  solve must be equally order-free".
* **Grounding is reported, never assumed.** At least one component must be
  grounded or the assembly is free to translate and rotate as a whole —
  "an under-constrained state the solver must *report*, not silently pin".
  Grounding is the `Fixed` mate from `P13-MATE-001`; there is no component
  flag and none is needed.

### Five states, never a boolean

`CLAUDE.md` requires sketch solving to distinguish under-constrained, fully
constrained, over-constrained, inconsistent and solver failure, and to report
conflicts, redundancy, unresolved DOF and residuals. The assembly solver is
held to the same standard: a solve that failed and a solve that succeeded
into an under-constrained assembly are different answers, and collapsing
either into `false` loses the one thing the engineer needs.

* [x] Define solver input/output contracts
* [x] Convert mate intent into a constraint problem
* [x] Implement component DOF representation
* [x] Implement residual evaluation for all 7 basic mate types
* [x] Implement Jacobian / derivative path
* [x] Implement nonlinear solve loop
* [x] Apply solved transforms as derived state only — returned by `solve()`, never written to the document or the file. Holding them beside the bodies and dropping them when a component's regeneration fails needs the assembly regeneration pipeline, which is `P13-REGEN-001`
* [x] Preserve canonical component placement intent
* [x] Detect fully constrained assemblies
* [x] Detect under-constrained assemblies / remaining DOF
* [x] Detect inconsistent / over-constrained systems
* [x] Detect redundant constraints where feasible — linear dependence at the solution, by Gram-Schmidt in mate ID order; non-linear redundancy is not detected
* [x] Validate convergence criteria and tolerances
* [x] Validate deterministic solver results
* [x] Validate failure atomicity / no partial solved state
* [x] Independently validate analytic assembly cases
* [x] Validate unresolved mate/reference handling
* [x] Adversarial review PASS
* [x] Debug / Release / Debug-shared regression PASS
* [x] Evidence in `docs/verification/P13-SOLVE-001/`

### Gate

```text
solver model correct
+ all 7 basic mate types solved
+ residuals/Jacobian validated
+ DOF classification correct
+ under/fully/over-constrained behavior correct
+ canonical/derived state separation preserved
+ convergence validated
+ independent analytical cases PASS
+ failure atomicity PASS
+ determinism PASS
+ adversarial review PASS
+ full regression PASS
+ 0 unexpected warnings
```

ADR-005 also states what verifying it looks like: a saved and reloaded
assembly solves to the same transforms as one built in memory; no transform
appears in the `.bcad` JSON; a parameter change moves a component and
restoring the parameter restores the transform; solving twice gives
identical transforms; and an assembly with no grounded component reports an
under-constrained state rather than choosing one.

Tolerances need a reason, and a Jacobian is not well-conditioned algebra:
expect to justify the convergence tolerance against the conditioning of the
system rather than inheriting `1e-12` from the transform tests.

Met: 1382/1382 on `debug`, `release` and `debug-shared` from clean, and
873/873 five times over in `release` and `debug`; 0 compiler warnings; the
Jacobian agrees with central differences to 3.7e-11 against a 1e-7 gate; every
DOF count is hand-derived and matches; the solver takes a `const Document&`,
so it cannot write back by type, and the `.bcad` file is byte-identical across
a solve. The adversarial review found 4 findings and 0 production defects — the last
of them a link failure that only the `debug-shared` preset could see. The
qualified tree IDs match the committed tree.

All five of ADR-005's verification checks are measured, three of them
bit-identical rather than to a tolerance.

Carried forward: the solved transforms are returned, not applied. Nothing
holds them beside the bodies, no regeneration calls the solver, and a mate's
own value cannot be driven by a parameter. An exactly stationary start is
reported `Inconsistent` when it is merely unreachable — shared with the sketch
solver, recorded, and pinned by a test.

```text
P13-SOLVE-001 → [x]
Next → P13-MATE-002
```

---

# DONE — P13-MATE-002

## Mechanical Mates

Four joints, each defined by the freedom it leaves rather than by the
constraint it adds.

```text
P13-MATE-001   seven basic constraints, stored as intent
P13-SOLVE-001  solves that intent into derived transforms
P13-MATE-002   four mechanical mates, through that same model and that same solver
```

`P13-SOLVE-001` deferred these four explicitly. They are the milestone's
stated scope boundary, not an oversight, and they arrive now with a solver
that can already classify what they do to the degrees of freedom.

### Keep the scope tight

This extends an already-qualified constraint model and an already-qualified
solver. It does not introduce a second one. Concretely, that means: no new
`MateConstraint`; no new solver; no new residual kind where an existing one
composes; no new reference vocabulary beyond ADR-004.

The solver already has six residual kinds — `Parallel`, `Perpendicular`,
`Angle`, `OffsetAlong`, `OffsetPerpendicular` and `SeparationPerpendicular` —
and the rank-honest equation counts to go with them. A mechanical mate that
needs a seventh needs a reason.

### Two of the four may already be expressible

`P13-SOLVE-001` measured what the existing equation sets leave free:

```text
Concentric            → 2 DOF   (slide along the axis, spin about it)
Coincident (planes)   → 3 DOF   (two in-plane translations, spin about the normal)
```

Those are the DOF contracts below for `Cylindrical` and `Planar`, exactly.
If that holds under scrutiny, the work for those two is intent, naming and
validation rather than new equations — the same relationship `Concentric`
already has with an axis-to-axis `Coincident`, which produces identical
equations and differs only in what the engineer meant.

Verify it rather than assume it. `Revolute` and `Slider` both need 5
independent equations against a free component's 6 unknowns, and neither is
an existing set.

* [x] Implement Revolute mate
* [x] Implement Slider mate — the one that needed a roll reference
* [x] Implement Cylindrical mate
* [x] Implement Planar mate
* [x] Define exact allowed DOF for each mate — 1/1/2/3, each predicted from rigid-body reasoning before it was measured
* [x] Reuse existing `MateConstraint` / solver architecture — no new solver, status or entry point; the joints are rows in the same matrix
* [x] Validate mate target/reference compatibility
* [x] Convert each mechanical mate into solver constraints
* [x] Validate residuals for each mate type
* [x] Validate Jacobians / derivatives — worst new mate 5.41e-12 against a 1e-7 gate
* [x] Validate remaining DOF analytically — and which motion survives, not only how many
* [x] Validate solved transforms independently
* [x] Validate mate combinations with basic constraints
* [x] Detect contradictory mechanical mates
* [x] Validate unresolved-reference behavior
* [x] Validate failure atomicity
* [x] Save/load preserves mechanical-mate intent — including the roll reference, without which a slider reloads as a sleeve
* [x] Deterministic solver behavior validated
* [x] Adversarial review PASS
* [x] Debug / Release / Debug-shared regression PASS
* [x] Evidence in `docs/verification/P13-MATE-002/`

### Expected DOF contract

```text
Revolute
→ 1 rotational DOF

Slider
→ 1 translational DOF

Cylindrical
→ 1 translational + 1 rotational DOF

Planar
→ 2 in-plane translation + 1 normal-axis rotation DOF
```

This contract is directly checkable: the solver reports
`degreesOfFreedom = unknowns - rank(Jacobian)`, so each mate applied to one
free component against a grounded one must leave exactly the count above —
6 unknowns less 5, 5, 4 and 3 independent equations respectively. Derive the
expected count from the geometry and assert the literal, as
`P13-SOLVE-001` did; never read it back from the solver.

Watch the rank-honesty trap that milestone hit: a constraint formulated as
three rows of rank two makes its mate look permanently redundant and turns a
correct assembly into `OverConstrained`. Every mechanical mate's equation
count must equal its rank.

### Gate

```text
all 4 mechanical mates correct
+ DOF semantics correct
+ solver integration correct
+ residual/Jacobian validation PASS
+ independent analytical validation PASS
+ contradictory systems handled correctly
+ unresolved references handled correctly
+ persistence PASS
+ failure atomicity PASS
+ determinism PASS
+ adversarial review PASS
+ full regression PASS
+ 0 unexpected warnings
```

The derivative gate applies unchanged: every new residual's analytic
Jacobian is verified against central differences before anything is built on
it. A Gauss-Newton solver with a wrong Jacobian converges, reports success,
and puts the parts somewhere plausible and wrong.

Met: 1421/1421 on `debug`, `release` and `debug-shared` from clean, and
914/914 five times over in `release` and `debug`; 0 compiler warnings; every
joint's Jacobian agrees with central differences to 5.41e-12 or better against
a 1e-7 gate; every DOF count hand-derived and matched; all 30 `P13-SOLVE-001`
solver cases unchanged. The adversarial review found 3 findings and 0
production defects. The qualified tree IDs match the committed tree.

The architectural finding, since it shaped the result: a prismatic joint needs
three rotational constraints and a single target pair affords at most two, so
`Slider` carries a roll reference and the other three do not. Raised before
implementing, three candidates weighed, recorded in the evidence.

Carried forward: no joint distinguishes the sense of its axis or normal — they
are built on `Parallel` rows, satisfied either way round. A slider whose roll
reference lies along its own axis is reported `OverConstrained` with the mate
named, rather than silently behaving as a sleeve. No joint carries a position
or a limit; those come from a `Distance` or `Angle` mate beside it. And
nothing consumes the solved transforms yet.

```text
P13-MATE-002 → [x]
Next → P13-CONF-001
```

---

# DONE — P13-CONF-001

## Assembly Configurations / Suppression

One assembly describing a family of builds: which components and which mates
are in force, per configuration.

```text
P13-MATE-001   the constraints, stored as intent
P13-SOLVE-001  solved into derived transforms
P13-MATE-002   four mechanical joints through the same solver
P13-CONF-001   which of all that is in force, per configuration
```

### What already exists, checked

Three of the checklist items are not starting from nothing, and knowing which
is which decides how big this milestone is:

| Item | State today |
| --- | --- |
| Mate suppression | **Exists.** `MateDefinition::suppressed`, honoured by the solver — a suppressed mate contributes no equations, tested in `P13-SOLVE-001` and `P13-MATE-002` |
| Component suppression | **The flag exists**, `ComponentDefinition::suppressed`, documented as "not in this build". **The solver ignores it** |
| A configuration model | **Exists.** `ConfigurationId`, `Configuration`, `CreateConfigurationCommand`, `ModifyConfigurationCommand` — P12-PARAM-002, for parameter overrides |

So the work is not "add suppression". It is **making suppression
configuration-dependent**, and closing the one real gap below.

### The gap that exists today

`System::build()` skips suppressed *mates* and does not skip suppressed
*components*: the loop over `components(document)` gives every component six
unknowns whether or not it is suppressed. A suppressed component therefore
still inflates the reported degrees of freedom.

That is unimplemented scope rather than a defect — `P13-SOLVE-001` was not
asked about suppression, and this milestone is where it belongs — but it is a
behaviour change to an already-qualified solver, so it needs its own
regression test and a note in the evidence saying what moved.

### Settle this before implementing

**Is an assembly configuration the existing `Configuration`, or a new one?**

The checklist says "implement strong `AssemblyConfigurationId`", and there is
already a `ConfigurationId`. Two configuration systems in one document would
be the second-system mistake `CLAUDE.md` names outright, and would force every
later question — which is active, what does switching mean, what does a file
hold — to be answered twice and kept in step.

The existing model is worth reading before deciding, because it already
solves this milestone's hardest problem. P12-PARAM-002's configurations are
**overrides on top of base values, never edits to them**:

```text
base values -> the active configuration's overrides -> what is in force
```

Its own header states the consequence: "switching Small -> Large -> Small
restores Small exactly: the base values never moved." That is precisely what
"switching configuration updates active assembly state deterministically" and
"configuration switching is atomic and recoverable" require, and it is
already qualified.

The natural reading is that `suppressed` is a base value like a parameter's,
a configuration carries an override for it, and what is in force is the base
with the override applied — the same shape, extended to a second kind of
overridable state. Put up the alternatives and compare them properly, but do
not add a second configuration concept without showing why this one cannot
carry it.

* [x] Define canonical assembly configuration model — overrides on the existing `Configuration`, base state never edited
* [x] Implement strong `AssemblyConfigurationId` — **delivered as `ConfigurationId`, not a second type.** A deliberate deviation, decided under CLAUDE.md's rule for architecturally significant choices and recorded in [ADR-007](docs/architecture/decisions/ADR-007-one-configuration-system.md): assembly configurations are the configurations this document already had, so a second identity type would identify nothing new
* [x] Add component suppression per configuration
* [x] Add mate suppression per configuration
* [x] Preserve default/base configuration behavior — and a parameter-only file gains no new keys
* [x] Switching configuration updates active assembly state deterministically — ten round trips, bit-identical
* [x] Suppressed components are excluded from solve participation — closing a gap that existed before this milestone: the flag was there and the solver ignored it
* [x] Suppressed mates are excluded from solver equations
* [x] Dependencies remain valid across configuration changes
* [x] Unresolved references handled correctly when components are suppressed — a mate on a suppressed component is **inactive**, not unresolved
* [x] Save/load preserves configurations and suppression state
* [x] Configuration switching is atomic and recoverable
* [x] Deterministic configuration results validated
* [x] Adversarial review PASS
* [x] Debug / Release / Debug-shared regression PASS
* [x] Evidence in `docs/verification/P13-CONF-001/`

### Gate

```text
configuration model correct
+ component suppression correct
+ mate suppression correct
+ solver participation correct
+ dependency behavior correct
+ persistence PASS
+ switching atomicity PASS
+ determinism PASS
+ adversarial review PASS
+ full regression PASS
+ 0 unexpected warnings
```

### Keep the scope tight

This milestone is **configuration and suppression state**: what is in force,
and what switching means. Not stable assembly references, and not the
regeneration pipeline — those are `P13-STREF-001` and `P13-REGEN-001`, and
they come next.

One thing to watch that suppression makes newly possible: a mate whose target
sits on a suppressed component. That is not an unresolved reference — the
component is still in the document with its ID and its geometry intact, which
is what suppression means here. Deciding what the solve does with such a mate
is this milestone's, and it is the case most likely to be got quietly wrong,
because "suppressed" and "missing" look alike from inside the solver and mean
entirely different things to the engineer.

Met: 1446/1446 on `debug`, `release` and `debug-shared` from clean, and
946/946 five times over in `release` and `debug`; 0 compiler warnings; every
active set, DOF count and status hand-derived before it was measured; ten
switching round trips bit-identical. All 42 of `P12-PARAM-002`'s
configuration tests pass against the changed load order. The adversarial
review found 1 finding — a real defect in this milestone's own code, found by
its tests — and 0 others. The qualified tree IDs match the committed tree.

The decision that shaped it: there is no `AssemblyConfigurationId`. One
configuration system, so the `Large` build is wider **and** has the bracket,
said once. ADR-007 records the two candidates rejected.

Carried forward: suppression does not propagate beyond an object's own mates,
so a component positioned only by a mate to a suppressed component becomes
free rather than suppressed in turn. There are no undoable commands for
suppression — that is `P13-CMD-001`. And nothing regenerates in response to a
configuration change; switching changes what a *solve* returns, and
`P13-REGEN-001` is what will react to it.

```text
P13-CONF-001 → [x]
Next → P13-STREF-001
```

---

# DONE — P13-STREF-001

## Stable Assembly References

A reference survives the model changing under it, or it is honestly broken.
Never quietly attached to something else.

```text
P13-REF-001    the reference vocabulary and the resolver
P13-MATE-001   mates reference semantic geometry only (ADR-004)
P13-CONF-001   suppression, which a reference must now survive
P13-STREF-001  and it must survive everything else too
```

### What already exists, checked

Most of this checklist is not starting from nothing. Three earlier milestones
built the machinery; knowing which parts are built decides what this one is
actually for.

| Item | State today |
| --- | --- |
| Stable face identity | **Built.** `FaceName` is a feature ID plus a role, not an index — "the persistent name of a face: the feature that generates it and the face's role there" (P12-STREF-001) |
| No raw topology identity in mates | **Enforced by the type.** A `MateTarget` holds a `PlaneReference`, an `AxisReference` or a `FaceName`, and has no field a `geometry::FaceSignature` could occupy. `MateReference.hpp` says why it is refused: a signature matches a plane in model space, not a named face |
| Stable component references | **Built.** `ComponentDefinition::part` is an `ObjectReference`: an `ObjectId`, never an index |
| Cross-document identity | **Contract built.** `ObjectReference` carries an optional `DocumentId` plus a locator, and `ReferenceResolver` distinguishes Resolved, DocumentUnavailable, **DocumentMismatch** and ObjectMissing — with ADR-003's rule that a locator leading to the wrong document is a failed reference, never a match |
| No silent rebinding | **Built and ticked** in `P13-MATE-001`: missing mate targets become unresolved, never rebound to nearest geometry |

So the weight of this milestone is **validation of what exists across the
changes that can now happen to it**, plus the specific gaps below. Do not
rebuild any of the above; if something needs extending, extend it.

### Where the real work is

Three things have never been measured end to end, and one of them only became
possible last milestone:

* **Across configuration switching.** `P13-CONF-001` landed suppression, so a
  mate target can now sit on a component that is suppressed in one build and
  present in another. That mate is *inactive*, not *unresolved* — the
  distinction is recorded in `docs/verification/P13-CONF-001/`. What is not
  yet proven is that its target still resolves to the **same geometry** after
  a switch there and back, rather than merely still resolving.
* **Across regeneration of the part beneath a component.** A mate naming
  `FaceName{feature, EndCap}` should follow that face when the feature
  regenerates with different parameters, and should become unresolved — not
  rebound — when the role stops existing. `P12-STREF-001` guarantees this for
  features; that it holds through a component instance is the assembly claim,
  and it is the heart of this milestone.
* **Recovery when the intended target returns.** Unresolved must be a state,
  not a death: restoring the geometry must restore the reference, with the
  same binding it had before. A reference that stays broken after its target
  comes back is as wrong as one that rebinds to a stranger.

### Two checklist items that need scoping, not implementing

**"Preserve references across regeneration"** — assembly regeneration is
`P13-REGEN-001` and is not this milestone. What is in scope is regeneration
of the *part* beneath a component, which exists today. Say which is meant in
the evidence rather than implying the other was done.

**"Validate cross-document identity rules"** — `TODO.md`'s accepted P13
constraints state that assemblies operate inside one `Document` and
cross-document dependencies are not implemented. So this is validation of the
**contract** — that a mismatched `DocumentId` is refused, that a locator is
never identity — and not an end-to-end external reference. Anything more
would be building `P13` external references ahead of their milestone.

- [x] Define stable assembly-reference model — three schemes, none index-based, all pre-existing
- [x] Implement stable component references
- [x] Implement stable mate-target references — one shared resolution path, which the solver now calls instead of its own
- [x] Preserve references across save/load
- [x] Preserve references across configuration switching — resolves to the *same* geometry after a round trip, compared exactly
- [x] Preserve references across regeneration — of the part beneath a component; assembly regeneration is `P13-REGEN-001`
- [x] Reject raw topology/index-based identity — enforced by the type, not by a check
- [x] Prevent silent rebinding after geometry changes — measured against an identically-dimensioned replacement at the same plane
- [x] Missing intended target becomes explicit unresolved state — new `unresolvedMateTargets()`, the mate counterpart of `unresolvedComponents()`
- [x] Validate reference recovery when intended target returns
- [x] Validate cross-document identity rules — the contract, per the accepted P13 constraints
- [x] Validate deterministic reference resolution
- [x] Validate failure atomicity
- [x] Adversarial review PASS
- [x] Debug / Release / Debug-shared regression PASS
- [x] Evidence in `docs/verification/P13-STREF-001/`

### Gate

```text
stable reference model correct
+ component/mate references stable
+ no raw topology identity
+ no silent rebinding
+ unresolved/recovery behavior correct
+ persistence PASS
+ configuration/regeneration stability PASS
+ determinism PASS
+ failure atomicity PASS
+ adversarial review PASS
+ full regression PASS
+ 0 unexpected warnings
```

### The failure this milestone exists to prevent

A mate that still solves, on the wrong face. It is the worst failure in the
system because nothing reports it: the solve succeeds, the assembly looks
plausible, and the parts are in the wrong places. Every test here should be
built to catch **that**, not to confirm that resolution returns something.

Which means: an assertion that a reference "still resolves" is nearly
worthless on its own. Assert **which geometry** it resolved to.

Met: 1461/1461 on `debug`, `release` and `debug-shared` from clean, and
1009/1009 five times over in `release` and `debug`; 0 compiler warnings; every
expected geometry derived by hand from the sketch and the extrude depth rather
than from the resolver under test. The adversarial review found 0 production
defects; its two findings are about the tests. The qualified tree IDs match
the committed tree.

Most of this milestone was already built — by `P12-STREF-001`,
`P13-REF-001` and `P13-MATE-001` — and the survey of what existed is in the
evidence. What was missing was the mate half of the reporting: a document
could report a broken *part* reference and open anyway, but a broken *mate
target* could only be discovered by attempting a solve. The solver ended up
17 lines shorter, because its inline resolution became the shared one rather
than a second path beside it.

Carried forward: a reference can be reported but not repaired — rebinding is
a commands question, `P13-CMD-001`. A face target needs the current bodies and
fails rather than guessing without them. External references remain a
validated contract and not a capability. And assemblies themselves still do
not regenerate, which is `P13-REGEN-001`.

```text
P13-STREF-001 → [x]
Next → P13-REGEN-001
```

---

# NEXT — P13-REGEN-001

## Dependency / Regeneration

The milestone that finally consumes the solver.

```text
P13-SOLVE-001   solves mate intent into derived transforms
P13-MATE-002    four joints through that solver
P13-CONF-001    which of it is in force
P13-STREF-001   and the references hold
P13-REGEN-001   and something finally calls it
```

Every one of those four carried the same line forward: *nothing consumes the
solved transforms yet -- they are returned, not applied, and no regeneration
calls the solver.* This is where that closes.

### What already exists, checked

`features::Regenerator` is not a stub. It already does the whole of
dependency-driven regeneration, and its own header states it:

| Capability | State today |
| --- | --- |
| Dirty tracking | **Built.** Items whose revision changed since they were last built are found, marked, and everything downstream with them |
| Only what changed | **Built.** "rebuilds only the dirty items, dependencies first. Unaffected items keep their results" |
| Ordering | **Built.** Evaluation in dependency order, reported in `rebuilt` |
| Cycles | **Built.** Members of a cycle are `Failed`, and `cycles` reports the groups |
| Blocked downstream | **Built.** Downstream of a failure, a missing reference or a cycle is `Blocked`, not silently skipped |
| Per-item errors | **Built.** `state()` and `error()` per object |
| A solver being called by regeneration | **Built, for sketches** — the "sketch" handler applies driving parameters and solves. That is the precedent to follow |
| A component handler | **Built** (`P13-REF-001`) — it resolves the part so that an unresolvable one fails loudly instead of silently. It produces no body |
| A mate handler | **Missing.** Mates are inert during regeneration |
| An assembly solve | **Missing.** Nothing calls `assembly::solve()` |

So four of the checklist's items — affected-state propagation, ordering,
cycles, per-item failure — are largely *validating* machinery that exists and
is qualified, for the assembly objects that now flow through it. Do not
rebuild any of it.

### Settle this before implementing

**A sketch solves per object. An assembly does not.**

The `Regenerator`'s unit of work is one object: a handler takes an `ObjectId`
and returns that object's body. That fits a sketch, which owns its own solve,
and it fits a feature. It does not obviously fit an assembly solve, which
spans every component and mate at once and produces transforms keyed by
`ComponentId` — there is no single object whose handler it is.

And the layering constrains the answer: `features` is layer 2 and `assembly`
is layer 3, so `Regenerator` cannot call `assembly::solve()` itself. Assembly
injects itself through `registerHandlers()`, which is the ADR-006 pattern and
the only door available today.

Candidates to weigh, and none is obviously right:

* **A handler on one designated object.** Which one? Picking a component makes
  that component special for no modelling reason, and solving once per
  component would solve the assembly N times.
* **A post-pass hook** the assembly module registers, running after the
  bodies are built. Fits the layering, but adds a second phase to a
  pipeline whose contract today is one pass over a graph.
* **Assembly owns its own derived store**, updated by whoever drives
  regeneration. Keeps `features` ignorant, but then "regenerate" no longer
  means one call, and every caller has to remember the second one.
* **A document-level derived result** in the `Regenerator`, stored opaquely.
  Generalises the machinery, and is the largest change.

ADR-005 already fixes part of the answer and should be read first: the solved
transform is derived state, "held beside the bodies keyed by `ComponentId`,
dropped when a component's regeneration fails exactly as a body is". That
says where the transforms live and how they die; it does not say who triggers
the solve. Compare the candidates properly and record the choice as an ADR.

### Two items that need scoping

**"Regenerate only affected assembly state"** — the assembly solve is
*global*. One mate's value changing moves the whole system, because that is
what a constraint system is. So the honest granularity is "the assembly
re-solves, or it doesn't", and the affected-state question is whether the
solve is triggered at all, not which components it recomputes. Say that in
the evidence rather than implying a per-component incrementality that the
mathematics does not support.

**"Handle unresolved references explicitly"** — `P13-STREF-001` built
`unresolvedMateTargets()` and `P13-REF-001` built `unresolvedComponents()`.
This milestone's job is that regeneration *uses* them: a mate whose target
does not resolve should make regeneration report it, the way the component
handler already makes an unresolvable part report. A mate handler is the
obvious shape, and mates currently have none.

- [ ] Define assembly regeneration contract
- [ ] Integrate component dependencies into regeneration
- [ ] Integrate mate dependencies into regeneration
- [ ] Regenerate only affected assembly state
- [ ] Re-solve assemblies when required dependencies change
- [ ] Preserve canonical intent; update derived solve state only
- [ ] Handle suppressed components/mates correctly
- [ ] Handle unresolved references explicitly
- [ ] Detect dependency cycles / invalid dependency states
- [ ] Validate regeneration ordering
- [ ] Validate failure atomicity and recovery
- [ ] Validate deterministic regeneration
- [ ] Validate save/load + regenerate behavior
- [ ] Adversarial review PASS
- [ ] Debug / Release / Debug-shared regression PASS
- [ ] Evidence in `docs/verification/P13-REGEN-001/`

### Gate

```text
dependency/regeneration model correct
+ affected-state propagation correct
+ solve triggering correct
+ canonical/derived separation preserved
+ suppression behavior correct
+ unresolved references handled explicitly
+ cycle/error handling correct
+ ordering correct
+ failure atomicity PASS
+ determinism PASS
+ persistence/regeneration PASS
+ adversarial review PASS
+ full regression PASS
+ 0 unexpected warnings
```

### The failure this milestone exists to prevent

**Stale derived state that looks current.** A transform left over from before
a mate changed is worse than no transform at all: the assembly renders, the
positions are plausible, and they are answers to a question nobody asked any
more. It is the same shape as `P13-STREF-001`'s wrong-face failure, and it
wants the same discipline — assert *which* transforms are in force after a
change, never merely that a solve happened.

The companion failure is its opposite: re-solving when nothing relevant
moved, which is not wrong but is how a CAD system becomes unusable on a large
assembly. Both need measuring.

Only then:

```text
P13-REGEN-001 → [x]
Next → P13-CMD-001
```

---

# Planned P13 Sequence

```text
P13-ARCH-001     Assembly architecture and contracts          DONE
P13-COMP-001     Component definitions and instances          DONE
P13-XFORM-001    Component transforms                         DONE
P13-REF-001      Internal / external reference infrastructure DONE
P13-MATE-001     Basic assembly constraints                   DONE
P13-SOLVE-001    Assembly constraint solver                   DONE
P13-MATE-002     Mechanical mates                             DONE
P13-CONF-001     Assembly configurations / suppression        DONE
P13-STREF-001    Stable assembly references                   DONE
P13-REGEN-001    Dependency / regeneration                    OPEN
P13-CMD-001      Commands / undo / redo
P13-PERSIST-001  Save / load assembly intent
P13-CLI-001      Headless assembly workflows
P13-STEP-001     Assembly STEP export / read-back
P13-REFMOD-001   Production assembly reference models
P13-QUAL-001     Full P13 qualification
```

Do not implement a milestone until its predecessor passes.

---

# Accepted P13 Constraints

* Assemblies currently operate inside one `Document`.
* Cross-document dependencies are not implemented.
* Future external references use stable identity/resolver concepts, not filesystem-path identity.
* Component configuration remains document-global for now.
* A component cannot independently select another part configuration yet.
* Mate targets may only use reference types allowed by ADR-004.
* Missing intended geometry must fail explicitly; never rebind to nearest geometry.
* Solved assembly positions are derived state and are not authoritative persisted intent.
* Opening/regenerating an assembly may therefore require a solve.

---

# Deferred CAD Work

Not currently authorized:

* Fillet setback controls
* Selectable fillet corner transitions
* Loft end conditions
* Full semantic topology
* STEP import
* DXF / IGES / OBJ interoperability

---

# Future Phases

```text
P14  Technical Drawings
P15  Materials / Engineering Data
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

Do not start without explicit authorization.

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
→ authorized/current work

ROADMAP.md
→ long-term direction

ARCHITECTURE.md
→ system architecture and invariants

CLAUDE.md
→ engineering process and Definition of Done

docs/architecture/decisions/
→ durable architectural decisions

docs/verification/<milestone>/
→ implementation and qualification evidence

docs/engineering/
→ reusable engineering templates
```
