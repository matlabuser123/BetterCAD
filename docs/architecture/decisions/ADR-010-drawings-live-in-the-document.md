# ADR-010 — Drawings are document objects in the model's own document

```text
Status:    Accepted
Date:      2026-09-22
Milestone: P14-ARCH-001
Builds on: ADR-002 (assemblies live in the document)
           ADR-003 (internal part references first)
```

## Context

A drawing has to live somewhere. Every commercial CAD system this is measured
against keeps drawings in their own file — `.slddrw` beside `.sldprt` — and
that is the shape an engineer expects.

What decides the question here is not the expectation but the dependency
graph. `DocumentObject::dependencies()` returns `std::vector<ObjectId>`
(`include/bettercad/core/document/DocumentObject.hpp:50`), and an `ObjectId`
means nothing outside the document that allocated it. `CommandHistory::bindTo`
and `Regenerator::regenerate` each refuse a second document
(`src/core/document/CommandHistory.cpp:10-21`,
`src/features/Regenerator.cpp:226-230`). The graph is not
"single-document today"; it is single-document by construction.

`P13-REF-001` built the *vocabulary* for naming an object in another document
— `ObjectReference{object, document, hint}` plus an injectable resolver — and
`P13` recorded what it did not build: "cross-document references do not
regenerate… an external part is no dependency edge, because the graph speaks
in `ObjectId`". A drawing in its own document would need that execution, not
just the vocabulary.

A drawing view also depends on the model in the ordinary way: change the
height of a boss and the view must rebuild. That is a dependency edge, and
edges are what the graph is for.

## Options

1. **Drawing objects in the existing `Document`.** `Sheet`, `View`,
   `Dimension`, `Annotation` are `DocumentObject`s alongside sketches,
   features, components and mates.
2. **A separate `DrawingDocument` referencing a model `Document`.** The
   familiar two-file model.
3. **A separate top-level document type sharing `core`.** As 2, but with its
   own object model rather than `DocumentObject`.

## Decision

**Option 1.** Drawing objects are `DocumentObject`s in the same `Document` as
the model they draw. A drawing references model geometry by ordinary internal
reference, and participates in the ordinary dependency graph.

## Rationale

Option 2 is not a milestone, it is a phase — the same finding ADR-003 made
about external part references, and for the same reason. It needs
cross-document dirty propagation, cross-document regeneration ordering,
cross-document cycle detection, a resolver supplied at every entry point, and
a save/load story for a pair of files that can be moved apart. `P13` scoped
all of that out after measuring it, and nothing has changed since.

Option 1 is nearly free, for a specific and checkable reason. Because a
drawing object *is* a `DocumentObject`, it serializes through the existing
`{id, type, name, data}` envelope into the existing `objects` array
(`src/io/json/DocumentJson.cpp:66-71`). No new top-level key, no format
version bump — the version policy at
`include/bettercad/io/DocumentFile.hpp:30-58` says a bump is for "a change
that an existing reader would get **wrong**", and an added object type is
refused loudly by an old reader (`DocumentJson.cpp:244`) rather than silently
skipped. A document with no drawing stays byte-identical to one written
before `P14`.

The same move buys three more things that Option 2 would have had to
re-invent: one command history (ADR-009's transaction shape and the
`assembly/Commands.hpp` pattern apply unchanged), one configuration system
(ADR-007 — a drawing of a configuration is the drawing seeing what is in
force, with no second active-configuration concept), and one undo stack.

Option 3 is Option 2 plus a second object model. It was considered only long
enough to reject: it would mean a second identity scheme, a second
serializer, a second graph and a second history, and ADR-002 already recorded
what makes that wrong — "graph participants must be document objects or
`dependencies()` cannot express them".

## Consequences

- A drawing lives in the same `.bcad` file as its model. A part file with
  drawings is larger than one without, and a drawing cannot be opened
  without its model — which is also the reason it can never be stale
  relative to one.
- Drawing object kinds must be added to the four dispatch sites ADR-002
  enumerated: `objectToJson` (`src/io/json/DocumentJson.cpp:16-65`, fails
  loudly), `objectFromJson` (`:74-245`, fails loudly), the regeneration
  handler table (near-silent if missed — the object is marked `UpToDate` and
  never validated, `src/features/Regenerator.cpp:341-344`), and the CLI's
  `describeObject` (**silent** — the fall-through `return {}` at
  `apps/bettercad_cli/DocumentCommands.cpp:624`). The last two need tests.
- `objectFromJson` dispatches on a **string literal**, never on a
  dll-imported `kTypeName`, because "binding a reference to a dll-imported
  constexpr static does not link in a shared build"
  (`src/io/json/DocumentJson.cpp:85-89`). Each drawing kind's JSON file must
  carry the `static_assert` that keeps literal and `kTypeName` from drifting,
  as `src/io/json/ComponentJson.cpp:132-135` does.
- One document means one name space: `Document::requireNameAvailable` makes
  object names unique across parameters and objects alike, so a sheet cannot
  be called `width` if a parameter already is.
- No format version bump.

## Rejected alternatives, and what would make them right

**A separate `DrawingDocument` (Option 2)** becomes the right answer the day
cross-document dependency *execution* exists — dirty propagation across
documents, ordering, cycle detection and resolver plumbing. `P13-REF-001`
built the identity half of that contract and `ADR-003` records the rest. When
that phase lands, moving drawings out is a migration of where objects live,
not a redesign of what they are: the references are already
`ObjectReference`s, and an internal one is defined as the degenerate external
one.

**A separate top-level type (Option 3)** would be right only if a drawing
needed an object model the `Document` genuinely cannot express. Nothing found
in this milestone does.

## Verification

For `P14`: a document with no drawing is byte-identical to one written before
`P14`; a drawing object round-trips through the existing envelope with no new
top-level key and no version bump; a drawing view is dirtied by a change to
the model object it draws, through the ordinary graph; and the CLI describes
every drawing kind, tested, because that dispatch site fails silently.
