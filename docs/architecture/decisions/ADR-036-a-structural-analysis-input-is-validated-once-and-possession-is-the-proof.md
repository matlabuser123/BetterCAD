# ADR-036 — A structural analysis input is validated once, and possession is the proof

```text
STATUS:    Accepted
DATE:      2026-10-06
MILESTONE: P17-ARCH-001
TOUCHES:   ADR-030 (a mesh is derived state), ADR-028 (a solver holds no
           material data), ADR-032 (a boundary names a CAD face)
```

## Context

P16 closed almost everything about mesh currency and left one gap it could not
close from inside:

> nothing FORCES a holder to call `isStale`. A `VolumeMesh` records its source
> and revision and answers truthfully, and the request path refuses stale
> geometry, so this bites only code that CACHES a mesh.

P17 is that code. And the gap is not an oversight — `Mesher::mesh()` returns a
stale mesh **deliberately**, because `P16-VIZ-001` inspects stale meshes and the
documented position is that such a mesh "is old, not wrong".

So a structural function with this signature cannot be made safe:

```cpp
solve(const VolumeMesh& mesh);               // cannot tell current from stale
```

The caller possesses a mesh. That is all the signature knows. A mesh of a body
the user changed ten edits ago is indistinguishable from one generated a
moment ago, and both are internally valid. A solver taking one would compute a
correct answer to the wrong question and report success — the failure mode
`ADR-030` was written to prevent, arriving one layer later.

The same shape of problem applies to the material. `ADR-028` already forbids a
solver holding material data of its own, but a function taking
`(mesh, E, nu)` invites exactly that: the caller resolves the material, and
nothing checks that it resolved it from this document, or at all.

## Constraints

- `Mesher::mesh()` must keep returning stale meshes. P16-VIZ-001 is qualified
  behaviour and depends on it.
- P16 remains the authority on mesh validity. P17 must not re-derive signed
  volumes, conformity or orientation; `ADR-033` put the mesher behind a
  boundary for the same reason.
- P15 remains the authority on material data (`ADR-028`), including the ranges:
  `requireLinearElasticConstants` already refuses a non-finite or non-positive
  Young's modulus and a Poisson ratio outside `-1 < nu < 0.5`.
- Geometry eligibility is P16's one boundary, and it already refuses a body
  behind a configuration override. P17 must inherit that refusal rather than
  reimplement it, because the carried configuration defect means a cached mesh
  of an overridden configuration describes the base configuration's body.
- Nothing in this milestone may perform a solve.

## Options

### A — take a document, a regenerator, a mesher and a control, and re-check inside the solver

Every entry point validates. Honest, and it works, but it is a rule each future
entry point must remember: `P17-SOLVE-001`, `P17-POST-001`, a CLI command and a
GUI action would each have to repeat it, and the one that forgets is the defect.

### B — a validated-input capability type, constructible only by the validator

One function checks; it returns a type with no public constructor. Anything that
wants to consume a structural input takes that type, and a function that wants
to skip the checks cannot construct its argument.

### C — a currency flag on the analysis, set when it was last checked

Cheap, and wrong in the way P16 already identified about `state()`: a flag
records what *was* true at some past moment, and the question is whether it is
true now. It would be a second source of truth about currency alongside
`MeshCurrency`, and the two would drift.

## Decision

**Option B.** `requireStructuralModel` is the single boundary, and
`StructuralModel` is the proof.

```cpp
Result<StructuralModel> requireStructuralModel(
    const Document& document, const features::Regenerator& regenerator,
    const meshing::Mesher& mesher, MeshControlId control);
```

`StructuralModel` has no public constructor and one friend. Possession proves,
at the moment of construction:

```text
the control exists
the body is eligible -- regenerated, current, a solid, non-empty, valid, and
  NOT behind a configuration override
a mesh is held for the control and its currency is Current
the mapping and the quality report came from that same mesh, in one lookup
the document's material resolves and yields complete linear-elastic constants
```

The device is not new here. `VolumeMesh` and `GeometryMeshMap` are built the
same way, and `P16-DATA-001` recorded the reasoning as *possession is the
evidence*. Reusing it was preferred to inventing a second convention.

### What the order of checks commits to

Geometry eligibility is asked **before** anything is read off the mesh, which
is the ordering `requireMeshableGeometry` already argues for: a mesh of a body
the user has already changed should be reported as describing a stale model, not
as whatever else happens to be wrong with it. A boundary that asked the mesh
first would, for a deleted body, report a perfectly healthy mesh.

