# ADR-002 — Assemblies live in the document, as document objects

Status: Accepted
Date: 2026-09-19

## Context

`P13` adds assemblies: several parts placed relative to one another and held
by constraints. The first question is where that state lives.

What already exists, and what it costs to reuse or to duplicate:

- `Document` is the single owner of persistent state and there is no global
  document (`ARCHITECTURE.md:99`). It holds one `IdAllocator` shared by
  parameters, objects and configurations, a `ParameterTable`, a
  `ConfigurationTable`, and `std::map<ObjectId, std::unique_ptr<DocumentObject>>`
  (`include/bettercad/core/document/Document.hpp:273`).
- `DocumentObject` is the extension point (`DocumentObject.hpp:30`): a kind
  supplies `typeName()`, `clone()`, `contentEquals()` and `dependencies()`.
  `dependencies()` is the *only* way an object declares graph edges
  (`src/core/document/DependencyGraph.cpp:194`).
- Everything downstream keys off that: dirty propagation by `revisionOf()`,
  topological order, `AddObjectCommand` for undo/redo with ID preservation
  (`src/core/document/Commands.cpp:150`), the `{id, type, name, data}` JSON
  envelope (`src/io/json/DocumentJson.cpp:16`), and the regeneration handler
  table keyed by `typeName` (`src/features/Regenerator.cpp:158`).
- `Regenerator::registerHandler` is **public**
  (`include/bettercad/features/Regenerator.hpp:74`), so a new module can
  register regeneration for its own kinds without modifying `features`.

`ARCHITECTURE.md:498` already reserved the shape: an assembly "holds
component instances, mate constraints, configurations and solved
transforms", and a component "carries `ComponentId`, source document,
configuration, placement, suppression state and metadata".

## Constraints

- Invariant 1: the Document owns canonical engineering state.
- Invariant 4: stable typed IDs, never indices, never reused.
- The anti-pattern list forbids "a second parameter system or document
  model" (`CLAUDE.md`, Architect phase) and "serialization of internal
  memory layout" (`ARCHITECTURE.md:485`).
- `.bcad` is `"version": 1` and every existing file must keep loading
  (`include/bettercad/io/DocumentFile.hpp:29`).

## Options

1. **Components and mates as `DocumentObject`s inside the existing
   `Document`.** An assembly is not a container; it is the set of component
   and mate objects a document holds, exactly as "the part" is the set of
   sketch and feature objects it holds.

2. **A separate `AssemblyDocument` type**, parallel to `Document`, owning its
   own components, mates, IDs and history.

3. **An `AssemblyTable` sub-container inside `Document`**, modelled on
   `ConfigurationTable`: components and mates are values, not document
   objects, reached through table accessors.

## Decision

Option 1. Components and mates are `DocumentObject` kinds. `ComponentId` and
`MateId` are new tag types that **widen to `ObjectId`**, like `SketchId`,
`FeatureId`, `ParameterId` and `BodyId` (`include/bettercad/core/Id.hpp:60`),
because they are graph participants.

## Rationale

Option 1 inherits, rather than re-implements, eight things that already work
and are already qualified: stable non-reusable IDs, revision tracking and
dirty propagation, dependency edges via `dependencies()`, topological
ordering with deterministic tie-breaking, `AddObjectCommand`-based undo and
redo that preserves IDs, name uniqueness, the JSON object envelope, and
regeneration with atomic per-item failure.

The persistence consequence is the decisive one. Because a component *is* a
document object, it serializes through the existing `{id, type, name, data}`
envelope into the existing `objects` array. **The file format does not
change**: no new top-level key, no version bump, and a document with no
components is byte-identical to one written before `P13`. The only additions
are new `type` strings and their branches in `objectToJson` /
`objectFromJson`. Contrast `configurations`, which needed a new optional
top-level key and careful "write only when non-empty" handling to keep old
files identical (`DocumentJson.cpp:355`).

Option 2 was rejected because it is the "second document model" the
anti-pattern list names. It would duplicate the ID allocator, the command
history (which binds to one `DocumentId` and refuses another,
`src/core/document/CommandHistory.cpp:10`), the dependency graph, the
regenerator and the file format, and every future capability — drawings,
BOMs, simulation — would then have to be taught two models.

Option 3 was rejected on a sharper point. `ConfigurationId` deliberately does
**not** widen to `ObjectId` (`Id.hpp:121`), and that is correct, because a
configuration is not a node in the dependency graph: it changes values that
feed the graph. A component is the opposite — it depends on a part, a mate
depends on components, and a mate's solved result feeds placement. Nodes in
the graph must be document objects, or `dependencies()` cannot express them
and the whole regeneration machinery has to be duplicated for assemblies.

Option 3 would have been right if components were passive data. They are not.

## Consequences

- One document holds both the parts and the assembly. A document is not
  typed as "part" or "assembly"; it is whatever objects it contains, which is
  consistent with a document already being able to hold several result
  bodies.
- `ComponentId` and `MateId` join the widening list in `Id.hpp`. Mixing kinds
  must still fail to compile.
- New object kinds must be added to four dispatch sites, all of which are
  `dynamic_cast` or `typeName` chains with no central registry:
  `objectToJson`, `objectFromJson`, the regeneration handler table, and the
  CLI's `describeObject` (`apps/bettercad_cli/DocumentCommands.cpp:382`).
  Missing the last one is silent — the CLI prints an empty description — so
  it needs a test.
- **A component cannot independently select a part configuration.**
  Configurations are document-global: there is one active configuration and
  `effectiveParameterValue` applies its overrides to every reader
  (`Document.cpp:415`). With the parts in the same document, two components
  of the same part cannot differ by configuration. This is a real limitation
  of this decision, it is not worked around, and it is recorded in
  `ARCHITECTURE.md` and `TODO.md`. Lifting it needs either per-component
  overrides or external part documents — see
  [ADR-003](ADR-003-internal-part-references-first.md).
- Because the format does not change, a pre-`P13` file and a `P13` file of
  the same part are the same bytes. That is a property worth testing
  directly.

## Verification

Nothing is implemented by this ADR. When `P13-COMP-001` implements it, the
decision is verified by: a component surviving save → load → regenerate with
its ID, name and placement intact; `.bcad` files written before `P13` loading
unchanged and re-saving byte-identically; undo and redo of a component
insertion restoring the same `ComponentId`; and a component's
`dependencies()` producing the expected graph edges.
