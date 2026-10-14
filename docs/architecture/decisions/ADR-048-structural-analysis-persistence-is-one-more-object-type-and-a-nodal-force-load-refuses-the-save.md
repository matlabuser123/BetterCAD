# ADR-048 — Structural-analysis persistence is one more object type, and a nodal force load refuses the save

```text
STATUS:    Accepted
DATE:      2026-10-14
MILESTONE: P17-PERSIST-001
SUPERSEDES: nothing
RELATES TO: ADR-030 (controls are document objects), ADR-031 (mesh-local ids
            are never persisted), ADR-047 (solver settings became canonical),
            P13-PERSIST-001 (the version rule), P16-PERSIST-001 (the
            derived-state exclusion argument)
```

## Context

P17 needs structural-analysis intent in the `.bcad` file. The question looks
large — a schema, a version, validation, determinism, reference encoding,
derived-state exclusion — and all but one part of it turned out to be already
decided by the project.

`StructuralAnalysis` is a `DocumentObject`, which ADR-030 chose for a meshing
control on the grounds that "undo, redo, persistence and dependency
invalidation come for free, because a control is an ordinary document object".
Persistence is the one of those four that still needed code written, because a
serializer has to exist; everything *around* it was settled.

What the audit found already decided, with a recorded reason in each case:

```text
file format            .bcad, transparent JSON, ordered_json
version rule           P13-PERSIST-001: bump only for a change an existing
                       reader would get WRONG
unknown fields         rejected, by requireObject's allow-list
face references        faceNameToJson / faceNameFromJson, shared with six
                       other object kinds
atomic save            temp file + rename, with its carried limitation
save-failure path      objectToJson already returns Result<Json>
derived-state rule     P16-PERSIST-001's structural argument
enum file keys         strings from a table, never integers, never toString()
collections            arrays, never objects keyed by id, because a repeated
                       JSON key silently keeps one
```

So the architecturally significant decisions in this milestone are two, and
only one of them is genuinely new.

## Decision 1 — one more object type, no version bump, no second format

P17 adds `"type": "structural-analysis"` to the existing format: a serializer
pair in `src/io/json/StructuralAnalysisJson.cpp`, declared in `ObjectJson.hpp`
beside every other kind's, dispatched in `DocumentJson.cpp` in the two places
every other kind is.

`kDocumentFormatVersion` stays at **2**. Adding an object type is additive, and
both failure directions are already safe: a new reader loading an old file
treats an absent section as "none of those", and an old reader meeting a new
file refuses by name rather than skipping silently. The version is for a field
that changes *meaning*, and P17 changes none.

Rejected alternatives:

- **A separate `analysis.p17fea`** — rejected. BetterCAD has no linked-subdocument
  architecture, and introducing one for this would create a second canonical
  authority whose consistency with the `.bcad` nothing maintains.
- **A bumped version with a migration path** — rejected. There is no earlier P17
  schema to migrate from, so the migration code would exist only to be tested,
  and the bump would break every existing reader for no gain.
- **A P17-specific forward-compatibility policy** (ignore unknown fields) —
  rejected. The project's policy is strict rejection and it is enforced by a
  shared helper; a second policy in one section would be a second contract.

### One deviation from the P16 precedent, with a measured reason

P16's reader ends in `MeshControl::create` and lets the core validate
everything. P17's reader ends in `StructuralAnalysis::create`, which calls
`validate(StructuralAnalysisDefinition)` — and **that function checks the mesh
handle and the solver settings and nothing else**. It is fourteen lines, and
P17-CMD-001's probe C11 measured the same asymmetry from the other side.

So the reader calls `validate(StructuralLoad)` and
`validate(StructuralRestraint)` itself — the core's own rules, called rather
than copied — and owns the duplicate-id rules, which are about the *file* and
are exactly where P16 put its equivalent.

`validate(StructuralAnalysisDefinition)` was deliberately **not** extended to
cover loads and restraints. It would change qualified behaviour that
persistence does not need, requalify the command layer for an unrelated reason,
and the asymmetry is load-bearing for P17-CMD-001's measured probe results.

### The collection order is preserved, not sorted

