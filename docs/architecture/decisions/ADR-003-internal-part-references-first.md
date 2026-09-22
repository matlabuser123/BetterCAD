# ADR-003 — Part references are internal for P13; external references are specified, not built

Status: Accepted, with the deferral clause overtaken — see the amendment below
Date: 2026-09-19

## Amendment — 2026-09-22 (P13-QUAL-001)

The decision below stands: a component names a part **within its own
document**, and every committed model does exactly that. What did not stand is
the scoping sentence attached to it.

This ADR says of the external-reference contract that "nothing in `P13`
implements it, and no placeholder type, field or key for it is added", and
gives as a `P13` verification criterion that "no persisted field exists for an
external source". `TODO.md` subsequently authorized
`P13-REF-001 — Internal / external reference infrastructure`, which built the
reference vocabulary and its resolver inside `P13`: `ObjectReference` carries
an optional `document` and a `hint`, and both are written to and read from
`.bcad` when a reference is external. The milestone honours every clause of
the contract recorded below — its evidence maps them one by one — but it is
not the "later milestone" this ADR imagined, and the ADR was never amended to
say so.

`TODO.md` authorizes implementation and outranks an ADR, so the work was
authorized and is qualified; this note records the change rather than
reversing it. Two things are worth keeping straight:

- The fields are **implemented and tested**, not the unused placeholder this
  ADR warned against. `P13-REF-001` added 22 Catch2 cases for them.
- No production artifact uses them. All eight committed assembly models
  reference their parts by bare local `ObjectId`, verified in
  `docs/verification/P13-QUAL-001/`, so the decision below describes the
  system as it actually ships.

Cross-document **execution** — regeneration, circularity detection, a resolver
supplied from the CLI — remains unimplemented, and is still correctly
described as deferred throughout.

## Context

A component instance has to name the part it is an instance of.
`ARCHITECTURE.md:498` reserved both possibilities: an assembly "references
part documents **or** internal definitions".

What the codebase can express today:

- **Nothing crosses a document boundary.** `DocumentId` appears outside
  headers in three places (`src/core/document/Document.cpp:36`, `:38`,
  `src/io/json/DocumentJson.cpp:450`) and in every one of them it is an
  *ownership guard*, not a reference. No object, no reference type and no
  persisted field names another document.
- `DocumentObject::dependencies()` returns `std::vector<ObjectId>`
  (`DocumentObject.hpp:50`). An `ObjectId` is meaningless outside its
  document, so the dependency graph is structurally single-document.
- `CommandHistory::bindTo` refuses a second document
  (`src/core/document/CommandHistory.cpp:10`), and so does
  `Regenerator::regenerate` (`src/features/Regenerator.cpp:216`).
- A missing `ObjectId` is already a first-class failure: the graph records it
  in `missing` (`DependencyGraph.cpp:199`) and the regenerator turns it into
  `NotFound` and blocks the dependents (`Regenerator.cpp:310`).

So "reference another document" is not a feature that slots in. It is a
change to the document model itself.

## Constraints

- Invariant 9: regeneration is dependency-driven. A reference that the graph
  cannot see is a reference regeneration cannot honour.
- Invariant 10: regeneration failures are atomic.
- Engineering intent is persistent (invariant 2) — so whatever identifies an
  external part must itself be durable, which a filesystem path is not.
- `CLAUDE.md`: do not overbuild; build the smallest correct subsystem,
  validate, stabilize, then expand.

## Options

1. **Internal only for `P13`.** A component references a part defined in the
   same document, by `ObjectId` — the feature whose body is the part. The
   external contract is designed and recorded, and implemented in a later
   milestone.

2. **External document references in `P13`.** A component names another
   `.bcad` document. Requires cross-document dependency edges, multi-document
   regeneration, undo across documents, a durable document identity that is
   not a path, and a failure model for missing, moved, stale and circular
   references.

3. **Copy on insert, remembering the origin.** Inserting a part copies its
   definition into the assembly document and records where it came from, with
   an explicit "update from source" action.

## Decision

Option 1 for `P13`. A component references a part **within its own
document**, by `ObjectId`, so component and mate edges are ordinary edges in
the existing graph.

The external-reference contract is specified below so the later milestone
starts from a decision rather than a blank page, but **nothing in `P13`
implements it**, and no placeholder type, field or key for it is added.

## Rationale

Option 2 is not a milestone, it is a phase. Every foundation it touches —
`Document` ownership, the dependency graph, the regenerator, the command
history, the file format — is foundation that `P11` and `P12` qualified.
Rebuilding all of it before a single mate exists inverts the order this
project works in, and would mean the first thing `P13` did was destabilise
the part modeller that was just qualified. The smallest correct subsystem is
an assembly of parts you already have.

Option 3 was rejected because it manufactures a silent-drift failure: the
copy and its source diverge and nothing detects it. This project's rule is
that a reference either resolves or fails loudly; a stale copy that still
regenerates cleanly is exactly the kind of quiet wrongness the stable-
reference contract exists to prevent. It also duplicates a part per use,
which defeats the point of an instance.

Option 1 has a real cost, and it is the one named in
[ADR-002](ADR-002-assemblies-live-in-the-document.md): with parts and
assembly in one document and configurations document-global, two components
of the same part cannot differ by configuration. That cost is accepted for
`P13` and recorded, not hidden.

## Consequences

- A component's `dependencies()` returns the `ObjectId` of the part feature
  it instances, so dirty propagation, ordering and blocking work with no new
  machinery.
- A part used twice is one definition and two components — the instancing
  the reserved shape intended.
- Deleting a part that a component instances must fail or be refused the way
  any missing reference is: reported, never silently resolved to another
  body.
- Per-component part configurations remain impossible until either external
  documents or per-component overrides exist.
- **No field, key or type is added in anticipation of external references.**
  A `source_document` field written now and unused would be the "placeholder
  presented as implementation" that the phase's own gate forbids.

### The external-reference contract, for the milestone that implements it

Recorded now while the constraints are fresh; binding on that milestone only.

- **Identity is the document UUID, not the path.** `Document` already has a
  `DocumentId` that is a UUID and is persisted (`DocumentJson.cpp:450`). The
  reference stores that UUID as the durable identity, plus a *hint* — a
  relative path — used only to locate a candidate. A hint that resolves to a
  document with a different UUID is a failed reference, not a match.
- **Resolution is explicit and injectable.** Loading must not read the
  filesystem implicitly. A resolver is passed in, so tests, the CLI and a
  future GUI supply their own, and a document can always be loaded with
  references left unresolved.
- **Unresolved is a state, not an error at load.** A document whose external
  parts are missing must still load, report each unresolved reference, and
  block exactly the components that depend on it — the same shape as
  `missing` in the dependency graph today.
- **The graph must span documents before mates may.** Cross-document edges
  need a node identity wider than `ObjectId`. Until that exists, external
  references cannot participate in regeneration, and a design that pretends
  otherwise is wrong.
- **Circularity is a hard error.** Document A referencing B referencing A
  must be detected and refused, as dependency cycles are today.

## Verification

For `P13`: a component whose part is deleted reports a missing reference and
blocks, with no substituted body; two components of one part share one
definition; and no persisted field exists for an external source.

For the later milestone: a moved file resolving by UUID through a supplied
resolver, a UUID mismatch failing, a document loading with unresolved
references and reporting them, and a circular reference refused.
