# P15-ASSIGN-001 — Material Assignment

```text
STATUS:    see RESULT.
MILESTONE: P15-ASSIGN-001
DATE:      2026-09-27
BASELINE:  27c1bac, clean tree, HEAD == origin/main,
           HEAD^{tree} = 1eab92c28b945511d5e12073d0de4e0a28d28090
```

## PREREQUISITES

Counted from the checklists:

```text
P15-ARCH-001         16 of 16 [x]   ADR-025..029
P15-UNITS-001        20 of 20 [x]
P15-MAT-001          18 of 18 [x]
P15-MECH-001         18 of 18 [x]
P15-THERM-001        14 of 14 [x]
INFRA-QT-DEPLOY-001   8 of 8  [x]
```

The qualified build path is still the external one: P15-THERM-001 was qualified
from `%LOCALAPPDATA%\bc-build` with 0 stages failed, and so is this.

## SCOPE

```text
IN     the canonical assignment and its four states; assign, replace, remove;
       resolution by identity only; the no-rebinding invariants; regeneration and
       failed regeneration; the occurrence, suppression and configuration
       policies; undo-ready mutation; the consumer boundary
OUT    mass, centre of mass and inertia (P15-MASS-001); commands and undo
       (P15-CMD-001); custom materials (P15-CUSTOM-001); provenance
       (P15-PROV-001); the file representation (P15-PERSIST-001); per-body and
       per-occurrence materials, both deferred by ADR-026 with named mechanisms
```

## VALID ASSIGNMENT TARGETS

The brief asks which of Part, Body, Solid, Feature, occurrence and configuration
can own a material. ADR-026 answered by audit, and the answer is not on that
list:

```text
Target            Assignable?  Why
Part              n/a          THERE IS NO PART DOCUMENT OBJECT. A part IS a
                               document; ComponentDefinition::part is an
                               ObjectReference naming one.
Body              n/a          THERE IS NO BODY DOCUMENT OBJECT either. BodyId
                               exists, but no type declares kTypeName "body": a
                               body is a regeneration RESULT, and the handle
                               callers use is the ObjectId of the feature that
                               produced it.
Solid             NO           no persistent identity at all (ADR-012, ADR-024)
Face              NO           same, and offering it would rebuild the defect
                               ADR-024 removed
Feature           NO           rejected by ADR-026: it invites a material on a
                               sketch or a datum plane, where the semantics are
                               undefined
Occurrence        NO, deferred needs a material named across a document
                               boundary; external references are deferred
                               (ADR-003)
Configuration     NO, forced   a configuration overrides free PARAMETER values,
                               and a MaterialId is not a parameter value
THE DOCUMENT      YES          document-level intent, exactly as ConfigurationId
                               is: there is one of it and nothing references it
```

Inheritance: **none**, so the effective material IS the direct assignment. No
fallback layer was invented, because none was authorized.

## ASSIGNMENT OWNERSHIP AND THE MATERIAL REFERENCE

```cpp
// core/document/Document.hpp
std::optional<MaterialId> materialAssignment() const noexcept;
Result<bool> setMaterialAssignment(std::optional<MaterialId> id);
```

One optional `MaterialId`, held by the `Document`. It stores **identity and
nothing else**: no designation, no object name, no library key, no index, no
position, no pointer. Renaming or redescribing a material therefore cannot move
an assignment, and there is nothing in it to go stale.

`Document::setMaterialAssignment` deliberately does **not** validate the ID, and
cannot: core does not know what a material is. That is not a hole left open, it
is what makes the `Invalid` state reachable and testable, and it is the setter a
loader and an undo need. `features::assignMaterial()` is the validating entry
point that ordinary code calls.

**One material per document, structurally.** There is a single optional field, not
a map, so two materials cannot be assigned at once — assigning B replaces A. Per-
body materials are deferred with the mechanism named: a body's only persistent
handle is its producing feature's `ObjectId`, so a later override can be keyed by
that.

## DIRECT ASSIGNMENT, EFFECTIVE MATERIAL, AND THE FOUR STATES

```text
Unassigned   nothing was assigned. A normal resting state, not a fault.
Resolved     the assigned material exists and was found.
Unresolved   an assignment exists and its material is not there NOW. The INTENT
             is kept, unchanged, so it resolves again if that exact material
             comes back.
Invalid      the assignment names an object that exists and is not a material.
```

