# P15-CMD-001 — Material commands with exact undo and redo

**STATUS: PASS.** Qualified on the first attempt across three presets, 2685/2685
each, 0 warnings, 34905 test executions, 0 failures. Full detail at the end.

**TASK** — create, delete, edit, assign and remove-assignment as undoable commands,
with the history holding canonical engineering intent and nothing derived.

---

## BASELINE

```text
branch               main
git status --short   clean
HEAD                 485ecfe46c8d6c20d289f0cc06e852e849695e14
origin/main          485ecfe46c8d6c20d289f0cc06e852e849695e14   (equal)
HEAD^{tree}          32593bfd1cb6f773807f1ce337fa31f1ca79a16b
git log -1           485ecfe BetterCAD: add material provenance and completeness reporting
suite before         2639 tests
```

## PREREQUISITES

All nine verified complete in `TODO.md` — 146 `[x]`, **0** open boxes between them —
each recording `RESULT: PASS` with evidence:

```text
P15-ARCH-001 16   P15-UNITS-001 20   P15-MAT-001 18   P15-MECH-001 18
P15-THERM-001 14  P15-ASSIGN-001 16  P15-MASS-001 17  P15-CUSTOM-001 13
P15-PROV-001 14
```

The external build path is still the qualified one: all three `-ext` presets are
defined and their build trees exist under `%LOCALAPPDATA%\bc-build`, outside the
synchronised folder (INFRA-QT-DEPLOY-001).

## EXISTING COMMAND INFRASTRUCTURE AUDIT

| Concept | Exists? | Semantics | Reusable for materials? |
| --- | --- | --- | --- |
| `Command` base | YES | `description`, `execute`, `undo`, `redo`; contract already says redo "reproduces the state execute() produced, **including the same IDs**" | YES, directly |
| `CommandHistory` | YES | binds to one document and refuses another; a new command clears redo; records only a command whose `execute()` succeeded | YES, directly |
| `AddObjectCommand` | YES | "redo re-inserts it with the same ID"; keeps the object as a prototype | **YES — a Material IS a DocumentObject** |
| `DeleteObjectCommand` | YES | "undo restores it with the same ID and name"; keeps the whole object plus the configurations' view of it | **YES** |
| `RenameObjectCommand` | YES | renames any object or parameter | YES |
| `CreateFeatureCommand<F>` | YES | **wraps `AddObjectCommand`** and adds a typed ID | the precedent this milestone follows |
| `ModifyFeatureCommand<F>` | YES | before/after `Definition` snapshots | pattern reusable; keyed on `FeatureId`, so not directly |
| assembly / drawing / sketch commands | YES | same base, same before/after shape | pattern confirmed across four modules |

**The audit's headline: create and delete were already correct, and I verified it by
probe rather than assuming.** A throwaway probe built a material through
`AddObjectCommand`, undid and redid it, then deleted it through `DeleteObjectCommand`
with an assignment pointing at it:

```text
AddObjectCommand     execute -> id 1; undo -> absent;
                     redo -> present, id 1, definition and provenance identical
DeleteObjectCommand  execute -> assignment Unresolved; undo -> assignment Resolved
```

So two of the five commands are **thin wrappers**, exactly as `CreateFeatureCommand<F>`
already wraps `AddObjectCommand`. The probe was deleted afterwards — it was a
measurement, not a deliverable.

**No parallel undo system was created.** All five commands derive from the existing
`Command` and run through the existing `CommandHistory`.

## SCOPE

```text
CreateMaterialCommand            wraps AddObjectCommand; adds a typed materialId()
DeleteMaterialCommand            wraps DeleteObjectCommand; adds the precondition
                                 that the ID really names a material
EditMaterialCommand              NEW -- before/after MaterialDefinition snapshots
AssignMaterialCommand            NEW -- document-level assignment, previous captured
RemoveMaterialAssignmentCommand  NEW -- the same, to std::nullopt
```

Not added, deliberately:

- **No rename command.** `RenameObjectCommand` already renames a material; adding a
  second would be the parallel system the brief forbids.
