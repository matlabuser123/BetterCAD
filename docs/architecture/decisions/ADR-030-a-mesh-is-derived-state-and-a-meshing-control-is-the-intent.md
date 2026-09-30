# ADR-030 — A mesh is derived state and a meshing control is the intent

```text
STATUS:    Accepted
DATE:      2026-09-30
MILESTONE: P16-ARCH-001
TOUCHES:   ADR-025 (a material is a document object), ADR-026 (assignment is
           intent and mass is derived), ARCHITECTURE.md's canonical/derived
           split, the Regenerator's ownership of derived bodies
```

## Context

`ARCHITECTURE.md` already places this milestone's central claim in the architecture, before
P16 existed:

> "Geometry, render meshes, drawings, **simulation meshes** and analysis results are derived
> representations of that model. That distinction is the basis of everything below."

and its system overview already lists `Simulations [future]` beside Parameters, Sketches,
Features and Bodies. So the question is not *whether* a mesh is derived. It is what the
**canonical** half is, who owns each half, and when the derived half stops being true.

The audit found that every mechanism needed already exists: `Document::revision()` and
`revisionOf(ObjectId)`, the `Regenerator`'s dirty propagation, and the configuration-override
refusal P15-MASS-001 wrote. Nothing here is new machinery.

P15 settled the shape of this answer once already, for mass. A mass is derived from geometry
and a density and is never persisted as authority; the *assignment* is the intent. A mesh
stands in the same relation to geometry and a set of meshing controls.

## Constraints

- The Document owns canonical engineering state; the GUI owns none.
- A derived representation must never outlive the truth of its inputs. P16's invariant: "A
  stale or failed model must never produce a nominally valid current mesh."
- The configuration-regeneration defect is **carried and unfixed**: a configuration override
  changes a parameter's effective value without marking dependent features dirty, so bodies
  under an active override are the base configuration's.
- Meshing intent must survive save/load and undo/redo exactly, and a generated mesh must not
  be in the file at all.
- A mesh is large. Node and element storage for a real part dwarfs the rest of a document,
  so "persist it and revalidate on load" is not a neutral choice.

## Options

**1. The Document owns both the controls and the generated mesh.** Simple to find, and wrong
for the reason the Regenerator exists: it puts megabytes of derived, invalidatable state into
the object that defines what the part *is*, and every document mutation would have to reason
about it. It also makes "is this document dirty" ambiguous.

**2. Controls and mesh both live in a meshing service, outside the Document.** Keeps the
Document small, but the controls are engineering intent — a chosen element size is a decision a
user made and expects to save, undo and see again. Intent outside the Document is the
GUI-owned-state failure in a new place, and it could not join the dependency graph, so changing
a body could not invalidate a mesh.

**3. The Document owns the controls as document objects; a `Mesher` service owns the generated
meshes.** Exactly the existing split between features (canonical, in the Document) and bodies
(derived, in the Regenerator).

## Decision

**Option 3, following the Regenerator precedent rather than inventing a parallel one.**

**A `MeshControl` is a document object.** It gets a `MeshControlId` that widens to `ObjectId`,
it is a node in the dependency graph, it is created and edited only through commands, and it is
persisted. It is a document object for the same reason ADR-025 made a material one: *"changing
a density must be able to invalidate a derived mass"* — changing a body must be able to
invalidate a derived mesh, and only a graph node can express that.

A control holds *what was asked for*: which body to mesh, the element family and order, global
sizing, local sizing bound to stable geometry references, and quality targets. It holds no
nodes, no elements, no backend handle and no result. The field-level shape belongs to
`P16-DATA-001` and `P16-SIZE-001`; what this ADR fixes is that the control is canonical and
contains only intent.

**A generated mesh is derived state held by a `Mesher` service**, keyed by its control, exactly
as the `Regenerator` holds bodies keyed by their features. It is never a document object, has
no `ObjectId`, and is not in the dependency graph.

**Defaults are the control's, never the backend's.** P16's invariant is that "missing sizing
data must use documented defaults, never accidental backend defaults". So a control resolves to
a complete, documented parameter set *before* the backend is called, and the backend is never
invoked with a field left unset for it to fill in. This is the rule P15 applied to material
properties: a missing value is reported, never silently defaulted.

**Invalidation reuses the revision machinery and is deliberately conservative.** A mesh records
a build stamp — the revision of its control, the revision of its source feature, and the
document revision — and is stale unless all three still match *and* the Regenerator reports
nothing dirty upstream of the source. The document revision is the outer guard: it makes the
mesh stale for changes that could not have affected it, which wastes work and cannot produce a
wrong answer. `P16-GEOM-001` may narrow it with measurement. **It may never widen it.**