The last three are never collapsed into one. This mirrors
`drawing::ResolutionState`, which could not be reused directly — it is layer 4 and
materials are layer 2 — so the meanings were copied deliberately rather than the
type.

```cpp
MaterialAssignment materialAssignment(const Document&);   // state + ID + diagnostic
const Material*    effectiveMaterial(const Document&);    // ADR-027's name
Result<const Material*> requireEffectiveMaterial(const Document&);
```

`effectiveMaterial()` is the name ADR-027 promised a downstream solver, so P17 and
P18 find what the architecture said would be there. With no inheritance it is the
direct assignment; the name is the one that survives if inheritance is ever added.
It returns `nullptr` for all three non-resolved states, which is why
`materialAssignment()` exists beside it — and why `requireEffectiveMaterial()`
gives different diagnostics for "nothing was assigned" and "the assigned material
is gone", because those need different things from the user.

## ASSIGN, REPLACE, REMOVE

```cpp
Result<bool> assignMaterial(Document&, MaterialId);      // refuses an unknown ID
Result<bool> removeMaterialAssignment(Document&);
```

Each returns whether anything changed, so the document's revision only moves on an
effective change — tested.

`assignMaterial` **refuses** an ID that does not name a material in this document.
An assignment that could be created pointing at nothing would make `Unresolved` a
state the API produces rather than one the world produces.

Replacing is the same call with a different ID, and is explicit. A and B may carry
the same designation; identity decides, and a test asserts the designations are
equal before replacing so the claim is not vacuous.

Removing the assignment is **not** deleting the material: both materials remain
after a remove, tested.

**No ID is ever allocated by an assignment operation.** A test captures
`lastAllocatedId()` across assign / remove / assign / A→B→A and requires it
unchanged, because an assignment that consumed IDs would drift the ID space over
repeated undo cycles.

## MISSING MATERIAL, DELETED MATERIAL, NO SILENT REBINDING

The mandatory fixture, and the centre of the milestone:

```text
A designated "Steel", B designated "Steel"
assign A
delete A            -> Unresolved, naming A
                    -> NOT B, though B is now the only "Steel" left
add C, also "Steel" -> still Unresolved, naming A
```

Nothing erases the assignment, replaces it with a default, repoints it at a
same-designation material, or reaches for the first library match. The stale
intended reference is what makes recovery possible, and recovery is **explicit**:
the user assigns C, or nobody does.

## IDENTICAL REPLACEMENT

Stronger than same-name: A and B are equal in **every field a user can see** —
designation, standard, family, notes, density, modulus, conductivity — and a test
asserts `definition() == definition()` before deleting A. Still `Unresolved`. A
third, also identical, changes nothing. Identity controls resolution; similarity
never does.

## REORDER STABILITY

Materials live in the document's ordered object map, so they cannot be shuffled
directly — the honest version of the brief's reorder test is to change what is
around the assigned material and require the resolution not to move:

```text
create A, B, C; assign B (second of three)
delete A            -> B is now FIRST      -> still resolves to B
add D, add E        -> B is neither first nor last -> still resolves to B
```

Checked by note as well as by ID, so a right answer for the wrong reason would
show.

## DUPLICATE NAMES

Three materials designated "Steel" coexist; each resolves by identity. Renaming
one changes only what that one displays — the assignment is untouched, because a
display query reads the current name **through** the ID rather than from a cached
copy. Tested with both the object name and the designation.

## MODEL REGENERATION

The assignment belongs to the document, not to any generated topology, so
regeneration cannot reach it. Tested on `BracketModel`, a real feature tree:

```text
assign; edit the width parameter; regenerate
  report.regenerated  non-empty, report.failed empty
  the pad's volume     changed
  the assignment       unchanged, same MaterialId, still Resolved
```

## FAILED REGENERATION

```text
assign; set depth to 0 mm; regenerate
  report.failed        non-empty
  the assignment       unchanged, still Resolved
restore depth; regenerate
  report.failed        empty
  the assignment       still the same material
```

Canonical engineering intent is not erased by a temporary geometry failure. A
derived mass becomes unavailable; the assignment does not.

## ASSEMBLY OCCURRENCE POLICY

