# ADR-017 — Sheets, views and annotations are document objects, not sub-objects

```text
Status:    Accepted
Date:      2026-09-22
Milestone: P14-ARCH-001
Builds on: ADR-002 (assemblies live in the document)
           ADR-010 (drawings live in the document)
           ADR-014 (drawing objects regenerate)
```

## Context

ADR-010 puts drawings in the document. It does not say at what granularity.
The codebase offers two established shapes, and they are genuinely different.

**The document-object shape.** `Component` and `Mate` are `DocumentObject`s.
Each has an ID from the document's counter that widens to `ObjectId`, each
appears in the dependency graph, each is dirtied independently, each is
serialized as its own `{id, type, name, data}` entry.

**The sub-object shape.** A `Sketch` is a `DocumentObject`; its entities and
constraints are not. `EntityId` and `ConstraintId` are allocated from the
sketch's own allocators, start at 1, are never reused, and **do not widen** —
they are meaningless in another sketch, which is exactly why `FaceSelector`
carries `alongSketch` alongside an entity ID. They are stored in
`std::map<EntityId, Entity>` inside the sketch, serialized as nested arrays
with the owner's high-water mark, and they never appear in the dependency
graph: a sketch's `dependencies()` returns only document-level IDs, with the
owner **aggregating** its sub-objects' dependencies.

A drawing could be built either way. A `Sheet` owning `View`s owning
`Dimension`s mirrors the sketch shape closely, and a drawing has the same
population profile as a sketch — hundreds of small annotated things.

The decisive constraint is ADR-002's, restated: graph participants must be
document objects, or `dependencies()` cannot express them.

## Options

1. **Every drawing concept is a `DocumentObject`.** `Sheet`, `View`,
   `Dimension`, `Annotation`, `Table`, `Balloon`, each with an ID widening to
   `ObjectId`.
2. **`Sheet` is a `DocumentObject`; everything else is a sub-object of it**,
   on the `Sketch`/`Entity` pattern, with the sheet aggregating dependencies.
3. **`Sheet` and `View` are `DocumentObject`s; dimensions and annotations are
   sub-objects of a view.**

## Decision

**Option 1**, with restraint about how many kinds there are.

`Sheet`, `View`, `Dimension` and `Annotation` are `DocumentObject`s.
`SheetId`, `ViewId`, `DimensionId` and `AnnotationId` are tags with
`isDocumentObjectTag` specialised, so they widen to `ObjectId` and are
allocated from the document's counter.

Tables and balloons are **not** new kinds in `P14-ARCH-001`. A BOM table is an
annotation kind and a balloon is an annotation that points at an occurrence;
whether either needs its own identity is `P14-BOM-001`'s to decide, on
evidence, and this ADR declines to allocate types in advance.

## Rationale

The argument that settles it is dependency expression, not aesthetics.

A **dimension depends on model geometry** — the faces it measures, the
parameters that drive them. Under Options 2 and 3 that dependency cannot be an
edge in the graph, because sub-object IDs never appear there; the owner would
have to aggregate it, as a sketch aggregates the `ParameterId`s of its
constraints. That works, and it is what makes the second consequence bite:
**the owner is then the unit of dirtiness.** Editing one dimension's text
position would dirty its view.

For a sketch that is acceptable — editing one entity re-solves the sketch, and
a sketch solve is milliseconds. For a drawing it is not. Under ADR-014 a view
is rebuilt by projecting and hidden-line-removing its bodies, which on an
assembly is the most expensive operation in the phase. Making every annotation
edit dirty its view means moving a dimension's text re-runs hidden-line
removal. That is the wrong cost for the most common interactive edit in a
drawing.

Option 1 also matches the most recent qualified precedent exactly. A `Mate`
depends on the `Component`s it relates and on the geometry its targets name,
and it is a `DocumentObject` for precisely that reason. A `Dimension` depends
on the `View` it sits on and on the geometry it measures. The shapes are the
same, so the identity model should be.

The cost of Option 1 is object count: a drawing with three hundred dimensions
adds three hundred document objects. That was weighed and accepted.
`ObjectId` is a 64-bit counter that is never reused, the document's storage is
a map, and nothing in the graph is quadratic in object count. The visible cost
is in the CLI and a future model tree, where a flat list of three hundred
dimensions is unhelpful — but that is a presentation problem, solved by
grouping on the `View` each dimension names, not by changing where identity
lives.

Option 3 was the closest call, and it fails on the same cost as Option 2: the
dimensions are where the volume and the frequent edits are, so putting them
inside the view puts the expensive rebuild behind the cheapest change.

## Consequences

- Four new ID tags and four `isDocumentObjectTag` specialisations in
  `Id.hpp`, four classes implementing `DocumentObject`, and four arms in each
  of the four dispatch sites ADR-010 enumerated. That is a real cost and it
  is the price of graph participation.
- Every drawing object's name must be unique across the whole document,
  because `Document::requireNameAvailable` spans parameters and objects
  alike. A sheet called `width` collides with a parameter called `width`.
  Drawing objects will want generated default names that do not collide.
- Containment is expressed by reference, not by ownership: a `View` names its
  `Sheet`, a `Dimension` names its `View`. Deleting a sheet therefore does
  **not** cascade — it leaves its views unresolved, exactly as deleting a
  component leaves its mates unresolved (`P13-CMD-001` recorded that same
  behaviour and declined to add cascading deletion). Whether a drawing needs
  cascade is `P14-CMD-001`'s question.
- Undo is per object and free: `AddObjectCommand`/`DeleteObjectCommand`
  already work on any `DocumentObject`, and the `assembly/Commands.hpp`
  pattern — module-owned validating wrappers around the generic commands, one
  history — applies unchanged.
- Configuration overrides on drawing objects, if ever wanted, cost more than
  they look: `ObjectOverrides`, `forgetObject`, `overridesFor`,
  `restoreOverridesFor`, `Configuration::empty()`, `equivalent()` and both
  halves of the configurations JSON all enumerate exactly the kinds they
  know. `P14` does not add drawing suppression, and this records why that is
  not free.
- `bettercad::drawing::Dimension` collides in name with `bettercad::Dimension`
  — the units dimensional-analysis type used as `template <Dimension D>`
  throughout `core`. The namespace resolves it, and the domain word is worth
  keeping, but no drawing header may write `using namespace bettercad;`.

## Rejected alternatives, and what would make them right

**Sub-object dimensions (Options 2, 3)** become right if view rebuilds turn
out to be cheap enough that dirtying a view per annotation edit does not
matter — which is a measurement `P14-REGEN-001` could produce — or if a
drawing's object count turns out to cost more than the rebuild. Either would
be a measured reversal, and the migration is not free: sub-object IDs do not
widen, so the file format would change shape.

## Verification

For `P14`: a drawing object round-trips with the same ID; mixing a `SheetId`
with a `ViewId` does not compile; a dimension is dirtied by a change to the
geometry it measures but **not** by an unrelated annotation edit on the same
view; deleting a sheet leaves its views explicitly unresolved rather than
silently removing them; and the CLI describes every drawing kind, because that
dispatch site fails silently.