- **No transaction framework.** Item 43: BetterCAD has no compound-command
  infrastructure, and inventing one is out of scope. Material commands go through the
  same `CommandHistory` as every other command, so they cannot bypass tracking that
  does not exist.
- **No compound "create and assign" command.** Item 44 says to add one only if the
  architecture naturally supports it. It does not, so composition stays with callers —
  two commands, two undo steps, which is also what a user would expect.
- **No occurrence or configuration assignment.** Not a decision this milestone may
  make; see DIRECT VS EFFECTIVE INTENT.
- Nothing from P15-PERSIST-001 or later. Command history is not persisted, and nothing
  here makes it so.

## COMMAND STATE PRINCIPLE

Every command stores the minimum canonical state needed for exact mutation and exact
reversal, and **nothing derived**. The complete member list:

```text
CreateMaterialCommand            name_, definition_, optional<AddObjectCommand>
DeleteMaterialCommand            id_, optional<DeleteObjectCommand>
EditMaterialCommand              id_, after_, optional<before_>   (MaterialDefinitions)
AssignMaterialCommand            id_, optional<optional<MaterialId>>
RemoveMaterialAssignmentCommand  optional<optional<MaterialId>>
```

An ID, a name, a `MaterialDefinition`, or a wrapped generic command. That is all.

The doubled `optional<optional<MaterialId>>` is deliberate: the outer one is the "has
executed" flag, the inner one is the assignment, which may legitimately have been
absent. It follows `SetActiveConfigurationCommand` rather than inventing a shape.

## CREATE MATERIAL COMMAND

```text
S0   material absent
     execute Create "Steel"
S1   present, id = N, definition == the requested one
     undo
S2   absent, findMaterial(N) == nullptr
     redo
S3   present, id = N   <-- the SAME N
```

`S1 == S3` is checked as the whole `MaterialDefinition`, including the provenance
record and its fixed date, plus the object name. A second test runs five
undo/redo cycles and checks the ID and definition every time, so a drift that only
appears on the second cycle would be caught.

**Failure atomicity.** Four distinct failures — a duplicate name, a zero density, an
invalid identifier, a missing material — each leave the material count, the document
revision and the undo depth unmoved, and `undoDescription()` still names the last
*successful* command. A fifth test checks the opposite error: a failed command must
**not** clear the redo stack, and the redo afterwards restores the original ID.

## DELETE MATERIAL COMMAND

The deletion **policy** is the qualified one and is not restated: deleting an assigned
material is allowed and the assignment becomes `Unresolved` (P15-ASSIGN-001).

```text
S0   A present, assignment -> A (Resolved)
     execute Delete A
S1   A absent, assignment still names A, state Unresolved
     undo
S2   A present with the SAME id, name and whole definition; assignment Resolved
     redo
S3   A absent, assignment Unresolved, still naming A
```

Undo restores from the stored `unique_ptr<DocumentObject>`, so nothing is
reconstructed from a name and no canonical state can be lost — provenance, every
property, the known/unknown pattern and the configurations' view of the object all come
back together.

**`DeleteMaterialCommand` refuses an ID that is not a material.** Without that
precondition it would delete a sketch and report success. Tested with a sketch's ID and
with an ID that names nothing; both leave the object count and the history untouched.

### NO-REBIND RESTORATION

Two materials sharing a **designation** — names are unique, designations are not — with
an assignment to A:

```text
delete A   -> Unresolved, material == A, != B, effectiveMaterial == nullptr
undo       -> Resolved,   material == A, effectiveMaterial->materialId() == A
redo       -> Unresolved, material == A, != B; B untouched throughout
```

A harder ordering is tested too: delete A, then **create** B with the same designation,
then undo B's creation and A's deletion. The assignment resolves A. The reason it
cannot do otherwise is upstream of these commands — an assignment holds a `MaterialId`
and the allocator never reuses one, so that ID can only ever name the material that
went away.

## EDIT MATERIAL COMMAND

Whole-definition before/after snapshots, which is what makes the awkward cases exactly
right rather than nearly right.

