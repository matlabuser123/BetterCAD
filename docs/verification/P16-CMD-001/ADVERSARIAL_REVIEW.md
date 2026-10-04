# P16-CMD-001 — adversarial review

```text
SUBJECT:  an attempt to disprove the milestone before marking it complete
DATE:     2026-10-04
METHOD:   the brief's 24 attack questions, answered against the final diff;
          then 12 mutations of the production code
```

## 1. The attack questions

Each answer names the code or the test that settles it. "No" without a
reference would be an opinion.

| # | Attack | Answer |
| --- | --- | --- |
| 1 | Can an undo command store a `VolumeMesh`? | **No.** A command stores only `MeshControlId`, its own argument, and two `MeshControlDefinition`s. `VolumeMesh` has a **private default constructor** (ADR-030), so it cannot be a member of the aggregate commands copy; storing one would need a deliberate `std::optional` and an explicit construction. |
| 2 | Can 100k tetrahedra of connectivity be copied into history? | **No**, and measured: a global-size step is 1190 B at 504 elements and the identical 1190 B at 932 elements (`..._AHistoryStepCostsTheIntentAndNotTheMesh`). |
| 3 | Can a `NodeId` become a canonical local-sizing target? | **No.** `LocalMeshSizing::face` is a `FaceName`; there is no constructor or setter taking a node. `NodeId` is mesh-local and not a `bettercad::Id` at all. |
| 4 | Can an `ElementId` be stored in a boundary-set command? | **No.** `NamedBoundarySet` holds `BoundarySetId`, a name and `std::vector<FaceName>`. `BoundaryFacetSet` is the derived form and is returned by value, never stored. |
| 5 | Can a Netgen boundary marker enter undo state? | **No.** Backend types live under `src/meshing/netgen/` and `architecture.layering` fails the build if an `nglib.h` include escapes it. |
| 6 | Can a viewer triangle index enter command state? | **No.** Nothing in `src/meshing/` includes a renderer header; the dependency points the other way (meshing is layer 4, renderer 6). |
| 7 | Can undo create a new local-control ID instead of restoring the old one? | **There is no such ID to create.** A local control's identity *is* its `FaceName` — P16-SIZE-001 refuses two controls on one face and declares the stored order meaningless. Asserted field by field in `..._AGeometryReferenceSurvivesUndoAndRedoUnchanged`. |
| 8 | Can remove→undo restore a control in a different semantic order? | **No.** The before-state is the whole definition, and enumeration is through `orderedLocalSizing()` / `orderedBoundarySets()`, which sort by `FaceName` and `BoundarySetId`. The fingerprint uses those accessors, so an order-dependent restore could not pass. |
| 9 | Can a geometry reference silently rebind after a topology edit? | **Not by this milestone's commands** — see finding **A5** for a case where the *resolution* layer can, which the commands neither cause nor mask. `..._AnUnresolvedFaceReferenceSurvivesUndoAndRedoUnrebound` asserts the stored reference still names the dead entity and not the live one. |
| 10 | Can a stale mesh facet selection create a new command? | **Out of reach in this milestone**: no command takes a facet. A mesh-originated edit must first resolve through P16-MAP, and nothing in the command API accepts anything else. |
| 11 | Can an invalid size mutate state before failing? | **No.** `Document::modifyObject` calls `MeshControl::setDefinition`, which validates *before* assigning. Four invalid values (0, negative, NaN, infinity) are checked against fingerprint, both stack depths and the document revision in `..._AnInvalidSizeFailsBeforeTouchingAnything`. Mutation **M5** inverts the order. |
| 12 | Can a failed command still clear the redo history? | **No.** `CommandHistory::execute` returns before `undo_.push_back`/`redo_.clear()` when `execute()` fails. Asserted by the `redoCount()` check in the same test. |
| 13 | Can a no-op size edit add history and stale the mesh? | It **adds a history entry and does not stale the mesh**, which is the deliberate answer — see §2 of this file. `setDefinition` returns `false` for an equal definition, so no revision moves (`..._ANoOpEditCostsNothingButAHistoryEntry`). Mutation **M4**. |
| 14 | Can undo restore settings but leave the mesh marked wrongly? | **No.** Currency is a *comparison*, not a stored flag, so it cannot drift from the intent. `..._ASizeEditMakesTheHeldMeshStaleAndNothingElse` walks Current → StaleIntent → Current → StaleIntent across undo and redo. |
| 15 | Can redo restore values but use different units numerically? | **No.** A `Length` is SI internally and is copied, not re-parsed. `..._AUnitEquivalentEditChangesNothing` shows 10 mm and `Length::fromSi(0.010)` are the same canonical value and produce no change at all. |
| 16 | Can a sizing command manipulate viewer styling? | **No.** No renderer or Qt symbol is reachable from `src/meshing/`; the build enforces it. |
| 17 | Can a command duplicate P16-SIZE conflict logic? | **No**, and this was checked by reading: `MeshControl::validate` delegates to `validate(MeshSizingControls)`, `validate(QualityThresholds)` and `validate(NamedBoundarySet)`, and carries the sizing report's own first-issue message through rather than flattening it to "invalid". |
| 18 | Can a boundary-set rename remesh 100k elements? | **No**, and this is why `currency` compares `VolumeMeshControls` **by value** instead of the control's `revision()` — the revision moves for a rename. `..._ALocalSizeEditInvalidatesAndARenameDoesNot` asserts the document revision *did* move while the currency stayed `Current`, so it cannot pass by the rename having done nothing. Mutation **M9**. |
| 19 | Can a local sizing edit fail to invalidate the mesh? | Covered by the same test's second half, and by mutation **M8**. |
| 20 | Can an old generated mesh be restored from command history after undo? | **No.** `..._DeletingAControlLeavesNoMeshBehindIt` deletes a control with a mesh held, forgets it, undoes — and requires the currency to be `NoMesh`. An undo that carried a mesh would make it `Current`. |
| 21 | Can a remesh destroy the ability to undo old settings? | **No.** `..._AHistoryOutlivesTheMeshesGeneratedUnderIt` generates three meshes and then undoes all the way to the start and redoes forward. |
| 22 | Can a failed Netgen regeneration corrupt command history? | **No.** `..._AFailedGenerationLeavesTheIntentAndTheHistoryIntact` and `..._AFailedRegenerationKeepsTheMeshItCouldNotReplace`: the old mesh is kept, the failure is recorded as its own state, the other control's mesh and intent are untouched, and the edit remains undoable. |
| 23 | Can command memory scale with generated element count? | **No**, measured — question 2. Mutation coverage is structural: `..._NoCommandIsLargeEnoughToHoldAMesh` bounds all ten command types against `sizeof(MeshControlDefinition)`. |
| 24 | Can Debug and Release produce different canonical fingerprints? | **No.** Nothing in the path uses unordered iteration, timing, randomness or locale; enumeration is sorted and the threshold map is ordered by `QualityMetric`. `..._TheSameSequenceGivesTheSameCanonicalState` compares a whole execute/undo/redo trace between two runs, and the three-preset regression runs it under all three presets. |

