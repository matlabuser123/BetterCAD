# P8 — Dependency graph and regeneration: verification

Date: 2026-09-14. Incremental build of the existing preset trees; every
changed translation unit was recompiled. CTest runs only after a successful
build. Raw logs are in this directory.

| Preset | Configure | Build (`-Werror`) | `warning:`/`error:` lines | Tests |
|--------|-----------|-------------------|---------------------------|-------|
| debug | exit 0 | exit 0 | 0 | 245/245 passed |
| release | exit 0 | exit 0 | 0 | 245/245 passed |
| debug-shared | exit 0 | exit 0 | 0 | 245/245 passed |

There are 15 new test cases: `tests/core/document/DependencyGraphTests.cpp`
(5) and `tests/features/RegeneratorTests.cpp` (10).

## Design

- **Dependency representation (core).** Each `DocumentObject` declares its
  inputs through the virtual `dependencies()`. `Sketch` returns the
  parameters that drive its constraints; `ExtrudeFeature` returns its profile
  sketch, depth parameter and target feature. `buildDependencyGraph(document)`
  produces a `DependencyGraph` over all parameters and objects, plus a list of
  missing references. Core never needs to know the concrete kinds.
- **Graph algorithms (`DependencyGraph`).**
  - Downstream closure (dirty propagation).
  - Kahn topological order, with ties broken by ID for determinism.
  - Tarjan strongly-connected components for cycles, including
    self-dependencies. It is iterative, so there is no recursion depth limit.
  - Nodes downstream of a cycle are reported as blocked.
- **Regeneration (`features::Regenerator`).** Each pass:
  1. Compares every item's current revision with the revision recorded when
     it was last built.
  2. Takes the changed items (plus previously failed items and those with
     missing references) and their downstream closure as dirty.
  3. Rebuilds only dirty items, in topological order, using per-kind
     handlers:
     - `"sketch"`: apply driving parameters, then solve. This happens on a
       clone, and the result is adopted only if the solve succeeds.
     - `"extrude"`: build the body. Handlers are registrable.
  4. Records revisions after building, so a sketch's own re-solve does not
     make it dirty again.
- **Failed-node reporting.** A failing item gets `Failed` and an `Error`; its
  dependents are `Blocked`. Failed and blocked items drop their results (no
  stale geometry) and are retried on the next pass. Cycle members fail with
  a "dependency cycle: A, B" error.

## Evidence

| Requirement | Test | Result |
|-------------|------|--------|
| Dependency representation | `A document's graph comes from the objects' declared dependencies` | edges from `dependencies()`, missing reference listed, closure from a parameter |
| Dirty propagation | `Dirty propagation reaches exactly the downstream nodes` | exact closures for several start sets |
| Topological regeneration | `Topological order puts dependencies first and breaks ties by ID`; `The first pass builds everything in dependency order` | order `[1, 2, 3, 4, 5, 20, 10]`; Sketch001 → Extrude001 → Sketch002 → Extrude002 |
| Cycle detection | `Cycles are detected and block what depends on them` (3-cycle, self-dependency, blocked node); `Long dependency chains are handled without recursion` (10000-node chain and 10000-node cycle); `Dependency cycles are reported and block their dependents` (features A ↔ B failed, C blocked, rest fine) | pass |
| Spec example: width → Sketch001 → Extrude001 | `Spec example: changing width rebuilds Sketch001 and Extrude001 only` | changed `{object:1}` (width); regenerated `{object:5, object:6}` (Sketch001, Extrude001); volume 120000.0 mm³; bottom line 120.0 mm; the unrelated Extrude002 stays `UpToDate` with the same body object |
| Only affected nodes regenerate | `Without changes, nothing is regenerated`; `Changing the depth rebuilds only the extrude`; `Editing a feature rebuilds that feature and what depends on it` (Extrude002 before the Cut that uses its body) | pass |
| Failed-node reporting | `Failures are reported and block dependents until fixed`: invalid driving value → Sketch001 `Failed` (`InvalidArgument`), Extrude001 `Blocked`; inconsistent sketch → message contains `INCONSISTENT`; fixing it regenerates both | pass |
| Missing references | `Missing references are reported as failures`: deleting Sketch002 fails Extrude002 with `NotFound`; undo restores both | pass |
| Determinism and binding | `Regeneration results are deterministic` (bit-identical volumes across two documents); `A regenerator belongs to one document; regenerateAll starts over` | pass |

## Other changes

IDs now have an `operator<<`, so test diagnostics print `object:5` instead
of `{?}`.