### What possession does NOT prove

```text
mesh quality acceptable for an accurate answer     P17-VALID-001
loads resolve                                      P17-LOAD-001
restraints resolve                                 P17-BC-001
the model is sufficiently constrained              P17-SOLVE-001
```

Those gates are added to this function by the milestones that define their
data. The type is where they land, and adding one strengthens every consumer at
once rather than each remembering.

### Delegation, not reimplementation

```text
geometry currency   meshing::geometryIneligibility  -- P16's reason, unchanged
mesh currency       Mesher::currency + describesTheModel
mesh validity       NOT re-derived. P16 refuses to HOLD a mesh that fails its
                    structural verdict, so a Current mesh is valid by
                    construction and this boundary relies on that contract
material            features::requireEffectiveMaterial +
                    requireLinearElasticConstants -- resolution AND range
                    validation are P15's
```

Nothing here stores a modulus, a default or a typical value. The four elastic
constants are held **by value** as a derived view of P15's canonical data, which
is what `ADR-028` permits and what a second material record would not be.

### What quality means at this boundary

`MeshQualityReport::satisfiesPolicy()` answers the question the *mesh control*
asked, under thresholds P16 ships empty on purpose — so it is true for any
structurally valid mesh. A structural acceptance policy therefore reads
`summaries`, which carry every measured minimum, maximum, mean and worst
element, and applies thresholds **P17** is responsible for. P16 measures; P17
judges. `P17-VALID-001` owns the numbers, and this milestone deliberately
invents none.

## Rationale

Option A's weakness is not that it fails, but that it fails *later* and
*silently*. The check is correct wherever it is written; the defect is the entry
point where nobody wrote it. Option B converts that from a review obligation
into a compile error, and the cost is one small type.

Against Option C: a cached verdict about currency is the mistake P16 already
documented about `state()` — it reports what the last pass did, not whether the
answer is current. A second answer to a question `MeshCurrency` already answers
would drift from it, and the drift would favour the stale answer.

What would have made Option A right: if there were exactly one consumer,
forever. There will be at least a solver, a post-processor, a CLI and a GUI.

The borrowed-reference design is a real cost and is stated rather than hidden: a
`StructuralModel` must not outlive the `Mesher`'s held mesh or survive an edit.
That is the lifetime `Mesher::mesh()`'s pointer already has, so the convention is
not new, and re-preparing is cheap — it is a handful of lookups and one material
resolution, with no geometry or mesh work. Re-preparing rather than holding is
the intended usage.

## Consequences

**Easy.** A later milestone adding a gate writes one check in one function.
Every consumer gets it.

**Hard.** A caller who legitimately wants to inspect a stale structural input —
"what would this have given?" — cannot use this type. That is deliberate for
now; if an inspection story needs it, it needs its own type with its own name
saying so, not a weakening of this one.

**Committed.** To `requireStructuralModel` being the only constructor of a
structural input, and to that function's check list only ever growing. Removing
a check from it silently weakens every consumer, which is why the list is in the
documentation and each entry is tested by name.

## Verification

`tests/structural/StructuralAnalysisBoundaryTests.cpp`, nine cases:

```text
a current mesh with a usable material        ACCEPTED, and possession carries
                                             the mesh, a complete map whose
                                             stamp matches it, the quality
                                             report and P15's four constants
a mesh of geometry that has since changed    REFUSED -- and the sharp case:
                                             the document is REGENERATED, so
                                             the geometry is current again and
                                             the only stale thing is the mesh
a mesh whose sizing intent changed           REFUSED
after a failed generation                    REFUSED as the failure, not as
                                             staleness
an ineligible body with a mesh still held    REFUSED on geometry, proving the
                                             check order
nothing meshed / unknown control             REFUSED
no material / unusable material              REFUSED, with P15's diagnostic
every declared InputProblem                  reached by a case above
```

Two further `InputProblem` values were drafted and **deleted** when the audit
showed nothing could return them: a held mesh is never structurally invalid
because `Mesher::generate` refuses to hold one, and a held mesh's mapping is a
member of the same struct so it can be neither absent nor mismatched. An enum
value nothing can return is a placeholder, and this ADR records their removal
rather than their presence.

Evidence: [docs/verification/P17-ARCH-001/](../../verification/P17-ARCH-001/README.md).