## 2. The no-op decision, stated rather than assumed

The brief prefers a no-op edit to leave **no history entry**. The framework
records every command whose `execute()` returned success, which is the
behaviour P15's material edit already has.

What a no-op avoids here is everything that matters: `setDefinition` returns
`false`, so no object revision and no document revision move, and therefore no
mesh is invalidated. The history entry is accepted rather than special-cased,
because suppressing it would mean one module's commands behaving differently
from every other module's — and changing `CommandHistory` to drop no-ops
globally is a core-framework change this milestone is not authorized to make.
`MeshControlEditCommand::changedAnything()` exposes the fact for a caller that
wants it. Recorded, with its reason, in
`..._ANoOpEditCostsNothingButAHistoryEntry`.

## 3. Findings

### A1 — `MeshControl::clone()` dropped the ID (production defect, fixed)

Found by **probe before any command was written**: the generic
`AddObjectCommand::undo` removes the object and keeps it, and `redo` calls
`document.insertObject(removed_->clone())`. `insertObject` requires a valid ID
("object '{}' has no ID; use addObject()"), so a clone built from
`(name, definition)` made redo fail and an added control could never come
back.

Fixed to the copy constructor, as `Material::clone()` does. The base class's
`clone()` declaration documents nothing about this, so the requirement is only
discoverable by reading the command that depends on it — which is why the
reason is now written at the site. Regression:
`MeshControlProbe_TheGenericAddCommandUndoesAndRedoesWithTheSameId`. Mutation
**M6** restores the defect.

### A2 — the canonical fingerprint ignored four of six selector fields (test defect, fixed)

The first fingerprint rendered a face as `(feature, role, entity)` and called
`.value()` on `FaceSelector::entity`, which is an `std::optional`. It threw
`bad optional access` on the first end-cap control — and, worse, had it not
thrown it would have ignored `along`, `alongSketch`, `edge` and `copies`
entirely. **A fingerprint that skips a field cannot detect an undo that loses
it**, so this would have weakened every exactness test that depends on it.

Fixed: `describe(const FaceName&)` renders all six fields, with an absent
optional as a distinct token.

### A3 — two staleness authorities exist, disjoint only by reachability (known limitation)