```text
S0   designation "Steel", density 7850, E 200 GPa, k UNKNOWN
     execute Edit
S1   designation "Custom Steel", density 7900, E 210 GPa, k known
     undo
S2   == S0, and k isUnknown() with value() empty -- NOT zero
     redo
S3   == S1
```

### PROPERTY EDITS AND PROPERTY REMOVAL

```text
Known -> removed     undo restores Known 235 MPa exactly; redo removes again
                     asserted as isUnknown() AND value().has_value() == false,
                     because a zero would satisfy only the second
```

### PROVENANCE EDITS

A density edited from 7850 to 7900 **and** its citation from `Measured` / "Rev A" /
2024-03-17 to `UserEntered`:

```text
undo  restores the value AND the kind AND the revision AND the date, together
redo  restores both again
```

A value/metadata split would fail this. **Mutation-tested:** an undo that restored only
the metadata fails the Known-restoration assertion and aborts.

A provenance-only edit is tested to move no number at all — the mass and the derived
shear modulus are compared exactly across execute, undo and redo.

### IDENTITY AND FAILURE

`MaterialId` is identical before execute, after execute, after undo and after redo;
the material count stays 1. Four invalid edits — a negative modulus, a zero density, an
incompressible Poisson ratio, and a property falsely claiming `Derived` — each leave the
definition, the revision and the history depth unmoved.

## ASSIGN MATERIAL COMMAND

```text
S0   unassigned
     execute Assign A   -> Resolved, material == A
     undo               -> Unassigned, document.materialAssignment() empty
     redo               -> material == A
```

### REPLACE ASSIGNMENT

```text
S0   P -> A
     execute Assign B   S1  P -> B
     undo               S2  P -> A
     redo               S3  P -> B
```

A and B share a designation, so only identity can decide, and `effectiveMaterial` is
checked alongside the canonical field at every step. The reverse direction (B → A) is
tested too.

Assigning a material that is not in this document is refused, so `Unresolved` stays a
state the world produces rather than one the API does. An ID naming a non-material
object is equally refused, and the existing assignment survives both.

## REMOVE ASSIGNMENT

```text
S0   P -> A
     execute Remove  -> Unassigned, effectiveMaterial == nullptr
     undo            -> Resolved, material == A
     redo            -> Unassigned
```

Removing an already-absent assignment **succeeds and changes nothing**, including the
document revision. That follows `SetActiveConfigurationCommand` rather than inventing a
convention.

## DIRECT VS EFFECTIVE INTENT

The canonical assignment is **one optional `MaterialId` on the Document** (ADR-026,
P15-ASSIGN-001). There is no per-body, per-occurrence or per-configuration assignment.

So the failure item 22 warns about — a remove on a child reaching a parent's intent
because the child's *effective* material resolves to it — has no parent to reach.
Likewise `AssignMaterialCommand(Occurrence, …)` cannot be written:
`ComponentDefinition` has no material field, and two compile-fail cases in the
`assign` group already prove that absence. And a configuration overrides parameter
*values*; a `MaterialId` is not one, so the mechanism cannot carry it.

Recorded as answered-by-construction rather than tested into existence. What **is**
tested is that the commands move the document's canonical field and that the derived
`effectiveMaterial` follows it, checked separately so the two cannot be conflated.

## FAILURE ATOMICITY AND HISTORY ATOMICITY

```text
failed create (4 kinds)   material count, revision and undo depth all unmoved
failed edit (4 kinds)     definition, revision and undo depth all unmoved
failed delete (2 kinds)   object count and undo depth unmoved; the object survives
failed assign (2 kinds)   the existing assignment survives; undo depth unmoved
cross-document execute    the second document untouched, the first unchanged
```

A failed command never becomes an undoable entry: `CommandHistory::execute` records
only a command whose `execute()` returned success. Afterwards `undoDescription()` is
checked to still name the last successful command, and undoing reaches *that*.

