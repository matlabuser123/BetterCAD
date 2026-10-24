# ADR-056 — Thermal analysis persistence is one more object type, and the file holds intent only

```text
STATUS:    Accepted
DATE:      2026-10-24
MILESTONE: P18-PERSIST-001
TOUCHES:   ADR-048 (structural analysis persistence, the precedent this follows), ADR-049 (the thermal analysis modes and the uniform initial temperature),
           ADR-055 (the thermal commands: the same canonical/derived line), ADR-030 (derived state carries its source), ADR-031 (a mesh-local id is never persisted),
           ADR-032 (the canonical face reference), ADR-028 (no solver holds material data of its own)
```

## Context

A thermal analysis is a document object (`ThermalAnalysis`) holding a `ThermalAnalysisDefinition`: a mesh-control handle, a mode, the heat loads, the boundary conditions, at most one uniform initial temperature,
two solver ratios and at most one set of transient settings. Until this milestone it could not be saved: `objectToJson` returned "objects of type 'thermal-analysis' cannot be saved". The model it describes can be
solved, recovered, balanced, validated, drawn and edited with undo; it could not be kept.

P16 (meshing control) and P17 (structural analysis) each added their object type to the one `.bcad` format and recorded the method. The brief for this milestone asks for the same boundary the command layer drew:
**what may not be in canonical undo history may not be in the canonical file.**

## Constraints

* One document format exists: transparent JSON, `nlohmann::ordered_json`, strict unknown-key rejection, path-named errors, an atomic save, a loader that returns a new `Document`, one global integer version that has
  never moved because adding an object type is additive.
* A saved file must be a function of canonical intent: saving before and after a solve, a recovery, a balance, a validation or a display build must give the same bytes.
* A `GeometryReference` is a `FaceName`; an unresolved one is intent. A reader that is given no document cannot rebind one.
* P15 is the sole owner of k, rho and cp (P18-MAT-001); a copy in the thermal section would be a second authority.
* No qualified file may change.

## Options

### A — a thermal-only file or a thermal blob inside the document

Rejected: a second file is a second atomicity and a second version; a blob (`thermalState`) invites exactly the leak the milestone exists to forbid.

### B — persist the analysis *with* its last result, as a cache

Rejected: a result is bound to a mesh stamp, a geometry revision, a material revision and an analysis revision that do not survive a reload, so it could never be current again; it would be a permanently dead
payload that makes the file's size follow the mesh. The brief's requirement (identical bytes before and after a solve) forbids it outright.

### C — one more object type in the existing format, built from the canonical definition alone (chosen)

## Decision

1. **One object type, `"thermal-analysis"`, in the existing document, no version bump.** The first BetterCAD document that can contain P18 intent is version 2. A new reader loads an old file (an absent section
   means none); an old reader refuses a new one by name.
2. **The section is the definition and nothing else.** `mesh`, `mode`, `loads[]`, `boundary_conditions[]`, `initial_temperature`, `solver`, `transient` — seven members for seven fields. The writer's only
   argument is the analysis object; the reader's only inputs are the JSON, a name and a path. There is no expression in either that depends on whether a solve has run, and nothing to rebind a reference to.
3. **Units are fixed SI and named by the key.** `.si()` out, `fromSi` in; K, W/m², W/m³, W/(m² K), s. No unit string, no Celsius, no conversion factor.
4. **Variants by an explicit string; enumerations by stable strings**, never integers or display text; a kind carries only its own keys.
5. **Validation is the core's, called and not copied**; the file adds only what the core cannot say: the path of a refusal, duplicate identities by path, the keys a kind may carry, the invalid feature handle (as the
   commands refuse it), and an identifier above the file cap — **on write as well as read**.
6. **Solver settings are always written and wholly required**; no default stands in for a missing field.
7. **Order is the user's and is preserved.** Empty collections are omitted; an explicit empty array is accepted and normalised away by the first save.
8. **Unknown keys are rejected** (the format's policy), so a fake result or a fake status flag cannot become anything: the file is refused, naming the key.
9. **Enforced, not described**: a source-text gate fails a thermal persistence file that includes a derived header, names derived work, a mesh identity or a material property, parses a number, is
   non-deterministic, resolves anything or converts a unit, or uses the core's owners fewer times than there are places that need them; it also fails the rest of `src/io` for any other thermal include.

## Consequences

* A document with a thermal analysis can be saved and reloaded, and after the load the analysis is intent only: no mesh, no numbering, no result, nothing current. Re-solving runs the normal chain.
* **Two measured facts the owner may want to decide, neither changed here:**
  * the JSON parser collapses a repeated key (`{"flux": 1, "flux": -1}` loads as −1) for *every* object type — pinned by a test, not a change to the generic parser;
  * the data model's `validate` accepts `FaceName{}`; the commands and now the file refuse it.
* `src/io` gains a link to `BetterCAD::thermal` (private). The layering check holds (thermal 45 below io); no new edge from thermal.
* A change to the schema, the key order, the units, the identity rules or the exclusion boundary changes the fingerprints in `FREEZE.md` and is a new milestone.