P16 sorts its local sizing controls because P16-SIZE-001 declares that order
meaningless. `StructuralAnalysisDefinition` declares the opposite in as many
words over both its vectors — "ORDER IS THE USER'S AND IS PRESERVED" — and
P17-CMD-001's probe C8 exists because an edit that silently reordered the loads
was a real defect. So the file preserves it, and determinism is unaffected
because here the order *is* part of the canonical state.

## Decision 2 — a `NodalForceLoad` refuses the save

This is the only genuinely new decision in the milestone.

`NodalForceLoad` is the one canonical payload holding mesh-local state: a
`meshing::MeshStamp` and a `meshing::NodeId`. Three authorities say
independently that neither may reach a file — ADR-031 ("never persisted"),
P17-LOAD-001's qualified declaration that the type is mesh-local and "NOT
durable point-load intent on the model", and the P17-PERSIST brief, which lists
a persisted `NodeId` as an automatic failure.

They agree, so there was no conflict to resolve — only a consequence to choose.

```text
A  write it, stamp and all      a NodeId in a file, against ADR-031; and the
                                stamp names a mesh generation that cannot
                                exist after a reload, so the load would be
                                refused by P17-LOAD-001's own rule forever
B  write it without the stamp   the forbidden rebind, in the mesh dimension:
                                P17-LOAD-001 refuses a nodal force against a
                                mesh "with the same node count and the same
                                numeric handles"
C  drop it silently on save     silent data loss, in a format whose character
                                is "a parse error naming it, never a silent
                                skip"
D  REFUSE THE SAVE, naming it   chosen
```

**Chosen: D.** The error names the load, says why, and says what to do instead.
`objectToJson` has always returned `Result<Json>`, so the failure path existed;
this is the first writer in the format to use it, and the only one that can
fail. `saveDocument` propagates before opening its temporary file, so the
refusal is atomic — no file is created, not even a truncated one — and the
document is unchanged.

**The cost was measured, not estimated.** `NodalForceLoad` has zero callers in
`apps/` and zero in `src/` outside its own definition; it appears only in tests,
which is exactly what P17-LOAD-001 said it was for. So the refusal guards a
path that does not exist yet, which is the cheapest moment to place a guard —
and the alternative is discovering in P17-CLI-001 that a save had been quietly
dropping loads.

The reader refuses `"type": "nodal_force"` by name as well, rather than falling
through to "unknown load type" and certainly rather than reading it as a zero
load.

## Consequences

**Good.**

- No command framework, no invalidation mechanism, no identity type, no second
  file format, no version bump, no migration code and no new reference
  encoding. The serializer is the whole of the production change.
- Derived-state exclusion is structural rather than maintained.
  `StructuralAnalysisDefinition` has five members and none is derived; a
  `StructuralResult` does not live on a document object at all. **No solve
  output is reachable from the writer's signature**, so a leak would require
  changing that signature and every caller — which is why two of the brief's
  requested mutations could not be written.
- No rebind is possible in the reader for the same structural reason: it takes
  a `Json`, a name and a path, with no `Document`, `Regenerator`, `Mesher` or
  geometry. There is nothing to rebind *to*.
- The byte-identity gate is exact rather than tolerant, and stronger than
  P16's: four states of one document — no result, current, **stale**, and after
  a **failed** solve — compared byte for byte.

**Costs, stated.**

- A document holding a fixture-only nodal force load cannot be saved. Intended,
  measured at zero product paths, and loud rather than silent.
- BetterCAD is not forward-compatible with a future P17 field: a newer file is
  refused by name. That is the project's existing policy for every object kind,
  not a P17 choice.
- A hand-written `"loads": []` is accepted and normalises away on the first
  save, so a hand-written file is byte-stable only from its first save onward.
  The same property P16's `boundary_sets` has, and the brief's §142 contemplates
  it.
- A durable point load is still missing. It needs a canonical CAD **vertex**
  reference P16 does not provide, and persistence does not fake one with a
  facet id, a coordinate query or a nearest-node search.

**Carried, unchanged.** The `FileIo` atomic-replace limitation recorded in
`TODO.md` is not fixed by this milestone and is not folded into it.