Both wrappers `reset()` their inner command on failure, so a second attempt starts
clean rather than inheriting a partly-executed `AddObjectCommand`.
`EditMaterialCommand` records `before_` only after the edit succeeds, so a failed edit
leaves the command unexecuted and `undo()` refuses.

`undo()` or `redo()` before `execute()` is refused with `FailedPrecondition` on all
five commands — ten calls tested, with the document checked untouched afterwards.

## REDO INVALIDATION

```text
Create A; Edit -> "X"; undo      canRedo() true, redoCount() 1
Edit -> "Y"                      canRedo() FALSE, redoCount() 0, redo() fails
                                 state is "Y"
undo, undo                       reaches the "Y" edit then the create --
                                 never the discarded "X"
```

A failed command does **not** clear redo, which is the opposite error and has its own
test.

## MATERIAL IDENTITY AND ALLOCATOR INTERACTION

The allocator contract, probed rather than assumed:

```text
Create A -> id 1
undo
Create B -> id 2        the ID is NOT reused (P15-MAT-001: the allocator only
                        ever counts up)
canRedo() after the new command -> FALSE
```

So the collision items 49 and 50 worry about is designed out **twice over**, by two
independent mechanisms: nothing can take the retired ID, and the redo that would
replay the creation has already been discarded. Both are tested.

Enumeration order survives a round trip: `materialIds()` walks the document's ordered
object map by ascending ID, so an undone and redone create returns to its place —
checked over five cycles.

## DERIVED MASS EXCLUSION

A member-by-member audit of all five commands appears under COMMAND STATE PRINCIPLE:
every member is an ID, a name, a `MaterialDefinition` or a wrapped generic command.

Grepping the header and implementation for the derived vocabulary:

```text
mass            3 hits   all in the comment that asserts the absence
volume          1 hit    the same comment
inertia         1 hit    the same comment
completeness    1 hit    the same comment
centroid        0
centreOfMass    0
centerOfMass    0
derivedShear    0
derivedBulk     0
effectiveMaterial 0
```

**Six hits, all in the comment that says none of it is there, and zero elsewhere.**
Five compile-fail cases prove there is no setter or accessor for any of it, and a
sixth proves `MaterialDefinition` has no `volume` field.

### The recomputation test

```text
20 x 30 x 50 mm = 30000 mm^3 = 3e-5 m^3, geometry fixed throughout

density 1000  ->  mass 0.03 kg        (M1)
EditMaterialCommand: density 2000
              ->  mass 0.06 kg        (M2 == 2 M1)
undo          ->  mass 0.03 kg
redo          ->  mass 0.06 kg
volume asserted 30000 mm^3 at the end
```

The mass follows because the restored density is multiplied by the geometry again, not
because 0.03 was stored — there is nowhere in any command for it to have been stored.

## DERIVED G/K EXCLUSION

`MechanicalProperties` has **no slot** for a shear or bulk modulus (ADR-027: it would
be a second source of truth that could disagree with the first), so neither can enter a
definition snapshot even by mistake.

```text
E 200 GPa, nu 0.30   ->  G = 200/2.6 GPa
Edit E -> 210 GPa    ->  G = 210/2.6
undo                 ->  G = 200/2.6
redo                 ->  G = 210/2.6
and G is still isDerived(), never isKnown()
```

A stored property claiming `Derived` is refused by `validate()`, which a test exercises
by attempting exactly that edit.

## COMPLETENESS REPORT EXCLUSION

```text
k Unknown          ThermalTransient -> Incomplete, missing {ThermalConductivity}
Edit: set k        ThermalTransient -> Ready, missing empty
undo               Incomplete again, same missing list
redo               Ready
```

Nothing stores a report; each one is built on demand from the definition.

## MULTI-COMMAND SEQUENCES

```text
1 Create A          2 Edit A (designation)   3 Assign -> A
4 Edit A (density)  5 Remove assignment
```

Five commands, undo depth 5. Undoing all five gives `equivalent(document, S0)` — a
whole-document comparison, not field by field — with the material count 0 and no
assignment. Redoing all five gives `equivalent(document, S5)`, the identity is still
the one the create produced, the definition is the step-4 one, and the mass recomputes
to what it was.