Two properties make that rule sound, and both were checked in the tree rather than assumed.

**The revision is monotonic.** Every write to `revision_` in `Document.cpp` is `++revision_`; the
only assignment is a copy preserving it. Nothing decrements it, and **undo increments it too**,
because undo applies its inverse through the same `restore*` entry points. So a stamp can never
match a *different* state that happens to carry an earlier number — there is no ABA hazard, and
after an undo the mesh is recomputed rather than wrongly believed.

**A mesh cannot go stale on disk, because it is never on disk.** Since nothing derived is
persisted, a stale mesh can only exist inside one process lifetime, against one `Document`
instance. That bounds the whole staleness problem to something the revision stamp can actually
see. The one way geometry changes without the document changing — the configuration-override
defect — is the case that is refused outright rather than stamped.

**A mesh under an active configuration override is refused, not computed.** The guard is the
one `src/features/material/MassProperties.cpp` already contains, reused unchanged: if the
active configuration has any overrides, return `FailedPrecondition`. Meshing the base
configuration's geometry and labelling it the override's would be the exact failure P16's
invariant names. When the regeneration defect is fixed, this guard and its tests are removed
together.

**Nothing derived is persisted.** A `.bcad` file holds controls and no mesh: no nodes, no
elements, no facets, no quality numbers, no backend version. A mesh is recomputed and
revalidated, never loaded and trusted. This is checkable the way P15-PERSIST-001 checked it —
a forbidden-token test over a file whose derived state was all computed before saving.

**A mesh is handed to a solver only after it passes validation, and the type system says so.**
A documented rule is not enough here: if the validating path and the inspection path both return
the same `Mesh`, then nothing stops a caller passing an inspected — possibly invalid — mesh to a
solver, and P16's invariant "every solver-facing mesh must pass validation before use" would rest
on a comment. So the separation is by type:

```text
Mesh            the data. May be invalid. What inspection hands back, with its
                validation report, so a FAILED mesh can still be looked at --
                which is the whole point of P16-VIZ-001.

ValidatedMesh   constructible only by the validating path in the meshing module.
                Carries no data of its own; it is evidence about a Mesh.
                Solver-facing APIs take this and nothing else.
```

P17's entry points take `const ValidatedMesh&`. There is then no public way to present an
unvalidated mesh to a solver, and the invariant is enforced by the compiler rather than by review.
The precedent is the Regenerator's, which already refuses to publish a derived transform it is not
sure of: *"It is never a stale one — a transform that is one edit out of date renders, which makes
it worse than nothing."*

## Consequences

- Undo, redo, persistence, dependency invalidation and the dependency graph all come for free,
  because a control is an ordinary document object. `P16-CMD-001` and `P16-PERSIST-001` get
  smaller, not larger.
- A mesh cannot be "restored" by undo, and must not appear to be. Undoing a size change
  invalidates the mesh; the next request recomputes it. P15's undo gate established the
  precedent by requiring a mass to be **recomputed** rather than restored.
- Meshing is unavailable under a configuration override until the carried defect is fixed. That
  is a refusal with a diagnostic, and it is the correct behaviour while the defect stands.
- Documents stay small and no mesh can go stale on disk.
- A cost accepted knowingly: a large mesh is recomputed after any document change, including
  changes that could not have affected it, until `P16-GEOM-001` measures a narrower rule.

## Validation

Owed by the milestones that implement this, and named here so they cannot be forgotten:

```text
P16-DATA-001     a control holds no derived field; a mesh holds no ObjectId
P16-GEOM-001     an override refuses; a stale stamp is detected in each of the
                 three ways it can go stale, one at a time
P16-CMD-001      the full chain undoes and redoes to an exactly equal canonical
                 state, with the mesh RECOMPUTED and not restored
P16-PERSIST-001  save/load/save/load byte-identical; a forbidden-token test over
                 a file whose mesh was generated before the save
P16-QUAL-001     no public path yields an unvalidated mesh for computation
```

## Invariants

```text
A MeshControl is canonical engineering intent, is a document object, and holds
no result.

A generated mesh is derived, is owned by the mesher and not the Document, has no
ObjectId, and is never persisted.

A mesh is stale unless its control, its source feature and the document are all
at the revisions it was built from and nothing upstream is dirty.

Every meshing parameter is resolved from documented BetterCAD defaults before a
backend is called. A backend never supplies a default.

A mesh is never generated under an active configuration override; the request is
refused with a diagnostic.

A mesh reaches a solver only as a ValidatedMesh, which only the validating path
can construct. The rule is enforced by the type system, not by documentation.

A mesher binds to one Document, as the Regenerator does, so a copied document
inherits no mesh.
```