**Overrides are NOT supported, and the absence is tested rather than asserted.**
`ComponentDefinition` has no material field, so
`compile_fail.assign.occurrence-material-field` and
`.occurrence-material-read` both fail with "no member named 'material'". A
deferral nobody tests is indistinguishable from an oversight and would be easy to
undo by accident.

Three occurrences of one part all resolve through the one document-level
assignment, so they cannot disagree, and adding occurrences changes nothing.

When external references arrive, ADR-026 says the override belongs on
`ComponentDefinition` beside `suppressed` and `placement` — which is exactly where
the compile-fail case proves it is not yet.

**A consequence worth stating, which is broader than ADR-026's wording.** ADR-026
says "an assembly's mass properties use each part's own material", which
presupposes each part is its own document. In P13/P15 a component's `part` is an
`ObjectReference` to an object in the SAME document — ADR-003 defers external
references — so a single-document assembly has exactly ONE material assignment for
everything in it. That is consistent with the decision and stronger than its
stated consequence, and it is recorded here rather than left for a reader to
discover.

## SUPPRESSION

```text
active -> suppressed -> active
```

The assignment stays `Resolved` throughout. Suppression means "not in this build",
not "material deleted" — the P13 wording, preserved. "Not participating" and
"material unresolved" remain different things.

## CONFIGURATION POLICY

**A configuration never changes which material a part uses**, and this is forced
rather than chosen: a configuration overrides free parameter values, and a
`MaterialId` is not one. Tested across base → wide → narrow → base → wide, with
the effective parameter value checked at each step so the configuration is proved
to be genuinely in force rather than inert.

**A defect found here, pre-existing, and NOT fixed by this milestone.** ADR-026
also reasons that "a configuration changes a part's *dimensions*, so it changes
volume and therefore mass — through geometry, which is exactly the existing
derivation chain and requires nothing new". That chain does not run. Measured:

```text
setConfigurationOverride(wide, depth, 40 mm); setActiveConfiguration(wide); regenerate()
  effectiveParameterValue(depth)  0.040     the override IS in force
  report.regenerated              1         and it is the SLOT, not the pad
  report.updatedParameters        1         slot_depth = depth * 0.6
  volume of Pad                   unchanged from the base configuration
```

A configuration override changes a parameter's EFFECTIVE value and never the
parameter object, so the parameter's revision does not change and nothing marks a
feature that reads it dirty. `features::Regenerator` contains no mention of
configurations. A feature depending on a DRIVEN parameter is rebuilt, because that
parameter's stored value and revision do change — which is why the slot rebuilt
and the pad did not. The assembly layer does not have this problem;
`SolveTrigger::ConfigurationChanged` exists and placements follow.

It matters beyond a wrong volume: **P15-MASS-001 computes mass from density and
volume**, so under any non-base configuration it would multiply a correct density
by a stale volume and report a wrong mass without saying so.

Recorded in `TODO.md` as a carried defect with the reproduction and a scope. It is
a regeneration concern, not a materials one, and was outside this milestone's
authorized scope. **This milestone deliberately asserts nothing about volume under
a configuration**, because pinning the present behaviour would record a defect as
if it were the contract.

## CROSS-DOCUMENT BEHAVIOUR

There is **no** cross-document material reference, and none is faked. A
`MaterialId` is document-local throughout BetterCAD, so an ID from elsewhere is not
a reference to elsewhere:

```text
material in document A, assigned to document B
  B has no material of that number      -> refused, "in this document"
  B has its own material of that number -> the assignment is about B's, not A's
```

Both tested, including that two fresh documents allocate the same numbers, so the
ambiguity the brief warns about is demonstrated to be resolved by ownership rather
than left to chance. A real cross-document assignment needs an `ObjectReference`
carrying a `DocumentId`, which ADR-003 defers.

## LIBRARY / LOCAL BEHAVIOUR

The qualified ADR-025 workflow, unchanged: a library entry is **imported**, which
creates a document-owned material, and the assignment names that. A library entry
cannot be assigned directly because it has no `ObjectId` to name.

Editing the imported copy does not move the assignment, and the library entry is
identical afterwards — so no library change can alter what a part is made of.

**No library-first or local-first preference exists.** A hand-made material and an
imported one carrying the same designation both live in the document; assigning
either resolves to exactly it, told apart by the presence of an import origin.
Resolution is by ID alone.

## UNDO-READY MUTATION SEMANTICS