Two further chains:

```text
Create, Assign, Delete, undo Delete, Edit
  -> the assignment resolves the SAME material, the edit succeeds, count stays 1
Create A, Assign A, Delete A, Create B (same designation),
  undo Create B, undo Delete A
  -> A resolves; at no point does the assignment reach B
```

## DETERMINISM

```text
the same sequence from the same fixed DocumentId, run twice
  -> equivalent() documents
enumeration after undo/redo, five cycles          -> identical order
create/undo/redo, five cycles                     -> identical ID and definition
report rebuilt after undo                          -> identical report
```

No command reads a clock, a hash or an unordered container. Identity comparisons are by
`MaterialId`, never by address.

## ADVERSARIAL REVIEW

**PASS — 28 attacks, 3 findings, no production defect.** Full text:
[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

```text
F1  create and delete were already correct -- verified by probe before writing
    anything, so two commands are thin wrappers rather than reimplementations
F2  three groups of attacks are answered by the assignment model rather than by
    new code: there is no direct/effective hierarchy to confuse
F3  item 42's object-lifecycle sequence cannot be built here -- the assignment's
    target is the document, not an object -- recorded as a boundary
```

Two of the five automatic-FAIL gates were **mutation-tested** rather than only
asserted:

```text
redo allocates a new MaterialId          -> 5 test cases fail
undo restores only the metadata          -> the Known-restoration assertion fails
```

I do not treat "no defect found" as reassuring on its own, which is why those two were
run.

## FULL REGRESSION

```text
preset             configure  clean  build  no-op rebuild  suite       warnings
debug-ext              0        0      0          0        2685/2685      0
release-ext            0        0      0          0        2685/2685      0
debug-shared-ext       0        0      0          0        2685/2685      0

repeat release-ext, [A-Za-z], until-fail:5   exit 0   13425 = 2685 x 5, 0 failures
repeat debug-ext,   [A-Za-z], until-fail:5   exit 0   13425 = 2685 x 5, 0 failures
qualification finished 19:54:36, 0 stage(s) failed
```

2685 = the 2639 of P15-PROV-001 plus the 46 added here, and all three presets discover
the same 2685. Both repeat stages show exactly 13425 passing executions, which is
2685 x 5 with nothing skipped.

**34905 test executions in total, 0 failures**. Qualified on the **first attempt**,
15:53:21 to 19:54:36 (4h 01m).

The tests carrying this milestone's claims were confirmed present and passing in **all
three** presets rather than only where they were developed:

```text
all 14 compile_fail.matcmd cases                              passed x3
create/undo/redo preserves the same MaterialId                passed x3
delete and undo never rebind to a same-designation material    passed x3
mass recomputes from restored density                         passed x3
a new command invalidates redo                                passed x3
the five-command chain undoes and redoes to the same states    passed x3
```

**The previous milestone's lesson was applied before the freeze, not after.**
P15-PROV-001's first qualification failed two hours in, on `debug-shared-ext build`
alone, because an out-of-line member sat on an unexported type. This time every
out-of-line definition was checked against its class's export macro — all five command
classes carry `BETTERCAD_FEATURES_EXPORT` — and the shared preset was built to
confirm it links, before the qualification was started. It did, and the qualification
passed first time.

`verify-harness.cmd` was run first and required a non-zero exit from a preset that does
not exist: 3 stages failed, exit 3.

**No replace fault anywhere** — zero occurrences of "Permission denied" or "cannot
replace" across every log.

A pre-qualification smoke run of the whole suite on `debug-ext` reported 2684/2685. The
one failure was `cli.new.unicode-path`, and the cause was the invocation rather than the
tree: that test needs code page 65001, which `qualify.cmd` sets and an ad-hoc `ctest`
does not. All three qualified presets pass it.

**Qualified tree = committed tree.** The eight tree IDs recorded before the first build
and after the last test run are identical:

```text
apps a3528787f4b24097c34ca11961d6c0c642315f0e
include d3732c1064eea175a4dddef0d0bc02b4e5d73e95
src 94d81d5c77947d58d2e8428876ad180f587ac86d
tests 5d9399e2b7a1e8f0321d890aa475585e5643023c
examples 2e1ef60bb5e67fa3589e1246613261cd746ddce2
cmake 7ed703a3031144d26063a1ff02996e7664e29524
CMakeLists.txt 13823e788ed3975792affa9ee4354ffce9479d80
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

Only `docs/` changed after the freeze, and `docs/` is outside the fingerprint and cannot
affect the executable or the tests.

## WARNINGS

0 in all three builds, with `-Werror` and the project's full warning set.

## KNOWN LIMITATIONS

1. **Command history is not persisted.** Out of scope (item 53) and BetterCAD does not
   persist any history. Nothing here makes the current canonical state harder to
   persist, which is P15-PERSIST-001's subject.
2. **No compound transaction.** BetterCAD has no compound-command infrastructure, so
   "create a material and assign it" is two commands and two undo steps. Composition
   stays with callers.
3. **Low-level domain mutation remains public.** `features::setMaterialDefinition`,
   `setMaterialMechanical`, `assignMaterial` and the rest are still callable directly,
   bypassing the history. That is deliberate and matches every other module — the
   commands are the *user-facing* path, the domain API is what they and the tests are
   built on. A GUI must use the commands; nothing forces it to at compile time.
4. **`EditMaterialCommand` takes a whole definition.** A caller changing one property
   reads, modifies and writes. That is what makes the edit atomic and the undo exact,
   but it is more verbose than a per-property command, and it means the caller decides
   whether P15-PROV's stale-citation clearing applies (by computing the definition
   through `setMaterialMechanical` first, or not).
5. **No rename command of its own.** `RenameObjectCommand` covers it. Worth knowing
   that a material rename therefore appears in history as an object rename.
6. **Item 42's sequence is unreachable**, and item 51's corrupted-history path is not
   forced into a test: the repository has no pattern for injecting history corruption,
   and inventing one would test the test harness rather than the product.

## RESULT

```text
TASK:            P15-CMD-001
IMPLEMENTATION:  five commands. CreateMaterialCommand and DeleteMaterialCommand
                 WRAP the existing AddObjectCommand and DeleteObjectCommand, which
                 a probe showed already handled materials correctly;
                 EditMaterialCommand, AssignMaterialCommand and
                 RemoveMaterialAssignmentCommand are new. No parallel undo system.
TESTS:           46 added (32 command tests, 14 compile-fail); 2639 -> 2685
VALIDATION:      exact undo/redo state transitions by whole-definition and
                 whole-document comparison; a member-by-member audit showing no
                 derived state in any payload; two automatic-FAIL gates
                 mutation-tested
RESULT:          PASS
EVIDENCE:        this directory; qualification/ for all 20 stage logs and the tree
                 fingerprints
TODO:            updated -- 15 boxes ticked
```

**Most of this milestone already existed, and I established that by measurement rather
than by reading.** A `Material` is a `DocumentObject`, and the generic object commands
already promised to restore the same ID; a throwaway probe confirmed they delivered it
for materials, provenance and all, before a line of new code was written. So two of the
five commands are thin wrappers over them, exactly as `CreateFeatureCommand<F>` already
is.

**Three groups of the brief's attacks are answered by the assignment model rather than
by new code.** The canonical assignment is one optional `MaterialId` on the Document
(ADR-026), so there is no direct/effective hierarchy to confuse, no occurrence override
to guard, and no configuration-local material intent to create by accident. Recorded as
answered-by-construction rather than tested into existence.

**The derived-state exclusion is structural, not a promise.** Every member of all five
commands is an ID, a name, a `MaterialDefinition` or a wrapped generic command.
Grepping the header and implementation for the derived vocabulary finds six hits, all in
the comment that asserts the absence, and zero elsewhere.

## REVISION

First revision. Written against the tree qualified above; no source or test file
changed after the freeze.