`renderer::statusOf` (P16-VIZ-001) compares `GeometryRevision`s, so it answers
"has the model changed?" and cannot answer "has the meshing intent changed?".
`Mesher::currency` answers both. Today they cannot disagree, because they are
reachable from disjoint code:

```text
$ grep -rln "meshing::Mesher" apps src tests
src/meshing/Mesher.cpp
tests/meshing/MeshingCommandTests.cpp

$ grep -rln "CreateMeshControlCommand" apps
(nothing)
```

The application meshes with default controls and creates no `MeshControl`, so
every mesh `statusOf` sees has no canonical sizing behind it and the two
questions coincide.

**They stop coinciding the moment the application generates from a control**,
and a mesh whose element size nobody asked for any more would read as
`Current`. Wiring it requires a `CommandHistory` in the GUI and Undo/Redo
actions — a GUI undo feature `TODO.md` does not authorize under this
milestone, and not one this milestone may allocate a home for.

Mitigation taken instead of a refactor: the limit is named at the risk site, in
`include/bettercad/renderer/MeshInspection.hpp`, pointing at this finding, so
the next caller to generate from a control cannot take `statusOf`'s answer by
accident.

### A4 — restoring a geometric parameter does not restore the stamp (accepted, conservative)

`geometryRevision` mixes the **revision counters** of a feature's dependency
sources, and a counter only moves forward. So 10 mm → 25 mm → 10 mm leaves the
mesh `StaleGeometry` although the solid is identical.

Wrong in the safe direction: the cost is one unnecessary remesh, against
presenting a mesh as describing a model it does not. Asserted with its reason,
including that the regenerated mesh has the original element count — which is
what makes "the same solid" a measurement rather than an assumption — in
`..._AGeometryChangeIsStaleForADifferentReason`.

### A5 — `EntityId` is per-sketch, so a `FaceName` can rebind across a re-profile (pre-existing, out of scope)

Found while building the unresolved-reference test. The first version
re-profiled an extrude onto a **second rectangle** and expected the control's
face to become unresolved. It did not: entity IDs are allocated **per sketch**,
so the replacement's lines came out as the same numbers, and the control's
`entity:7` resolved against the new sketch's seventh entity — a *different
physical face*, silently.

```text
failed: !(line == *target.face.entity) for: !(entity:7 == entity:7)
```

This is a property of face naming and resolution, not of this milestone: the
commands store the `FaceName` verbatim and neither cause nor mask it. It
belongs with the semantic-topology work (P21) and the face-naming gaps already
recorded for P16-MAP-001.

Two consequences were taken here. The test was rebuilt to re-profile onto a
**circle**, which genuinely destroys the line entity, so the test demonstrates
the unresolved path instead of asserting a rebind while claiming to forbid
one; and the reason is written into the fixture so the next person does not
repeat it.

### A6 — a contract claim had invented numbers (evidence defect, fixed)

`HISTORY_CONTRACT.md` first stated the step and mesh footprints from a
plausible estimate ("under 200 bytes", "nodes x 24 B"). Replaced with measured
figures from two new tests that report them, with the build and fixture named.

### A7 — two of my own assertions were wrong rather than the code (test defects, fixed)

- `..._AMultiStepSequenceUndoesAndRedoesExactly` asserted `!canUndo()` after
  undoing five edits, forgetting the fixture's own create. Replaced with
  `undoCount() == 1`, which additionally proves the five edits consumed
  exactly five entries — an edit that recorded two, or none, would now show.
- The payload test asserted a `> 4x` element-count ratio from a guess, and a
  block's planar boundary refines so little that the first version of the same
  assertion read `24 > 48`. Replaced with strict growth plus the reported
  figures, because the achievable ratio is a property of OCCT's triangulator
  and not something this test may claim.

### A8 — an unresolved control makes meshing refuse, not proceed (contract clarified)

`resolveSizing`'s documentation says an unresolved face "is not a failure of
this function ... whether to proceed is the caller's decision". The caller,
`volumeMeshFor`, decides to **refuse**:

```text
volume mesh: a local sizing control does not resolve against the geometry
(1 of 1 local control(s) did not resolve; the first is unresolved)
```

That is the right decision — quietly meshing while ignoring a refinement the
user asked for would return a mesh that looks like the requested one and is
not. The test was corrected to assert the refusal, which makes it stronger: it
also shows the failure is reported with a diagnostic, leaves the canonical
reference untouched, keeps the previous mesh inspectable, and is deterministic
across the undo/redo cycle.

### A9 — a bare `ObjectId` written for a `FaceName` is accepted by brace elision (C++ aggregate property, probed at the call site instead)