Commands are P15-CMD-001. What is true now is that the canonical before/after
state of every mutation is a single `std::optional<MaterialId>`, so a command
stores **intent** and never a derived effective material:

```text
state0 = document.materialAssignment()   (nullopt)
assign A; state1 = ...                    (A)
assign B; state2 = ...                    (B)
setMaterialAssignment(state1) -> A        restored exactly
setMaterialAssignment(state0) -> Unassigned
setMaterialAssignment(state2) -> B
```

`A -> B -> A` restores the same identity, not an equivalent one, and consumes no
ID.

## FAILED MUTATION ATOMICITY

An ID naming nothing, the invalid ID, and an ID naming a parameter: each fails and
leaves the previous assignment exactly as it was, verified through both
`materialAssignment()` and `effectiveMaterial()`. There is no half-applied
mutation, because the mutation is a single field written after the check.

A duplicate-ID load is refused by the document (`AlreadyExists`) with nothing
overwritten, and the assignment still names the original — P15-MAT-001's collision
behaviour, integrated rather than duplicated.

## MATERIAL DELETION INTERACTION

P15-MAT-001's rule is used as it stands: deletion is **allowed** while referenced,
and the assignment becomes `Unresolved`. No second deletion policy was invented.
Recovery works on P15-MAT-001's terms: an ID is never reused, so the only way a
material returns is the same object being reinserted — which is what an undo of a
delete does, and which a test exercises. No retired ID is forged to make recovery
pass.

## NO IMPLICIT DEFAULT MATERIAL

There is none. `requireEffectiveMaterial()` on an unassigned document fails naming
the document; no generic steel, air, vacuum or "Default" exists anywhere in the
resolution path.

## PERSISTENCE READINESS

Not implemented (P15-PERSIST-001), and not falsely claimed. The canonical state is
one `std::optional<MaterialId>` — a `uint64` or nothing. Nothing in it depends on
a pointer, a runtime handle, a container position or a display name, so it is
representable persistently whenever the owner is.

A material still cannot be saved at all, so an assignment adds no new silent-loss
path: the document writer refuses an object type it does not know, loudly, and
that is tested from P15-MAT-001 onward.

## TARGET DELETION

The target is the document, which cannot be deleted from inside itself, so there
is no orphaned-assignment case to clean up. Deleting the model target and deleting
the material are different things, and only the second is representable here.

## COMPILE-TIME TYPE SAFETY

Six cases in `tests/compile_fail/AssignmentMisuse.cpp`:

```text
assign.assign-object-id             an ObjectId is not a MaterialId
assign.assign-component-id          nor is a ComponentId, though both widen
assign.assign-integer               no naked-integer assignment API exists
assign.document-assign-object-id    the unvalidated setter is still typed
assign.occurrence-material-field    ComponentDefinition has NO material field
assign.occurrence-material-read     and none to read
```

`assignMaterial(document, 5)` cannot be written, so an index or a loop counter
cannot become an assignment.

This needed a small extension to shared test infrastructure:
`bettercad_compile_fail_group` and `_case` now take optional extra libraries,
because the targets are compile-only OBJECT libraries linking `BetterCAD::core`
and a case above layer 0 needs the include directories of its own layer (a
generated `Export.hpp` lives with its target). Two lines, `${ARGN}`, and every
existing group is unaffected.

## THE STATE TABLE

Walked as one sequence by
`MaterialAssignment_WalksTheWholeStateTableInOrder`, so the table below has a
single test behind it rather than a claim assembled from several.

```text
Case                            Expected        Actual
No assignment                   Unassigned      Unassigned, no ID
Assign A                        Resolved A      Resolved, names A
Rename A                        Resolved A      Resolved, names A
Delete A                        Unresolved A    Unresolved, names A
B same designation exists       Unresolved A    Unresolved, names A (not B)
C identical content added       Unresolved A    Unresolved, names A (not C)
Replace with B explicitly       Resolved B      Resolved, names B
Remove assignment               Unassigned      Unassigned, no ID
```

## ADVERSARIAL REVIEW

All 22 questions from the brief were answered against the code. Three findings.

**1. A configuration override does not rebuild the geometry it changes.**
Pre-existing, contradicts an ADR-026 consequence, and material to P15-MASS-001.
Measured, root-caused as far as this milestone could, recorded in `TODO.md`, and
deliberately **not** fixed — it is a regeneration concern and outside the
authorized scope. The reasoning for not asserting the present behaviour is in
CONFIGURATION POLICY above.