The obvious compile-fail probe for "a face is not a feature" **compiles**:

```cpp
meshing::LocalMeshSizing{.face = ObjectId::fromValue(1U), .targetSize = 2_mm}  // OK
```

`FaceName` is an aggregate of `{ObjectId feature; FaceSelector face;}`, so
brace elision initializes its **first member**: the `ObjectId` silently becomes
`feature`, with a default `EndCap` selector. The result is a well-formed
reference — just not the one a careless writer meant.

That is a property of C++ aggregate initialization, and `FaceName` cannot
refuse it without gaining a constructor, which is a core change outside this
milestone. Measured, so the claim is not theoretical:

```text
LocalMeshSizing{.face = ObjectId::fromValue(1U), ...}      COMPILES
AddLocalMeshSizingCommand{control, ObjectId::fromValue(1U), 2_mm}
    -> no matching function for call to AddLocalMeshSizingCommand   REFUSED
```

**The path the commands expose is the one that can be guaranteed**, and it is:
a parameter typed `FaceName` is copy-initialized, where no elision applies. The
compile-fail case was rewritten to probe the call site rather than the
aggregate, and the first version — which asserted something false — was
deleted rather than left passing for the wrong reason.

Two neighbouring cases also had to be corrected: GCC says "could **not**
convert", not "cannot convert", so their regexes matched nothing. A
compile-fail case whose regex is wrong still fails loudly, which is how these
were caught; a case whose *premise* is wrong passes silently, which is why A9
is recorded as a finding.

### A10 — an exported `constexpr` broke the shared build (production defect, fixed)

`Mesher.hpp` declared

```cpp
[[nodiscard]] BETTERCAD_MESHING_EXPORT constexpr bool describesTheModel(MeshCurrency) noexcept
```

which compiles in both static presets and fails in `debug-shared-ext`:

```text
error: inline function 'constexpr bool bettercad::meshing::describesTheModel(MeshCurrency)'
       declared as dllimport: attribute ignored [-Werror=attributes]
```

The macro expands to `dllimport` for a consumer, and an inline function
defined in the header cannot be imported. The fix is to drop the macro: the
body is compiled by every caller, so there is nothing to export. No other
`constexpr` in `include/bettercad/` carries the macro, so the convention was
already right and this was the one place that departed from it.

**Found by the pre-freeze shared build, which is the whole reason that gate
runs before the freeze.** Only `debug-shared-ext` exercises a DLL boundary; had
the freeze been taken first, the three-preset qualification would have failed
at its third preset, hours in, and voided the run.

## 4. Mutation testing

Twelve mutations, each a single substitution in production code, applied to a
pristine snapshot and judged by whether `[meshcmd]` fails. Results and the
harness are in
[qualification/mutation/](qualification/mutation/) — `mutations.json`,
`apply.py`, `mutate.sh` and `results.txt`.

A mutation that **survives** is a gap in the tests, not a harmless variant.

| # | Mutation | Verdict |
| --- | --- | --- |
| M1 | undo applies the after-state | **killed** — 76 failed |
| M2 | the before-state is captured after the edit | **killed** — 76 failed |
| M3 | redo re-derives the edit instead of replaying it | survived; **equivalent mutant**, proven |
| M4 | `setDefinition` reports every assignment as a change | **killed** — 4 failed |
| M5 | an invalid definition is assigned and left in place | first version **faulty**; corrected and re-run |
| M6 | `clone()` drops the ID | **killed** — 4 failed |
| M7 | removing a local control removes every control on the feature | **killed** — 4 failed |
| M8 | a sizing change does not invalidate the mesh | **killed** — 5 failed |
| M9 | a rename invalidates the mesh | **killed** — 2 failed |
| M10 | `findMeshControl` trusts the ID instead of the type | **killed** — 3 failed |
| M11 | a failed generation discards the mesh it could not replace | **killed** — 2 failed |
| M12 | the redo branch is not discarded | **killed** — 3 failed |

```text
10 killed   1 equivalent mutant (M3)   1 faulty mutation, corrected (M5)
0 genuine survivors
```

**M9 is the one worth singling out.** It makes a boundary-set rename
invalidate the mesh — a defect of *over*-invalidation, which a test that only
checked "edit → stale" would have survived. It died, so the precision claim is
tested in both directions and not just in the easy one.

M3 and M5 are written up in full in
[qualification/mutation/README.md](qualification/mutation/README.md); M5's
first version assigned the invalid definition and then rolled it back, which
is observationally identical to validating first, and both survivors were
re-run under the wider `[meshcmd],[meshcontrol]` filter so that "equivalent"
could not just mean "not covered by the filter I chose".