**2. Three compile-fail regexes expected the wrong GCC wording, again.** These
cases said "could not convert" where the mechanical and thermal cases say "cannot
convert": GCC's phrasing depends on whether the conversion is an argument to a
function taking a distinct type or an overload-resolution failure. That is the
third milestone running in which a compile-fail assertion has needed fixing for a
reason unrelated to the code under test, and the pattern is worth naming plainly:
**these tests assert on a compiler message, which is not part of the API and shifts
when the surrounding code's shape changes.** They are still worth having — they are
the only way to test an absence — but their assertions need re-verifying whenever a
signature nearby changes, and a total repeat filter is what makes that happen.

**3. My own state-table assertion was nonsense before it was fixed.** The
"identical content" step had a garbled conditional expression that compared a
definition to itself through a ternary. It would have passed while proving
nothing. Rewritten to compare C's definition against a freshly built fixture of
A's content.

Answered and already covered:

```text
resolve by name                       everything resolves by ID; no name lookup exists
duplicate "Steel" collapse            ADeletedMaterialNeverRebindsToAnotherOfTheSameName
deleting A adopts B                   same test; and the identical-content test
identical replacement adopts stale    AnIdenticalReplacementDoesNotAdoptAStaleAssignment
rename breaks assignment              SurvivesARenameAndShowsTheCurrentName
property edit changes identity        SurvivesAPropertyEditAndConsumersSeeTheNewValues
list reorder changes target           ResolutionDoesNotDependOnPositionAmongTheMaterials
cross-document IDs collide            AnAssignmentOnlyEverNamesAMaterialInItsOwnDocument
built-in/local IDs collide            a library entry has no ObjectId at all (ADR-025)
invalid assignment partially applies  AFailedMutationLeavesThePreviousAssignmentExactlyAsItWas
failed regeneration erases intent     SurvivesAFailedRegeneration
feature regeneration moves the owner  the owner is the document; SurvivesModelRegeneration...
suppressed occurrence loses material  SuppressingAComponentDoesNotRemoveTheMaterial
configuration mutates assignment      SwitchingConfigurationsNeverChangesWhichMaterialIsUsed
occurrence override mutates part      no such field; two compile-fail cases
two occurrences share override state  there is no per-occurrence state to share
Unassigned vs Unresolved              UnassignedAndUnresolvedAreDifferentStates
default material masks a gap          ADocumentStartsUnassignedAndThatIsNotAFault
deleted target orphans state          the target is the document; not representable
commands undo via derived state       CanonicalStateIsEnoughForACommandToUndo
save/load persists a name             nothing but an ID is stored; save refuses loudly
consumers bypass canonical resolution effectiveMaterial() is the one resolver
```

## DETERMINISM

`MaterialAssignment_ResolvesToTheSameAnswerEveryTime` compares the whole
`MaterialAssignment` — state, ID and diagnostic — twenty times over, in the
Resolved case and again in the Unresolved case, so the diagnostic text is part of
what is required to be stable. Resolution reads an ordered map and an optional; no
unordered container, hash or pointer ordering is involved. Both repeat stages ran
the whole suite five times with no failure.

## FULL REGRESSION

```text
preset             configure  clean  build  no-op rebuild  suite       warnings
debug-ext              0        0      0          0        2479/2479      0
release-ext            0        0      0          0        2479/2479      0
debug-shared-ext       0        0      0          0        2479/2479      0

repeat release-ext, [A-Za-z], until-fail:5   exit 0   12395 = 2479 x 5, 0 failures
repeat debug-ext,   [A-Za-z], until-fail:5   exit 0   12395 = 2479 x 5, 0 failures
qualification finished 16:59:40, 0 stage(s) failed
```

2479 = the 2446 of P15-THERM-001 plus the 33 added here, and all three presets
discover the same 2479.

`verify-harness.cmd` was run first and required a non-zero exit from a preset that
does not exist: 3 stages failed, exit 3.

**No replace fault anywhere**, across 24790 repeat executions plus three full
suites. No controlled rerun was needed.

**Qualified tree = committed tree.** The eight tree IDs recorded before the first
build and after the last test run are identical, and equal to the committed tree:

```text
apps a3528787f4b24097c34ca11961d6c0c642315f0e
include 3b89c1ce75ab266fc768b9088abef7a3f10fb395
src fff7ec7f685ba2cb8aae0cb470e6a06e904f39d4
tests 4220c2e8b210ddf27cc38932a775d84b8bfef139
examples 2e1ef60bb5e67fa3589e1246613261cd746ddce2
cmake 7ed703a3031144d26063a1ff02996e7664e29524
CMakeLists.txt 13823e788ed3975792affa9ee4354ffce9479d80
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

Qualified on the first attempt. `debug-shared-ext` was worth watching: this
milestone adds a field to `Document`, which every layer above core uses, and adds
one to `Document::clone()` — a function that copies its members by hand, so a
field left out would compile silently and only fail in a clone test.
`MaterialAssignment_SurvivesADocumentClone` is that test.

## WARNINGS

0 in all three builds, with `-Werror` and the project's full warning set.

## KNOWN LIMITATIONS

- **A configuration override does not rebuild the geometry it changes.** Found
  here, pre-existing, carried in `TODO.md`, and a precondition for
  P15-MASS-001 being correct under a non-base configuration.
- **One material per document.** Per-body materials are deferred; the mechanism
  is a feature-keyed override (ADR-026).
- **An assembly occurrence cannot differ from its part**, and in a
  single-document assembly everything shares the one assignment, because part
  references are internal until ADR-003's external references arrive.
- **No cross-document assignment.** A `MaterialId` from another document is not a
  reference to it.
- **Nothing is persisted yet.** P15-PERSIST-001; saving a material refuses
  loudly.
- **`Invalid` is only reachable through `Document::setMaterialAssignment()`**,
  which is deliberate: the validating API cannot create it, and the state exists
  for what a loader may hand over.
- **No commands, so no undo**, though the canonical state is shown to support
  one.

## RESULT

```text
TASK:            P15-ASSIGN-001 -- Material Assignment
BASELINE:        27c1bac, clean, HEAD == origin/main
PREREQUISITES:   P15-ARCH 16/16, P15-UNITS 20/20, P15-MAT 18/18, P15-MECH 18/18,
                 P15-THERM 14/14, INFRA-QT-DEPLOY 8/8
IMPLEMENTATION:  one optional MaterialId on the Document (ADR-026); four states
                 never collapsed; assign / replace / remove; resolution by
                 identity alone; the consumer boundary ADR-027 named
TESTS:           33 new, 2446 -> 2479. 27 behavioural, 6 compile-fail, two of
                 which prove an ABSENCE (ComponentDefinition has no material
                 field) rather than a rejection
VALIDATION:      the state table walked as one sequence; the no-rebind fixture
                 with a same-designation AND a content-identical rival; reorder
                 stability; regeneration and failed regeneration on a real
                 feature tree
ADVERSARIAL:     22 questions; 3 findings. 1 pre-existing product defect found
                 and NOT fixed (out of scope, carried in TODO.md); 2 test defects
                 fixed
DETERMINISM:     12395 = 2479 x 5 in release-ext AND debug-ext, 0 failures; the
                 whole assignment including its diagnostic stable over 20 reads
REGRESSION:      3 presets from an external build root, 2479/2479 each, 0
                 warnings, fresh binaries, 0 stages failed, first attempt
RESULT:          PASS
EVIDENCE:        docs/verification/P15-ASSIGN-001/
NOT CLAIMED:     that a configuration changes a part's volume -- it does not, and
                 that is a carried defect, not this milestone's contract.
                 That an occurrence can differ from its part, or that a
                 cross-document assignment exists. That anything is persisted.
TODO:            P15-ASSIGN-001 16 of 16 ticked. P15-MASS-001 NOT started, and it
                 should not start before the carried regeneration defect is
                 settled.
```

## REVISION

```text
2026-09-27  Implemented, adversarially reviewed and qualified on the first
            attempt. ADR-026 had already decided the target, the occurrence
            policy and the configuration policy by audit, and the audit's answer
            was that neither a Part nor a Body object exists to own a material --
            so the assignment is document-level state. One pre-existing defect
            found and deliberately left unfixed: a configuration override does
            not rebuild the geometry it changes, which contradicts an ADR-026
            consequence and would make P15-MASS-001 silently wrong under a
            non-base configuration.
```
